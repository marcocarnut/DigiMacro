/* Checks web/table.js on its own, and emits a table for the firmware to play.
 *
 *   node webtable.js            the checks
 *   node webtable.js --emit     the table, as hex on stdout
 *
 * The checks here are the ones that need no device: that text survives a trip
 * through bytes and back, and that bad input is refused with something a
 * person can act on. What they cannot answer is whether the firmware agrees
 * about what those bytes mean, which is what --emit and hexplay are for.
 */
const t = require('../web/table.js');

let fails = 0;
const check = (ok, what) => { if (!ok) { console.log('  FAIL: ' + what); fails++; } };

/* Every macro the tests write, in one place: what the firmware plays is
   built from the same list the round trip is checked against. */
const ROWS = [
  { code: 'IR3', text: 'x{IR 00 46}y' },  /* first, so a tap that wrongly
                                             matched it would show */
  { code: '.',   text: 'hello from DigiMacro\\n' },
  { code: '-',   text: 'wait{pause 1.0} ok' },
  { code: '..',  text: 'ab{4}' },      /* four b in all, not five */
  { code: '.-',  text: '{Ctrl+Left}z' },
  { code: '-.',  text: '{Ctrl+Shift+Left}{Shift+Left}{Alt+Tab}{F5}' },
  { code: '--',  text: 'A!Z {{x}} \\t' },
  { code: '...', text: 'coração{Enter}' },      /* needs the dead keys below */
  { code: '..-', text: 'tv{IR 00 45}on{IR 80 c5}!' },  /* and the remote's: see
                                                     run.sh */
  { code: '...-', text: 'z{goto IR3}' },
];

/* The rows include accented text, so the layout has to be the one that can
   type it -- and saying so here keeps --emit deterministic. */
t.setLayout('us-intl');

/* Secrets, for the firmware to open (run.sh): with the key, each must type
   exactly what the same macro typed in the clear does. */
const SECRET_EMIT = [
  { code: '.',  text: 'user\\t{secret}hunter2{Enter}' },
  { code: '-',  text: '{secret!}{Ctrl+a}top (secret){3} {pause 0.5}{goto ..}' },
  { code: '..', text: 'plain{IR 00 45}x' },
  { code: '.-', text: '{secret}' + 'The quick brown fox jumps over the lazy dog. '.repeat(5) },
  { code: '-.', text: 'abc{4}{secret}(xy){3}z{Shift+Left}' },
];
const EMIT_KEY = Uint8Array.from({ length: 16 }, (_, i) => 0xA0 + i);
const hexOf = b => [...b].map(v => v.toString(16).padStart(2, '0')).join(' ');
if (process.argv.includes('--emit-secret') || process.argv.includes('--emit-clear')) {
  const clear = process.argv.includes('--emit-clear');
  let s = 1;
  const random = n => Uint8Array.from({ length: n }, () => (s = (s * 69069 + 1) >>> 0) >>> 24);
  const rows = clear ? SECRET_EMIT.map(r => ({ ...r, text: r.text.replace(/\{secret!?\}/, '') }))
                     : SECRET_EMIT;
  const table = t.encodeTable(rows, clear ? {} : { key: EMIT_KEY });
  if (!clear) {                       /* sealed with that randomness, so that a
                                         failure can be reproduced */
    const seal = plain => t.sealSecret(EMIT_KEY, plain, random);
    table.set(t.packTable(rows.map(r => [r.code, t.encodeMacro(r.text, seal)])));
  }
  const e = new Uint8Array(512).fill(0xFF);
  e.set(table, 0);
  if (!clear) e.set(t.keyCheck(EMIT_KEY), t.CHECK_ADDR);
  process.stdout.write(hexOf(e) + '\n' + (clear ? '' : 'key=' + hexOf(EMIT_KEY).replace(/ /g, '')));
  process.exit(0);
}

if (process.argv.includes('--emit')) {
  process.stdout.write([...t.encodeTable(ROWS)]
    .map(v => v.toString(16).padStart(2, '0')).join(' '));
  process.exit(0);
}

console.log('1. text survives being encoded and decoded');
/* Not every form is its own normal form -- "{Enter}" comes back as "\\n", and
   "{Ctrl++}" as "{Ctrl+Shift+=}", since + IS shift and equals. What must hold
   is that encoding the decoded form gives the same bytes: a table read off a
   board and written back unchanged must not drift. */
for (const text of [...ROWS.map(r => r.text),
     '{Enter}{Tab}{Space}{Backspace}', '{Ctrl++}', '{Gui+r}', 'a{x255}', 'a{255}',
     'x{pause 25.5}y', '{Esc}{Home}{PgDn}{Del}{Menu}', 'a\\\\b\\nc',
     '{{', '{Shift+F12}', '{Win+d}', '{Control+Alt+Delete}']) {
  let bytes, back, again;
  try {
    bytes = t.encodeMacro(text);
    back = t.decodeMacro(bytes);
    again = t.encodeMacro(back);
  } catch (e) {
    check(false, `${JSON.stringify(text)} threw: ${e.message}`);
    continue;
  }
  check(JSON.stringify(bytes) === JSON.stringify(again),
        `${JSON.stringify(text)} -> ${JSON.stringify(back)} is not stable`);
}

console.log('2. a whole table survives it too');
const table = t.encodeTable(ROWS);
const readBack = t.decodeTable(table);
check(readBack.length === ROWS.length,
      `${ROWS.length} macros in, ${readBack.length} out`);
check(JSON.stringify(t.encodeTable(readBack)) === JSON.stringify([...table]) ||
      JSON.stringify([...t.encodeTable(readBack)]) === JSON.stringify([...table]),
      'a table read back and written again must be the same bytes');

console.log('3. the walk stops where the firmware stops');
/* Anything an earlier, longer table left past the terminator is unreachable
   to the firmware, so the editor must not show it either. */
const littered = new Uint8Array(t.TABLE_TOP);
littered.set(table, 0);
littered.set([0x00, 0x0b, 0x00], table.length);      /* a stale entry after it */
check(t.decodeTable(littered).length === ROWS.length,
      'entries past the terminator must stay invisible');
check(t.decodeTable(new Uint8Array(4).fill(0xFF)).length === 0,
      'a virgin chip is an empty table');

console.log('4. a mistake is refused, with a reason');
t.setLayout('us-intl');
for (const [text, want] of [
  ['{',              /no \}/],
  ['{Ctrl+}',        /no key after/],
  ['{Nope+a}',       /not a modifier/],
  ['{Blah}',         /not a key/],
  ['{pause 0}',      /0\.1 to 25\.5/],
  ['{pause 30}',     /0\.1 to 25\.5/],
  ['a{0}',           /1 to 256/],
  ['a{x0}',          /1 to 256/],
  ['a{x300}',        /1 to 256/],
  ['a{257}',         /1 to 256/],
  ['{x3}',           /nothing to repeat/],
  ['\\q',            /means nothing/],
  ['λ',         /no key for/],      /* no layout here can type a lambda */
]) {
  let msg = null;
  try { t.encodeMacro(text); } catch (e) { msg = e.message; }
  check(msg !== null && want.test(msg),
        `${JSON.stringify(text)} should be refused with ${want}, got ` +
        (msg === null ? 'no error at all' : JSON.stringify(msg)));
}

console.log('5. a table that cannot fit is refused');
let msg = null;
try {
  t.encodeTable([{ code: '.', text: 'x'.repeat(t.TABLE_TOP) }]);
} catch (e) { msg = e.message; }
check(msg !== null && /only \d+ fit/.test(msg), `oversize table: ${msg}`);

msg = null;
try { t.encodeTable([{ code: '.', text: 'a' }, { code: '.', text: 'b' }]); }
catch (e) { msg = e.message; }
check(msg !== null && /twice/.test(msg), `a repeated code: ${msg}`);

msg = null;
try { t.encodeTable([{ code: '......', text: 'a' }]); }
catch (e) { msg = e.message; }
check(msg !== null && /not a code/.test(msg), `six symbols: ${msg}`);

console.log('5b. a count counts, and a group says what it counts');
/* {n} reads as a quantifier does in a regular expression: n in all. The
   firmware counts the EXTRA ones, so the two differ by one and the encoder
   is where that is reconciled. */
check(JSON.stringify(t.encodeMacro('a{3}')) === JSON.stringify([0x04, 0x03, 0x02]),
      'a{3} is "a" and two more');
check(JSON.stringify(t.encodeMacro('x{1}')) === JSON.stringify([0x1B]),
      'one of something is just the something');
/* The reason the group exists: an accented letter is two keystrokes, so
   repeating "the previous keystroke" repeats the letter alone. */
check(JSON.stringify(t.encodeMacro('ç{5}')) ===
      JSON.stringify([t.ESC_MARK, 0x34, 0x06, 0x03, 0x04]),
      'ç{5} marks the pair, so the accent repeats with the letter');
check(JSON.stringify(t.encodeMacro('(ab){3}')) ===
      JSON.stringify([t.ESC_MARK, 0x04, 0x05, 0x03, 0x02]), '(ab){3} is a group');
/* A parenthesis is a parenthesis unless a count follows the closing one. */
check(!t.encodeMacro('f(x)').includes(t.ESC_MARK), 'f(x) has no group in it');
check(t.encodeMacro('f(x){2}').includes(t.ESC_MARK), 'f(x){2} does');
msg = null;
try { t.encodeMacro('(a{2}b){3}'); } catch (e) { msg = e.message; }
check(msg !== null && /one mark/.test(msg),
      `a count inside a group must be refused, got ${JSON.stringify(msg)}`);
msg = null;
try { t.encodeMacro('(){2}'); } catch (e) { msg = e.message; }
check(msg !== null && /nothing in it/.test(msg), `an empty group: ${msg}`);

for (const text of ['a{3}', 'ç{5}', '(ab){3}', 'f(x)', 'f(x){2}', 'x{1}',
                    '{Ctrl+c}{3}', '(oi){2}!', '(\\n){3}', 'coração{2}',
                    'a(b){2}c', 'x{256}', '((a){2}){3}', 'f\\(x\\){2}']) {
  const bytes = t.encodeMacro(text);
  const again = t.encodeMacro(t.decodeMacro(bytes));
  check(JSON.stringify(bytes) === JSON.stringify(again),
        `${JSON.stringify(text)} drifts: ${JSON.stringify(t.decodeMacro(bytes))}`);
}

console.log('5c. jumps, and the loop nothing could stop');
check(JSON.stringify(t.encodeMacro('a{goto .}')) ===
      JSON.stringify([0x04, t.ESC_JUMP, 0x01]),
      'the jump carries the header plus one, since a zero would end the macro');
check(t.decodeMacro(t.encodeMacro('{goto -.}')) === '{goto -.}',
      'and reads back as the code it names');

/* Touching the pad stops a macro, so a loop that types or pauses can always be
   got out of and is a feature. A loop that does neither never reaches a point
   where anyone is asked, so the firmware counts jumps instead -- and nobody
   meant to write one. Only what happens BEFORE the jump counts, since nothing
   after a jump ever runs. */
for (const [rows, want] of [
  [[{ code: '.', text: 'a{goto -}' }, { code: '-', text: 'b' }], null],
  [[{ code: '.', text: 'a{pause 1.0}{goto .}' }], null],
  [[{ code: '.', text: 'a{goto .}' }], null],
  [[{ code: '.', text: 'a{goto -}' }, { code: '-', text: 'b{goto .}' }], null],
  [[{ code: '.', text: '{goto -}' }, { code: '-', text: '{goto .}' }], /never type or pause/],
  [[{ code: '.', text: '{goto .}' }], /never type or pause/],
  [[{ code: '.', text: 'a{goto -}{pause 1.0}' }, { code: '-', text: '{goto .}' }], null],
  [[{ code: '.', text: 'a{goto --}' }], /no macro with that code/],
]) {
  let msg = null;
  try { t.encodeTable(rows); } catch (e) { msg = e.message; }
  if (want === null)
    check(msg === null, `${rows[0].text} should be allowed, got ${msg}`);
  else
    check(msg !== null && want.test(msg),
          `${rows[0].text} should be refused with ${want}, got ` +
          (msg === null ? 'nothing' : JSON.stringify(msg)));
}

console.log('5d. v2\'s host check: gone, but readable');
/* Firmware 16 refuses every {host}; one read off an older table still shows,
   so that it can be changed into a {secret} */
msg = null;
try { t.encodeMacro('{host}pw'); } catch (e) { msg = e.message; }
check(msg !== null && /\{secret\}/.test(msg), `{host} is refused, pointing at {secret}: ${msg}`);
msg = null;
try { t.encodeMacro('a{HOST!}'); } catch (e) { msg = e.message; }
check(msg !== null, '{host!} too, any case');
check(t.decodeMacro([0x13, t.ESC_HOST, t.HOST_SILENT, 0x1A]) === 'p{host}w' &&
      t.decodeMacro([t.ESC_HOST, t.HOST_LOUD]) === '{host!}',
      'and both read back as themselves');
check(t.encodeMacro('host').length === 4, 'the word is just a word');

/* The two ends define these separately, so read the firmware's own values
   rather than trust that somebody remembered to change both. */
const ino = require('fs').readFileSync(require('path').join(__dirname, '../DigiMacro.ino'), 'utf8');
const def = name => Number(new RegExp(`#define ${name}\\s+(0x[0-9A-Fa-f]+|\\d+)`).exec(ino)?.[1]);
for (const name of ['ESC_HOST', 'HOST_SILENT', 'HOST_LOUD', 'ESC_JUMP', 'ESC_MARK',
                    'ESC_IR'])
  check(def(name) === t[name], `${name}: the firmware says ${def(name)}, table.js ${t[name]}`);

console.log('5e. the remote');
const ir = text => JSON.stringify(t.encodeMacro(text));
check(ir('{IR 00 45}') === JSON.stringify([t.ESC_IR, 0x80, 0xC5, 0x80]),
      `{IR 00 45}: ${ir('{IR 00 45}')}`);
check(ir('{ir FF 80}') === JSON.stringify([t.ESC_IR, 0xFF, 0x80, 0x83]),
      `{ir FF 80}: high bits in the third byte, any case: ${ir('{ir FF 80}')}`);
check(ir('{IR 7 a}') === ir('{IR 07 0a}'), 'one digit is a byte too');
for (const bad of ['{IR}', '{IR 00}', '{IR 100 00}', '{IR 00 45 01}', '{IR xx 00}',
                   '{IR0045}'])
  { msg = null; try { t.encodeMacro(bad); } catch (e) { msg = e.message; }
    check(msg !== null && /two bytes/.test(msg), `${bad} is refused: ${msg}`); }
check(!t.encodeMacro('{IR 00 45}x').includes(0), 'never a zero, address 0 or not');
for (const text of ['{IR 00 45}', 'a{IR ff ff}b', '{IR 80 c5}{IR 01 7f}',
                    '{IR 80 45}{IR 00 c5}',
                    '{IR 02 03}x{2}'])
  check(t.decodeMacro(t.encodeMacro(text)) === text,
        `${text} reads back as ${t.decodeMacro(t.encodeMacro(text))}`);
/* {IR 02 03} holds 0x82 and 0x83 -- the mark's and the host check's values
   -- so anything that walks a macro byte by byte rather than escape by
   escape finds things that are not there. */
check(t.macroWarnings(t.encodeMacro('{IR 02 02}{secret!}x',
                                    p => t.sealSecret(new Uint8Array(16), p))).length === 0,
      'and nothing shows: a {secret!} after one is still first');
msg = null;
try { t.encodeMacro('(a{IR 02 03}){2}'); } catch (e) { msg = e.message; }
check(msg === null, `nor a mark or a count inside a group: ${msg}`);
msg = null;
try { t.encodeMacro('x{IR 00 45}{2}'); } catch (e) { msg = e.message; }
check(msg !== null, 'a place is not a thing a count can count');
msg = null;
try { t.encodeTable([{ code: '.', text: '{IR 00 45}{goto .}' }]); } catch (e) { msg = e.message; }
check(msg !== null && /never type or pause/.test(msg), 'nor an action');

console.log('5f. IR1 to IR30, the codes no tapping makes');
const tapped = [];
for (let n = 1; n <= t.MAX_SYM; n++)
  for (let b = 0; b < 1 << n; b++) {
    let code = '';
    for (let i = n - 1; i >= 0; i--) code += (b >> i) & 1 ? '-' : '.';
    tapped.push(code);
  }
check(tapped.length === 62, 'sixty-two codes can be tapped');
for (const code of tapped)
  check(t.codeOf(t.header(code)) === code && t.tappable(code), `${code} round trips`);
const tapHdr = new Set(tapped.map(t.header));
for (let k = 1; k <= t.IR_CODES; k++) {
  const h = t.header('IR' + k);
  check(!tapHdr.has(h), `IR${k} is no tapped code's header`);
  check(t.codeOf(h) === 'IR' + k && !t.tappable('IR' + k), `IR${k} round trips`);
  check((h & 0x07) < t.MAX_SYM && h + 1 < 0xFD, `IR${k} is a header a goto can name`);
}
check(t.header('ir7') === t.header('IR7'), 'either case');
for (const bad of ['IR0', 'IR31', 'IR', 'IR 3', 'IR03', 'IR-1']) {
  msg = null;
  try { t.header(bad); } catch (e) { msg = e.message; }
  check(msg !== null && /not a code/.test(msg), `${bad} is refused: ${msg}`);
}
/* Untappable headers the page never writes come back as something it
   refuses, not as the tappable code their low bits spell. */
check(t.codeOf(0x21) === '#21', `0x21 reads as ${t.codeOf(0x21)}`);
msg = null;
try { t.header(t.codeOf(0x21)); } catch (e) { msg = e.message; }
check(msg !== null, 'and is not written back');
check(t.decodeMacro(t.encodeMacro('{goto ir5}')) === '{goto IR5}', 'a goto to one');

console.log('6. accents, on a layout that can type them');
/* The board sends scan codes, so an accent is the host's business: two
   keystrokes on a dead-key layout, and impossible on a plain US one. Getting
   that wrong silently types something else, so it is refused rather than
   guessed at. */
t.setLayout('us');
msg = null;
try { t.encodeMacro('ó'); } catch (e) { msg = e.message; }
check(msg !== null && /dead keys/.test(msg), `accent on plain US: ${msg}`);
check(t.getLayout() === 'us', 'the layout is where it was put');

t.setLayout('us-intl');
const APOS = String.fromCharCode(39);
check(JSON.stringify(t.encodeMacro('ó')) === JSON.stringify([0x34, 0x12]),
      'ó is the acute key and then o');
check(JSON.stringify(t.encodeMacro('ç')) === JSON.stringify([0x34, 0x06]),
      'ç is the acute key and then c');
check(JSON.stringify(t.encodeMacro('ã')) === JSON.stringify([0xB5, 0x04]),
      'ã is the tilde and then a');
/* A bare mark is the mark and a space, which is how a dead-key layout is
   asked for the mark alone. */
check(JSON.stringify(t.encodeMacro(APOS)) === JSON.stringify([0x34, 0x2C]),
      'a literal apostrophe is the key and a space');
/* ...and the mark with nothing that combines after it has its own form, the
   only one that survives being encoded again. */
check(t.decodeMacro([0x34, 0x17]) === '{' + APOS + '}t',
      `apostrophe then t reads back as {${APOS}}t, got ` +
      JSON.stringify(t.decodeMacro([0x34, 0x17])));
/* Pasted text may carry its accents as separate combining marks, which look
   identical and are not. */
check(JSON.stringify(t.encodeMacro('e\u0301')) === JSON.stringify(t.encodeMacro('é')),
      'a decomposed accent must encode as the composed one does');

for (const text of ['coração', 'Olá, você!', 'não{Enter}', '{Ctrl+c}á',
                    APOS, '{' + APOS + '}t', 'Ação', 'até logo']) {
  const bytes = t.encodeMacro(text);
  const again = t.encodeMacro(t.decodeMacro(bytes));
  check(JSON.stringify(bytes) === JSON.stringify(again),
        `${JSON.stringify(text)} drifts: ${JSON.stringify(t.decodeMacro(bytes))}`);
}

/* LEARN's two bytes, read by key position so the layout cannot change them */
check(t.irHeard('04 08') === '{IR 04 08}', 'irHeard reads "04 08"');
check(t.irHeard(' 8E c5 ') === '{IR 8e c5}', 'irHeard: any case, spaces around');
check(t.irHeard('04') === null && t.irHeard('04 0') === null,
      'irHeard waits for both bytes');
check(t.irHeard('04 08 1') === null && t.irHeard('0g 08') === null,
      'irHeard refuses anything else');
check(['Digit0', 'Numpad7', 'KeyA', 'KeyF', 'Space'].map(t.irKeyChar).join('') === '07af ',
      'irKeyChar maps digits, a-f and the space');
check(t.irKeyChar('KeyG') === null && t.irKeyChar('Enter') === null,
      'irKeyChar ignores other keys');

console.log('6b. ABNT2, Brazil\'s');
/* Its accents are keys of their own, ç is a key, and its apostrophe and
   quote are not dead. "/" is the keypad's and "?" is AltGr+W: the board
   cannot send ABNT2's own key for them, usage 0x87. */
t.setLayout('abnt2');
const bytesOf = s => JSON.stringify(t.encodeMacro(s));
const want = (s, b, what) => check(bytesOf(s) === JSON.stringify(b),
                                   `abnt2: ${what}, got ${bytesOf(s)}`);
want('ç', [0x33], 'ç is its own key');
want('Ç', [0xB3], 'Ç is Shift and that key');
want('á', [0x2F, 0x04], 'á is the acute key and then a');
want('à', [0xAF, 0x04], 'à is Shift and the acute key, then a');
want('ã', [0x34, 0x04], 'ã is the tilde key and then a');
want('ô', [0xB4, 0x12], 'ô is Shift and the tilde key, then o');
want('ü', [0xA3, 0x18], 'ü is Shift and 6, then u');
want(APOS + '"', [0x35, 0xB5], 'the apostrophe and the quote are plain keys');
want('~', [0x34, 0x2C], 'a bare tilde is the key and a space');
want('/', [0x54], '/ is the keypad\'s');
want('?', [t.ESC_MOD, 0x40, 0x1A], '? is AltGr+W');
want('º', [t.ESC_MOD, 0x40, 0x32], 'º is AltGr and the key beside Enter');
want(']}\\\\|;:', [0x32, 0xB2, 0x64, 0xE4, 0x38, 0xB8], 'the keys that moved');
want('{Ctrl+ç}', [t.ESC_MOD, 0x01, 0x33], 'ç takes a modifier');
check(t.decodeMacro([t.ESC_MOD, 0x40, 0x1A]) === '?', 'AltGr+W reads back as ?');
check(t.decodeMacro([0x2F, 0x17]) === '{´}t', 'the acute then t reads back as {´}t');
msg = null;
try { t.encodeMacro('ń'); } catch (e) { msg = e.message; }
check(msg !== null && /no key/.test(msg), `ń has no key on ABNT2: ${msg}`);
for (const text of ['Ação, você? Até já!', 'nº 1, 2ª via, 30°C, §3', 'pão{Enter}',
                    '(a/b?){2}', '?{3}', 'lingüiça', '~^´`¨ ' + APOS + '"',
                    '{Ctrl+c}á', 'ÀÉÎÕÜ', 'x{´}t']) {
  const bytes = t.encodeMacro(text);
  const back = t.decodeMacro(bytes);
  check(JSON.stringify(t.encodeMacro(back)) === JSON.stringify(bytes),
        `abnt2: ${JSON.stringify(text)} drifts: ${JSON.stringify(back)}`);
}
/* Every character a layout has a key for reads back as itself */
for (const name of Object.keys(t.LAYOUTS)) {
  t.setLayout(name);
  const l = t.LAYOUTS[name];
  for (const ch of [...Object.keys(l.plain), ...Object.keys(l.shifted),
                    ...Object.keys(l.altgr)]) {
    if (l.dead && ch in l.dead) continue;       /* the mark and a space */
    const back = t.decodeMacro(t.usages(ch));
    check(back === ch || back === { '\n': '\\n', '\t': '\\t', '\b': '\\b',
                                    '\\': '\\\\', '{': '{{' }[ch],
          `${name}: ${JSON.stringify(ch)} reads back as ${JSON.stringify(back)}`);
  }
}
t.setLayout('us-intl');

console.log('6c. the pointer: {x,y} and {click}');
/* {host}'s other arguments: x in 128 steps, y in 64, then a click */
const hexs = s => t.encodeMacro(s).map(v => v.toString(16)).join(' ');
check(hexs('{0,0}') === '83 80 83 40', `the corner: ${hexs('{0,0}')}`);
check(hexs('{100,100}') === '83 ff 83 7f', `the far corner: ${hexs('{100,100}')}`);
check(hexs('{click}') === '83 31', `a click: ${hexs('{click}')}`);
check(hexs('{right-click}{hold}{right-hold}{release}') === '83 32 83 21 83 22 83 20',
      `the other buttons: ${hexs('{right-click}{hold}{right-hold}{release}')}`);
check(t.decodeMacro(t.encodeMacro('{Right-Click}{right-release}')) === '{right-click}{release}',
      'any case; a release lets every button up');
check(t.decodeMacro(t.encodeMacro('{25,75}{click}')) === '{25,75}{click}',
      'a point in whole percents reads back as itself');
let drifts = 0;
for (let x = 0; x <= 100; x += 0.5)
  for (let y = 0; y <= 100; y += 0.5) {
    const b = t.encodeMacro(`{${x},${y}}`);
    if (JSON.stringify(t.encodeMacro(t.decodeMacro(b))) !== JSON.stringify(b)) drifts++;
  }
check(drifts === 0, `${drifts} points drift when read back`);
const unround = [];
for (let p = 0; p <= 100; p += 5)
  if (t.decodeMacro(t.encodeMacro(`{${p},${p}}`)) !== `{${p},${p}}`) unround.push(p);
check(!unround.length, `round percents read back as typed, though y has 64 steps: ${unround}`);
check(hexs('{50,50}{click}{3}') === '83 c0 83 60 82 83 31 3 2',
      `three clicks are a group: ${hexs('{50,50}{click}{3}')}`);
msg = null;
try { t.encodeMacro('{100.5,0}'); } catch (e) { msg = e.message; }
check(msg !== null && /percent/.test(msg), `past the edge: ${msg}`);
check(t.decodeMacro([0x83, 0x01]) === '{host}', '{host} still reads as itself');
/* {x*a,y*b}: the move's counts a report, 4a, ahead of it -- only where not 1 */
check(hexs('{40*2,90*2.75}') === '83 8 83 1b 83 b3 83 79',
      `a scaled move: ${hexs('{40*2,90*2.75}')}`);
check(hexs('{40*1,90}') === hexs('{40,90}'), 'a scale of 1 is no scale');
check(hexs('{40,90 * 3.75}') === '83 1f 83 b3 83 79', `y alone: ${hexs('{40,90 * 3.75}')}`);
for (const s of ['{40*2,90*2.75}{click}', '{40,90*0.75}', '{12*3.5,25}'])
  check(t.decodeMacro(t.encodeMacro(s)) === s, `${s} reads back as itself: ${t.decodeMacro(t.encodeMacro(s))}`);
check(t.decodeMacro(t.encodeMacro('{40*2.1,90}')) === '{40*2,90}', 'a scale goes in quarters');
msg = null;
try { t.encodeMacro('{40/2,90}'); } catch (e) { msg = e.message; }
check(msg !== null, 'the spike\'s slash is not a scale any more');
for (const bad of ['{40*0.5,90}', '{40*4,90}', '{40,90*0}']) {
  msg = null;
  try { t.encodeMacro(bad); } catch (e) { msg = e.message; }
  check(msg !== null && /scale/.test(msg), `${bad}: ${msg}`);
}

console.log('6d. modifiers alone: {Gui}, {Win}, {Cmd}, and Gui still a modifier');
check(hexs('{Gui}') === '2 8 81 f6', `{Gui}: ${hexs('{Gui}')}`);
check(hexs('{win}') === hexs('{Gui}') && hexs('{Cmd}') === hexs('{Gui}'),
      'Win and Cmd are Gui');
check(hexs('{Ctrl+Shift}') === '2 3 81 f6', `{Ctrl+Shift}: ${hexs('{Ctrl+Shift}')}`);
for (const s of ['{Gui}', 'a{Gui}b', '{Ctrl+Shift}', '{Gui}{3}', '{Gui+d}', '{Shift}x'])
  check(t.decodeMacro(t.encodeMacro(s)) === s, `${s} reads back as ${t.decodeMacro(t.encodeMacro(s))}`);
msg = null;
try { t.encodeTable([{ code: '.', text: '{Gui}' }, { code: '..', text: '{Gui}{goto .}' }]); }
catch (e) { msg = e.message; }
check(msg === null, `not a jump: ${msg}`);

console.log('6e. {clear}: the lockout forgotten, by the owner\'s choice');
check(hexs('{clear}') === '81 f7', `{clear}: ${hexs('{clear}')}`);
check(t.decodeMacro(t.encodeMacro('ab{Clear}c')) === 'ab{clear}c', 'reads back, any case');
msg = null;
try { t.encodeTable([{ code: '.-', text: 'a{clear}b' }, { code: '..', text: '{clear}{goto .-}' }]); }
catch (e) { msg = e.message; }
check(msg === null, `not a jump to nowhere, nor a loop: ${msg}`);

console.log('7. secrets');
const hex = b => [...b].map(v => v.toString(16).padStart(2, '0')).join('');
const unhex = h => Uint8Array.from(h.match(/../g).map(x => parseInt(x, 16)));
/* XTEA against the spike's vectors (build/cipher-spike, from the C reference
   checked against the published one): the published vector, then two random */
for (const [k, v, c] of [
  ['00000000000000000000000000000000', '0000000000000000', 'd8d4e9ded91e13f7'],
  ['ccc823e037ddb699c51d38fa71d871b8', 'f5de8fe876283f27', '2ca13c8511c9606e'],
  ['38bbf812743365ccb9a37f8e2f6e037a', 'a2572dc73d7e2a38', '85861e25177007f3']])
  check(hex(t.xtea(unhex(v), unhex(k))) === c, `xtea(${v}, ${k}) gives ${c}`);

/* Deterministic randomness, so a failure can be reproduced */
let seed = 85;
const rnd = n => Uint8Array.from({ length: n }, () =>
  (seed = (seed * 1103515245 + 12345) >>> 0) >>> 24);
const KEY = rnd(16), OTHER = rnd(16);
const sealer = plain => t.sealSecret(KEY, plain, rnd);

check((t.SECRET_QUIET - 1 & 7) >= t.MAX_SYM && (t.SECRET_LOUD - 1 & 7) >= t.MAX_SYM,
      "a secret's marker is a jump to a code no macro can have");
check(def('ESC_JUMP') === t.ESC_JUMP, 'the marker is the firmware\'s jump');

/* Sealing: always walkable, never zero, and back to the same keystrokes */
let sealedOk = 0, opened = 0, tries = 0;
for (let n = 0; n < 300; n++) {
  const plain = [...rnd(1 + (n * 7) % t.SECRET_MAX)].map(v => (v & 0x7F) < 4 ? 0x04 : v);
  const blob = t.sealSecret(KEY, plain, rnd);
  if (t.walkSafe(blob) && !blob.includes(0) && (blob[0] | blob[1] | blob[2] | blob[3]))
    sealedOk++;
  if (JSON.stringify(t.openSecret(KEY, blob)) === JSON.stringify(plain)) opened++;
  if (JSON.stringify(t.openSecret(OTHER, blob)) === JSON.stringify(plain)) tries++;
}
check(t.sealSecret(KEY, new Array(t.SECRET_MAX).fill(4), rnd).length === 4 + t.SECRET_MAX + 1,
      'the longest secret seals');
{ let m = null; try { t.sealSecret(KEY, new Array(t.SECRET_MAX + 1).fill(4), rnd); }
  catch (e) { m = e.message; }
  check(m !== null && /at most 255/.test(m), `one keystroke more does not: ${m}`); }
check(sealedOk === 300, `every sealed secret walks safely (${sealedOk} of 300)`);
check(opened === 300, `every sealed secret opens again (${opened} of 300)`);
check(tries === 0, `another key opens none (${tries} did)`);
/* ...though about one in 256 decrypts to something that ends in a zero:
   which is why a key is checked against the check value, never by trying */

/* The keystream, spelled out from xtea() alone: byte j is byte j&7 of
   XTEA(nonce, j>>3) -- what the firmware will compute, so a sealer that
   agreed only with its own opener would fail here */
{
  const plain = [...'abcdefghijkl'].map(c => c.charCodeAt(0) - 93);  /* a..l */
  const blob = t.sealSecret(KEY, plain, rnd);
  const ks = [...plain, 0].map((v, j) => t.xtea([...blob.slice(0, 4), j, 0, 0, 0], KEY)[0]);
  check(JSON.stringify(blob.slice(4)) ===
        JSON.stringify([...plain, 0].map((v, j) => v ^ ks[j])),
        "the keystream is XTEA(nonce, the byte's place)'s first byte, little-endian");
  check(JSON.stringify([...t.keyCheck(KEY)]) === JSON.stringify([...t.xtea(new Uint8Array(8), KEY)]),
        'the check value is XTEA(0, 0)');
}

/* A nonce of zero would give the check value's block: never used */
let zeros = 2;
const zeroFirst = n => zeros-- > 0 ? new Uint8Array(n) : rnd(n);
check(t.sealSecret(KEY, [0x04], zeroFirst).slice(0, 4).some(v => v),
      'a nonce of zero is drawn again');

/* The walk refuses what would lose its place */
check(!t.walkSafe([0x04, 0x00, 0x04]), 'a zero is not safe');
check(!t.walkSafe([0x04, 0x80, 0x81, 0x81, 0x81]), 'an {IR} is not safe');
check(!t.walkSafe([0x04, 0x01]), 'an escape whose argument is the terminator is not');
check(t.walkSafe([0x01, 0x80, 0x04]), "but 0x80 as an escape's argument is");

/* Text, with and without the key */
const SECRET_ROWS = [
  { code: '.',  text: 'user\\t{secret}hunter2{Enter}' },
  { code: '-',  text: '{secret!}{Ctrl+a}top (secret){3} {pause 0.5}{goto .}' },
  { code: '..', text: 'plain{IR 00 45}' },
];
/* The EEPROM as the page writes it: the table, and the key's check value */
const eeprom = (table, key) => { const e = new Uint8Array(512).fill(0xFF);
  e.set(table, 0); if (key) e.set(t.keyCheck(key), t.CHECK_ADDR); return e; };
const hidusb = require('fs').readFileSync(require('path').join(__dirname, '../app/hidusb.h'), 'utf8');
check(t.CHECK_ADDR === Number(/#define DM_CHECK_ADDR\s+(0x[0-9A-Fa-f]+)/.exec(hidusb)?.[1]),
      'the check value is where the firmware looks for it');
const stTable = t.encodeTable(SECRET_ROWS, { key: KEY });
check(stTable[stTable.length - 1] === 0xFF && stTable.length < t.TABLE_TOP,
      'a table with secrets ends as any table does');
const st = eeprom(stTable, KEY);
check(!hex(st).includes(hex(t.encodeMacro('hunter2'))), 'the secret is not in the table');
const withKey = t.decodeTable(st, KEY);
check(JSON.stringify(withKey.map(r => r.text)) === JSON.stringify(
        ['user\\t{secret}hunter2\\n', '{secret!}{Ctrl+a}top (secret){3} {pause 0.5}{goto .}',
         'plain{IR 00 45}']),
      'with the key, secrets read back as text: ' + JSON.stringify(withKey.map(r => r.text)));
const blind = t.decodeTable(st);
check(/^user\\t\{secret [0-9a-f]+\}$/.test(blind[0].text) && /^\{secret! [0-9a-f]+\}$/.test(blind[1].text),
      'without it, as their hex: ' + JSON.stringify(blind.map(r => r.text)));
/* Without the key, a table can still be rearranged: sealed secrets move */
const moved = eeprom(t.encodeTable([blind[2], { code: '--', text: 'new' }, blind[1], blind[0]],
                                   { check: t.keyCheck(KEY) }), KEY);
const movedBack = t.decodeTable(moved, KEY);
check(movedBack.find(r => r.code === '.').text === withKey[0].text &&
      movedBack.find(r => r.code === '-').text === withKey[1].text,
      'sealed secrets move without the key, and still open');
/* Old walks: the firmware's, by escapes, and the old page's, by zeros, agree */
const heads = (b, esc) => { const out = []; let i = 0;
  while (i < t.TABLE_TOP && (b[i] & 7) < t.MAX_SYM) { out.push(i); i++;
    if (esc) { while (b[i]) i += b[i] === t.ESC_IR ? 4 : (b[i] & 0x7F) < 4 && b[i] !== t.ESC_MARK ? 2 : 1; }
    else while (b[i]) i++;
    i++; } return out.join(); };
check(heads(moved, true) === heads(moved, false) && heads(st, true) === heads(st, false),
      'walking by escapes and by zeros finds the same macros');

/* Refusals */
const throws = (f, re, what) => { let m = null; try { f(); } catch (e) { m = e.message; }
  check(m !== null && re.test(m), `${what}: ${m}`); };
throws(() => t.encodeMacro('a{secret}b'), /needs the key/, 'no key');
throws(() => t.encodeMacro('a{secret}b{secret}c', sealer), /one \{secret\}/, 'two secrets');
throws(() => t.encodeMacro('(a{secret}b){2}', sealer), /inside a group/, 'in a group');
throws(() => t.encodeMacro('{secret}x{IR 00 45}', sealer), /remote/, 'an IR after');
throws(() => t.encodeMacro(blind[0].text + 'x'), /nothing can follow/, 'after a sealed one');
throws(() => t.encodeMacro(blind[0].text.replace(/[0-9a-f]\}$/, '}')), /changed/, 'a cut blob');
throws(() => t.encodeTable([{ code: '.', text: blind[0].text }], { key: OTHER, check: t.keyCheck(KEY) }),
       /not the key/, 'another key, writing');
throws(() => t.decodeTable(st, OTHER), /not the key/, 'another key, reading');
check(t.macroWarnings(t.encodeMacro('x{secret!}y', sealer)).includes('secret-late') &&
      t.macroWarnings(t.encodeMacro('{secret!}y', sealer)).length === 0 &&
      t.macroWarnings(t.encodeMacro('x{secret}y', sealer)).length === 0,
      'something before {secret!} is a warning, before {secret} not');
check(t.macroWarnings(t.encodeMacro('{40,40}{secret!}y', sealer)).includes('secret-late') &&
      t.macroWarnings(t.encodeMacro('{click}{secret!}y', sealer)).includes('secret-late') &&
      t.macroWarnings(t.encodeMacro('{clear}{secret!}y', sealer)).length === 0 &&
      t.macroWarnings(t.encodeMacro('{Gui}{secret!}y', sealer)).includes('secret-late'),
      'the mouse and {Gui} show, as typing does; {clear} does not');
/* What a secret holds is never walked for jumps: its bytes would read as
   gotos to codes that are not there */
let spurious = 0;
for (let n = 0; n < 300; n++)
  try { t.encodeTable([{ code: '.', text: '{secret}' + 'abcdefghij'.slice(0, 1 + n % 10) },
                       { code: '-', text: '{goto .}' }], { key: KEY }); }
  catch (e) { spurious++; }
check(spurious === 0, `a secret's bytes never read as jumps (${spurious} of 300 did)`);

console.log(fails ? '\nSOME CHECKS FAILED' : '\nall checks passed');
process.exit(fails ? 1 : 0);
