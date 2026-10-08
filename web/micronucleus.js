/* micronucleus.js -- the Micronucleus bootloader over WebUSB, and Intel HEX.
 *
 * Ported from DigiISP (web/src/micronucleus.ts and ihex.ts), which follows the
 * upstream command line tool (commandline/library/micronucleus_lib.c) and has
 * been run on hardware from Micronucleus 1.x up to 2.6. Speaks both: new
 * Digisparks still ship with 1.x.
 *
 * The board is a HID device and this is not: Micronucleus is plain vendor
 * class, so it is WebUSB, with a picker of its own. test/micronucleus.js
 * checks the hex parsing and the reset-vector layout in node.
 */

const MICRONUCLEUS_VID = 0x16d0;
const MICRONUCLEUS_PID = 0x0753;

const MN_INFO = 0, MN_TRANSFER_PAGE = 1, MN_ERASE = 2, MN_WRITE_DATA = 3,
      MN_RUN = 4;

/* The ATtiny85's signature, as Micronucleus 2.x reports it. */
const T85_SIGNATURE = [0x93, 0x0B];

/* Messages go through the app's translator when there is one. */
const mnText = (key, p = {}) =>
  typeof t === 'function' ? t(key, p) : `${key} ${JSON.stringify(p)}`;

const mnSleep = ms => new Promise(r => setTimeout(r, ms));

/* ---- Intel HEX ------------------------------------------------------ */

/* Record types 00, 01, 02 and 04 into a flat image of `size` bytes, erased
   bytes 0xFF; `end` is one past the highest address the file set. */
function parseIntelHex(text, size) {
  const img = { data: new Uint8Array(size).fill(0xFF),
                used: new Uint8Array(size), end: 0 };
  let base = 0;
  const lines = text.split(/\r?\n/);
  for (let n = 0; n < lines.length; n++) {
    const line = lines[n].trim();
    if (!line) continue;
    if (line[0] !== ':' || line.length < 11 || line.length % 2 !== 1)
      throw new Error(`hex line ${n + 1}: malformed`);
    const bytes = new Uint8Array((line.length - 1) / 2);
    for (let i = 0; i < bytes.length; i++)
      bytes[i] = parseInt(line.substr(1 + 2 * i, 2), 16);
    const len = bytes[0];
    if (bytes.length !== len + 5) throw new Error(`hex line ${n + 1}: length mismatch`);
    if (bytes.reduce((a, b) => a + b, 0) & 0xFF)
      throw new Error(`hex line ${n + 1}: bad checksum`);
    const addr = (bytes[1] << 8) | bytes[2];
    const type = bytes[3];
    const payload = bytes.subarray(4, 4 + len);
    if (type === 0x00) {
      for (let i = 0; i < len; i++) {
        const a = base + addr + i;
        if (a >= size)
          throw new Error(`hex line ${n + 1}: address 0x${a.toString(16)} beyond 0x${size.toString(16)}`);
        img.data[a] = payload[i];
        img.used[a] = 1;
        img.end = Math.max(img.end, a + 1);
      }
    } else if (type === 0x01) {
      break;
    } else if (type === 0x02) {
      base = ((payload[0] << 8) | payload[1]) << 4;
    } else if (type === 0x04) {
      base = ((payload[0] << 8) | payload[1]) << 16;
    }
    /* 03 and 05, start addresses, mean nothing to AVR flash */
  }
  return img;
}

/* ---- laying an application out for the bootloader ------------------- */

/* rjmp from word address `from` to word address `to`, wrapping round 8 KB. */
const rjmp = (from, to) => 0xC000 | ((to - from - 1) & 0x0FFF);

/* What the upload tool does for Micronucleus 2: the reset vector jumps to the
 * bootloader, and the application's own reset target moves into an rjmp at
 * bootloaderStart - 4, which the bootloader takes when it exits. The OSCCAL
 * byte at bootloaderStart - 6 stays 0xFF; the bootloader fills it in.
 * Micronucleus 1 does this itself, so it gets the image as it is. */
function layoutApplication(app, flashSize, bootloaderStart) {
  if (bootloaderStart > 0x2000) throw new Error('only devices up to 8 KB are supported');
  if (app.end > flashSize)
    throw new Error(mnText('mn-too-big', { n: app.end, max: flashSize }));
  const out = new Uint8Array(bootloaderStart).fill(0xFF);
  out.set(app.data.subarray(0, app.end));

  const word0 = out[0] | (out[1] << 8);
  let userReset;                        /* a word address */
  if (word0 === 0x940C) userReset = out[2] | (out[3] << 8);         /* jmp */
  else if ((word0 & 0xF000) === 0xC000) userReset = ((word0 & 0x0FFF) + 1) & 0x0FFF;
  else throw new Error(mnText('mn-no-jump'));

  const toBoot = rjmp(0, bootloaderStart / 2);
  out[0] = toBoot & 0xFF;
  out[1] = toBoot >> 8;
  const at = bootloaderStart - 4;
  const toApp = rjmp(at / 2, userReset);
  out[at] = toApp & 0xFF;
  out[at + 1] = toApp >> 8;
  return out;
}

/* ---- the bootloader ------------------------------------------------- */

class Micronucleus {
  constructor(device, info) { this.device = device; this.info = info; }

  /* Put the picker up for it; needs a click behind it. Null if the picker was
     closed without choosing. */
  static async request() {
    let device;
    try {
      device = await navigator.usb.requestDevice(
        { filters: [{ vendorId: MICRONUCLEUS_VID, productId: MICRONUCLEUS_PID }] });
    } catch (e) {
      if (e.name === 'NotFoundError') return null;
      throw e;
    }
    await device.open();
    if (device.configuration === null) await device.selectConfiguration(1);
    await device.claimInterface(0);

    /* bcdDevice is major.minor in two bytes, and WebUSB splits the minor byte
       into nibbles: read naively, 1.06 is 1.0. */
    const major = device.deviceVersionMajor;
    const minor = (device.deviceVersionMinor << 4) | device.deviceVersionSubminor;
    const version = `${major}.${String(minor).padStart(2, '0')}`;
    if (major < 1 || major > 2) {
      await device.close();
      throw new Error(mnText('mn-unsupported', { version }));
    }
    const r = await device.controlTransferIn(
      { requestType: 'vendor', recipient: 'device', request: MN_INFO, value: 0, index: 0 },
      major >= 2 ? 8 : 4);
    if (r.status !== 'ok' || !r.data || r.data.byteLength < (major >= 2 ? 6 : 4)) {
      await device.close();
      throw new Error(mnText('mn-no-info'));
    }
    const b = new Uint8Array(r.data.buffer, r.data.byteOffset, r.data.byteLength);
    const flashSize = (b[0] << 8) | b[1];
    const pageSize = b[2];
    const pages = Math.ceil(flashSize / pageSize);
    const writeSleep = (b[3] & 0x7F) + (major >= 2 ? 2 : 0);   /* as the tool
                                                   does, unless in fast mode */
    return new Micronucleus(device, {
      version, major, minor, flashSize, pageSize,
      bootloaderStart: pages * pageSize,
      writeSleep,
      eraseSleep: b[3] & 0x80 ? writeSleep * pages / 4 : writeSleep * pages,
      signature: major >= 2 ? [b[4], b[5]] : null,
    });
  }

  /* 2.6 or newer, which is what the upgrader installs. */
  get current() {
    return this.info.major > 2 || (this.info.major === 2 && this.info.minor >= 6);
  }

  /* An ATtiny85, as far as the bootloader says; 1.x does not say. */
  checkChip() {
    const s = this.info.signature;
    if (s && (s[0] !== T85_SIGNATURE[0] || s[1] !== T85_SIGNATURE[1]))
      throw new Error(mnText('mn-not-t85'));
  }

  async out(request, value = 0, index = 0) {
    const r = await this.device.controlTransferOut(
      { requestType: 'vendor', recipient: 'device', request, value, index });
    if (r.status !== 'ok') throw new Error(`Micronucleus request ${request} failed: ${r.status}`);
  }

  async erase() {
    try { await this.out(MN_ERASE); }
    catch (e) { /* it stops answering while it erases, as upstream notes */ }
    await mnSleep(this.info.eraseSleep);
  }

  /* The flash image to write for an application. */
  prepare(app) {
    const { flashSize, bootloaderStart, major } = this.info;
    if (major >= 2) return layoutApplication(app, flashSize, bootloaderStart);
    if (app.end > flashSize)
      throw new Error(mnText('mn-too-big', { n: app.end, max: flashSize }));
    const out = new Uint8Array(bootloaderStart).fill(0xFF);
    out.set(app.data.subarray(0, app.end));
    return out;
  }

  /* Page by page, blank pages skipped -- except the first, which holds the
     reset vector, and the last, which holds the application's entry jump and
     goes last on purpose: an upload interrupted before it leaves the board in
     the bootloader, not running half a program. */
  async write(image, progress) {
    const { flashSize, pageSize, bootloaderStart, writeSleep, major, minor } = this.info;
    for (let address = 0; address < flashSize; address += pageSize) {
      const last = address >= bootloaderStart - pageSize;
      let length = pageSize;
      /* like the upstream tool: 1.00 to 1.02 want a short last page */
      if (major === 1 && minor <= 2 && last) length = flashSize % pageSize || pageSize;
      const page = image.slice(address, address + length);
      if (address === 0 || last || page.some(x => x !== 0xFF)) {
        if (major >= 2) {
          await this.out(MN_TRANSFER_PAGE, length, address);
          for (let i = 0; i < length; i += 4)
            await this.out(MN_WRITE_DATA, page[i] | (page[i + 1] << 8),
                                          page[i + 2] | (page[i + 3] << 8));
        } else {
          const r = await this.device.controlTransferOut(
            { requestType: 'vendor', recipient: 'device', request: MN_TRANSFER_PAGE,
              value: length, index: address }, page);
          if (r.status !== 'ok')
            throw new Error(`Micronucleus page write at 0x${address.toString(16)} failed: ${r.status}`);
        }
        await mnSleep(writeSleep);
      }
      progress?.((address + pageSize) / flashSize);
    }
  }

  /* Start what was written; the bootloader leaves the bus. */
  async run() {
    try { await this.out(MN_RUN); }
    catch (e) { /* it may go before the status stage completes */ }
    try { await this.device.close(); } catch (e) { /* already gone */ }
  }
}

if (typeof module !== 'undefined')
  module.exports = { parseIntelHex, layoutApplication, rjmp, Micronucleus,
                     MICRONUCLEUS_VID, MICRONUCLEUS_PID };
