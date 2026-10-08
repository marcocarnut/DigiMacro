/* web/coreupdate.js against a fake v2 board.
 *
 * The fake follows core/core.c's rules for an update -- half a page per
 * report, the header erased before any other page is written, the commit
 * checking magic, API, length and CRC -- and adds what a real host meets:
 * requests that fail while the board is halted for an erase or a write, and
 * another program's INFO landing between this one's question and its read.
 * The real core's side is core/test; this is the page's.
 */
const fs = require('fs');
const path = require('path');
const { parseIntelHex } = require('../web/micronucleus.js');
const cu = require('../web/coreupdate.js');

let fails = 0;
const check = (ok, what) => { if (!ok) { console.log('  FAIL: ' + what); fails++; } };

/* ---- core/layout.h, read rather than retyped ------------------------ */

const layout = fs.readFileSync(path.join(__dirname, '../core/layout.h'), 'utf8');
const def = name => {
  const m = new RegExp(`#define\\s+${name}\\s+(\\S+)`).exec(layout);
  if (!m) throw new Error(`layout.h has no ${name}`);
  return m[1];
};
const APP_START = Number(def('APP_START'));
const APP_END = Number(def('APP_END'));
const PAGE = Number(def('PAGE'));
const SLOTS = Number(def('APP_VECTORS')) + Number(def('APP_NVECTORS'));
const API = Number(def('CORE_API'));

console.log('1. the page and layout.h agree on the header');
check(cu.CU_MAGIC_AT === 2 * SLOTS,
      `coreupdate.js looks for the magic at ${cu.CU_MAGIC_AT}, layout.h puts it at ${2 * SLOTS}`);

/* ---- an app image, as the build makes one --------------------------- */

const crc16 = (data) => {
  let c = 0xFFFF;
  for (const b of data) { c ^= b; for (let i = 0; i < 8; i++) c = c & 1 ? (c >> 1) ^ 0xA001 : c >> 1; }
  return c;
};

function makeApp(bodyLen, version, seed, api = API) {
  const len = 2 * SLOTS + 6 + bodyLen;
  const bytes = new Uint8Array(len + 2);
  for (let i = 0; i < 2 * SLOTS; i++) bytes[i] = 0xC0 + i;
  const m = 2 * SLOTS;
  bytes[m] = 0x44; bytes[m + 1] = 0x4D;
  bytes[m + 2] = len & 0xFF; bytes[m + 3] = len >> 8;
  bytes[m + 4] = version; bytes[m + 5] = api;
  for (let i = 0; i < bodyLen; i++) bytes[m + 6 + i] = (seed + i * 7) & 0xFF;
  const c = crc16(bytes.subarray(0, len));
  bytes[len] = c & 0xFF; bytes[len + 1] = c >> 8;
  return bytes;
}

/* As Intel HEX, so the page's own parser reads it. */
function toHex(bytes, at) {
  const lines = [];
  for (let i = 0; i < bytes.length; i += 16) {
    const chunk = bytes.subarray(i, i + 16), a = at + i;
    const rec = [chunk.length, a >> 8, a & 0xFF, 0, ...chunk];
    const sum = (-rec.reduce((x, y) => x + y, 0)) & 0xFF;
    lines.push(':' + [...rec, sum].map(v => v.toString(16).padStart(2, '0')).join('').toUpperCase());
  }
  return lines.join('\n') + '\n:00000001FF\n';
}

/* ---- the fake board ------------------------------------------------- */

class FakeBoard {
  constructor({ password = null, app = null, mode = cu.CU_CORE_APP } = {}) {
    this.flash = new Uint8Array(8192).fill(0xFF);
    if (app) this.flash.set(app, APP_START);
    this.mode = app ? mode : 0;
    this.password = password;
    this.unlocked = false;
    this.window = 0;            /* tenths since it opened: the app's count */
    this.vnd = new Uint8Array(36);
    this.halt = 0;              /* requests still to fail: an erase, a write */
    this.lead = 0;              /* requests answered first, before the halt:
                                   the core waits for the host's next keyboard
                                   poll before it erases (core.c, spmSlot()) */
    this.leadEach = 6;          /* how many, per page */
    this.passWait = false;      /* a wrong password: a second of refusals */
    this.gone = 0;              /* requests after a commit before the board
                                   drops off the bus to restart */
    this.badBit = null;         /* a page whose write the flash gets wrong */
    this.pending = null;
    this.clobber = 0;           /* 0..1: how often a foreign INFO cuts in */
    this.committed = false;
    this.writes = 0;
    this.pulled = false;
  }
  info() {
    const v = this.vnd;
    v.fill(0); v[0] = cu.CU.INFO; v[1] = 0x44; v[2] = 0x4D; v[3] = 3; v[4] = 1;
    v[7] = (this.password ? 0x02 : 0) | (!this.password || this.unlocked ? 0x04 : 0);
    v[12] = this.mode;
    v[13] = this.mode & cu.CU_CORE_APP ? this.flash[APP_START + 2 * SLOTS + 4] : 0;
    v[14] = API;
    v[15] = APP_START & 0xFF; v[16] = APP_START >> 8;
    v[17] = APP_END & 0xFF; v[18] = APP_END >> 8; v[19] = PAGE;
  }
  maybeHalted() {
    if (this.pulled) throw new Error('the device is gone');
    if (this.committed && --this.gone < 0) throw new Error('the device is gone');
    if (this.lead > 0) { this.lead--; return; }
    if (this.halt > 0) {
      if (--this.halt === 0) this.finishWrite();
      throw new Error('Failed to write the feature report.');
    }
  }
  finishWrite() {
    const { at, buf } = this.pending;
    /* the header first, unless it is the one being written */
    if (at !== APP_START) this.flash.fill(0xFF, APP_START, APP_START + PAGE);
    this.flash.set(buf, at);
    if (this.badBit === at) this.flash[at + 5] ^= 0x10;
    this.writes++;
    this.vnd[0] = cu.CU.PAGE; this.vnd[1] = at & 0xFF; this.vnd[2] = at >> 8;
    this.pending = null;
  }
  async send(r) {
    this.maybeHalted();
    this.vnd.set(r);
    const cmd = r[0], addr = r[1] | (r[2] << 8), n = Math.min(r[3], 32);
    const app = !!(this.mode & cu.CU_CORE_APP);
    const refuse = () => { this.vnd[0] = cu.CU.REFUSED; };
    switch (cmd) {
    case cu.CU.INFO: this.info(); break;
    case cu.CU.UNLOCK: {
      const want = new Uint8Array(12); if (this.password) want.set(this.password);
      const got = new Uint8Array(12); got.set(r.subarray(4, 4 + n));
      if (this.password && this.passWait) refuse();         /* within the second */
      else if (!this.password || got.every((b, i) => b === want[i])) this.unlocked = true;
      else { this.passWait = true; refuse(); }
      this.vnd.fill(0, 4);
      break;
    }
    case cu.CU.UPDATE:
      if (app) this.window = 0; else refuse();
      break;
    case cu.CU.FREAD:
      if (this.pending) { refuse(); break; }  /* the old page: not yet */
      for (let i = 0; i < n; i++) this.vnd[4 + i] = this.flash[addr + i];
      break;
    case cu.CU.PAGE: {
      const locked = this.password && !this.unlocked;
      if (locked || this.committed || (app && this.window >= 60) || this.pending
          || addr % 32 || addr < APP_START || addr >= APP_END || n !== 32) { refuse(); break; }
      if (addr % PAGE === 0) { this.half = { at: addr, buf: new Uint8Array(PAGE) };
                               this.half.buf.set(r.subarray(4, 36)); }
      else if (!this.half || this.half.at !== addr - 32) { refuse(); break; }
      else {
        this.half.buf.set(r.subarray(4, 36), 32);
        this.pending = this.half; this.half = null;
        this.mode = cu.CU_CORE_UPDATE;   /* app or none, as core.c's update() */
        this.vnd[0] = 0;
        this.lead = this.leadEach;
        this.halt = 3;          /* the header's erase, the page's, the write */
      }
      break;
    }
    case cu.CU.COMMIT: {
      const m = APP_START + 2 * SLOTS, f = this.flash;
      const len = f[m + 2] | (f[m + 3] << 8);
      const ok = this.mode === cu.CU_CORE_UPDATE && !this.pending
               && f[m] === 0x44 && f[m + 1] === 0x4D && f[m + 5] === API
               && len >= 2 * SLOTS + 6 && len <= APP_END - APP_START - 2
               && crc16(f.subarray(APP_START, APP_START + len))
                  === (f[APP_START + len] | (f[APP_START + len + 1] << 8));
      if (ok) { this.committed = true; this.gone = 2; } else refuse();
      break;
    }
    default: refuse();
    }
  }
  async receive() {
    this.maybeHalted();
    if (this.clobber && Math.random() < this.clobber) this.info();  /* another tab */
    return this.vnd.slice();
  }
}

const imgOf = bytes => parseIntelHex(toHex(bytes, APP_START), 8192);
const infoOf = async b => cu.coreInfo(await cu.askOwn(b, cu.CU.INFO));

async function main() {
  console.log('2. a whole update, header last, and committed');
  {
    const b = new FakeBoard({ app: makeApp(300, 10, 1) });
    const app = makeApp(1500, 11, 2);
    let seen = 0;
    await cu.writeApp(b, imgOf(app), await infoOf(b), { onPage: n => { seen = n; } });
    check(b.committed, 'the commit was refused');
    check(b.flash.subarray(APP_START, APP_START + app.length).every((v, i) => v === app[i]),
          'the flash does not hold the new app');
    check(seen === Math.ceil(app.length / PAGE), `${seen} pages reported`);
  }

  console.log('3. another program asking at the same time changes nothing');
  {
    const b = new FakeBoard({ app: makeApp(300, 10, 1) });
    b.clobber = 0.3;
    const app = makeApp(2000, 12, 3);
    await cu.writeApp(b, imgOf(app), await infoOf(b));
    check(b.committed, 'the commit was refused with another program asking');
    check(b.flash.subarray(APP_START, APP_START + app.length).every((v, i) => v === app[i]),
          'and the flash is wrong');
  }

  console.log('4. what the page refuses before it sends anything');
  {
    const b = new FakeBoard({ app: makeApp(300, 10, 1) });
    const info = await infoOf(b);
    const tryOne = async (img) => {
      try { await cu.writeApp(b, img, info); return null; } catch (e) { return e.message; }
    };
    check(/cu-api/.test(await tryOne(imgOf(makeApp(300, 11, 2, API + 1)))),
          'an app for another core API went ahead');
    check(/cu-region/.test(await tryOne(parseIntelHex(toHex(makeApp(300, 11, 2), APP_START + PAGE), 8192))),
          'an app not at the boundary went ahead');
    const noHeader = makeApp(300, 11, 2); noHeader[2 * SLOTS] = 0;
    check(/cu-no-header/.test(await tryOne(imgOf(noHeader))), 'an image with no header went ahead');
    check(b.writes === 0, `${b.writes} pages written by refused images`);
  }

  console.log('5. a password, and the update window');
  {
    const b = new FakeBoard({ app: makeApp(300, 10, 1), password: [0x74, 0x65, 0x73, 0x74] });
    const app = makeApp(500, 11, 2);
    let msg = null;
    try { await cu.writeApp(b, imgOf(app), await infoOf(b)); } catch (e) { msg = e.message; }
    check(/cu-first-refused/.test(msg || ''), `locked, it said: ${msg}`);
    await cu.askOwn(b, cu.CU.UNLOCK, 0, 4, Uint8Array.from([0x74, 0x65, 0x73, 0x74]));
    b.window = 70;              /* six seconds past */
    msg = null;
    try { await cu.writeApp(b, imgOf(app), await infoOf(b)); } catch (e) { msg = e.message; }
    check(/cu-first-refused/.test(msg || ''), `the window shut, it said: ${msg}`);
    await cu.askOwn(b, cu.CU.UPDATE);
    await cu.writeApp(b, imgOf(app), await infoOf(b));
    check(b.committed, 'unlocked and in the window, it was refused');
  }

  console.log('6. cut off half way: no app, and the next update finishes it');
  {
    const b = new FakeBoard({ app: makeApp(300, 10, 1) });
    const app = makeApp(1500, 11, 2);
    let n = 0;
    try {
      await cu.writeApp(b, imgOf(app), await infoOf(b),
                        { pause: async () => { if (++n === 5) b.pulled = true; } });
    } catch (e) { /* the cable */ }
    b.pulled = false;           /* it boots again, and finds no app */
    b.mode = 0; b.pending = b.half = null; b.halt = 0;
    check(b.flash[APP_START + 2 * SLOTS] === 0xFF, 'the header survived a cut-off update');
    const info = await infoOf(b);
    check(info.state === 0 && info.app === 0, 'INFO does not say "no app"');
    await cu.writeApp(b, imgOf(app), info);
    check(b.committed, 'the second update was refused');
  }

  console.log('6a. the same firmware again, and versions that share pages');
  for (const lead of [0, 3, 8, 20]) {
    /* The core answers for a while before it halts -- however long the
       host's next keyboard poll takes -- with the old page still there. */
    const same = makeApp(1500, 10, 5);
    const b = new FakeBoard({ app: same });
    b.leadEach = lead;
    let msg = null;
    try { await cu.writeApp(b, imgOf(same), await infoOf(b)); } catch (e) { msg = e.message; }
    check(b.committed, `the same firmware again, lead ${lead}: ${msg}`);
  }
  {
    const old = makeApp(1500, 10, 5), next = makeApp(1500, 11, 5);
    next[2 * SLOTS + 6 + 400] ^= 1;     /* one page differs, the rest do not */
    const len = 2 * SLOTS + 6 + 1500, c = crc16(next.subarray(0, len));
    next[len] = c & 0xFF; next[len + 1] = c >> 8;
    const b = new FakeBoard({ app: old });
    b.leadEach = 8;
    let msg = null;
    try { await cu.writeApp(b, imgOf(next), await infoOf(b)); } catch (e) { msg = e.message; }
    check(b.committed, `versions sharing pages: ${msg}`);
    check(b.flash.subarray(APP_START, APP_START + next.length).every((v, i) => v === next[i]),
          'and the flash holds the new one');
  }

  console.log('6b. a page the flash did not take is caught, not committed');
  {
    const b = new FakeBoard({ app: makeApp(300, 10, 1) });
    b.badBit = APP_START + 3 * PAGE;
    let msg = null;
    try { await cu.writeApp(b, imgOf(makeApp(1500, 11, 2)), await infoOf(b)); }
    catch (e) { msg = e.message; }
    check(/cu-not-held/.test(msg || ''), `a bad page went by: ${msg}`);
    check(!b.committed, 'and was committed');
  }

  console.log('7. the shipped v2 app, if built, fits the core it is for');
  {
    const FW = path.join(__dirname, '../web/firmware');
    if (fs.existsSync(path.join(FW, 'digimacro-v2.hex'))) {
      const meta = JSON.parse(fs.readFileSync(path.join(FW, 'digimacro-v2.json'), 'utf8'));
      const img = parseIntelHex(fs.readFileSync(path.join(FW, 'digimacro-v2.hex'), 'utf8'), 8192);
      const { version } = cu.appPages(img, { start: APP_START, end: APP_END, page: PAGE, api: API });
      check(version === meta.app, `the hex says app ${version}, the json ${meta.app}`);
      check(meta.api === API, `the json says API ${meta.api}, layout.h ${API}`);
    } else {
      console.log('  (not built yet: make release)');
    }
  }

  console.log('8. the core, laid out for Micronucleus, starts where the core will put its reset back');
  {
    const FW = path.join(__dirname, '../web/firmware');
    const f = path.join(FW, 'digimacro-v2-core.hex');
    if (fs.existsSync(f)) {
      const { layoutApplication } = require('../web/micronucleus.js');
      const core = parseIntelHex(fs.readFileSync(f, 'utf8'), 8192);
      check(core.end <= 0x1A00 - 6, `the core is ${core.end} bytes: past Micronucleus`);
      check(core.end <= APP_START, `the core runs to 0x${core.end.toString(16)}, past the boundary`);
      const own = core.data[0] | (core.data[1] << 8);       /* rjmp __init */
      const img = layoutApplication(core, 8192, 0x1A00);
      const w = at => img[at] | (img[at + 1] << 8);
      const to = (at, word) => ((((at >> 1) + (word & 0x0FFF) + 1) & 0x0FFF) << 1);
      check(to(0, w(0)) === 0x1A00, 'the reset does not go to Micronucleus');
      check(to(0x1A00 - 4, w(0x1A00 - 4)) === to(0, own),
            "Micronucleus's exit does not land on the core's own start");
    } else {
      console.log('  (not built yet: make release)');
    }
  }

  console.log(fails ? `${fails} failed` : 'all checks passed');
  process.exit(fails ? 1 : 0);
}

main().catch(e => { console.log('  FAIL: ' + e.stack); process.exit(1); });
