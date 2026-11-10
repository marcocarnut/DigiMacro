#!/usr/bin/env python3
"""
What an app may rely on about the core beyond the C ABI: the contract in
core/layout.h, checked against the core being built, so a new core that
breaks it fails its own build. The assembly app (app/asm/digimacro.S) is
what holds it to these:

  - r2-r11 are never touched: the app keeps its clock and state there.
  - neither are the spare I/O registers the app keeps state in, and GPIOR1.
  - main() holds nothing in r2-r17, Y across APP_SETUP and APP_LOOP.
  - hidPassSet, hidUnlocked, hidReport, hidLock, hidReady, hidAwake change
    only r24, r25 and the flags.
  - pageBuf is at 0x62, 64 bytes, and GPIOR0 bit 5 says it is half received.

    checkcore.py core.elf [objdump] [nm]
"""
import re, subprocess, sys

# data-space addresses of the app's registers, as lds/sts would name them
SPARE = {'OCR0A': 0x49, 'OCR0B': 0x48, 'OCR1B': 0x4B, 'USIDR': 0x2F, 'GPIOR1': 0x32}
ACCESSORS = ('hidPassSet', 'hidUnlocked', 'hidReport', 'hidLock', 'hidReady', 'hidAwake',
             'eeprom_read_byte')


def main():
    elf = sys.argv[1]
    objdump = sys.argv[2] if len(sys.argv) > 2 else 'avr-objdump'
    nm = sys.argv[3] if len(sys.argv) > 3 else 'avr-nm'
    syms = subprocess.check_output([nm, '-S', elf], universal_newlines=True)
    dis = subprocess.check_output([objdump, '-d', elf], universal_newlines=True)
    fail = []

    start = int(re.search(r'^([0-9a-f]+) \w __ctors_end$',
                          subprocess.check_output([nm, elf], universal_newlines=True), re.M).group(1), 16)
    funcs, cur = {}, None
    code = []
    for line in dis.splitlines():
        m = re.match(r'^([0-9a-f]+) <(\S+)>:', line)
        if m:
            cur = m.group(2)
            funcs.setdefault(cur, [])
            continue
        m = re.match(r'\s*([0-9a-f]+):\t(?:[0-9a-f]{2} ){2,4}\s*\t(.*)', line)
        if not m or int(m.group(1), 16) < start:
            continue
        text = m.group(2).split(';')[0].strip()
        code.append((int(m.group(1), 16), text))
        if cur:
            funcs[cur].append((int(m.group(1), 16), text))

    # 1. the registers
    for a, t in code:
        if re.search(r'\br([2-9]|1[01])\b', t):
            fail.append('uses r2-r11: %04x %s' % (a, t))

    # 2. the spare I/O registers, as I/O addresses and as data addresses
    io = {k: v - 0x20 for k, v in SPARE.items()}
    for a, t in code:
        m = re.match(r'(in|out|sbi|cbi|sbis|sbic)\s+(.*)', t)
        if m and any(int(x, 16) in io.values() for x in re.findall(r'0x([0-9a-f]+)', m.group(2))):
            fail.append('uses a spare I/O register: %04x %s' % (a, t))
        m = re.match(r'(lds|sts)\s+(.*)', t)
        if m and any(int(x, 16) & 0xFFFF in SPARE.values()
                     for x in re.findall(r'0x([0-9a-f]+)', m.group(2))):
            fail.append('uses a spare I/O register: %04x %s' % (a, t))

    # 3. main: nothing in the callee-saved registers after the first call out
    main_ = funcs.get('main', [])
    icalls = [a for a, t in main_ if t.startswith('icall')]
    if not icalls:
        fail.append('main has no icall: not the core this check knows')
    else:
        for a, t in main_:
            if a >= min(icalls) and re.search(r'\br([2-9]|1[0-7]|28|29)\b', t):
                fail.append('main holds a register across the app: %04x %s' % (a, t))

    # 4. the accessors the app calls without saving what it should have to
    for name in ACCESSORS:
        if name not in funcs:
            fail.append('no %s in the core' % name)
            continue
        allowed = re.compile(r'\br(0?1|24|25)\b')
        for a, t in funcs[name]:
            for reg in re.findall(r'\br(\d+)\b', t):
                if reg not in ('1', '24', '25'):
                    fail.append('%s changes r%s: %04x %s' % (name, reg, a, t))
            m = re.match(r'rcall\s+\S+\s*(?:;.*)?$', t)
            # calls within the accessors: only each other
    # 5. the page buffer, as {totp} borrows it
    m = re.search(r'^([0-9a-f]+) ([0-9a-f]+) \w pageBuf$', syms, re.M)
    if not m or int(m.group(1), 16) != 0x800062 or int(m.group(2), 16) != 64:
        fail.append('pageBuf is not at 0x62, 64 bytes: %s' % (m.group(0) if m else 'absent'))
    if not any(re.match(r'sbi\s+0x11, 5', t) for _, t in code):
        fail.append("GPIOR0 bit 5 is no longer takePage's pageHalf")

    if fail:
        print('checkcore: the core breaks the contract in layout.h:')
        print('\n'.join(fail[:20]))
        return 1
    print('checkcore: contract holds (r2-r11 and the spare I/O untouched, accessors clean, pageBuf at 0x62)')
    return 0


if __name__ == '__main__':
    sys.exit(main())
