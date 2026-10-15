/* Checks web/vkbd.js, the page's own keyboard: what each key does to a
 * field's text and caret, the shift's three states, and that every key it
 * offers is one the board can type -- held to web/table.js, so a key that
 * could only put an error in a macro cannot get onto the keyboard.
 */
const vk = require('../web/vkbd.js');
const T = require('../web/table.js');

let fails = 0;
const check = (ok, what) => { if (!ok) { console.log('  FAIL: ' + what); fails++; } };
const ed = (v, a, b, k, max) => { const r = vk.vkEdit(v, a, b, k, max); return `${r.value}|${r.at}`; };

console.log('1. a key goes in at the caret, over a selection');
check(ed('abd', 2, 2, 'c') === 'abcd|3', 'into the middle');
check(ed('abXXe', 2, 4, 'cd') === 'abcde|4', 'over a selection');
check(ed('ab', 2, 2, 'Space') === 'ab |3', 'a space');
check(ed('ab', 0, 0, 'z') === 'zab|1', 'at the start');

console.log('2. maxlength holds');
check(ed('a', 1, 1, 'b', 1) === 'a|1', 'a full field takes nothing');
check(ed('a', 0, 1, 'b', 1) === 'b|1', 'but a selection in it can be replaced');
check(ed('a', 1, 1, 'b', -1) === 'ab|2', 'no maxlength, no limit');

console.log('3. backspace');
check(ed('abc', 2, 2, 'Back') === 'ac|1', 'the character before the caret');
check(ed('abc', 0, 0, 'Back') === 'abc|0', 'nothing before the start');
check(ed('abcd', 1, 3, 'Back') === 'ad|1', 'a selection');

console.log('4. arrows move the caret, and collapse a selection to its side');
check(ed('abc', 1, 1, 'Left') === 'abc|0', 'left');
check(ed('abc', 0, 0, 'Left') === 'abc|0', 'not past the start');
check(ed('abc', 1, 1, 'Right') === 'abc|2', 'right');
check(ed('abc', 3, 3, 'Right') === 'abc|3', 'not past the end');
check(ed('abcd', 1, 3, 'Left') === 'abcd|1', 'left of a selection');
check(ed('abcd', 1, 3, 'Right') === 'abcd|3', 'right of a selection');

console.log('4b. the field scrolls to keep the caret in sight');
check(vk.vkScroll(0, 50, 100) === 0, 'in sight, no scroll');
check(vk.vkScroll(0, 130, 100) === 30, 'past the right edge');
check(vk.vkScroll(40, 10, 100) === 10, 'past the left edge');
check(vk.vkScroll(40, 140, 100) === 40, 'right at the edge');

console.log('5. shift: once, locked, off');
check(vk.vkShiftTap(0) === 1 && vk.vkShiftTap(1) === 2 && vk.vkShiftTap(2) === 0, 'the cycle');
check(vk.vkShiftTyped(1) === 0, 'once is spent by a letter');
check(vk.vkShiftTyped(2) === 2, 'locked stays');
check(vk.vkChar('ç', 1) === 'Ç' && vk.vkChar('q', 0) === 'q', 'shifted letters');

console.log('6. every key is one the board can type');
const chars = page => vk.VK_PAGES[page].flat().filter(k => k.length === 1 || k === 'Space')
  .flatMap(k => k === 'Space' ? [' '] : page === 'sym' ? [k] : [k, vk.vkChar(k, 1)]);
for (const [layout, pages] of [['us-intl', ['abc', 'sym', 'acc']], ['abnt2', ['abc', 'sym', 'acc']],
                              ['us', ['abc', 'sym']]]) {
  T.setLayout(layout);
  for (const page of pages)
    for (const c of chars(page)) {
      let ok = true;
      try { T.usages(c); } catch (e) { ok = false; }
      check(ok, `${layout}: "${c}" on the ${page} page cannot be typed`);
    }
}
T.setLayout('us');
let accentOk = true;
try { T.usages('á'); } catch (e) { accentOk = false; }
check(!accentOk, 'US would type an accent: the accent page is only for dead-key layouts');

console.log('7. every page fits a phone, and has a way back');
for (const [page, rows] of Object.entries(vk.VK_PAGES)) {
  for (const row of rows) check(row.length <= 11, `${page}: a row of ${row.length} keys`);
  const keys = rows.flat();
  for (const k of ['Back', 'Enter', 'Space'])
    check(keys.includes(k), `${page} has no ${k}`);
  for (const other of Object.keys(vk.VK_PAGES))
    if (other !== page)
      check(keys.includes(other[0].toUpperCase() + other.slice(1)), `${page} cannot reach ${other}`);
  for (const k of keys.filter(k => k.length > 1 && k !== 'Space'))
    check(k in vk.VK_LABELS, `${k} has no label`);
}

console.log('8. it serves text fields, not checkboxes or files');
const inp = (type, more = {}) => ({ tagName: 'INPUT', disabled: false, readOnly: false,
                                    getAttribute: () => type, ...more });
for (const ty of ['text', 'password', 'number', null])
  check(vk.vkServes(inp(ty)), `does not serve type=${ty}`);
for (const ty of ['checkbox', 'file'])
  check(!vk.vkServes(inp(ty)), `serves type=${ty}`);
check(!vk.vkServes(inp('text', { disabled: true })), 'serves a disabled field');
check(!vk.vkServes({ tagName: 'SELECT', getAttribute: () => null }), 'serves a select');

console.log(fails ? `${fails} failed` : 'all checks passed');
process.exit(fails ? 1 : 0);
