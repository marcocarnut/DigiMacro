#!/usr/bin/env python3
"""
{totp} end to end: the page's own code (web/table.js) makes the table, the
clock is set the two ways a host can, the simulated board types the digits,
and they are compared with Python's hmac, not with anything of ours.

    totp_e2e.py image.hex
"""
import hashlib, hmac, os, struct, subprocess, sys, tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
SIM = os.path.join(ROOT, 'build/v2/sim_diff')
CORE = os.path.join(ROOT, 'build/v2/core.elf')
SECRET = b'12345678901234567890'    # what 'GEZD GNBV ...' decodes to


def code(t):
    d = hmac.new(SECRET, struct.pack('>Q', t // 30), hashlib.sha1).digest()
    o = d[19] & 15
    return '%06d' % ((struct.unpack('>I', d[o:o + 4])[0] & 0x7FFFFFFF) % 1000000)


def keys_for(digits):
    return ''.join('<00/%02X>' % (0x27 if c == '0' else 0x1D + int(c)) for c in digits)


def main():
    img = sys.argv[1]
    em = subprocess.run(['node', os.path.join(ROOT, 'test/webtable.js'), '--emit-totp'],
                        stdout=subprocess.PIPE, universal_newlines=True, check=True).stdout
    eeprom, key = em.strip().rsplit('\n', 1)
    key = key.split('=')[1]
    ee = 'eefill FF\nee 0 ' + eeprom.strip() + '\nee 1e0 DE AD BE EF\n'
    t1, t2 = 1700000000, 1234567000
    def sc(body):
        return ee + 'boot\nrun 300\n' + body
    tests = [
        ('page path: the plain macro, clock set behind the window',
         sc('touch 1\nrun 4300\ntouch 0\nrun 300\n'
            'cmd 6c 0 4 %s\nexpect_ans 0 6C\nclearkeys\ncode .\nquiesce 1200\nexpect_keys %s\n'
            % (struct.pack('<I', t1).hex(), '<00/04><00/05>' + keys_for(code(t1)) + '<00/06>'))),
        ('host path: the sealed macro, clock set with the key',
         sc('cmd 6a 0 14 %s%s\nexpect_ans 0 6A\nclearkeys\ncode -\nquiesce 1400\nexpect_keys %s\n'
            % (key, struct.pack('<I', t2).hex(), keys_for(code(t2)) + '<00/07>'))),
    ]
    fails = 0
    for what, body in tests:
        with tempfile.NamedTemporaryFile('w', suffix='.txt', delete=False) as f:
            f.write(body)
            name = f.name
        r = subprocess.run([SIM, CORE, img, name], stdout=subprocess.PIPE,
                           stderr=subprocess.STDOUT, universal_newlines=True)
        os.unlink(name)
        ok = r.returncode == 0 and ' EXPECT FAIL' not in r.stdout
        print('%s %s' % ('ok  ' if ok else 'FAIL', what))
        if not ok:
            fails += 1
            print('\n'.join(l for l in r.stdout.splitlines() if ' EXPECT ' in l or 'KEYS' in l or 'BOOT' in l))
    print('all checks passed' if not fails else 'SOME CHECKS FAILED')
    return fails != 0


if __name__ == '__main__':
    sys.exit(main())
