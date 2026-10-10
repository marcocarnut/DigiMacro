/* Checks web/webusb.js, the page's WebHID stand-in on Android, against a fake
 * navigator.usb: it must offer only boards it can reach, keep one object per
 * board, and send the feature reports to the vendor interface exactly as the
 * board's endpoint 0 takes them from WebHID (core/core.c, usbFunctionSetup).
 */
const { usbHid, WU_VENDOR_CLASS } = require('../web/webusb.js');
const { askOwn, CU } = require('../web/coreupdate.js');

let fails = 0;
const check = (ok, what) => { if (!ok) { console.log('  FAIL: ' + what); fails++; } };

const iface = (n, cls) => ({ interfaceNumber: n, alternates: [{ interfaceClass: cls }] });

/* A board as Chrome describes it, which answers INFO like the core does:
   whatever arrives with the vendor report's SET_REPORT, the GET_REPORT
   after it carries back. */
function fakeBoard({ vid = 0x16c0, pid = 0x27db, vendor = true } = {}) {
  const cfg = { interfaces: [iface(0, 3), ...(vendor ? [iface(1, WU_VENDOR_CLASS)] : [])] };
  const d = {
    vendorId: vid, productId: pid, productName: 'DigiMacro',
    configurations: [cfg], configuration: null, opened: false, claimed: [],
    setups: [], status: 'ok', vnd: new Uint8Array(36),
    async open() { this.opened = true; },
    async selectConfiguration(n) { if (n === 1) this.configuration = cfg; },
    async claimInterface(n) {
      if (cfg.interfaces.find(i => i.interfaceNumber === n).alternates[0].interfaceClass === 3)
        throw new Error('protected class');
      this.claimed.push(n);
    },
    async controlTransferOut(setup, data) {
      this.setups.push(setup);
      const b = new Uint8Array(data);
      if (b[0] === 0x40) {
        this.vnd.fill(0);
        this.vnd.set([0x40, 0x44, 0x4d, 3, 4]);   /* INFO: 'DM', protocol 3, core 4 */
      }
      return { status: this.status, bytesWritten: b.length };
    },
    async controlTransferIn(setup, n) {
      this.setups.push(setup);
      if (this.status !== 'ok') return { status: this.status };
      return { status: 'ok', data: new DataView(this.vnd.buffer.slice(0, Math.min(n, 36))) };
    },
  };
  return d;
}

function fakeUsb(devices) {
  const listeners = {};
  return {
    picked: null, filters: null, listeners,
    async getDevices() { return devices; },
    async requestDevice({ filters }) {
      this.filters = filters;
      if (!this.picked) { const e = new Error('none'); e.name = 'NotFoundError'; throw e; }
      if (this.picked === 'boom') throw new Error('boom');
      return this.picked;
    },
    addEventListener(type, f) { (listeners[type] = listeners[type] || []).push(f); },
    fire(type, device) { for (const f of listeners[type] || []) f({ device }); },
  };
}

(async () => {
  console.log('1. no WebUSB, no stand-in');
  check(usbHid(undefined) === null, 'usbHid without navigator.usb is not null');

  console.log('2. only boards with the vendor interface, one object each');
  const core4 = fakeBoard(), core3 = fakeBoard({ vendor: false }), other = fakeBoard({ vid: 0x1234 });
  const usb = fakeUsb([core3, core4, other]);
  const hid = usbHid(usb);
  let known = await hid.getDevices();
  check(known.length === 1 && known[0].device === core4,
        `getDevices offers ${known.length} boards, not just core 4's`);
  check((await hid.getDevices())[0] === known[0], 'a second getDevices gives another object');
  check(known[0].collections.some(c => c.usagePage === 0xFF00), 'no vendor usage page to look for');

  console.log("3. the picker: only what it can reach, and closing it is no board");
  check((await hid.requestDevice({})).length === 0, 'a closed picker is not an empty list');
  const f = usb.filters && usb.filters[0];
  check(f && f.vendorId === 0x16c0 && f.productId === 0x27db && f.classCode === WU_VENDOR_CLASS,
        'the picker is not filtered to boards with the vendor interface');
  usb.picked = core4;
  check((await hid.requestDevice({}))[0] === known[0], 'the picked board is another object');
  usb.picked = 'boom';
  let threw = false;
  try { await hid.requestDevice({}); } catch (e) { threw = e.message === 'boom'; }
  check(threw, 'an error other than a closed picker is swallowed');

  console.log('4. open claims the vendor interface');
  const b = known[0];
  check(!b.opened, 'opened before open()');
  await b.open();
  check(core4.configuration !== null, 'configuration not selected');
  check(core4.claimed.length === 1 && core4.claimed[0] === 1, `claimed ${core4.claimed}`);
  check(b.opened, 'not opened after open()');

  console.log("5. INFO, through coreupdate.js's askOwn, as the page asks it");
  const io = {
    send: out => b.sendFeatureReport(2, out),
    receive: async () => { const v = await b.receiveFeatureReport(2);
                           return new Uint8Array(v.buffer, v.byteOffset, v.byteLength); },
  };
  const r = await askOwn(io, CU.INFO);
  check(r[0] === 0x40 && r[1] === 0x44 && r[2] === 0x4d && r[4] === 4, 'INFO did not come back');
  const [out, inn] = core4.setups.slice(-2);
  for (const [s, req, name] of [[out, 0x09, 'SET_REPORT'], [inn, 0x01, 'GET_REPORT']])
    check(s.requestType === 'class' && s.recipient === 'interface' && s.request === req
          && s.value === 0x0302 && s.index === 1,
          `${name} went out as ${JSON.stringify(s)}`);

  console.log('6. a failed transfer is an error, not an answer');
  core4.status = 'stall';
  for (const [what, go] of [['send', () => b.sendFeatureReport(2, new Uint8Array(36))],
                            ['receive', () => b.receiveFeatureReport(2)]]) {
    threw = false;
    try { await go(); } catch (e) { threw = true; }
    check(threw, `a stalled ${what} went through`);
  }

  console.log('7. a board without the interface cannot be opened');
  const old = fakeBoard({ vendor: false });
  const ou = fakeUsb([]); ou.picked = old;
  threw = false;
  try { await (await usbHid(ou).requestDevice({}))[0].open(); } catch (e) { threw = /core 4/.test(e.message); }
  check(threw, 'opened a board with no vendor interface');

  console.log("8. connect and disconnect name the page's own object, and only ours");
  const seen = [];
  hid.addEventListener('disconnect', ev => seen.push(ev.device));
  usb.fire('disconnect', core4);
  usb.fire('disconnect', core3);
  usb.fire('disconnect', other);
  check(seen.length === 1 && seen[0] === b, `disconnect named ${seen.length} devices, or not the page's`);

  console.log(fails ? `${fails} failed` : 'all checks passed');
  process.exit(fails ? 1 : 0);
})().catch(e => { console.log('  FAIL: threw ' + e.message); process.exit(1); });
