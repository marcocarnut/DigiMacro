/* table.js -- macros as text, and as the bytes DigiMacro plays.
 *
 * The one original: the app loads it in the browser, and test/run.sh loads it
 * in node and plays what it encodes against the real firmware. The table is
 * the interface between the two halves of this project, so the app's encoder
 * and the firmware's decoder are checked against each other rather than each
 * being trusted on its own.
 *
 * Keep the escape values in step with DigiMacro.ino; test/webtable.js reads
 * them out of it and checks. Note that 0x04 is NOT free: it is the usage for
 * "a". The escapes live in 0x01 to 0x03, which are states a keyboard reports
 * rather than keys anyone presses, the same three with bit 7 set, and 0x80
 * -- Shift and no key at all -- and all seven are now taken.
 */

const ESC_PAUSE  = 0x01;    /* nn tenths of a second */
const ESC_MOD    = 0x02;    /* a HID modifier mask, for the next keystroke */
const ESC_REPEAT = 0x03;    /* press the previous keystroke nn more times */
const ESC_JUMP   = 0x81;    /* carry on with the macro whose header is arg-1:
                               plus one because a zero byte ends a macro, and
                               the header of "." is zero */
const ESC_MARK   = 0x82;    /* the repeat after this goes back to here */
const ESC_HOST   = 0x83;    /* stop unless the host has proved itself: */
const HOST_SILENT = 1;      /* ...quietly, {host} */
const HOST_LOUD   = 2;      /* ...as if the code were not there, {host!} */
const ESC_IR     = 0x80;    /* where the remote's button starts: a, c, x --
                               three bytes, each with bit 7 set, since the
                               button's address and command may be zero */
const SHIFT      = 0x80;    /* in a stored keystroke */

/* {IR a c} as the table holds it, and back */
const irBytes = (a, c) => [ESC_IR, a | 0x80, c | 0x80,
                           0x80 | (a >> 7) | ((c >> 7) << 1)];
const irButton = (b, i) => [(b[i] & 0x7F) | ((b[i + 2] & 1) << 7),
                            (b[i + 1] & 0x7F) | ((b[i + 2] & 2) << 6)];
const irHex = v => v.toString(16).padStart(2, '0');

/* How many bytes the thing at b[i] takes: a keystroke or a mark one, an
   {IR} four, any other escape two. Everything that walks a macro walks it
   with this, since an argument can be any byte at all. */
const escStep = (b, i) => b[i] === ESC_IR ? 4
                     : (b[i] & 0x7F) < 0x04 && b[i] !== ESC_MARK ? 2 : 1;

const AUTO_ADDR  = 0x1F3;   /* the macro to type when the board is plugged
                               in, as a header byte; anything impossible as
                               one means nothing is set */
const AUTO_NONE  = 0xFF;
const TABLE_TOP  = 0x1E0;   /* the firmware stops walking here: 480,
                               leaving 32 for the serial number, the host
                               key, the lockout, the power-on macro and the
                               password */
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
/* What it says when something is wrong                                 */
/* ==================================================================== */

/* These reach the person editing the macro, so the app translates them. The
   default is English and the tests read it, which is why the text lives here
   rather than in the app: a message with no default would be a message no
   test could assert on. */
const MSG = {
  'lone-backslash':  () => 'a lone \\ at the end',
  'bad-escape':      p => `\\${p.what} means nothing`,
  'unclosed':        () => 'a { with no }',
  'empty-braces':    () => '{} is empty',
  'pause-range':     p => `{${p.what}}: a pause runs from 0.1 to 25.5 seconds`,
  'repeat-range':    p => `{${p.what}}: a repeat runs from 1 to 256`,
  'nothing-to-rep':  p => `{${p.what}}: nothing to repeat yet`,
  'group-empty':     () => '() has nothing in it',
  'nested-repeat':   () => 'a count inside a group: the firmware has one mark, '
                         + 'so the inner count would take the outer one over',
  'goto-nowhere':    p => `{goto ${p.what}}: there is no macro with that code`,
  'ir-button':       p => `{${p.what}}: a remote's button is {IR} and two bytes `
                         + `in hex, as in {IR 00 45}`,
  'loop-empty':      p => `${p.what}: these only jump to each other and never `
                         + `type or pause, so the firmware would give up on `
                         + `them rather than run them`,
  'no-key-after':    p => `{${p.what}}: no key after the +`,
  'not-a-modifier':  p => `"${p.what}" is not a modifier`,
  'not-a-key':       p => `"${p.what}" is not a key`,
  'no-key-for':      p => `no key for "${p.what}"`,
  'needs-dead-keys': p => `"${p.what}" needs a keyboard with dead keys -- see ` +
                          `the keyboard setting beside Macros`,
  'not-a-code':      p => `not a code: "${p.what}"`,
  'code-twice':      p => `"${p.what}" is used twice`,
  'not-a-byte':      p => `"${p.what}" has a byte that is not a byte`,
  'has-a-zero':      p => `"${p.what}" contains a zero, which would end it early`,
  'too-big':         p => `${p.n} bytes of table, and only ${p.fits} fit`,
};

let translate = null;
function setMessages(fn) { translate = fn; }
function fail(key, params = {}) {
  const s = translate && translate(key, params);
  throw new Error(s || MSG[key](params));
}

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
  fail('no-key-for', { what: ch });
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
      fail('needs-dead-keys', { what: ch });
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
 *    x{11}               eleven of them, as a quantifier reads in a regular
 *                        expression -- eleven in all, not eleven more
 *    (ab){3}             ...of a whole group, rather than one thing
 *    {goto .-}           carry on with another macro, and never come back
 *    {IR 00 45}          where the remote's button 00 45 starts typing
 *    \(  \)              a parenthesis that is only a parenthesis
 *
 * The group exists because a keystroke stopped being a character. "ç" on a
 * dead-key keyboard is the accent and then the letter, so "ç{5}" asked to
 * repeat the letter and typed çcccc. The encoder now looks at what the count
 * is counting: one keystroke repeats as one keystroke, and anything longer
 * gets a mark in front of it so the firmware knows where to go back to. That
 * makes "ç{5}" work without anyone writing a group at all, and parentheses
 * are for when the repeated thing is more than one character.
 */
/* The first unescaped ")" after `from`, or -1. */
function closeParen(text, from) {
  for (let i = from; i < text.length; i++) {
    if (text[i] === '\\') { i++; continue; }
    if (text[i] === ')') return i;
  }
  return -1;
}
const REPEAT_NEXT = /^\{\s*x?\s*\d+\s*\}/;

function encodeMacro(text) {
  const out = [];
  let i = 0;

  /* Where in `out` the last thing that a count could count began, or -1.
     One byte repeats as one byte; anything longer needs a mark in front of
     it, which is inserted here once we know a count follows. */
  let unitAt = -1;

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
      if (n === undefined) fail('lone-backslash');
      const lit = { n: '\n', t: '\t', b: '\b', '\\': '\\',
                    '(': '(', ')': ')' }[n];
      if (lit === undefined) fail('bad-escape', { what: n });
      unitAt = out.length;
      push(...usages(lit));
      i++;
      continue;
    }

    if (ch === '{' && text[i + 1] === '{') {
      unitAt = out.length; push(...usages('{')); i += 2; continue;
    }

    /* "(" opens a group only when a count follows the matching ")". Anywhere
       else it is a parenthesis, which is what it is nine times in ten. */
    if (ch === '(') {
      const close = closeParen(text, i + 1);
      if (close >= 0 && REPEAT_NEXT.test(text.slice(close + 1))) {
        const inner = text.slice(i + 1, close);
        if (inner === '') fail('group-empty');
        const body = encodeMacro(inner);
        /* The firmware keeps one mark, so a count inside a group would send
           the group's own repeat back to the wrong place. Refusing says so;
           emitting it would type something nobody asked for. */
        for (let j = 0; j < body.length; j += escStep(body, j))
          if (body[j] === ESC_REPEAT || body[j] === ESC_MARK)
            fail('nested-repeat');
        unitAt = out.length;
        push(ESC_MARK);             /* explicit groups always get one, so that
                                       what comes back reads the same way */
        push(...body);
        i = close + 1;              /* the count is handled next time round */
        continue;
      }
    }

    if (ch === '{') {
      const end = text.indexOf('}', i);
      if (end < 0) fail('unclosed');
      const body = text.slice(i + 1, end).trim();
      i = end + 1;

      const pause = /^pause\s+([0-9]*\.?[0-9]+)$/i.exec(body);
      if (pause) {
        const tenths = Math.round(parseFloat(pause[1]) * 10);
        if (tenths < 1 || tenths > 255)
          fail('pause-range', { what: body });
        unitAt = out.length;
        push(ESC_PAUSE, tenths);
        continue;
      }

      /* {11} as well as {x11}: a bare count reads as a quantifier does in a
         regular expression, which is what it is. No key is a bare number --
         a digit is typed as itself -- so there is nothing to collide with. */
      const rep = /^x?\s*(\d+)$/i.exec(body);
      if (rep) {
        const n = parseInt(rep[1], 10);         /* how many in all */
        if (n < 1 || n > 256) fail('repeat-range', { what: body });
        if (unitAt < 0) fail('nothing-to-rep', { what: body });
        if (n > 1) {
          /* One byte is repeated as itself; anything longer needs the
             firmware told where it began. A group put its own mark in. */
          if (out.length - unitAt > 1 && out[unitAt] !== ESC_MARK)
            out.splice(unitAt, 0, ESC_MARK);
          push(ESC_REPEAT, n - 1);              /* the firmware counts MORE */
        }
        unitAt = -1;                            /* a count cannot count a count */
        continue;
      }

      /* {host} stops the macro here unless the host has sent its key
         lately; {host!} stops it as if the code were not in the table. */
      const host = /^host(!?)$/i.exec(body);
      if (host) {
        push(ESC_HOST, host[1] ? HOST_LOUD : HOST_SILENT);
        unitAt = -1;                            /* a check is not a thing to
                                                   count */
        continue;
      }

      /* {IR a c}: the remote's button a c starts typing from here. Played
         through from the code, it is nothing. */
      if (/^ir(\s|\d|$)/i.test(body)) {
        const ir = /^ir\s+([0-9a-f]{1,2})\s+([0-9a-f]{1,2})$/i.exec(body);
        if (!ir) fail('ir-button', { what: body });
        push(...irBytes(parseInt(ir[1], 16), parseInt(ir[2], 16)));
        unitAt = -1;                            /* a place, not a thing */
        continue;
      }

      const go = /^goto\s+([.-]+|ir\d+)$/i.exec(body);
      if (go) {
        push(ESC_JUMP, header(go[1]) + 1);
        unitAt = -1;                            /* nothing after it runs */
        continue;
      }

      /* the rest is [mod+]...key */
      const parts = body.split('+').map(p => p.trim()).filter(p => p !== '');
      if (parts.length === 0 && body !== '+') fail('empty-braces');

      /* "+" is both the separator and a key, so it has to be said twice to
         mean itself: {Ctrl++} is Ctrl and plus, while {Ctrl+} is a modifier
         with nothing to modify, which is a typo rather than a shortcut. */
      let keyName;
      if (body === '+' || body.endsWith('++')) keyName = '+';
      else if (body.endsWith('+')) fail('no-key-after', { what: body });
      else keyName = parts.pop();
      let mask = 0;
      for (const m of parts) {
        const name = MOD_ALIAS[m.toLowerCase()] || m.toLowerCase();
        if (!(name in MODS)) fail('not-a-modifier', { what: m });
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
        fail('not-a-key', { what: keyName });
      }

      unitAt = out.length;
      if (mask) push(ESC_MOD, mask);
      push(usage);
      continue;
    }

    unitAt = out.length;
    push(...usages(ch));
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

/* Text that a count can follow without parentheses: one character, or one
   brace form. "ç" is one thing to a reader even though it is two keystrokes,
   and that is the whole point of the group. */
const oneUnit = s => [...s].length === 1 || /^\{[^}]*\}$/.test(s);

function decodeMacro(bytes) {
  let out = '', mod = 0;
  let groupAt = -1, groupByte = 0;      /* where a marked group began */

  for (let i = 0; i < bytes.length; i++) {
    const b = bytes[i];

    if (b === ESC_MARK) {               /* no argument, so not below */
      groupAt = out.length;
      groupByte = i + 1;
      continue;
    }

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

    if (b === ESC_IR) {
      const [a, c] = irButton(bytes, i + 1);
      out += `{IR ${irHex(a)} ${irHex(c)}}`;
      i += 3;
      continue;
    }

    if ((b & 0x7F) < 0x04) {                    /* an escape */
      const arg = bytes[++i];
      if (b === ESC_PAUSE)  { out += `{pause ${(arg / 10).toFixed(1)}}`; continue; }
      if (b === ESC_JUMP) { out += `{goto ${codeOf(arg - 1)}}`; continue; }
      if (b === ESC_HOST) { out += arg === HOST_LOUD ? '{host!}' : '{host}'; continue; }
      if (b === ESC_REPEAT) {
        const n = arg + 1;              /* the firmware counts the extra ones */
        if (groupAt < 0) {
          /* A ")" just before a count would read as a group closing, so it
             has to say that it is only a parenthesis. */
          if (out.endsWith(')') && !out.endsWith('\\)'))
            out = out.slice(0, -1) + '\\)';
          out += `{${n}}`;
          continue;
        }
        const inner = out.slice(groupAt);
        const span = (i - 1) - groupByte;   /* bytes between mark and repeat */
        /* One byte under a mark has to keep its parentheses: written without
           them it would encode as a bare repeat, mark and all dropped. */
        out = out.slice(0, groupAt)
            + (span > 1 && oneUnit(inner) ? inner : `(${inner})`)
            + `{${n}}`;
        groupAt = -1;
        continue;
      }
      if (b === ESC_MOD)    { mod = arg; continue; }
      out += `{?${b.toString(16)}}`;            /* an escape this editor does
                                                   not know */
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
   a dash being a 1 and the first symbol in the highest bit the count uses.
   "IR1" to "IR30" are headers no tapping can make: one symbol, with bits
   set above it. The remote reaches them, and so does a goto. */
const IR_CODES = 30;

function header(code) {
  const ir = /^ir([1-9]\d?)$/i.exec(code);
  if (ir && +ir[1] <= IR_CODES) return (+ir[1] + 1) << 3;
  if (!new RegExp(`^[.-]{1,${MAX_SYM}}$`).test(code))
    fail('not-a-code', { what: code });
  let bits = 0;
  for (const c of code) bits = (bits << 1) | (c === '-' ? 1 : 0);
  return (bits << 3) | (code.length - 1);
}

/* Any other untappable header comes back as something header() refuses,
   rather than as the tappable code its low bits spell. */
function codeOf(header) {
  const n = (header & 0x07) + 1, bits = header >> 3;
  if (bits >> n) return n === 1 ? 'IR' + (bits - 1)
                                : '#' + header.toString(16).padStart(2, '0');
  let code = '';
  for (let i = n - 1; i >= 0; i--) code += (bits >> i) & 1 ? '-' : '.';
  return code;
}

/* Whether a code can be tapped at all. */
function tappable(code) { return /^[.-]+$/.test(code); }

/* Where a macro goes when it is done, and whether it does anything on the
   way. Execution is linear until a jump, and a jump never comes back, so only
   the first one counts -- and only what happens BEFORE it ever runs. */
function entryFate(bytes) {
  let acts = false;
  for (let i = 0; i < bytes.length; i += escStep(bytes, i)) {
    const b = bytes[i];
    if (b === ESC_JUMP)  return { acts, goes: bytes[i + 1] - 1 };
    if (b === ESC_MARK || b === ESC_IR || b === ESC_HOST || b === ESC_MOD)
      continue;                                 /* nothing shows */
    acts = true;                                /* a keystroke, a pause */
  }
  return { acts, goes: null };
}

/* A macro that jumps to itself runs for ever, which is the point of having
   jumps, and touching the pad stops it -- the firmware reads the pad between
   keystrokes and during pauses. So a loop is only a problem when it does
   neither: macros that merely jump to each other never reach a point where
   anybody is asked, and the firmware bounds them by counting instead. That is
   never what anyone meant, so it is worth saying so here. */
function loopCheck(entries) {
  const fate = new Map();
  for (const [code, body] of entries) fate.set(header(code), entryFate(body));

  for (const start of fate.keys()) {
    const path = [];
    let at = start;
    while (at !== null && fate.has(at)) {
      const seen = path.indexOf(at);
      if (seen >= 0) {                          /* a cycle, from here on */
        const cycle = path.slice(seen);
        if (!cycle.some(h => fate.get(h).acts))
          fail('loop-empty',
               { what: [...cycle, cycle[0]]
                       .map(h => `"${codeOf(h)}"`).join(' -> ') });
        break;
      }
      path.push(at);
      at = fate.get(at).goes;
    }
  }
}

/* Things that are allowed but probably not meant, as message keys. A {host!}
   is only indistinguishable from an empty code if nothing shows before it:
   type something first and a guesser learns the code exists. */
function macroWarnings(bytes) {
  const warn = [];
  let shown = false;
  for (let i = 0; i < bytes.length; i += escStep(bytes, i)) {
    const b = bytes[i];
    if (b === ESC_HOST && bytes[i + 1] === HOST_LOUD && shown)
      warn.push('host-late');
    if (b === ESC_MARK || b === ESC_IR || b === ESC_HOST || b === ESC_MOD ||
        b === ESC_JUMP) continue;
    shown = true;
  }
  return warn;
}

const hasHostCheck = bytes => {
  for (let i = 0; i < bytes.length; i += escStep(bytes, i))
    if (bytes[i] === ESC_HOST) return true;
  return false;
};

/* [[code, keystrokes], ...] -> the whole table, terminator and all. Entries
   are packed, so their order in the array is their order in EEPROM. */
function packTable(entries) {
  const out = [];
  const seen = new Set();
  for (const [code, body] of entries) {
    const h = header(code);
    if (seen.has(h)) fail('code-twice', { what: code });
    seen.add(h);
    if (body.some(b => !(b >= 0 && b <= 255)))
      fail('not-a-byte', { what: code });
    if (body.includes(0))
      fail('has-a-zero', { what: code });
    out.push(h, ...body, 0);
  }
  out.push(0xFF);
  if (out.length > TABLE_TOP)
    fail('too-big', { n: out.length, fits: TABLE_TOP });

  /* A jump to a code that is not here ends the macro rather than doing
     something wrong, so this is a warning the firmware does not need -- but
     it is almost always a typo, and silence would be unkind. */
  const present = new Set(entries.map(([code]) => header(code)));
  for (const [code, body] of entries) {
    const f = entryFate(body);
    if (f.goes !== null && !present.has(f.goes))
      fail('goto-nowhere', { what: codeOf(f.goes) });
  }
  loopCheck(entries);
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
    { code: '.',  text: 'hello from DigiMacro\\n' },
    { code: '-',  text: 'wait{pause 1.0} ok' },
    { code: '..', text: 'ab{4}' },
    { code: '.-', text: '{Ctrl+Left}z' },
  ];
}

if (typeof module !== 'undefined') module.exports = {
  ESC_PAUSE, ESC_MOD, ESC_REPEAT, ESC_JUMP, SHIFT, TABLE_TOP, MAX_SYM,
  ESC_MARK, ESC_HOST, HOST_SILENT, HOST_LOUD, macroWarnings, hasHostCheck,
  ESC_IR,
  AUTO_ADDR, AUTO_NONE, LAYOUTS, setLayout, getLayout, setMessages, MSG,
  usages, header, codeOf, tappable, IR_CODES, packTable, unpackTable,
  encodeMacro, decodeMacro, encodeTable, decodeTable, sampleRows };
