/* The board over WebUSB, for a browser that has it and not WebHID: Chrome on
 * Android. WebUSB will not claim a HID interface, so core 4 puts a second
 * one beside it, vendor class and no endpoints (core/vusb/usbconfig.h), and
 * the board takes the same feature reports sent there: usbFunctionSetup()
 * looks at the request, not at the interface it was sent to.
 *
 * What the page uses of navigator.hid and of a HIDDevice, and no more, so
 * that the page goes on as if this were WebHID. Neither the picker nor
 * getDevices() offers a board without the interface: one set up on an
 * older core, which only a programmer rewrites.
 */

const WU_VID = 0x16c0, WU_PID = 0x27db, WU_VENDOR_CLASS = 0xff;
const WU_GET_REPORT = 0x01, WU_SET_REPORT = 0x09, WU_FEATURE = 3;
const WU_MAX = 64;              /* the board answers with what the report has */

const wuVendorIface = cfg =>
  cfg && cfg.interfaces.find(i => i.alternates.some(a => a.interfaceClass === WU_VENDOR_CLASS));

class UsbBoard {
  constructor(device) { this.device = device; this.iface = null; }
  get vendorId() { return this.device.vendorId; }
  get productId() { return this.device.productId; }
  get productName() { return this.device.productName; }
  /* the vendor usage page the page looks for, which this stands in for */
  get collections() { return [{ usagePage: 0xFF00 }]; }
  get opened() { return this.device.opened && this.iface !== null; }

  async open() {
    const d = this.device;
    if (!d.opened) await d.open();
    if (d.configuration === null) await d.selectConfiguration(1);
    const i = wuVendorIface(d.configuration);
    if (!i) throw new Error('no vendor interface: the board needs core 4');
    await d.claimInterface(i.interfaceNumber);
    this.iface = i.interfaceNumber;
  }

  setup(request, id) {
    return { requestType: 'class', recipient: 'interface', request,
             value: (WU_FEATURE << 8) | id, index: this.iface };
  }
  async sendFeatureReport(id, data) {
    const r = await this.device.controlTransferOut(this.setup(WU_SET_REPORT, id), data);
    if (r.status !== 'ok') throw new Error('SET_REPORT: ' + r.status);
  }
  async receiveFeatureReport(id) {
    const r = await this.device.controlTransferIn(this.setup(WU_GET_REPORT, id), WU_MAX);
    if (r.status !== 'ok' || !r.data) throw new Error('GET_REPORT: ' + r.status);
    return r.data;
  }
}

/* navigator.hid's part, over navigator.usb; null without it. One UsbBoard
   per USBDevice, so that the page can tell its board by identity, as it does
   a HIDDevice. */
function usbHid(usb = typeof navigator !== 'undefined' && navigator.usb) {
  if (!usb) return null;
  const boards = new WeakMap();
  const board = d => {
    let b = boards.get(d);
    if (!b) boards.set(d, b = new UsbBoard(d));
    return b;
  };
  const ours = d => d.vendorId === WU_VID && d.productId === WU_PID
                    && d.configurations.some(wuVendorIface);
  return {
    async getDevices() { return (await usb.getDevices()).filter(ours).map(board); },
    /* As WebHID's: no board, not an error, when the picker is closed. */
    async requestDevice() {
      try {
        return [board(await usb.requestDevice({ filters: [
          { vendorId: WU_VID, productId: WU_PID, classCode: WU_VENDOR_CLASS }] }))];
      } catch (e) {
        if (e.name === 'NotFoundError') return [];
        throw e;
      }
    },
    addEventListener(type, f) {
      usb.addEventListener(type, ev => { if (ours(ev.device)) f({ device: board(ev.device) }); });
    },
  };
}

if (typeof module !== 'undefined')
  module.exports = { UsbBoard, usbHid, WU_VENDOR_CLASS };
