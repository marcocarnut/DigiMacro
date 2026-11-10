#!/usr/bin/env python3
"""
The differential test: every scenario on two firmware images, and their traces
compared stream by stream, timestamps within a tolerance.

    run.py REF.hex [OTHER.hex] [-j N] [-s subsys,...] [-o build/diff] [-t tol]

Called with one image it only runs it and checks its expect_ lines. Two images
that disagree anywhere fail, and the first differences are printed.

Streams (ordering is only compared within a stream, so an LED edge and a USB
report landing in the same millisecond cannot fail a scenario):
    LED, reports (KBD/PTR/REPORT/POLLERR), answers (ANS), EEPROM (EE),
    memory (RAM/PEEK/MSBYTES), everything else (BOOT, KEYS, EXPECT...).
LED edges and reports are timed by the gap from the previous event of their
stream (the first of each by its absolute time), within --tol ms (default 20:
the host polls every 10). The rest of the
timestamps depend on when the script got there, and are not compared.
"""
import argparse, json, os, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
STREAM = {'LED': 'led', 'KBD': 'rep', 'PTR': 'rep', 'REPORT': 'rep', 'POLLERR': 'rep',
          'ANS': 'ans', 'EE': 'ee', 'RAM': 'ram', 'PEEK': 'ram', 'MSBYTES': 'clock'}
EXACT_T = {'EE', 'RAM', 'PEEK', 'ANS', 'KEYS', 'EXPECT', 'NOTE', 'LEDSTATE', 'REBOOT',
           'BOOT', 'END'}


def parse(text):
    out = []
    for line in text.splitlines():
        if line.startswith('@ '):
            parts = line[2:].split(' ', 2)
            kind = parts[1]
            out.append((float(parts[0]), kind, parts[2] if len(parts) > 2 else ''))
    return out


def streams(trace):
    s = {}
    for t, k, v in trace:
        s.setdefault(STREAM.get(k, 'misc:' + k), []).append((t, k, v))
    return s


def clock_value(v):
    p = v.split()
    return int(p[0], 16) | int(p[1], 16) << 8


def mask_version(e):
    t, k, v = e
    p = v.split()
    if len(p) > 15 and p[1] == '40':    # ANS 36 40 ...: byte [13] is the app's
        p[14] = 'xx'
    return (t, k, ' '.join(p))


def compare(a, b, tol):
    """Events must match in each stream. Only the LED and the reports are
    timed, and by the gap from the event before, so that one late poll does
    not shift everything after it. The app's clock may differ by a few ms."""
    diffs = []
    A, B = streams(a), streams(b)
    for name in sorted(set(A) | set(B)):
        x, y = A.get(name, []), B.get(name, [])
        if name == 'ans':               # INFO's firmware version may differ
            x, y = [mask_version(e) for e in x], [mask_version(e) for e in y]
        if len(x) != len(y):
            diffs.append('%s: %d lines against %d' % (name, len(x), len(y)))
        for i, (p, q) in enumerate(zip(x, y)):
            if name == 'clock':
                if abs(clock_value(p[2]) - clock_value(q[2])) > 4:
                    diffs.append('clock[%d]: %s / %s' % (i, p[2], q[2]))
                continue
            if p[1:] != q[1:]:
                diffs.append('%s[%d]: %s %s / %s %s (t=%.2f)' % (name, i, p[1], p[2][:80], q[1], q[2][:80], p[0]))
            elif name in ('led', 'rep'):
                # the first of a stream by its absolute time: both boot alike
                g = p[0] - q[0] if not i else (p[0] - x[i - 1][0]) - (q[0] - y[i - 1][0])
                if abs(g) > tol:
                    diffs.append('%s[%d]: %s gap differs by %.2f ms (t=%.2f)' % (name, i, p[1], g, p[0]))
            if len(diffs) > 12:
                return diffs
    return diffs


def run1(sim, core, hexf, scen, webtable):
    text = open(scen).read()
    tmp = None
    if 'webtable' in text.split('\n')[0]:
        # the page's own table: one EEPROM line from `webtable.js --emit`
        lines = text.split('\n')
        lines[0] = open(webtable).read().strip()
        tmp = scen + '.' + os.path.basename(hexf)
        with open(tmp, 'w') as f:
            f.write('\n'.join(lines))
        scen = tmp
    r = subprocess.run([sim, core, hexf, scen], stdout=subprocess.PIPE, stderr=subprocess.STDOUT,
                       universal_newlines=True, timeout=1800)
    if tmp:
        os.unlink(tmp)
    return r.stdout, r.returncode


def main():
    p = argparse.ArgumentParser()
    p.add_argument('images', nargs='+')
    p.add_argument('-j', type=int, default=os.cpu_count())
    p.add_argument('-s', default='')
    p.add_argument('-n', default='', help='only these scenario names, comma separated')
    p.add_argument('-o', default=os.path.join(ROOT, 'build/diff'))
    p.add_argument('-t', type=float, default=20)
    p.add_argument('--sim', default=os.path.join(ROOT, 'build/v2/sim_diff'))
    p.add_argument('--core', default=os.path.join(ROOT, 'build/v2/core.elf'))
    p.add_argument('--slow', action='store_true', help='include the slow scenarios')
    p.add_argument('--stop', action='store_true', help='stop at the first failing scenario')
    p.add_argument('--keep', action='store_true', help='keep the traces')
    args = p.parse_args()

    os.makedirs(args.o, exist_ok=True)
    scen = os.path.join(args.o, 'scen')
    subprocess.check_call([sys.executable, os.path.join(HERE, 'scenarios.py'), scen],
                          stdout=subprocess.DEVNULL)
    index = json.load(open(os.path.join(scen, 'index.json')))
    wanted = sorted(index)
    if args.s:
        subs = set(args.s.split(','))
        wanted = [n for n in wanted if subs & set(index[n]['subs'])]
    if len(args.images) > 1:            # features the reference does not have
        wanted = [n for n in wanted if 'asmonly' not in index[n]['subs']]
    if not args.slow and not args.n:
        wanted = [n for n in wanted if 'slow' not in index[n]['subs']]
    if args.n:
        names = set(args.n.split(','))
        wanted = [n for n in wanted if n in names]

    webtable = os.path.join(args.o, 'webtable.txt')
    if not os.path.exists(webtable) or os.path.getmtime(webtable) < os.path.getmtime(
            os.path.join(ROOT, 'web/table.js')):
        em = subprocess.run(['node', os.path.join(ROOT, 'test/webtable.js'), '--emit'],
                            stdout=subprocess.PIPE, universal_newlines=True, check=True)
        data = em.stdout.split()
        with open(webtable, 'w') as f:
            f.write('eefill FF\nee 1e0 DE AD BE EF\nee 0 ' + ' '.join(data) + '\n')

    def job(name):
        res = []
        for img in args.images:
            res.append(run1(args.sim, args.core, img, os.path.join(scen, name + '.txt'), webtable))
        return name, res

    failed, n = 0, 0
    peaks = [(0, '') for _ in args.images]
    with ThreadPoolExecutor(args.j) as ex:
        for name, res in ex.map(job, wanted):
            n += 1
            problems = []
            if args.keep:
                for img, (out, code) in zip(args.images, res):
                    d = os.path.join(args.o, 'traces', os.path.basename(img))
                    os.makedirs(d, exist_ok=True)
                    with open(os.path.join(d, name + '.log'), 'w') as f:
                        f.write(out)
            for i, (out, code) in enumerate(res):
                for m in re.finditer(r'^STACK low \w+ end \w+ peak (\d+)$', out, re.M):
                    if int(m.group(1)) > peaks[i][0]:
                        peaks[i] = (int(m.group(1)), name)
            ref = parse(res[0][0])
            if res[0][1]:
                problems.append('%s: exits %d (an expect_ failed?)' % (args.images[0], res[0][1]))
            problems += ['%s: %s %s' % (args.images[0], k, v) for t, k, v in ref
                         if k == 'EXPECT' and v.startswith('FAIL')]
            problems += ['%s: board: %s' % (args.images[0], v) for t, k, v in ref
                         if k in ('BOOT', 'REBOOT') and 'fail=0' not in v]
            if len(args.images) > 1:
                other = parse(res[1][0])
                if res[1][1]:
                    problems.append('%s: exits %d' % (args.images[1], res[1][1]))
                problems += compare(ref, other, args.t)
            if problems:
                failed += 1
                print('FAIL %s' % name)
                for q in problems[:14]:
                    print('   ', q)
                sys.stdout.flush()
                if args.stop:
                    break
            elif not args.images[1:]:
                pass
    for img, (pk, where) in zip(args.images, peaks):
        print('%s: peak stack %d bytes (in %s)' % (os.path.basename(img), pk, where))
    print('%d of %d scenarios %s' % (n - failed, n, 'pass' if not failed else 'pass, %d FAIL' % failed))
    return 1 if failed else 0


if __name__ == '__main__':
    sys.exit(main())
