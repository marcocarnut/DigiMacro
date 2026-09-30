/* table.js -- macros as text, and as the bytes DigiMacro plays.
 *
 * The one original: the app loads it in the browser, and test/run.sh loads it
 * in node and plays what it encodes against the real firmware. The table is
 * the interface between the two halves of this project, so the app's encoder
 * and the firmware's decoder are checked against each other rather than each
 * being trusted on its own.
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

/* ==================================================================== */
/* The US layout                                                        */
/* ==================================================================== */

const PLAIN = {' ': 0x2C, '-': 0x2D, '=': 0x2E, '[': 0x2F, ']': 0x30,
               '\\': 0x31, ';': 0x33, "'": 0x34, '`': 0x35, ',': 0x36,
               '.': 0x37, '/': 0x38, '\n': 0x28, '\t': 0x2B, '\b': 0x2A};
const SHIFTED = {'!': 0x1E, '@': 0x1F, '#': 0x20, '$': 0x21, '%': 0x22,
                 '^': 0x23, '&': 0x24, '*': 0x25, '(': 0x26, ')': 0x27,
                 '_': 0x2D, '+': 0x2E, '{': 0x2F, '}': 0x30, '|': 0x31,
                 ':': 0x33, '"': 0x34, '~': 0x35, '<': 0x36, '>': 0x37,
                 '?': 0x38};

/* Keys that are not characters, so they can be named rather than numbered.
   "Break" rather than "Pause", because {pause 1.5} is a wait. */
const NAMED = {
  enter: 0x28, esc: 0x29, backspace: 0x2A, tab: 0x2B, space: 0x2C,
  capslock: 0x39,
  f1: 0x3A, f2: 0x3B, f3: 0x3C, f4: 0x3D, f5: 0x3E, f6: 0x3F,
  f7: 0x40, f8: 0x41, f9: 0x42, f10: 0x43, f11: 0x44, f12: 0x45,
  printscreen: 0x46, scrolllock: 0x47, break: 0x48,
  insert: 0x49, home: 0x4A, pgup: 0x4B, delete: 0x4C, end: 0x4D, pgdn: 0x4E,
  right: 0x4F, left: 0x50, down: 0x51, up: 0x52, menu: 0x65,
};

/* The spellings people actually reach for. The canonical name is what a
   table decodes back to; these are only accepted on the way in. */
const KEY_ALIAS = { del: 'delete', ins: 'insert', escape: 'esc',
                    return: 'enter', pageup: 'pgup', pagedown: 'pgdn',
                    spacebar: 'space', bksp: 'backspace', prtsc: 'printscreen' };

const MODS = { ctrl: 0x01, shift: 0x02, alt: 0x04, gui: 0x08,
               rctrl: 0x10, rshift: 0x20, ralt: 0x40, rgui: 0x80 };
const MOD_ALIAS = { control: 'ctrl', win: 'gui', cmd: 'gui', meta: 'gui',
                    super: 'gui', option: 'alt', altgr: 'ralt' };

/* reverse maps, for turning a table back into something editable */
const UNPLAIN = {}, UNSHIFTED = {}, UNNAMED = {};
for (const [c, u] of Object.entries(PLAIN))   UNPLAIN[u] = c;
for (const [c, u] of Object.entries(SHIFTED)) UNSHIFTED[u] = c;
for (const [n, u] of Object.entries(NAMED))   if (!(u in UNNAMED)) UNNAMED[u] = n;
for (let i = 0; i < 26; i++) UNPLAIN[0x04 + i] = String.fromCharCode(97 + i);
for (let i = 0; i < 26; i++) UNSHIFTED[0x04 + i] = String.fromCharCode(65 + i);
for (let i = 0; i < 9; i++)  UNPLAIN[0x1E + i] = String(i + 1);
UNPLAIN[0x27] = '0';

/* ==================================================================== */
/* Dead keys                                                            */
/* ==================================================================== */

/* The board sends scan codes, so what appears on screen is the host's
 * business, not ours. "o" is the key next to "i" whatever language anyone is
 * writing; "o-with-an-acute" is not a key at all. On a layout with dead keys
 * it is two presses -- the accent, then the letter -- and on a plain US
 * layout it cannot be typed at all.
 *
 * So the accent handling is a statement about the machine the board will be
 * plugged into, not about the text. Set the layout and accented characters
 * are accepted and expanded; leave it at plain US and they are refused, which
 * is better than silently typing something else.
 */
const DEAD = {
  "'": { base: 'aeiouycnszAEIOUYCNSZ', out: 'áéíóúýçńśźÁÉÍÓÚÝÇŃŚŹ' },
  '`': { base: 'aeiouAEIOU',           out: 'àèìòùÀÈÌÒÙ' },
  '^': { base: 'aeiouAEIOU',           out: 'âêîôûÂÊÎÔÛ' },
  '~': { base: 'anoANO',               out: 'ãñõÃÑÕ' },
  '"': { base: 'aeiouyAEIOUY',         out: 'äëïöüÿÄËÏÖÜŸ' },
};

/* composed character -> [the dead key, the letter under it] */
const COMPOSE = {};
for (const [mark, m] of Object.entries(DEAD))
  [...m.out].forEach((ch, i) => { COMPOSE[ch] = [mark, [...m.base][i]]; });

const LAYOUTS = {
  'us':      { label: 'US', dead: false },
  'us-intl': { label: 'US International', dead: true },
};
let layout = 'us';
function setLayout(name) { layout = name in LAYOUTS ? name : 'us'; }
function getLayout() { return layout; }
const deadKeys = () => LAYOUTS[layout].dead;

/* one character -> [usage, needsShift] */
function charUsage(ch) {
  if (ch >= 'a' && ch <= 'z') return [ch.charCodeAt(0) - 97 + 0x04, false];
  if (ch >= 'A' && ch <= 'Z') return [ch.charCodeAt(0) - 65 + 0x04, true];
  if (ch >= '1' && ch <= '9') return [ch.charCodeAt(0) - 49 + 0x1E, false];
  if (ch === '0')             return [0x27, false];
  if (ch in PLAIN)            return [PLAIN[ch], false];
  if (ch in SHIFTED)          return [SHIFTED[ch], true];
  throw new Error(`no key for "${ch}"`);
}

const keystroke = ch => { const [u, sh] = charUsage(ch); return sh ? u | SHIFT : u; };

/* Plain text, nothing else. Bit 7 carries Shift, so one byte covers every
   character on the keyboard and the firmware needs no ASCII table.
   
   With dead keys, two things change. An accented character becomes the accent
   followed by the letter; and a bare accent mark becomes itself followed by a
   space, because that is how a dead-key layout is asked for the mark alone.
   To send one of those keys with nothing after it -- which is what a host does
   when the next character does not combine -- write it in braces: {'}. */
function usages(text) {
  const out = [];
  for (const ch of text) {
    if (deadKeys()) {
      if (ch in COMPOSE) {
        const [mark, base] = COMPOSE[ch];
        out.push(keystroke(mark), keystroke(base));
        continue;
      }
      if (ch in DEAD) { out.push(keystroke(ch), keystroke(' ')); continue; }
    }
    if (ch in COMPOSE)
      throw new Error(`"${ch}" needs a layout with dead keys -- see the ` +
                      `keyboard setting, or type it as ' and the letter`);
    out.push(keystroke(ch));
  }
  return out;
}

/* ==================================================================== */
/* Macro text -> bytes                                                  */
/* ==================================================================== */

/* The syntax, which is the app's whole user interface to a macro:
 *
 *    anything            typed as itself
 *    \n  \t  \\          newline, tab, backslash
 *    {{                  a literal {
 *    {Enter} {Left} {F5} a key that is not a character
 *    {Ctrl+c} {Alt+Tab}  modifiers on any key or character
 *    {pause 1.5}         wait, in seconds, 0.1 to 25.5
 *    {11}  {x11}         press the previous keystroke 11 more times
 */
function encodeMacro(text) {
  const out = [];
  let i = 0, wrote = false;

  /* Text pasted from elsewhere may carry its accents as separate combining
     marks -- "e" followed by U+0301 rather than a single "é" -- which looks
     identical and is not. Compose it first, so that what is looked up is what
     is on screen. */
  text = text.normalize('NFC');

  const push = (...b) => { for (const v of b) out.push(v); };

  while (i < text.length) {
    const ch = text[i];

    if (ch === '\\') {                          /* the two-character escapes */
      const n = text[++i];
      if (n === undefined) throw new Error('a lone \\ at the end');
      const lit = { n: '\n', t: '\t', b: '\b', '\\': '\\' }[n];
      if (lit === undefined) throw new Error(`\\${n} means nothing`);
      push(...usages(lit));
      wrote = true;
      i++;
      continue;
    }

    if (ch === '{' && text[i + 1] === '{') { push(...usages('{')); wrote = true; i += 2; continue; }

    if (ch === '{') {
      const end = text.indexOf('}', i);
      if (end < 0) throw new Error('a { with no }');
      const body = text.slice(i + 1, end).trim();
      i = end + 1;

      const pause = /^pause\s+([0-9]*\.?[0-9]+)$/i.exec(body);
      if (pause) {
        const tenths = Math.round(parseFloat(pause[1]) * 10);
        if (tenths < 1 || tenths > 255)
          throw new Error(`{${body}}: a pause runs from 0.1 to 25.5 seconds`);
        push(ESC_PAUSE, tenths);
        continue;                               /* a pause is not a keystroke */
      }

      /* {11} as well as {x11}: a bare count reads as a quantifier does in a
         regular expression, which is what it is. No key is a bare number --
         a digit is typed as itself -- so there is nothing to collide with. */
      const rep = /^x?\s*(\d+)$/i.exec(body);
      if (rep) {
        const n = parseInt(rep[1], 10);
        if (n < 1 || n > 255)
          throw new Error(`{${body}}: repeat runs from 1 to 255`);
        if (!wrote) throw new Error(`{${body}}: nothing to repeat yet`);
        push(ESC_REPEAT, n);
        continue;
      }

      /* the rest is [mod+]...key */
      const parts = body.split('+').map(p => p.trim()).filter(p => p !== '');
      if (parts.length === 0 && body !== '+') throw new Error('{} is empty');

      /* "+" is both the separator and a key, so it has to be said twice to
         mean itself: {Ctrl++} is Ctrl and plus, while {Ctrl+} is a modifier
         with nothing to modify, which is a typo rather than a shortcut. */
      let keyName;
      if (body === '+' || body.endsWith('++')) keyName = '+';
      else if (body.endsWith('+')) throw new Error(`{${body}}: no key after the +`);
      else keyName = parts.pop();
      let mask = 0;
      for (const m of parts) {
        const name = MOD_ALIAS[m.toLowerCase()] || m.toLowerCase();
        if (!(name in MODS)) throw new Error(`"${m}" is not a modifier`);
        mask |= MODS[name];
      }

      let usage;
      const low = keyName.toLowerCase();
      const named = NAMED[KEY_ALIAS[low] || low];
      if (named !== undefined) {
        usage = named;
      } else if ([...keyName].length === 1) {
        const [u, sh] = charUsage(keyName);
        usage = u;
        if (sh) mask |= MODS.shift;
      } else {
        throw new Error(`"${keyName}" is not a key`);
      }

      if (mask) push(ESC_MOD, mask);
      push(usage);
      wrote = true;
      continue;
    }

    push(...usages(ch));
    wrote = true;
    i++;
  }
  return out;
}

/* ==================================================================== */
/* Bytes -> macro text                                                  */
/* ==================================================================== */

function modNames(mask) {
  const names = [];
  for (const [n, bit] of Object.entries(MODS))
    if (mask & bit) names.push(n[0].toUpperCase() + n.slice(1));
  return names;
}

function keyName(usage) {
  if (usage in UNNAMED) return UNNAMED[usage][0].toUpperCase() + UNNAMED[usage].slice(1);
  if (usage in UNPLAIN) return UNPLAIN[usage];
  return '0x' + usage.toString(16).padStart(2, '0');
}

function literal(ch) {
  if (ch === '\n') return '\\n';
  if (ch === '\t') return '\\t';
  if (ch === '\b') return '\\b';
  if (ch === '\\') return '\\\\';
  if (ch === '{')  return '{{';
  return ch;
}

/* The character a plain keystroke types, or undefined if it types none */
function plainChar(b) {
  if ((b & 0x7F) < 0x04) return undefined;
  return (b & SHIFT) ? UNSHIFTED[b & 0x7F] : UNPLAIN[b & 0x7F];
}

function decodeMacro(bytes) {
  let out = '', mod = 0;

  for (let i = 0; i < bytes.length; i++) {
    const b = bytes[i];

    /* On a dead-key layout, what the host shows for two keystrokes is not
       what either of them shows alone, and the editor should say what will
       appear. A mark with nothing that combines after it is written {'}, the
       one form that survives being encoded again. */
    if (deadKeys() && !mod) {
      const mark = plainChar(b);
      if (mark !== undefined && mark in DEAD) {
        const next = i + 1 < bytes.length ? plainChar(bytes[i + 1]) : undefined;
        if (next === ' ') { out += literal(mark); i++; continue; }
        const joined = next !== undefined && DEAD[mark].base.includes(next)
                     ? [...DEAD[mark].out][[...DEAD[mark].base].indexOf(next)]
                     : undefined;
        if (joined !== undefined) { out += joined; i++; continue; }
        out += `{${mark}}`;
        continue;
      }
    }

    if ((b & 0x7F) < 0x04) {                    /* an escape */
      const arg = bytes[++i];
      if (b === ESC_PAUSE)  { out += `{pause ${(arg / 10).toFixed(1)}}`; continue; }
      if (b === ESC_REPEAT) { out += `{${arg}}`; continue; }
      if (b === ESC_MOD)    { mod = arg; continue; }
      out += `{?${b.toString(16)}}`;            /* a jump, once there are any */
      continue;
    }

    const usage = b & 0x7F;
    const mask = mod | ((b & SHIFT) ? MODS.shift : 0);
    mod = 0;

    /* A keystroke reads back as the character it types, wherever one exists:
       Shift plus the usage for "1" is "!", and needs no saying. */
    if (!mask) {
      const ch = UNPLAIN[usage];
      if (ch !== undefined) { out += literal(ch); continue; }
    } else if (mask === MODS.shift) {
      const ch = UNSHIFTED[usage];
      if (ch !== undefined) { out += literal(ch); continue; }
      /* Shift on a key that types nothing -- Shift+Left -- has to be said */
    }

    out += mask ? `{${modNames(mask).join('+')}+${keyName(usage)}}`
                : `{${keyName(usage)}}`;
  }
  return out;
}

/* ==================================================================== */
/* The table                                                            */
/* ==================================================================== */

/* ".-" -> the header byte: three bits of count-minus-one, five of symbols,
   a dash being a 1 and the first symbol in the highest bit the count uses. */
function header(code) {
  if (!new RegExp(`^[.-]{1,${MAX_SYM}}$`).test(code))
    throw new Error(`not a code: "${code}"`);
  let bits = 0;
  for (const c of code) bits = (bits << 1) | (c === '-' ? 1 : 0);
  return (bits << 3) | (code.length - 1);
}

function codeOf(header) {
  const n = (header & 0x07) + 1, bits = header >> 3;
  let code = '';
  for (let i = n - 1; i >= 0; i--) code += (bits >> i) & 1 ? '-' : '.';
  return code;
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

/* ...and back, so a board's table can be edited rather than retyped. Stops at
   the terminator exactly as the firmware's walk does, so whatever an earlier,
   longer table left beyond it stays invisible here too. */
function unpackTable(bytes) {
  const entries = [];
  let i = 0;
  while (i < bytes.length && i < TABLE_TOP) {
    const h = bytes[i++];
    if ((h & 0x07) >= MAX_SYM) break;           /* 0xFF, or any impossible length */
    const body = [];
    while (i < bytes.length && i < TABLE_TOP && bytes[i]) body.push(bytes[i++]);
    i++;                                        /* the terminator */
    entries.push([codeOf(h), body]);
  }
  return entries;
}

/* What the editor holds: text per code, in table order. */
function encodeTable(rows) {
  return packTable(rows.map(r => [r.code, encodeMacro(r.text)]));
}

function decodeTable(bytes) {
  return unpackTable(bytes).map(([code, body]) =>
    ({ code, text: decodeMacro(body) }));
}

/* The sample table both the app and the test use, so what gets tested is
   what the button writes. */
function sampleRows() {
  return [
    { code: '.',  text: 'hello from WebUSB\\n' },
    { code: '-',  text: 'wait{pause 1.0} ok' },
    { code: '..', text: 'ab{4}' },
    { code: '.-', text: '{Ctrl+Left}z' },
  ];
}

if (typeof module !== 'undefined') module.exports = {
  ESC_PAUSE, ESC_MOD, ESC_REPEAT, ESC_JUMP, SHIFT, TABLE_TOP, MAX_SYM,
  LAYOUTS, setLayout, getLayout,
  usages, header, codeOf, packTable, unpackTable,
  encodeMacro, decodeMacro, encodeTable, decodeTable, sampleRows };
