/* The page's own keyboard, for a phone. Android takes DigiMacro for what it
 * is, a keyboard, and puts its on-screen one away while the board is plugged
 * in -- so without this a phone could not type into the page at all: not a
 * macro, and not the password the board asks for first. This one types into
 * the page's fields and nowhere else; it never reaches the board.
 *
 * It shows under a field with the focus while it is switched on, which it is
 * by default where the pointer is a finger. The fields it serves are told
 * inputmode="none", so that a phone's own keyboard, where there is one,
 * stays away rather than coming up over it.
 *
 * The keys are what the board can type: test/vkbd.js holds every one of them
 * to web/table.js, the accents under US International and ABNT2.
 */

const VK_PAGES = {
  abc: [[...'1234567890'], [...'qwertyuiop'], [...'asdfghjkl'],
        ['Shift', ...'zxcvbnm', 'Back'],
        ['Sym', 'Acc', ',', 'Space', '.', 'Left', 'Right', 'Enter']],
  sym: [[...'1234567890'], [...'!@#$%^&*()'], [...'-_=+[]{}\\|'],
        [...';:\'"`~<>/?', 'Back'],
        ['Abc', 'Acc', ',', 'Space', '.', 'Left', 'Right', 'Enter']],
  acc: [[...'áàâãäéèêë'], [...'íìîïóòôõö'], ['Shift', ...'úùûüçñ', 'Back'],
        ['Abc', 'Sym', ',', 'Space', '.', 'Left', 'Right', 'Enter']],
};
const VK_LABELS = { Shift: '⇧', Back: '⌫', Left: '←', Right: '→', Enter: '⏎',
                    Sym: '?123', Abc: 'ABC', Acc: 'áç' };
const VK_REPEATS = new Set(['Back', 'Left', 'Right']);

/* One key on a field's text: the text and where the caret ends up. `a` and
   `b` are the selection, `max` the field's maxlength, or -1. */
function vkEdit(value, a, b, key, max = -1) {
  const put = s => {
    const v = value.slice(0, a) + s + value.slice(b);
    return max >= 0 && v.length > max ? { value, at: b } : { value: v, at: a + s.length };
  };
  switch (key) {
  case 'Back':
    if (a !== b) return { value: value.slice(0, a) + value.slice(b), at: a };
    if (a === 0) return { value, at: 0 };
    return { value: value.slice(0, a - 1) + value.slice(a), at: a - 1 };
  case 'Left':  return { value, at: a !== b ? a : Math.max(0, a - 1) };
  case 'Right': return { value, at: a !== b ? b : Math.min(value.length, b + 1) };
  case 'Space': return put(' ');
  default:      return put(key);
  }
}

/* How far the field must scroll for the caret, at `x` px into its text, to
   be in sight: `left` is the scroll now, `room` the width the text shows in. */
function vkScroll(left, x, room) {
  if (x < left) return x;
  if (x > left + room) return x - room;
  return left;
}

/* Shift: a tap for the next letter, a second for all of them, a third off. */
const vkShiftTap = s => (s + 1) % 3;
const vkShiftTyped = s => (s === 1 ? 0 : s);
const vkChar = (key, shift) => (shift ? key.toUpperCase() : key);

/* What it serves: the page's text fields, not its checkboxes or file input. */
const vkServes = el => !!el && el.tagName === 'INPUT' && !el.disabled && !el.readOnly
  && ['text', 'password', 'number', 'search', ''].includes(el.getAttribute('type') || '');

function vkInit(root, { label = k => k } = {}) {
  let on = false, field = null, page = 'abc', shift = 0, dirty = false, repeat = null;

  const draw = () => {
    root.innerHTML = VK_PAGES[page].map(row => '<div class="vk-row">' + row.map(k => {
      const special = k.length > 1;
      const text = k === 'Space' ? label('vk-space')
                 : special ? VK_LABELS[k] : vkChar(k, shift && page !== 'sym');
      const cls = 'vk-key' + (special ? ' vk-' + k.toLowerCase() : '')
                + (k === 'Shift' && shift ? ' on' : '') + (k === 'Shift' && shift === 2 ? ' lock' : '');
      return `<div class="${cls}" data-k="${k.replace(/"/g, '&quot;')}">${
        text.replace(/&/g, '&amp;').replace(/</g, '&lt;')}</div>`;
    }).join('') + '</div>').join('');
  };

  const show = el => {
    field = el;
    if (root.hidden) { page = el.type === 'number' ? 'sym' : 'abc'; shift = 0; draw(); }
    root.hidden = false;
    document.body.style.paddingBottom = root.offsetHeight + 'px';
    el.scrollIntoView({ block: 'center' });
  };
  const hide = () => {
    root.hidden = true;
    field = null;
    document.body.style.paddingBottom = '';
  };

  /* Into the field as typing would: the page's own listeners hear an input
     event for each key, and a change when the field is left. */
  const type = key => {
    const el = field;
    if (!el || !el.isConnected) return;
    const sel = el.selectionStart !== null;     /* number fields have none */
    const a = sel ? el.selectionStart : el.value.length;
    const b = sel ? el.selectionEnd : el.value.length;
    const r = vkEdit(el.value, a, b, key, el.maxLength);
    if (r.value !== el.value) {
      el.value = r.value;
      dirty = true;
      el.dispatchEvent(new Event('input', { bubbles: true }));
    }
    if (sel) { el.setSelectionRange(r.at, r.at); reveal(el, r.at); }
  };

  /* A caret moved from a script is not scrolled to, as a typed one is: the
     field is scrolled here, measuring its text in its own font. */
  const ruler = document.createElement('canvas').getContext('2d');
  const reveal = (el, at) => {
    const cs = getComputedStyle(el);
    ruler.font = cs.font;
    const text = el.type === 'password' ? '•'.repeat(at) : el.value.slice(0, at);
    const x = ruler.measureText(text).width + at * (parseFloat(cs.letterSpacing) || 0);
    const room = el.clientWidth - parseFloat(cs.paddingLeft) - parseFloat(cs.paddingRight) - 2;
    el.scrollLeft = vkScroll(el.scrollLeft, x, room);
  };

  const press = k => {
    if (k === 'Shift') { shift = vkShiftTap(shift); draw(); return; }
    if (k === 'Sym' || k === 'Abc' || k === 'Acc') { page = k.toLowerCase(); draw(); return; }
    if (k === 'Enter') {
      const el = field;
      el.dispatchEvent(new KeyboardEvent('keydown', { key: 'Enter', bubbles: true }));
      if (document.activeElement === el) el.blur();
      return;
    }
    if (k.length > 1) { type(k); return; }
    type(vkChar(k, shift && page !== 'sym'));
    if (shift === 1) { shift = vkShiftTyped(shift); draw(); }
  };

  /* Keys act as the finger lands, and the field keeps the focus. */
  const stop = () => { clearTimeout(repeat); clearInterval(repeat); repeat = null; };
  root.addEventListener('pointerdown', e => {
    e.preventDefault();
    const key = e.target.closest('.vk-key');
    if (!key) return;
    const k = key.dataset.k;
    press(k);
    if (VK_REPEATS.has(k))
      repeat = setTimeout(() => { repeat = setInterval(() => press(k), 60); }, 400);
  });
  for (const ev of ['pointerup', 'pointercancel', 'pointerleave']) root.addEventListener(ev, stop);
  root.addEventListener('touchstart', e => e.preventDefault(), { passive: false });
  root.addEventListener('mousedown', e => e.preventDefault());

  /* Before the focus lands, so a phone's own keyboard is never asked for. */
  const quiet = el => { if (vkServes(el)) { el.inputMode = 'none'; el.dataset.vk = ''; } };
  document.addEventListener('pointerdown', e => { if (on) quiet(e.target); }, true);
  document.addEventListener('focusin', e => {
    if (!on || !vkServes(e.target)) return;
    quiet(e.target);
    if (field !== e.target) dirty = false;
    show(e.target);
  });
  document.addEventListener('focusout', e => {
    if (e.target !== field) return;
    if (dirty) { dirty = false; e.target.dispatchEvent(new Event('change', { bubbles: true })); }
    setTimeout(() => { if (!vkServes(document.activeElement) || !on) { stop(); hide(); } }, 0);
  });

  return {
    get on() { return on; },
    set on(v) {
      on = !!v;
      if (on) { if (vkServes(document.activeElement)) { quiet(document.activeElement); show(document.activeElement); } return; }
      for (const el of document.querySelectorAll('input[data-vk]')) {
        el.removeAttribute('inputmode'); delete el.dataset.vk;
      }
      stop(); hide();
    },
    redraw() { if (!root.hidden) draw(); },
  };
}

if (typeof module !== 'undefined')
  module.exports = { VK_PAGES, VK_LABELS, vkEdit, vkScroll, vkShiftTap, vkShiftTyped, vkChar, vkServes };
