#!/usr/bin/env python3
"""
Watch the frame lock on a board running the frames-fix test build: what the
analog comparator sees on D-, how the lock uses it, and the clock against
this computer's. (The frames-probe build answers in another layout: use the
probe.py in its own worktree for that one.)

    probe.py /dev/hidrawN [seconds]

Read-only, like clockdrift.py: it only sends the 0x6C read, which needs no
gate and sets nothing. Runs for the seconds given (default: until Ctrl-C),
then prints a summary. One line per second the board completed:

    seq  edges/s  kept/s  tries  period  RC ppm  miss  ocr  offset s  drift ppm

  seq      the board's own second number (mod 256), counted in its ticks
  edges/s  comparator interrupts: one per keep-alive, plus some per packet
  kept/s   ticks whose keep-alive came where it was due and was used:
           1000 while locked; fewer only when keep-alives went unseen
  tries    edges tried as the keep-alive while the lock was lost: 0 locked
  period   the lock's tick length, OCR1C in counts (fractional), when read
  RC ppm   the chip's clock error that period means
  miss     ticks since the last keep-alive used, when read (32: lost)
  ocr      OCR1C when read: the period, plus this tick's correction
  offset   the board's clock minus this computer's, if the clock is set
  drift    the offset's change since the first line, in ppm of the time

The answer it reads: clockdrift.py's eleven bytes (0x6C, three, the seconds,
the ticks into the second, set or not), then the probe's: edges and kept
(16 bits each) and tries, a spare byte, the second's number -- all for the
last whole second -- then the period (8.8), the miss count and OCR1C now.
"""
import fcntl, struct, sys, time

VND_ID, VND_LEN = 2, 36
SET = 0xC0004806 | ((1 + VND_LEN) << 16)    # HIDIOCSFEATURE(1 + VND_LEN)
GET = 0xC0004807 | ((1 + VND_LEN) << 16)    # HIDIOCGFEATURE(1 + VND_LEN)
TICK_MS = 1.0               # locked, a tick is one of the host's frames


def ask_clock(fd):
    """The 0x6C read, as clockdrift.py sends it: (time asked, answer)."""
    buf = bytearray(1 + VND_LEN)
    buf[0], buf[1] = VND_ID, 0x6C
    fcntl.ioctl(fd, SET, bytes(buf))
    buf = bytearray(1 + VND_LEN)
    buf[0] = VND_ID
    now = time.time()
    fcntl.ioctl(fd, GET, buf)
    return now, bytes(buf[1:] if buf[0] == VND_ID else buf)


def parse(a):
    """The answer (report ID stripped) as a dict, or None if it is not the
    clock's."""
    if len(a) < 22 or a[0] != 0x6C:
        return None
    n, kept = struct.unpack('<HH', a[11:15])
    period = struct.unpack('<H', a[18:20])[0] / 256
    return {
        'secs': struct.unpack('<I', a[4:8])[0],
        'ticks': struct.unpack('<H', a[8:10])[0],
        'set': a[10] != 0,
        'edges': n, 'kept': kept, 'tries': a[15], 'seq': a[17],
        'period': period, 'miss': a[20], 'ocr': a[21],
    }


def rc_ppm(period):
    """The RC's error that a locked period means: a frame is 16500/128
    counts of Timer1 at 16.5 MHz, and the tick is OCR1C + 1 of them."""
    return ((period + 1) / (16500 / 128) - 1) * 1e6


class Probe:
    """Turns answers into lines: one per new board second, and a summary."""

    def __init__(self):
        self.seq = None
        self.first = None
        self.seconds = self.edges = self.kept = self.tries = self.missed = 0
        self.lo, self.hi, self.maxmiss = 999.0, 0.0, 0
        self.offset = None

    def feed(self, now, a):
        p = parse(a)
        if p is None:
            return '%s: not the clock\'s answer: %s' % (
                time.strftime('%H:%M:%S'), a[:12].hex(' '))
        if p['seq'] == self.seq:
            return None
        skipped = (p['seq'] - self.seq - 1) & 0xFF if self.seq is not None else 0
        first = self.seq is None
        self.seq = p['seq']
        if first:                       # the second under way when we came
            return None
        self.missed += skipped
        self.seconds += 1
        self.edges += p['edges']
        self.kept += p['kept']
        self.tries += p['tries']
        self.lo, self.hi = min(self.lo, p['period']), max(self.hi, p['period'])
        self.maxmiss = max(self.maxmiss, p['miss'])
        if p['set']:
            board = p['secs'] + p['ticks'] * TICK_MS / 1000
            self.offset = off = board - now
            if self.first is None:
                self.first = (now, off)
            el = now - self.first[0]
            ppm = (off - self.first[1]) / el * 1e6 if el > 0 else 0.0
            clock = '%+10.3f %+8.1f' % (off, ppm)
        else:
            clock = '  clock not set'
        return '%3d %6d %6d %5d %7.2f %+7.0f %4d %4d %s%s' % (
            p['seq'], p['edges'], p['kept'], p['tries'], p['period'],
            rc_ppm(p['period']), p['miss'], p['ocr'], clock,
            '  (%d second(s) not seen)' % skipped if skipped else '')

    def summary(self, now):
        if not self.seconds:
            return 'no whole second seen: is this the frames-fix build?'
        s = ('%d seconds: %.1f edges/s, %.1f kept/s, %d tries, period %.2f-%.2f '
             '(RC %+.0f..%+.0f ppm), miss up to %d' % (
                 self.seconds, self.edges / self.seconds, self.kept / self.seconds,
                 self.tries, self.lo, self.hi, rc_ppm(self.lo), rc_ppm(self.hi),
                 self.maxmiss))
        if self.missed:
            s += ', %d second(s) not seen' % self.missed
        if self.first is not None and now > self.first[0]:
            el = now - self.first[0]
            d = self.offset - self.first[1]
            s += '\nclock: offset %+.3f s, drift %+.3f s over %.0f s, %+.1f ppm' % (
                self.offset, d, el, d / el * 1e6)
        return s


HEAD = 'seq edges/s kept/s tries  period  RC ppm miss  ocr   offset s drift ppm'


def main():
    if len(sys.argv) < 2:
        raise SystemExit(__doc__)
    duration = float(sys.argv[2]) if len(sys.argv) > 2 else None
    probe = Probe()
    print(HEAD)
    start = time.time()
    now = start
    with open(sys.argv[1], 'rb+', buffering=0) as f:
        try:
            while duration is None or now - start < duration:
                now, a = ask_clock(f.fileno())
                line = probe.feed(now, a)
                if line:
                    print(line)
                    sys.stdout.flush()
                time.sleep(0.5)        # under a second: no second unseen
        except KeyboardInterrupt:
            pass
    print(probe.summary(now))


if __name__ == '__main__':
    main()
