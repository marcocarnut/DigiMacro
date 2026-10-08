#!/usr/bin/env python3
"""Talk to a DigiMacro over hidraw, the way the page will over WebHID.

  spikectl.py info              what the board says it is
  spikectl.py flash app.hex     replace the app: pages, header last, commit
  spikectl.py flash app.hex --stop-after N
                                send N pages and stop, as a pulled cable would
  spikectl.py flash app.hex --slow S
                                wait S seconds between pages: time to pull it
  spikectl.py flash app.hex --no-arm
                                without asking the app to open the update
                                window first: works only in the six seconds
                                after a replug, or with no app
  spikectl.py ... --wait        first wait for the board to be replugged
  spikectl.py read ADDR COUNT   flash, as the core reads it
  spikectl.py unlock PASS       give the password (then flash in the same run
                                with: flash app.hex --pass PASS)
  spikectl.py setpass PASS      set the password; '' clears it

Linux only; finds the board by its serial prefix, as digimacro-host does.
"""
import fcntl, glob, os, struct, sys, time

VND_ID, VND_LEN, CHUNK = 2, 36, 32
INFO, PAGE, FREAD, COMMIT, REFUSED = 0x40, 0x65, 0x66, 0x67, 0xEE
UNLOCK, SETPASS, UPDATE = 0x62, 0x63, 0x68


def _ioc(dir_, nr, size):
    return (dir_ << 30) | (size << 16) | (ord('H') << 8) | nr


def HIDIOCSFEATURE(n): return _ioc(3, 0x06, n)
def HIDIOCGFEATURE(n): return _ioc(3, 0x07, n)


def present():
    for d in glob.glob('/sys/class/hidraw/hidraw*'):
        try:
            if 'HID_UNIQ=postcogito.org:' in open(d + '/device/uevent').read():
                return True
        except OSError:
            pass
    return False


def wait_replug():
    """Until the board has gone and come back: for testing what it does in
    the first seconds after a replug."""
    print('unplug the board, and plug it back in...', flush=True)
    while present():
        time.sleep(0.05)
    while not present():
        time.sleep(0.05)
    time.sleep(0.3)                     # udev, permissions
    print('back', flush=True)


def find():
    for d in sorted(glob.glob('/sys/class/hidraw/hidraw*')):
        ue = open(d + '/device/uevent').read()
        if 'HID_UNIQ=postcogito.org:' not in ue:
            continue
        # The vendor collection is on the same node as the keyboard: one
        # interface, two collections.
        return '/dev/' + os.path.basename(d)
    sys.exit('no DigiMacro found')


class Board:
    def __init__(self):
        self.fd = os.open(find(), os.O_RDWR)

    def set(self, cmd, addr=0, count=0, data=b''):
        buf = bytearray([VND_ID, cmd, addr & 0xFF, addr >> 8, count]) + data
        buf += bytes(1 + VND_LEN - len(buf))
        fcntl.ioctl(self.fd, HIDIOCSFEATURE(len(buf)), buf)

    def get(self):
        buf = bytearray(1 + VND_LEN)
        buf[0] = VND_ID
        fcntl.ioctl(self.fd, HIDIOCGFEATURE(len(buf)), buf)
        # The device answers with the report alone, no ID in front, and
        # hidraw hands over what came: the command is the first byte.
        return bytes(buf[:VND_LEN])

    def ask(self, cmd, addr=0, count=0, data=b''):
        self.set(cmd, addr, count, data)
        return self.get()

    def ask_own(self, cmd, addr=0, count=0, data=b''):
        """Ask, and make sure the answer is to this question. The board has
        one report for everybody: another program with it open -- a browser
        tab with the app, say, reading INFO for its status line -- can ask
        between this one's question and its read, and then the answer read
        is the other program's. Ours echoes the command, or is a refusal,
        and keeps the address; anything else, ask again."""
        for _ in range(10):
            r = self.ask(cmd, addr, count, data)
            if r[0] in (cmd, REFUSED) and (r[1] | r[2] << 8) == addr:
                return r
            time.sleep(0.005)
        sys.exit('no answer of our own to 0x%02X: something else keeps '
                 'asking the board' % cmd)


def info(b):
    r = b.ask(INFO)
    if r[1:3] != b'DM':
        sys.exit('not a DigiMacro: ' + r.hex())
    out = {'protocol': r[3], 'firmware/core': r[4], 'state': '0x%02X' % r[7],
           'fuses': ' '.join('%02X' % x for x in r[8:12])}
    if r[3] >= 3:
        out.update({'core state': '0x%02X' % r[12], 'app version': r[13],
                    'api': r[14],
                    'app region': '0x%04X-0x%04X' % (r[15] | r[16] << 8,
                                                     r[17] | r[18] << 8),
                    'page': r[19]})
    return r, out


def read_hex(path):
    mem, base = {}, 0
    for line in open(path):
        line = line.strip()
        if not line.startswith(':'):
            continue
        rec = bytes.fromhex(line[1:])
        n, addr, kind = rec[0], rec[1] << 8 | rec[2], rec[3]
        if kind == 0:
            for i in range(n):
                mem[base + addr + i] = rec[4 + i]
        elif kind == 4:
            base = (rec[4] << 8 | rec[5]) << 16
    return mem


def wait_page(b, addr, page):
    """The board halts while it erases and writes: give it time, then read
    the page back. Its own answer to the write would do, if nothing else
    could overwrite it before it is read -- see ask_own() -- so what decides
    is the flash itself."""
    time.sleep(0.03)
    for _ in range(40):
        try:
            back = b''
            for h in range(0, len(page), CHUNK):
                r = b.ask_own(FREAD, addr + h, CHUNK)
                back += r[4:4 + CHUNK]
        except OSError:                 # a halt, still: not yet
            time.sleep(0.01)
            continue
        if back == page:
            return
        time.sleep(0.01)
    sys.exit('page 0x%04X: the flash does not hold it' % addr)


def flash(b, path, stop_after=None, slow=0, arm=True):
    r, _ = info(b)
    if r[3] < 3:
        sys.exit('this board has no core to take pages (protocol %d)' % r[3])
    if arm and r[12] & 1:               # an app is running: ask it to open
        rr = b.ask_own(UPDATE)          # the window (layout.h)
        print('update window:', 'opened' if rr[0] == UPDATE else 'REFUSED')
    start, end, size = r[15] | r[16] << 8, r[17] | r[18] << 8, r[19]
    mem = read_hex(path)
    lo, hi = min(mem), max(mem) + 1
    if lo != start or hi > end:
        sys.exit('image 0x%04X-0x%04X does not fit 0x%04X-0x%04X'
                 % (lo, hi, start, end))
    pages = list(range(start, hi, size))
    # the header page last: the core erases it before writing anything else
    order = pages[1:] + pages[:1]
    t = time.time()
    for i, p in enumerate(order):
        if stop_after is not None and i == stop_after:
            print('stopped after %d of %d pages, as asked' % (i, len(order)))
            return
        page = bytes(mem.get(p + j, 0xFF) for j in range(size))
        for h in range(0, size, CHUNK):
            if h:                       # the second: answered later
                b.set(PAGE, p + h, CHUNK, page[h:h + CHUNK])
            else:
                rr = b.ask_own(PAGE, p, CHUNK, page[:CHUNK])
                if rr[0] != PAGE:
                    sys.exit('page 0x%04X, first half refused%s (answer %s)' % (p,
                             ': no password given?' if i == 0 else '',
                             rr[:8].hex()))
        wait_page(b, p, page)
        print('\rpage 0x%04X  %d/%d' % (p, i + 1, len(order)), end='', flush=True)
        time.sleep(slow)
    print('  %.1f s' % (time.time() - t))
    rr = b.ask_own(COMMIT)
    if rr[0] != COMMIT:
        sys.exit('commit refused: the app did not check out')
    print('committed; the board comes back as the new app')


def unlock(b, pw):
    r = b.ask_own(UNLOCK, 0, len(pw), pw.encode())
    return r[0] == UNLOCK


def main(a):
    if not a:
        sys.exit(__doc__)
    if '--wait' in a:
        a.remove('--wait')
        wait_replug()
    arm = '--no-arm' not in a
    if not arm:
        a.remove('--no-arm')
    b = Board()
    if '--pass' in a:
        i = a.index('--pass')
        pw = a[i + 1]
        del a[i:i + 2]
        print('unlock:', 'accepted' if unlock(b, pw) else 'REFUSED')
    if a[0] == 'info':
        _, out = info(b)
        for k, v in out.items():
            print('%-14s %s' % (k, v))
    elif a[0] == 'flash' and len(a) == 4 and a[2] == '--slow':
        flash(b, a[1], slow=float(a[3]), arm=arm)
    elif a[0] == 'flash' and len(a) in (2, 4):
        flash(b, a[1], int(a[3]) if len(a) == 4 else None, arm=arm)
    elif a[0] == 'read' and len(a) == 3:
        addr, n = int(a[1], 0), int(a[2], 0)
        out = b''
        while len(out) < n:
            k = min(CHUNK, n - len(out))
            r = b.ask_own(FREAD, addr + len(out), k)
            if r[0] != FREAD:
                sys.exit('refused')
            out += r[4:4 + k]
        for i in range(0, n, 16):
            print('%04X  %s' % (addr + i, out[i:i + 16].hex(' ')))
    elif a[0] == 'unlock' and len(a) == 2:
        print('accepted' if unlock(b, a[1]) else 'REFUSED')
    elif a[0] == 'setpass' and len(a) == 2:
        r = b.ask(SETPASS, 0, len(a[1]), a[1].encode())
        print('set' if r[0] == SETPASS else 'REFUSED')
    else:
        sys.exit(__doc__)


if __name__ == '__main__':
    main(sys.argv[1:])
