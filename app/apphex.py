#!/usr/bin/env python3
"""Finish an app image, or join hex files into one.

  apphex.py app  ADDR HDR in.bin out.hex [tail.hex]
                                       stamp the app's CRC and write its hex;
                                       HDR is where the magic is (layout.h);
                                       tail.hex, if given, joins the image
                                       where its addresses say, the gap
                                       blank, and the length grows to hold it
  apphex.py join out.hex in.hex...     several hex files as one

The app's header says how long the image is (layout.h); the CRC-16 of those
bytes goes right after them, which is what the core checks before it will run
the app. The linker cannot compute a CRC, hence this.
"""
import sys

def crc16(data):
    """avr-libc's _crc16_update, from 0xFFFF: poly 0xA001, reflected."""
    crc = 0xFFFF
    for b in data:
        crc ^= b
        for _ in range(8):
            crc = (crc >> 1) ^ 0xA001 if crc & 1 else crc >> 1
    return crc


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
        elif kind == 2:
            base = (rec[4] << 8 | rec[5]) << 4
        elif kind == 4:
            base = (rec[4] << 8 | rec[5]) << 16
    return mem


def write_hex(path, mem):
    out = []
    addrs = sorted(mem)
    i = 0
    while i < len(addrs):
        start = addrs[i]
        chunk = [mem[start]]
        while (i + 1 < len(addrs) and addrs[i + 1] == start + len(chunk)
               and len(chunk) < 16):
            i += 1
            chunk.append(mem[addrs[i]])
        i += 1
        rec = bytes([len(chunk), start >> 8 & 0xFF, start & 0xFF, 0]) + bytes(chunk)
        out.append(':%s%02X' % (rec.hex().upper(), -sum(rec) & 0xFF))
    out.append(':00000001FF')
    open(path, 'w').write('\n'.join(out) + '\n')


def app(addr, at, binpath, hexpath, tailpath=None):
    addr, at = int(addr, 0), int(at, 0)
    data = bytearray(open(binpath, 'rb').read())
    if data[at:at + 2] != b'DM':
        sys.exit('apphex: no app header at the start of ' + binpath)
    length = data[at + 2] | data[at + 3] << 8
    if length != len(data):
        sys.exit('apphex: header says %d bytes, image is %d' % (length, len(data)))
    if tailpath:
        # The uninstaller's Micronucleus: part of the image, so the CRC
        # covers it and the core will not run the app without it.
        tail = read_hex(tailpath)
        lo, hi = min(tail) - addr, max(tail) + 1 - addr
        if lo < len(data):
            sys.exit('apphex: %s starts at 0x%04X, inside the app' % (tailpath, lo + addr))
        data += b'\xff' * (hi - len(data))
        for a, b in tail.items():
            data[a - addr] = b
        data[at + 2:at + 4] = bytes([len(data) & 0xFF, len(data) >> 8])
    c = crc16(data)
    data += bytes([c & 0xFF, c >> 8])
    write_hex(hexpath, {addr + i: b for i, b in enumerate(data)})
    print('%s: %d bytes at 0x%04X, CRC %04X' % (hexpath, len(data), addr, c))


def join(out, *ins):
    mem = {}
    for p in ins:
        for a, b in read_hex(p).items():
            if a in mem:
                sys.exit('apphex: %s overlaps at 0x%04X' % (p, a))
            mem[a] = b
    write_hex(out, mem)


if __name__ == '__main__':
    if len(sys.argv) in (6, 7) and sys.argv[1] == 'app':
        app(*sys.argv[2:])
    elif len(sys.argv) >= 4 and sys.argv[1] == 'join':
        join(*sys.argv[2:])
    else:
        sys.exit(__doc__)
