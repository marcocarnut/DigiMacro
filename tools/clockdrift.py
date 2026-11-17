#!/usr/bin/env python3
"""
Compare a DigiMacro board's running clock with this computer's, by the 0x6C
read that needs no gate. For Marco's hands-on drift measurement: run it at
intervals over a few hours (or leave it looping) and watch the difference move.

    clockdrift.py /dev/hidrawN [seconds-between-reads]

Read-only: it only ever asks the clock. Set the clock first with the page or
digimacro-host. The board's seconds are its count since that set; the
difference from this computer's, and its slope, is the drift. Two of this
computer's clocks, on each line:

  wall  time.time(): the system clock, which NTP keeps right by slewing it.
        The board against true time, give or take what NTP is doing now.
  raw   CLOCK_MONOTONIC_RAW, which NTP never touches: this computer's own
        crystal. Wobble in "wall" that "raw" does not have is NTP's.

The board counts the host's USB frames, so both lines also show the USB
controller's crystal; "raw" runs on the same crystal only on some machines.

The board takes its snapshot as it handles the SET that asks the clock, so
each read is timed around the SET: the midpoint of the clocks read just
before it and just after. That is off by at most half the SET's time,
shown as "set"; "rtt" is the whole question, SET to the GET's answer.
"""
import fcntl, struct, sys, time

VND_ID, VND_LEN = 2, 36
SET = 0xC0004806 | ((1 + VND_LEN) << 16)    # HIDIOCSFEATURE(1 + VND_LEN)
GET = 0xC0004807 | ((1 + VND_LEN) << 16)    # HIDIOCGFEATURE(1 + VND_LEN)
TICK_MS = 1.0               # firmware 18 with the frame lock: 1000 a second


def raw():
    return time.clock_gettime(time.CLOCK_MONOTONIC_RAW)


def read_clock(fd):
    """One read: a dict of wall and raw (the SET's midpoints), set_s (the
    SET's own time), rtt_s, and board (seconds) and isset; or, for an answer
    that is not the clock's, board None and what came back as 'other'."""
    buf = bytearray(1 + VND_LEN)
    buf[0], buf[1] = VND_ID, 0x6C
    w0, r0 = time.time(), raw()
    fcntl.ioctl(fd, SET, bytes(buf))
    r1, w1 = raw(), time.time()
    buf = bytearray(1 + VND_LEN)
    buf[0] = VND_ID
    fcntl.ioctl(fd, GET, buf)
    r2 = raw()
    r = {'wall': (w0 + w1) / 2, 'raw': (r0 + r1) / 2, 'set_s': r1 - r0, 'rtt_s': r2 - r0}
    a = buf[1:] if buf[0] == VND_ID else buf
    if a[0] != 0x6C:                    # say what came back instead, and go on
        r.update(board=None, other=bytes(a[:12]).hex(' '))
        return r
    secs = struct.unpack('<I', bytes(a[4:8]))[0]
    ticks = struct.unpack('<H', bytes(a[8:10]))[0]
    r.update(board=secs + ticks * TICK_MS / 1000, isset=a[10])
    return r


def drift(first, now, clock):
    """The board against one of our clocks since the first read: (s, ppm)."""
    elapsed = now[clock] - first[clock]
    d = (now['board'] - first['board']) - elapsed
    return d, d / elapsed * 1e6 if elapsed > 0 else 0.0


def line(first, r):
    w, wp = drift(first, r, 'wall')
    q, qp = drift(first, r, 'raw')
    return ('%8.0f s: wall %+.3f s %+7.1f ppm, raw %+.3f s %+7.1f ppm, rtt %.1f ms, set %.1f ms'
            % (r['raw'] - first['raw'], w, wp, q, qp, r['rtt_s'] * 1e3, r['set_s'] * 1e3))


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    period = float(sys.argv[2]) if len(sys.argv) > 2 else 60
    first = None
    with open(sys.argv[1], 'rb+', buffering=0) as f:
        while True:
            r = read_clock(f.fileno())
            if r['board'] is None:
                print('%s: not the clock\'s answer: %s' % (time.strftime('%H:%M:%S'), r['other']))
            elif not r['isset']:
                print('the clock is not set: set it with the page or digimacro-host')
            elif first is None:
                first = r
                print('start: board %.3f, offset %+.3f s, rtt %.1f ms, set %.1f ms'
                      % (r['board'], r['board'] - r['wall'], r['rtt_s'] * 1e3, r['set_s'] * 1e3))
            else:
                print(line(first, r))
            sys.stdout.flush()
            time.sleep(period)


if __name__ == '__main__':
    main()
