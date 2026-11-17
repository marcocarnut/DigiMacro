/* Updating a v2 board: the app above the core, written over the same vendor
 * report the rest of the page uses. The board's side is core/core.c; what
 * both sides agree on is core/layout.h.
 *
 * Nothing changes device, API or picker: the board stays the keyboard it was,
 * takes the new app half a page per report, and restarts into it. Two rules
 * the board taught the spike:
 *
 *   An answer counts only if it is to this question. The board has one report
 *   for everybody, so another program with it open -- the page in a second
 *   tab, say -- can ask between this one's question and its read of the
 *   answer.
 *
 *   A page is written when the flash, read back, holds it -- not when the
 *   board says so, since its saying so could be overwritten the same way.
 *   And the reading back is the wait: the board halts for each erase and
 *   write, a request that meets a halt fails, and failing fast is what lets
 *   this go without timers, which Chrome slows to a second in a tab that is
 *   not in front.
 */

const CU = { INFO: 0x40, UNLOCK: 0x62, PAGE: 0x65, FREAD: 0x66, COMMIT: 0x67,
             UPDATE: 0x68, REFUSED: 0xEE };
const CU_LEN = 36, CU_DATA = 4, CU_CHUNK = 32;

/* core/layout.h: the app's header is a table of 3 + 14 rjmps (setup, loop,
   request, then the chip's vectors), then the magic 'DM', the length, the
   app's version and the core API it was built for. test/coreupdate.js holds
   this to layout.h. */
const CU_MAGIC_AT = 2 * (3 + 14);
const CU_CORE_APP = 0x01, CU_CORE_UPDATE = 0x02, CU_CORE_HALTED = 0x04;

const cuText = (key, p = {}) =>
  typeof t === 'function' ? t(key, p) : `${key} ${JSON.stringify(p)}`;

/* Whether `b` answers `cmd` at `addr`: it echoes the command, or refuses it,
   and keeps the address. INFO answers over the address -- but any INFO
   answer says the same, whoever asked for it. */
function isOwnAnswer(cmd, addr, b) {
  if (cmd === CU.INFO) return b[0] === CU.INFO;
  return (b[0] === cmd || b[0] === CU.REFUSED) && (b[1] | (b[2] << 8)) === addr;
}

/* `io.send(report)` and `io.receive()` move one 36-byte report each way.
   Asks until the answer is its own; another program answered in between is
   not an error, only a reason to ask again. `once` is for what must not be
   asked twice -- a password, which a v1 board counts each time it is wrong:
   then a lost answer comes back as null, and the caller finds out another
   way. */
async function askOwn(io, cmd, addr = 0, count = 0, data = null, once = false) {
  for (let i = 0; i < (once ? 1 : 20); i++) {
    const out = new Uint8Array(CU_LEN);
    out[0] = cmd; out[1] = addr & 0xFF; out[2] = addr >> 8; out[3] = count;
    if (data) out.set(data, CU_DATA);
    await io.send(out);
    const b = await io.receive();
    if (isOwnAnswer(cmd, addr, b)) return b;
  }
  if (once) return null;
  throw new Error(cuText('cu-crowded'));
}

/* What INFO says about the core, or null for a v1 board (protocol 2). */
function coreInfo(b) {
  if (b[3] < 3) return null;
  return { core: b[4], state: b[12], app: b[13], api: b[14],
           start: b[15] | (b[16] << 8), end: b[17] | (b[18] << 8), page: b[19] };
}

/* An app image (parseIntelHex's) checked against the board's core: it starts
   at the boundary, ends inside the region, carries the header and was built
   for this core's API. The pages to write, the header's last -- the core
   erases the header before writing anything else, so a board cut off half
   way has no app to run, rather than half of one. */
function appPages(img, info) {
  const start = img.used.indexOf(1);
  if (start !== info.start || img.end > info.end)
    throw new Error(cuText('cu-region', { start: hex4(start), end: hex4(img.end),
                                          rs: hex4(info.start), re: hex4(info.end) }));
  const d = img.data, m = info.start + CU_MAGIC_AT;
  if (d[m] !== 0x44 || d[m + 1] !== 0x4D) throw new Error(cuText('cu-no-header'));
  if (d[m + 5] !== info.api)
    throw new Error(cuText('cu-api', { app: d[m + 5], core: info.api }));
  const pages = [];
  for (let p = info.start; p < img.end; p += info.page) pages.push(p);
  pages.push(pages.shift());
  return { pages, version: d[m + 4] };
}

const hex4 = n => '0x' + n.toString(16).toUpperCase().padStart(4, '0');

/* ---- the clock's trim, in an image -----------------------------------
 *
 * Firmware 19 on keeps four bytes just after the header, at APP_CODE, that
 * say how its clock is trimmed (clock_poll in app/asm/digimacro.S): a count
 * of seconds, the length of the odd second that ends it, the length that
 * ends any second, low bytes of 999, 1000 or 1001 ticks; and a zero.
 * tools/settrim.py writes the same bytes, and test/coreupdate.js holds the
 * two to each other. The app's CRC, which the core checks before it runs
 * the app, covers them, so it is stamped again.
 */
const CU_TRIM_AT = CU_MAGIC_AT + 6, CU_TRIM_FIRST = 19;

/* avr-libc's _crc16_update over the app, from 0xFFFF: app/apphex.py's */
function appCrc(d, from, len) {
  let c = 0xFFFF;
  for (let i = from; i < from + len; i++) {
    c ^= d[i];
    for (let k = 0; k < 8; k++) c = c & 1 ? (c >> 1) ^ 0xA001 : c >> 1;
  }
  return c;
}

/* 0, or 4 to 1000 ppm either way: the clock gains that much */
function trimBytes(ppm) {
  if (ppm === 0) return [0, 0xE8, 0xE8, 0];
  const a = Math.abs(ppm);
  if (!Number.isInteger(ppm) || a < 4 || a > 1000)
    throw new Error(cuText('cu-trim-range'));
  const n = Math.floor((Math.floor(2000 / a) + 1) / 2);    /* round(1000 / a) */
  return ppm > 0 ? [n - 1, 0xE7, 0xE8, 0] : [n - 1, 0xE9, 0xE9, 0];
}

/* The ppm the bytes trim by (0 for none), or null if they are not a trim */
function trimPpm(b) {
  const [count, odd, roll, pad] = b;
  if (pad !== 0) return null;
  if (odd === 0xE8 && roll === 0xE8) return 0;
  const n = count + 1;
  if (odd === 0xE7 && roll === 0xE8) return 1e6 / (1000 * n - 1);
  if (odd === 0xE9 && roll === 0xE9) return -1e6 / (1000 * n + 1);
  return null;
}

/* The whole ppm that writes these bytes again: what a field shows for them */
function trimField(ppm) {
  return ppm === 0 ? 0
       : Math.sign(ppm) * Math.min(1000, Math.max(4, Math.round(Math.abs(ppm))));
}

/* An image (parseIntelHex's) checked as one with a trim: the header, its
   CRC right, firmware 19 or later, the bytes a trim. Its start, length,
   version and trim in ppm. */
function imageTrim(img) {
  const s = img.used.indexOf(1), d = img.data, m = s + CU_MAGIC_AT;
  if (s < 0 || d[m] !== 0x44 || d[m + 1] !== 0x4D) throw new Error(cuText('cu-no-header'));
  const len = d[m + 2] | d[m + 3] << 8, version = d[m + 4];
  if (appCrc(d, s, len) !== (d[s + len] | d[s + len + 1] << 8))
    throw new Error(cuText('cu-trim-crc'));
  if (version < CU_TRIM_FIRST)
    throw new Error(cuText('cu-trim-old', { fw: version, first: CU_TRIM_FIRST }));
  const ppm = trimPpm(d.subarray(s + CU_TRIM_AT, s + CU_TRIM_AT + 4));
  if (ppm === null) throw new Error(cuText('cu-trim-bytes'));
  return { start: s, len, version, ppm };
}

/* A copy of the image, its clock trimmed by ppm and its CRC stamped again */
function withTrim(img, ppm) {
  const { start, len } = imageTrim(img), b = trimBytes(ppm);
  const out = { data: img.data.slice(), used: img.used.slice(), end: img.end };
  out.data.set(b, start + CU_TRIM_AT);
  const c = appCrc(out.data, start, len);
  out.data[start + len] = c & 0xFF;
  out.data[start + len + 1] = c >> 8;
  return out;
}

/* Reads the page back until it holds what was sent: a request that meets a
   halt fails, and the core refuses FREAD while the page waits to be
   written -- before then the flash still holds the old page, which may be
   the same, and would pass for written. */
async function pageHolds(io, addr, page) {
  for (let tries = 0; tries < 400; tries++) {
    try {
      let same = true;
      for (let h = 0; h < page.length && same; h += CU_CHUNK) {
        const b = await askOwn(io, CU.FREAD, addr + h, CU_CHUNK);
        if (b[0] !== CU.FREAD) { same = false; break; }
        for (let i = 0; i < CU_CHUNK && same; i++)
          if (b[CU_DATA + i] !== page[h + i]) same = false;
      }
      if (same) return true;
    } catch (e) {
      if (e.message === cuText('cu-crowded')) throw e;
    }
  }
  return false;
}

/* The whole update. The board must already take pages: unlocked if it has a
   password, and with an app running, inside the update window the app opens
   (CU.UPDATE, which the page asks for behind the hold). `onPage(done, all)`
   follows along; `pause()`, if given, runs between pages -- for testing a
   cable pulled half way. After the commit the board restarts into the new
   app, off the bus and back. */
async function writeApp(io, img, info, { onPage, pause } = {}) {
  const { pages } = appPages(img, info);
  for (const [i, p] of pages.entries()) {
    const page = img.data.slice(p, p + info.page);
    const first = await askOwn(io, CU.PAGE, p, CU_CHUNK, page.subarray(0, CU_CHUNK));
    if (first[0] !== CU.PAGE)
      throw new Error(cuText(i ? 'cu-page-refused' : 'cu-first-refused', { at: hex4(p) }));
    const out = new Uint8Array(CU_LEN);
    out[0] = CU.PAGE; out[1] = (p + CU_CHUNK) & 0xFF; out[2] = (p + CU_CHUNK) >> 8;
    out[3] = CU_CHUNK; out.set(page.subarray(CU_CHUNK), CU_DATA);
    await io.send(out);                 /* answered by writing: see pageHolds() */
    if (!await pageHolds(io, p, page))
      throw new Error(cuText('cu-not-held', { at: hex4(p) }));
    if (onPage) onPage(i + 1, pages.length);
    if (pause) await pause();
  }
  /* Asked once: a board that took it restarts a tenth of a second later,
     and asking again would meet it gone. Only a refusal is a failure; an
     answer lost to another program, or to the restart, is not -- and the
     board, when it comes back, says which it was. */
  let c = null;
  try { c = await askOwn(io, CU.COMMIT, 0, 0, null, true); } catch (e) {}
  if (c && c[0] === CU.REFUSED) throw new Error(cuText('cu-commit-refused'));
}

if (typeof module !== 'undefined')
  module.exports = { CU, CU_CORE_APP, CU_CORE_UPDATE, CU_CORE_HALTED, CU_MAGIC_AT,
                     isOwnAnswer, askOwn, coreInfo, appPages, writeApp,
                     CU_TRIM_AT, appCrc, trimBytes, trimPpm, trimField, imageTrim, withTrim };
