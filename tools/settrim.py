#!/usr/bin/env python3
"""Trim a built DigiMacro image's clock, without building it again.

  settrim.py IMAGE.hex                 what IMAGE's trim is
  settrim.py IMAGE.hex PPM OUT.hex     IMAGE with its clock trimmed by PPM,
                                       into OUT (which may be IMAGE)

PPM: the clock gains that many parts per million (negative: loses), 0 for
none, or 4 to 1000 either way. It is the computer's crystal error, as
tools/clockdrift.py measures it, with its sign turned: a board whose clock
drifts -25 ppm on that computer wants 25. The same as make CLOCK_TRIM=PPM.

IMAGE is the app (build/v2/digimacro-asm.hex, web/firmware/digimacro-v2.hex)
or core and app together (digimacro-asm-full.hex, digimacro-v2-full.hex).
The trim is four bytes at APP_CODE, just after the app's header (see
clock_poll in app/asm/digimacro.S); the app's CRC, which the core checks
before it will run the app, is stamped again over the new bytes. Firmware 19
or later only: an older app has code there. A PROBE=1 build has the bytes
but ignores them.

To put a trimmed image on a board, serve a copy of the page with it as
firmware/digimacro-v2.hex (make asmweb does the copying), or flash the full
image with a programmer.
"""
import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'app'))
from apphex import crc16, read_hex, write_hex   # noqa: E402

APP_START = 0x0F80            # core/layout.h
APP_MAGIC = APP_START + 2 * (3 + 14)
APP_CODE = APP_MAGIC + 6
FIRST_FW = 19                 # the first firmware with the trim's bytes


def image(mem):
    """the app's bytes, from its header's length, its CRC checked"""
    if bytes(mem.get(APP_MAGIC + i, 0) for i in range(2)) != b'DM':
        sys.exit('settrim: no DigiMacro app header at 0x%04X' % APP_MAGIC)
    length = mem[APP_MAGIC + 2] | mem[APP_MAGIC + 3] << 8
    ver = mem[APP_MAGIC + 4]
    data = bytes(mem.get(APP_START + i, 0xFF) for i in range(length))
    crc = mem.get(APP_START + length, -1) | mem.get(APP_START + length + 1, -1) << 8
    if crc != crc16(data):
        sys.exit('settrim: the image\'s CRC is wrong: not a finished app image')
    if ver < FIRST_FW:
        sys.exit('settrim: firmware %d has no trim (%d or later)' % (ver, FIRST_FW))
    return length, ver


def decode(b):
    count, odd, roll, pad = b
    if pad != 0 or (odd, roll) not in ((0xE8, 0xE8), (0xE7, 0xE8), (0xE9, 0xE9)):
        sys.exit('settrim: the bytes at 0x%04X are not a trim: %s'
                 % (APP_CODE, ' '.join('%02X' % x for x in b)))
    if odd == 0xE8:
        return 'none'
    n = count + 1
    ppm = 1e6 / (1000 * n - 1) if odd == 0xE7 else -1e6 / (1000 * n + 1)
    return '%+.1f ppm (one second in %d is %d ticks)' % (ppm, n, 999 if odd == 0xE7 else 1001)


def encode(ppm):
    if ppm == 0:
        return bytes([0, 0xE8, 0xE8, 0])
    a = abs(ppm)
    if a < 4 or a > 1000:
        sys.exit('settrim: 0, or 4 to 1000 ppm either way')
    n = (2000 // a + 1) // 2           # round(1000 / a), as the Makefile's
    return bytes([n - 1, 0xE7, 0xE8, 0]) if ppm > 0 else bytes([n - 1, 0xE9, 0xE9, 0])


def main(argv):
    if len(argv) not in (2, 4):
        sys.exit(__doc__)
    mem = read_hex(argv[1])
    length, ver = image(mem)
    old = bytes(mem[APP_CODE + i] for i in range(4))
    print('%s: firmware %d, trim %s' % (argv[1], ver, decode(old)))
    if len(argv) == 2:
        return
    try:
        ppm = int(argv[2])
    except ValueError:
        sys.exit('settrim: PPM is a whole number')
    new = encode(ppm)
    for i, b in enumerate(new):
        mem[APP_CODE + i] = b
    data = bytes(mem[APP_START + i] for i in range(length))
    c = crc16(data)
    mem[APP_START + length] = c & 0xFF
    mem[APP_START + length + 1] = c >> 8
    write_hex(argv[3], mem)
    print('%s: trim %s, CRC %04X' % (argv[3], decode(new), c))


if __name__ == '__main__':
    main(sys.argv)
