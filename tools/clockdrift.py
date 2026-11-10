#!/usr/bin/env python3
"""
Compare a DigiMacro board's running clock with this computer's, by the 0x6C
read that needs no gate. For Marco's hands-on drift measurement: run it at
intervals over a few hours (or leave it looping) and watch the difference move.

    clockdrift.py /dev/hidrawN [seconds-between-reads]

Read-only: it only ever asks the clock. Set the clock first with the page or
digimacro-host. The board's seconds are its count since that set; the
difference from wall time, and its slope, is the drift.

Not run against a board yet: written alongside the firmware, hardware untested.
"""
import fcntl, struct, sys, time

VND_ID, VND_LEN = 2, 36
SET = 0xC0004806 | ((1 + VND_LEN) << 16)    # HIDIOCSFEATURE(1 + VND_LEN)
GET = 0xC0004807 | ((1 + VND_LEN) << 16)    # HIDIOCGFEATURE(1 + VND_LEN)
TICK_MS = 129 * 128 / 16.5e6 * 1000


def read_clock(fd):
    buf = bytearray(1 + VND_LEN)
    buf[0], buf[1] = VND_ID, 0x6C
    fcntl.ioctl(fd, SET, bytes(buf))
    buf = bytearray(1 + VND_LEN)
    buf[0] = VND_ID
    now = time.time()
    fcntl.ioctl(fd, GET, buf)
    a = buf[1:] if buf[0] == VND_ID else buf
    if a[0] != 0x6C:
        raise SystemExit('the board did not answer the clock read (firmware 18?)')
    secs = struct.unpack('<I', bytes(a[4:8]))[0]
    ticks = struct.unpack('<H', bytes(a[8:10]))[0]
    return now, secs + ticks * TICK_MS / 1000, a[10]


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    period = float(sys.argv[2]) if len(sys.argv) > 2 else 60
    first = None
    with open(sys.argv[1], 'rb+', buffering=0) as f:
        while True:
            now, board, isset = read_clock(f.fileno())
            if not isset:
                print('the clock is not set: set it with the page or digimacro-host')
            elif first is None:
                first = (now, board)
                print('start: board %.3f, offset %.3f s' % (board, board - now))
            else:
                elapsed = now - first[0]
                drift = (board - first[1]) - elapsed
                ppm = drift / elapsed * 1e6 if elapsed > 0 else 0
                print('%8.0f s: drift %+.3f s, %+.1f ppm' % (elapsed, drift, ppm))
            sys.stdout.flush()
            time.sleep(period)


if __name__ == '__main__':
    main()
