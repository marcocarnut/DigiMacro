/* table.js -- text and codes in, a packed DigiMacro table out.
 *
 * The one original: eeprom.html loads it in the browser, and test/run.sh
 * loads it in node and plays what it produces against the real firmware. The
 * table is the interface between the two halves of this project, so the app's
 * encoder and the firmware's decoder are checked against each other rather
 * than each being trusted on its own.
 *
 * Keep the escape values in step with DigiMacro.ino. Note that 0x04 is NOT
 * free here: it is the usage for "a". What is free is 0x01 to 0x03, which are
 * states a keyboard reports rather than keys anyone presses, and the same
 * three with bit 7 set.
 */

const ESC_PAUSE  = 0x01;    /* nn tenths of a second */
const ESC_MOD    = 0x02;    /* a HID modifier mask, for the next keystroke */
const ESC_REPEAT = 0x03;    /* press the previous keystroke nn more times */
const ESC_JUMP   = 0x81;    /* not implemented in the firmware yet */
const SHIFT      = 0x80;    /* in a stored keystroke */

const TABLE_TOP  = 0x1F1;   /* the firmware stops walking here */
const MAX_SYM    = 5;

/* US layout, enough of it to write a table */
const PLAIN = {' ': 0x2C, '-': 0x2D, '=': 0x2E, '[': 0x2F, ']': 0x30,
               '\\': 0x31, ';': 0x33, "'": 0x34, '`': 0x35, ',': 0x36,
               '.': 0x37, '/': 0x38, '\n': 0x28, '\t': 0x2B, '\b': 0x2A};
const SHIFTED = {'!': 0x1E, '@': 0x1F, '#': 0x20, '$': 0x21, '%': 0x22,
                 '^': 0x23, '&': 0x24, '*': 0x25, '(': 0x26, ')': 0x27,
                 '_': 0x2D, '+': 0x2E, '{': 0x2F, '}': 0x30, '|': 0x31,
                 ':': 0x33, '"': 0x34, '~': 0x35, '<': 0x36, '>': 0x37,
                 '?': 0x38};

/* Text to keystrokes. Bit 7 carries Shift, so one byte covers every character
   on the keyboard and the firmware needs no ASCII table of its own. */
function usages(text) {
  const out = [];
  for (const ch of text) {
    if (ch >= 'a' && ch <= 'z')      out.push(ch.charCodeAt(0) - 97 + 0x04);
    else if (ch >= 'A' && ch <= 'Z') out.push((ch.charCodeAt(0) - 65 + 0x04) | SHIFT);
    else if (ch >= '1' && ch <= '9') out.push(ch.charCodeAt(0) - 49 + 0x1E);
    else if (ch === '0')             out.push(0x27);
    else if (ch in PLAIN)            out.push(PLAIN[ch]);
    else if (ch in SHIFTED)          out.push(SHIFTED[ch] | SHIFT);
    else throw new Error(`no usage for "${ch}"`);
  }
  return out;
}

/* ".-" -> the header byte: three bits of count-minus-one, five of symbols,
   a dash being a 1 and the first symbol in the highest bit the count uses. */
function header(code) {
  if (!new RegExp(`^[.-]{1,${MAX_SYM}}$`).test(code))
    throw new Error(`not a code: "${code}"`);
  let bits = 0;
  for (const c of code) bits = (bits << 1) | (c === '-' ? 1 : 0);
  return (bits << 3) | (code.length - 1);
}

/* [[code, keystrokes], ...] -> the whole table, terminator and all. Entries
   are packed, so their order in the array is their order in EEPROM. */
function packTable(entries) {
  const out = [];
  const seen = new Set();
  for (const [code, body] of entries) {
    const h = header(code);
    if (seen.has(h)) throw new Error(`"${code}" appears twice`);
    seen.add(h);
    if (body.some(b => !(b >= 0 && b <= 255)))
      throw new Error(`"${code}" has a byte that is not a byte`);
    if (body.includes(0))
      throw new Error(`"${code}" contains a zero, which would end it early`);
    out.push(h, ...body, 0);
  }
  out.push(0xFF);
  if (out.length > TABLE_TOP)
    throw new Error(`${out.length} bytes of table, and only ${TABLE_TOP} fit`);
  return Uint8Array.from(out);
}

/* The sample table both the page and the test use, so what gets tested is
   what the button writes. */
function sampleTable() {
  return packTable([
    ['.',  usages('hello from WebUSB\n')],
    ['-',  [...usages('wait'), ESC_PAUSE, 10, ...usages(' ok')]],
    ['..', [...usages('ab'), ESC_REPEAT, 4]],
    ['.-', [ESC_MOD, 0x01, 0x50, ...usages('z')]],      /* Ctrl+Left, then z */
  ]);
}

if (typeof module !== 'undefined') module.exports = {
  ESC_PAUSE, ESC_MOD, ESC_REPEAT, ESC_JUMP, SHIFT, TABLE_TOP,
  usages, header, packTable, sampleTable };
