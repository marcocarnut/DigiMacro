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
  { code: '.',   text: 'hello from WebUSB\\n' },
  { code: '-',   text: 'wait{pause 1.0} ok' },
  { code: '..',  text: 'ab{4}' },
  { code: '.-',  text: '{Ctrl+Left}z' },
  { code: '-.',  text: '{Ctrl+Shift+Left}{Shift+Left}{Alt+Tab}{F5}' },
  { code: '--',  text: 'A!Z {{x}} \\t' },
];

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
for (const [text, want] of [
  ['{',              /no \}/],
  ['{Ctrl+}',        /no key after/],
  ['{Nope+a}',       /not a modifier/],
  ['{Blah}',         /not a key/],
  ['{pause 0}',      /0\.1 to 25\.5/],
  ['{pause 30}',     /0\.1 to 25\.5/],
  ['a{0}',           /1 to 255/],
  ['a{x0}',          /1 to 255/],
  ['a{x300}',        /1 to 255/],
  ['{x3}',           /nothing to repeat/],
  ['\\q',            /means nothing/],
  ['é',         /no key for/],
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

console.log(fails ? '\nSOME CHECKS FAILED' : '\nall checks passed');
process.exit(fails ? 1 : 0);
