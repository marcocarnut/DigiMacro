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
  { code: '.',   text: 'hello from DigiMacro\\n' },
  { code: '-',   text: 'wait{pause 1.0} ok' },
  { code: '..',  text: 'ab{4}' },      /* four b in all, not five */
  { code: '.-',  text: '{Ctrl+Left}z' },
  { code: '-.',  text: '{Ctrl+Shift+Left}{Shift+Left}{Alt+Tab}{F5}' },
  { code: '--',  text: 'A!Z {{x}} \\t' },
  { code: '...', text: 'coração{Enter}' },      /* needs the dead keys below */
];

/* The rows include accented text, so the layout has to be the one that can
   type it -- and saying so here keeps --emit deterministic. */
t.setLayout('us-intl');

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

console.log('5d. the host check, quiet and loud');
check(JSON.stringify(t.encodeMacro('{host}pw')) ===
      JSON.stringify([t.ESC_HOST, t.HOST_SILENT, 0x13, 0x1A]),
      '{host} is the escape with 1');
check(JSON.stringify(t.encodeMacro('{HOST!}')) ===
      JSON.stringify([t.ESC_HOST, t.HOST_LOUD]), '{host!} with 2, any case');
check(t.decodeMacro(t.encodeMacro('user\\t{host}pw')) === 'user\\t{host}pw',
      'and both read back as themselves');
msg = null;
try { t.encodeMacro('{host}{3}'); } catch (e) { msg = e.message; }
check(msg !== null, 'a check is not a thing a count can count');
/* A loud check is only as good as what comes before it: nothing. */
check(t.macroWarnings(t.encodeMacro('{host!}secret')).length === 0,
      'first is right');
check(t.macroWarnings(t.encodeMacro('{Ctrl+a}{host!}x')).length === 1,
      'a keystroke first gives the code away');
check(t.macroWarnings(t.encodeMacro('name{host}pw')).length === 0,
      'a quiet one may come anywhere: it says nothing to anyone');
check(t.hasHostCheck(t.encodeMacro('a{pause 1.0}{host}')), 'a check is found');
check(!t.hasHostCheck(t.encodeMacro('host')), 'and the word is not one');
/* A loop that only checks the host still never types or pauses */
msg = null;
try { t.encodeTable([{ code: '.', text: '{host}{goto .}' }]); } catch (e) { msg = e.message; }
check(msg !== null && /never type or pause/.test(msg), 'a check is not an action');

/* The two ends define these separately, so read the firmware's own values
   rather than trust that somebody remembered to change both. */
const ino = require('fs').readFileSync(require('path').join(__dirname, '../DigiMacro.ino'), 'utf8');
const def = name => Number(new RegExp(`#define ${name}\\s+(0x[0-9A-Fa-f]+|\\d+)`).exec(ino)?.[1]);
for (const name of ['ESC_HOST', 'HOST_SILENT', 'HOST_LOUD', 'ESC_JUMP', 'ESC_MARK'])
  check(def(name) === t[name], `${name}: the firmware says ${def(name)}, table.js ${t[name]}`);

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

console.log(fails ? '\nSOME CHECKS FAILED' : '\nall checks passed');
process.exit(fails ? 1 : 0);
