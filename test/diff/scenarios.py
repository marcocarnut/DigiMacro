#!/usr/bin/env python3
"""
The differential test's scenarios: what the C-bound tests exercise, as stimuli
for test/sim/difftest.c on the simulated board -- the real EEPROM images, the
pad, the remote, USB commands, time passing.

Each scenario is a script (build/diff/scen/<name>.txt), and names the sections
of the C-bound tests it stands for, so the inventory can be checked for gaps:

    scenarios.py DIR          write the scripts into DIR, and DIR/index.json
    scenarios.py --inventory  print the mapping, and any section nothing covers

The expect_* lines hold the C image to the same outcomes test.cpp asks of the
C code: without them two images that both did nothing would agree.
"""
import json, os, struct, sys

# ---- the table format, from DigiMacro.ino ------------------------------

ESC_PAUSE, ESC_MOD, ESC_REPEAT = 1, 2, 3
ESC_JUMP, ESC_MARK, ESC_HOST, ESC_IR = 0x81, 0x82, 0x83, 0x80
HOST_SILENT, HOST_LOUD = 1, 2
SECRET_QUIET, SECRET_LOUD, JUMP_CLEAR, JUMP_ALONE = 0xFE, 0xFF, 0xF7, 0xF6
KEY_SHIFT = 0x80

LOCK_SOFT, LOCK_HARD, LOCK_WAIT, LOCK_MISSES, LOCK_SOFTS, LOCK_CLEARED = range(0x1EC, 0x1F2)
KEY_WAIT, AUTO_ADDR, PASS_ADDR, CHECK_AT, SERIAL = 0x1F2, 0x1F3, 0x1F4, 0x1E4, 0x1E0

INFO, RESTART, READ, WRITE, UNLOCK, SETPASS = 0x40, 0x42, 0x60, 0x61, 0x62, 0x63
UPDATE, LEARN, KEYAUTH, PROVE, REFUSED = 0x68, 0x69, 0x6A, 0x6B, 0xEE
ST_OPEN, ST_SOFT, ST_HARD, ST_KEY = 0x01, 0x08, 0x10, 0x80


def K(c):
    return ord(c) - ord('a') + 4


def KS(c):
    return K(c) | KEY_SHIFT


K_LEFT = 0x50


def bits(code):
    n = 0
    for s in code:
        n = n << 1 | (s == '-')
    return n


def HDR(code):
    return bits(code) << 3 | (len(code) - 1)


def jump_to(code):
    return [ESC_JUMP, HDR(code) + 1]


def IRTAG(a, c):
    return [ESC_IR, a | 0x80, c | 0x80, 0x80 | a >> 7 | (c >> 7) << 1]


def xtea(v, key):
    """XTEA, 32 cycles, as app/xtea.S: 8 and 16 bytes, little-endian words"""
    v0, v1 = struct.unpack('<2I', bytes(v))
    k = struct.unpack('<4I', bytes(key))
    s = 0
    for _ in range(32):
        v0 = (v0 + ((((v1 << 4) ^ (v1 >> 5)) + v1) ^ (s + k[s & 3]))) & 0xFFFFFFFF
        s = (s + 0x9E3779B9) & 0xFFFFFFFF
        v1 = (v1 + (((v0 << 4 ^ v0 >> 5) + v0) ^ (s + k[s >> 11 & 3]))) & 0xFFFFFFFF
    return list(struct.pack('<2I', v0, v1))


SKEY = bytes(range(1, 17))
SBAD = bytes(range(1, 16)) + b'\x11'
PASS = b'secret'


def is_esc(b):
    return (b & 0x7F) < 4


def walk_safe(b):
    if 0 in b:
        return False
    i = 0
    while i < len(b):
        if b[i] == ESC_IR:
            return False
        i += 2 if is_esc(b[i]) and b[i] != ESC_MARK else 1
    return i == len(b)


def sealed(plain, key):
    """nonce + ciphertext, walk-safe, as web/table.js seals it"""
    p = list(plain) + [0]
    nonce = 0x01010101
    while True:
        s = list(struct.pack('<I', nonce & 0xFFFFFFFF))
        out = list(s)
        for n, b in enumerate(p):
            blk = xtea(s + [n, 0, 0, 0], key)
            out.append(b ^ blk[0])
        if walk_safe(out):
            return out
        nonce += 1


def check_value(key):
    return xtea([0] * 8, key)


# ---- a scenario --------------------------------------------------------

ALL = []


class S:
    def __init__(self, name, src, subs):
        self.name, self.src, self.subs = name, src, subs
        self.l = []
        ALL.append(self)

    def w(self, line):
        self.l.append(line)

    def ee(self, addr, data):
        self.w('ee %x %s' % (addr, ' '.join('%02X' % b for b in data)))

    def blank(self):
        self.w('eefill FF')
        self.w('ee 1e0 DE AD BE EF')

    def table(self, *entries):
        """(code, bytes) entries, tightly packed, then the end of table"""
        self.blank()
        t = []
        for code, body in entries:
            t += [HDR(code)] + list(body) + [0]
        t.append(0xFF)
        self.ee(0, t)
        return t

    def raw(self, b):
        self.blank()
        self.ee(0, b)

    def boot(self, args=''):
        self.w('boot ' + args)
        self.w('run 300')

    def play(self, code, keys=None, wait=1000):
        self.w('clearkeys')
        self.w('code ' + code)
        self.w('quiesce %d %d' % (wait, max(60000, wait + 10000)))
        if keys is not None:
            self.w('expect_keys ' + (keys or '-'))
        else:
            self.w('keys')

    def hold(self):
        self.w('touch 1')
        self.w('run 4300')
        self.w('touch 0')
        self.w('run 300')

    def press(self, a, c, keys=None, gap=None):
        self.w('ir %x %x %x %x' % (a, ~a & 0xFF, c, ~c & 0xFF))
        self.w('run 300')
        self.w('quiesce 300')
        if keys is not None:
            self.w('expect_keys ' + (keys or '-'))
        else:
            self.w('keys')

    def cmd(self, f, addr=0, n=0, data=b''):
        self.w('cmd %x %x %x %s' % (f, addr, n, bytes(data).hex()))

    def lockout(self, soft, hard, wait):
        self.ee(LOCK_SOFT, [soft, hard, wait])

    def password(self, p=PASS):
        self.ee(PASS_ADDR, list(p) + [0] * (12 - len(p)))

    def eekeys(self, key=SKEY):
        self.ee(CHECK_AT, check_value(key))

    def end(self):
        self.w('eedump')
        self.w('led')
        self.w('keys')
        self.w('stack')


def alls(prefix, src):
    pass


# ---- the macro language: test.cpp 1-13, 16-18 ----------------------------

s = S('t01_virgin', '1', ['tap', 'playback'])
s.blank()
s.boot()
s.play('.', '')
s.play('-----', '')
s.end()

s = S('t02_length_is_part_of_code', '2 3', ['tap', 'playback'])
s.table(('.', [K('a')]), ('..', [K('b')]), ('-', [K('c')]), ('..-', [K('d')]))
s.boot()
s.play('.', '<00/04>')
s.play('..', '<00/05>')
s.play('-', '<00/06>')
s.play('..-', '<00/07>')
s.play('-.', '')
s.end()

s = S('t04_shift', '4', ['playback'])
s.table(('.', [K('h'), KS('i'), K('j')]))
s.boot()
s.play('.', '<00/0B><02/0C><00/0D>')
s.end()

s = S('t05_mod', '5 5b', ['playback'])
s.table(('.', [ESC_MOD, 1, K('c'), K('v')]), ('..', [ESC_MOD, 1, KS('t')]))
s.boot()
s.play('.', '<01/06><00/19>')
s.play('..', '<03/17>')
s.end()

s = S('t06_repeat', '6', ['playback', 'repeats'])
s.table(('.', [K_LEFT, ESC_REPEAT, 3]), ('..', [ESC_MOD, 1, KS('a'), ESC_REPEAT, 1]))
s.boot()
s.play('.', '<00/50><00/50><00/50><00/50>')
s.play('..', '<03/04><03/04>')
s.end()

s = S('t06b_gui_alone', '6b', ['playback', 'gui'])
s.table(('.', [K('a'), ESC_MOD, 8, ESC_JUMP, JUMP_ALONE, K('b')]),
        ('..', [ESC_MOD, 3, ESC_JUMP, JUMP_ALONE, ESC_REPEAT, 1, K('c')]))
s.boot()
s.play('.', '<00/04><08/00><00/05>')
s.play('..', '<03/00><03/00><00/06>')
s.end()

s = S('t07_pause', '7', ['playback'])
s.table(('.', [K('a'), ESC_PAUSE, 5, K('b')]))
s.boot()
s.play('.', '<00/04><00/05>', wait=1500)
s.end()

s = S('t08_zero_arg', '8', ['playback'])
s.raw([HDR('.'), K('a'), ESC_PAUSE, 0, K('b'), 0, 0xFF])
s.boot()
s.play('.', '<00/04>')
s.end()

s = S('t09_ir_tag', '9', ['playback', 'ir'])
s.table(('.', [K('a'), ESC_IR, 0x81, 0xC5, 0x82, K('b')]))
s.boot()
s.play('.', '<00/04><00/05>')
s.end()

s = S('t09_ir_tag_short', '9', ['playback', 'ir'])
s.table(('.', [K('a'), ESC_IR, 5, K('b')]), ('-', [K('c')]))
s.boot()
s.play('.', '<00/04>')
s.end()

s = S('t09b_pointer_escapes', '9b', ['mouse', 'playback'])
s.table(('.', [K('a'), ESC_HOST, 0xC0, ESC_HOST, 0x60, ESC_HOST, 0x31, K('b')]))
s.boot()
s.play('.', None, wait=2000)
s.end()

s = S('t09b_host_stops', '9b', ['playback'])
s.table(('.', [K('a'), ESC_HOST, HOST_SILENT, K('b')]))
s.boot()
s.play('.', '<00/04>')
s.end()

s = S('t09b_scale_escapes', '9b', ['mouse', 'playback'])
s.table(('.', [K('a'), ESC_HOST, 3, ESC_HOST, 0x1F, K('b')]))
s.boot()
s.play('.', '<00/04><00/05>')
s.end()

s = S('t10_walk_bounded', '10', ['playback'])
t = []
for a in range(0, 480 - 2, 3):
    t += [HDR('-'), K('z'), 0]
s.blank()
s.ee(0, t)
s.ee(0x1DE, [HDR('-'), K('z')])
s.ee(PASS_ADDR, list(b'hunter2\0') + [0xFF] * 4)
s.lockout(0xFF, 0xFF, 0xFF)
s.boot()
s.play('...', '')
s.end()

s = S('t11_play_bounded', '11', ['playback'])
s.blank()
s.ee(0, [HDR('.')] + [K('z')] * 479)
s.ee(480, [K('q')] * 32)
s.ee(LOCK_SOFT, [0xFF])
s.boot()
s.play('.', '<00/1D>' * 479, wait=2000)
s.w('expect_keys -')
s.end()

s = S('t11b_escape_last_byte', '11b', ['playback'])
s.blank()
s.ee(0, [HDR('.')] + [K('a')] * 478 + [ESC_REPEAT])
s.ee(480, [5])
s.boot()
s.play('.', '<00/04>' * 478, wait=2000)
s.end()

s = S('t12_too_many_symbols', '12', ['tap'])
s.table(('....', [K('a')]))
s.boot()
s.play('....', '<00/04>')
s.play('......', '')
s.end()

s = S('t12b_five_symbols', '12', ['tap'])
s.table(('.....', [K('a')]), ('-----', [K('b')]), ('.-.-.', [K('c')]))
s.boot()
s.play('.....', '<00/04>', wait=1100)
s.play('-----', '<00/05>', wait=1100)
s.play('.-.-.', '<00/06>', wait=1100)
s.end()

s = S('t13_tapping', '13', ['tap'])
s.table(('.-.', [K('x')]))
s.boot()
s.play('.-.', '<00/1B>')
s.w('ram code_len 1')
s.end()

s = S('t13b_dropout', '13b', ['tap'])
s.table(('.', [K('q')]))
s.boot()
s.w('touch 1')
s.w('run 60')
s.w('touch 0')
s.w('run 5')             # one reading or so of nothing
s.w('touch 1')
s.w('run 60')
s.w('touch 0')
s.w('quiesce 1100')
s.w('expect_keys <00/14>')
s.w('ram code_len 1')
s.end()

s = S('t13c_dot_dash_boundary', '13', ['tap'])
s.table(('.', [K('a')]), ('-', [K('b')]))
s.boot()
for ms in (230, 240, 245, 250, 255, 260, 270):
    s.w('note press %d' % ms)
    s.w('touch 1')
    s.w('run %d' % ms)
    s.w('touch 0')
    s.w('quiesce 1100')
    s.w('keys')
s.end()

s = S('t13d_end_of_code_timing', '13', ['tap'])
s.table(('.', [K('a')]), ('..', [K('b')]))
s.boot()
for gap in (600, 680, 720, 800):
    s.w('note gap %d' % gap)
    s.w('touch 1')
    s.w('run 120')
    s.w('touch 0')
    s.w('run %d' % gap)
    s.w('touch 1')
    s.w('run 120')
    s.w('touch 0')
    s.w('quiesce 1200')
    s.w('keys')
s.end()

s = S('t14_hold', '14 14b', ['tap', 'window'])
s.table(('.', [K('q')]))
s.boot()
s.cmd(INFO)
s.w('expect_ansbits 7 1 0')
s.hold()
s.cmd(INFO)
s.w('expect_ansbits 7 1 1')
s.w('keys')
s.play('.', '<00/14>')
s.cmd(INFO)
s.w('expect_ansbits 7 1 1')
s.end()

s = S('t14c_hold_almost', '14', ['tap', 'window'])
s.blank()
s.boot()
s.w('touch 1')
s.w('run 3900')
s.w('touch 0')
s.w('run 300')
s.cmd(INFO)
s.w('expect_ansbits 7 1 0')
s.end()

s = S('t15_window_closes', '15', ['window'])
s.blank()
s.boot()
s.hold()
s.w('run 25000')
s.cmd(INFO)
s.w('expect_ansbits 7 1 1')
s.w('run 6000')
s.cmd(INFO)
s.w('expect_ansbits 7 1 0')
s.end()

s = S('t16_groups', '16', ['playback', 'repeats'])
s.table(('.', [K('a'), K('b'), ESC_REPEAT, 2]), ('..', [ESC_MARK, K('a'), K('b'), ESC_REPEAT, 2]))
s.boot()
s.play('.', '<00/04><00/05><00/05><00/05>')
s.play('..', '<00/04><00/05><00/04><00/05><00/04><00/05>')
s.end()

s = S('t16b_group_carries_on', '16b', ['repeats'])
s.table(('.', [ESC_MARK, K('a'), ESC_REPEAT, 1, K('z')]))
s.boot()
s.play('.', '<00/04><00/04><00/1D>')
s.end()

s = S('t16c_mark_harmless', '16c', ['repeats'])
s.table(('.', [ESC_MARK, K('a'), K('b')]))
s.boot()
s.play('.', '<00/04><00/05>')
s.end()

s = S('t16d_group_pause_inside', '16', ['repeats', 'playback'])
s.table(('.', [ESC_MARK, K('a'), ESC_PAUSE, 2, K('b'), ESC_REPEAT, 2]))
s.boot()
s.play('.', '<00/04><00/05>' * 3, wait=2000)
s.end()

s = S('t17_power_on', '17', ['autoexec'])
s.table(('.', [K('a')]), ('-.', [K('b')]))
s.ee(AUTO_ADDR, [HDR('-.')])
s.boot()
s.w('quiesce 900')
s.w('expect_keys <00/05>')
s.end()

s = S('t17a_settle', '17a', ['autoexec'])
s.table(('.', [K('a')]))
s.ee(AUTO_ADDR, [HDR('.')])
s.w('boot')
s.w('run 200')
s.w('expect_keys -')
s.w('quiesce 800')
s.w('expect_keys <00/04>')
s.end()

s = S('t17b_never_says_hello', '17b2', ['autoexec'])
s.table(('.', [K('a')]))
s.ee(AUTO_ADDR, [HDR('.')])
s.w('boot nohello')
s.w('run 400')
s.w('expect_keys -')
s.w('run 1400')
s.w('quiesce 900')
s.w('expect_keys <00/04>')
s.end()

s = S('t17b_once', '17b', ['autoexec'])
s.table(('.', [K('a')]))
s.ee(AUTO_ADDR, [HDR('.')])
s.w('boot')
s.w('quiesce 900')
s.w('expect_keys <00/04>')
s.w('run 2000')
s.w('expect_keys -')
s.end()

s = S('t17c_nothing_set', '17c', ['autoexec'])
s.table(('.', [K('a')]))
s.boot()
s.w('run 2000')
s.w('expect_keys -')
s.end()

s = S('t17d_dangling', '17d', ['autoexec'])
s.table(('.', [K('a')]))
s.ee(AUTO_ADDR, [HDR('-----')])
s.boot()
s.w('run 2000')
s.w('expect_keys -')
s.end()

s = S('t17e_not_enumerated', '17a', ['autoexec'])
s.table(('.', [K('a')]))
s.ee(AUTO_ADDR, [HDR('.')])
s.w('boot noenum')
s.w('run 2500')
s.w('expect_keys -')
s.end()

s = S('t18_jump', '18', ['jump'])
s.table(('.', [K('a')] + jump_to('-') + [K('z')]), ('-', [K('b')]))
s.boot()
s.play('.', '<00/04><00/05>')
s.end()

s = S('t18b_jump_absent', '18b', ['jump'])
s.table(('.', [K('a')] + jump_to('-----') + [K('z')]))
s.boot()
s.play('.', '<00/04>')
s.end()

s = S('t18c_self_jump_touch_stops', '18c 18d 18e', ['jump', 'stop'])
s.table(('.', [K('a')] + jump_to('.')))
s.boot()
s.w('clearkeys')
s.w('code .')
s.w('run 900')
s.w('note a finger stops it')
s.w('touch 1')
s.w('run 120')
s.w('touch 0')
s.w('quiesce 900')
s.w('keys')
s.w('ram code_len 1')
s.w('note holding on after the stop opens the window')
s.w('clearkeys')
s.w('code .')
s.w('run 900')
s.w('touch 1')
s.w('run 4400')
s.w('touch 0')
s.w('run 200')
s.w('keys')
s.cmd(INFO)
s.w('expect_ansbits 7 1 1')
s.end()

s = S('t18f_jump_loop_bounded', '18f', ['jump'])
s.table(('.', jump_to('-')), ('-', jump_to('.')))
s.boot()
s.play('.', '', wait=1500)
s.w('run 500')
s.cmd(INFO)
s.w('note the board is still answering')
s.end()

s = S('t18f2_hundred_jumps', '18f', ['jump'])
# exactly a hundred jumps is allowed, a hundred and one is not
ch = [('.', jump_to('.-'))]
s.table(('.', [K('a'), ESC_JUMP, HDR('-') + 1]),
        ('-', [ESC_JUMP, HDR('.') + 1]))
s.boot()
s.w('clearkeys')
s.w('code .')
s.w('run 3000')
s.w('touch 1')
s.w('run 120')
s.w('touch 0')
s.w('quiesce 1100')
s.w('keys')
s.end()

s = S('t18g_long_macro_window', '18g', ['window', 'playback'])
s.table(('.', [ESC_PAUSE, 255, ESC_PAUSE, 255, ESC_PAUSE, 255, K('a')]))
s.boot()
s.hold()
s.w('touch 0')
s.play('.', '<00/04>', wait=80000)
s.cmd(INFO)
s.w('expect_ansbits 7 1 0')
s.end()

s = S('t18h_repeat_stoppable', '18h', ['repeats', 'stop'])
s.table(('.', [K('a'), ESC_REPEAT, 200]))
s.boot()
s.w('clearkeys')
s.w('code .')
s.w('run 1300')
s.w('touch 1')
s.w('run 120')
s.w('touch 0')
s.w('quiesce 900')
s.w('keys')
s.end()

s = S('t18i_jump_clears_group_mod', '18', ['jump', 'repeats'])
s.table(('.', [ESC_MARK, K('a'), ESC_MOD, 1] + jump_to('-') + [K('z')]),
        ('-', [K('b'), ESC_REPEAT, 1, K('c')]))
s.boot()
s.play('.', '<00/04><00/05><00/05><00/06>')
s.end()

s = S('t10b_end_of_table_byte', '10', ['playback'])
# A header whose count field is 5 or more ends the table, wherever it sits
s.raw([HDR('.'), K('a'), 0, 0x05, K('z'), 0, HDR('-'), K('b'), 0, 0xFF])
s.boot()
s.play('.', '<00/04>')
s.play('-', '')
s.end()

s = S('t10c_end_of_table_byte_high', '10', ['playback'])
s.raw([HDR('.'), K('a'), 0, 0x87, K('z'), 0, HDR('-'), K('b'), 0, 0xFF])
s.boot()
s.play('.', '<00/04>')
s.play('-', '')
s.end()

s = S('t17f_power_on_impossible_header', '17c', ['autoexec'])
s.table(('.', [K('a')]))
s.ee(AUTO_ADDR, [0x05])
s.boot()
s.w('run 2000')
s.w('expect_keys -')
s.end()

s = S('t16e_mark_forgotten_after_group', '16b', ['repeats'])
s.table(('.', [ESC_MARK, K('a'), ESC_REPEAT, 1, K('b'), ESC_REPEAT, 2]))
s.boot()
s.play('.', '<00/04><00/04><00/05><00/05><00/05>')
s.end()

s = S('t18f3_long_chain_is_allowed', '18f', ['jump'])
codes = [c for n in range(1, 6) for c in
         (''.join('-' if b >> i & 1 else '.' for i in range(n - 1, -1, -1)) for b in range(1 << n))][:32]
chain = [(c, jump_to(codes[i + 1])) for i, c in enumerate(codes[:-1])] + [(codes[-1], [K('z')])]
s.table(*chain)
s.boot()
s.play(codes[0], '<00/1D>')
s.end()

# ---- the password ------------------------------------------------------

s = S('t19_no_password', '19', ['commands'])
s.blank()
s.boot()
s.cmd(INFO)
s.w('expect_ansbits 7 6 4')
s.hold()
s.cmd(READ, 0x1E0, 4)
s.end()

s = S('t19b_setpass_gates', '19b 19b2 19c', ['commands', 'password'])
s.blank()
s.boot()
s.note = 0
s.w('note no window: refused')
s.cmd(SETPASS, 0, 3, b'abc')
s.w('expect_ans 0 EE')
s.hold()
s.cmd(SETPASS, 0, 3, b'abc')
s.w('expect_ans 0 63')
s.w('expect_ee 1f4 61 62 63 00 00 00 00 00 00 00 00 00')
s.cmd(INFO)
s.w('expect_ansbits 7 6 6')
s.cmd(SETPASS, 0, 13, b'abcdefghijklm')
s.w('expect_ans 0 EE')
s.cmd(SETPASS, 0, 0)
s.w('expect_ans 0 63')
s.w('expect_ee 1f4 FF FF FF FF FF FF FF FF FF FF FF FF')
s.cmd(INFO)
s.w('expect_ansbits 7 6 4')
s.end()

s = S('t19b_unlock_rules', '19b 19b2', ['commands', 'password'])
s.blank()
s.password(b'abc')
s.boot()
s.cmd(INFO)
s.w('expect_ansbits 7 6 2')
s.hold()
s.w('note wrong')
s.cmd(UNLOCK, 0, 3, b'abd')
s.w('expect_ans 0 EE')
s.cmd(INFO)
s.w('expect_ansbits 7 6 2')
s.w('run 1100')
s.w('note prefix')
s.cmd(UNLOCK, 0, 2, b'ab')
s.w('expect_ans 0 EE')
s.w('run 1100')
s.w('note longer')
s.cmd(UNLOCK, 0, 4, b'abcd')
s.w('expect_ans 0 EE')
s.w('run 1100')
s.w('note right')
s.cmd(UNLOCK, 0, 3, b'abc')
s.w('expect_ans 0 62')
s.cmd(INFO)
s.w('expect_ansbits 7 6 6')
s.w('note within a second of a wrong one even the right one is turned away')
s.w('run 25000')
s.end()

s = S('t19b2_second_after_wrong', '19b2', ['password'])
s.blank()
s.password(b'abc')
s.boot()
s.hold()
s.cmd(UNLOCK, 0, 3, b'abd')
s.cmd(UNLOCK, 0, 3, b'abc')
s.w('expect_ans 0 EE')
s.cmd(INFO)
s.w('run 500')
s.cmd(UNLOCK, 0, 3, b'abc')
s.w('expect_ans 0 EE')
s.w('run 800')
s.cmd(UNLOCK, 0, 3, b'abc')
s.w('expect_ans 0 62')
s.end()

s = S('t19d_password_plays_anyway', '19d', ['password', 'playback'])
s.table(('.', [K('a')]))
s.password()
s.boot()
s.play('.', '<00/04>')
s.end()

# ---- the lockout -------------------------------------------------------

miss_code = '---'


def miss(s):
    s.play(miss_code, '')


def serve(s, seconds=11):
    s.w('run %d' % (seconds * 1000))


s = S('t21_no_lockout_erased', '21', ['lockout'])
s.table(('.', [K('a')]))
s.boot()
for i in range(3):
    miss(s)
s.w('expect_ee 1ef FF')
s.lockout(1, 1, 1)
for i in range(3):
    miss(s)
s.w('expect_ee 1ef FF')
s.play('.', '<00/04>')
s.end()

s = S('t21b_soft_lock', '21b 21c', ['lockout'])
s.table(('.', [K('a')]))
s.password()
s.lockout(2, 3, 1)
s.boot()
miss(s)
s.w('expect_ee 1ef FE')
s.cmd(INFO)
s.w('expect_ansbits 7 18 0')
miss(s)
s.w('expect_ee 1f0 FE')
s.cmd(INFO)
s.w('expect_ansbits 7 18 8')
s.play('.', '')
s.w('note the blip while locked')
s.w('run 5000')
s.cmd(INFO)
s.w('expect_ansbits 7 18 8')
s.w('run 4500')
s.cmd(INFO)
s.w('expect_ansbits 7 18 0')
s.play('.', '<00/04>')
s.w('expect_ee 1ef FF')
s.w('expect_ee 1f0 FE')
s.end()

s = S('t21d_replug_restarts_wait', '21d', ['lockout'])
s.table(('.', [K('a')]))
s.password()
s.lockout(2, 3, 1)
s.boot()
miss(s)
miss(s)
s.w('run 8000')
s.w('note a replug: the wait starts again')
s.cmd(INFO)
s.w('run 200')
s.end()

s = S('t21d_replug_real', '21d', ['lockout'])
s.table(('.', [K('a')]))
s.password()
s.lockout(1, 0xFF, 1)
s.ee(LOCK_MISSES, [0xFE])
s.boot()
s.cmd(INFO)
s.w('expect_ansbits 7 18 8')
s.w('run 9000')
s.cmd(INFO)
s.w('expect_ansbits 7 18 8')
s.w('run 1500')
s.cmd(INFO)
s.w('expect_ansbits 7 18 0')
s.end()

s = S('t21e_hard_lock', '21e 21f 21g', ['lockout', 'password'])
s.table(('.', [K('a')]))
s.password()
s.lockout(2, 2, 1)
s.boot()
miss(s)
miss(s)
s.w('run 11000')
miss(s)
miss(s)
s.cmd(INFO)
s.w('expect_ansbits 7 18 10')
s.w('run 11000')
s.cmd(INFO)
s.w('expect_ansbits 7 18 10')
s.play('.', '')
s.w('note the fast flicker: LED edges in the trace')
s.w('run 300')
s.w('expect_ee 1f0 FD')
s.w('note a wrong password, then the right one lifts it')
s.hold()
s.cmd(UNLOCK, 0, 5, b'wrong')
s.w('expect_ans 0 EE')
s.cmd(INFO)
s.w('expect_ansbits 7 18 10')
s.w('run 1100')
s.cmd(UNLOCK, 0, 6, PASS)
s.w('expect_ans 0 62')
s.cmd(INFO)
s.w('expect_ansbits 7 18 0')
s.w('expect_ee 1f1 FE')
s.play('.', '<00/04>')
s.end()

s = S('t21f_hard_stops_power_on', '21f', ['lockout', 'autoexec'])
s.table(('.', [K('a')]))
s.password()
s.lockout(2, 2, 1)
s.ee(LOCK_SOFTS, [0xFD])          # two soft locks: hard
s.ee(AUTO_ADDR, [HDR('.')])
s.boot()
s.w('run 2000')
s.w('expect_keys -')
s.end()

s = S('t21h_wrong_password_counts', '21h', ['lockout', 'password'])
s.table(('.', [K('a')]))
s.password()
s.lockout(2, 0xFF, 1)
s.boot()
s.hold()
s.cmd(UNLOCK, 0, 5, b'guess')
s.w('expect_ee 1ef FE')
s.cmd(UNLOCK, 0, 5, b'guess')
s.w('expect_ee 1ef FE')
s.w('run 1100')
s.cmd(UNLOCK, 0, 5, b'guess')
s.cmd(INFO)
s.w('expect_ansbits 7 18 8')
s.w('run 1100')
s.cmd(UNLOCK, 0, 6, PASS)
s.w('expect_ans 0 62')
s.cmd(INFO)
s.w('expect_ansbits 7 18 0')
s.w('expect_ee 1f1 FF')
s.end()

for w, name in ((0, '0'), (241, '241'), (0xFF, 'ff')):
    s = S('t21i_wait_nonsense_' + name, '21i', ['lockout'])
    s.table(('.', [K('a')]))
    s.password()
    s.lockout(1, 0xFF, w)
    s.boot()
    miss(s)
    s.w('run 57000')
    s.cmd(INFO)
    s.w('expect_ansbits 7 18 8')
    s.w('run 3500')
    s.cmd(INFO)
    s.w('expect_ansbits 7 18 0')
    s.end()

s = S('t21i_forty_minutes', '21i', ['lockout', 'slow'])
s.table(('.', [K('a')]))
s.password()
s.lockout(1, 0xFF, 240)
s.boot()
miss(s)
s.w('run 2370000')
s.cmd(INFO)
s.w('expect_ansbits 7 18 8')
s.w('run 40000')
s.cmd(INFO)
s.w('expect_ansbits 7 18 0')
s.end()

s = S('t21j_no_hard_threshold', '21j', ['lockout'])
s.table(('.', [K('a')]))
s.password()
s.lockout(1, 0xFF, 1)
s.boot()
for i in range(3):
    miss(s)
    serve(s)
s.cmd(INFO)
s.w('expect_ansbits 7 18 0')
s.w('expect_ee 1f0 FC')
s.end()

s = S('t21j2_clear', '21j2', ['lockout', 'clear'])
s.table(('.', [K('a')]), ('..', [K('b'), ESC_JUMP, JUMP_CLEAR, K('c')]))
s.password()
s.lockout(3, 2, 1)
s.boot()
miss(s)
miss(s)
s.play('.', '<00/04>')
s.w('expect_ee 1ef FD')
miss(s)
s.cmd(INFO)
s.w('expect_ansbits 7 18 8')
serve(s)
s.w('expect_ee 1f0 FE')
s.play('..', '<00/05><00/06>')
s.w('expect_ee 1ef FF')
s.w('expect_ee 1f0 FF')
s.end()

s = S('t21k_new_password_clean_slate', '21k', ['lockout', 'password'])
s.table(('.', [K('a')]))
s.password()
s.lockout(5, 3, 1)
s.ee(LOCK_MISSES, [0xFD])
s.ee(LOCK_SOFTS, [0xFD])
s.ee(LOCK_CLEARED, [0xFE])
s.boot()
s.hold()
s.cmd(UNLOCK, 0, 6, PASS)
s.w('expect_ee 1ef FF')
s.w('note the right password cleared those; put some back and change it')
s.ee(LOCK_MISSES, [0xFD])
s.ee(LOCK_SOFTS, [0xFD])
s.w('run 100')
s.cmd(SETPASS, 0, 5, b'other')
s.w('expect_ee 1ef FF')
s.w('expect_ee 1f0 FF')
s.w('expect_ee 1f1 FE')
s.end()

s = S('t21_count_saturates', '21', ['lockout'])
s.table(('.', [K('a')]))
s.password()
s.lockout(0xFE, 0xFF, 1)
s.ee(LOCK_MISSES, [0x01])
s.ee(LOCK_SOFTS, [0x00])
s.boot()
miss(s)
s.w('eedump 1ec 8')
miss(s)
s.w('eedump 1ec 8')
s.end()

s = S('t21_miss_before_blink', '21', ['lockout'])
s.table(('.', [K('a')]))
s.password()
s.lockout(5, 0xFF, 1)
s.boot()
s.w('code ---')
s.w('run 702')
s.w('note the count is written before the blink shows')
s.w('eedump 1ef 2')
s.w('run 500')
s.w('eedump 1ef 2')
s.end()

# ---- secrets -----------------------------------------------------------


def secret_body(before, loud, plain, key=SKEY):
    return list(before) + [ESC_JUMP, SECRET_LOUD if loud else SECRET_QUIET] + sealed(plain, key)


s = S('t22_no_key', '22', ['secret'])
s.table(('.', secret_body([K('a')], 0, [K('b'), K('c')])),
        ('-', secret_body([], 1, [K('x')])),
        ('..', [K('y')]))
s.eekeys()
s.boot()
s.play('.', '<00/04>')
s.play('..', '<00/1C>')
s.end()

s = S('t22_no_check_value', '22', ['secret'])
s.table(('.', secret_body([K('a')], 0, [K('b')])))
s.boot()
s.cmd(KEYAUTH, 0, 16, SKEY)
s.w('expect_ans 0 EE')
s.end()

s = S('t22b_right_key', '22b 22c', ['secret'])
s.table(('.', secret_body([K('a')], 0, [K('b'), K('c')])),
        ('-', secret_body([], 1, [K('x')])))
s.eekeys()
s.boot()
s.cmd(INFO)
s.w('expect_ansbits 7 80 0')
s.cmd(KEYAUTH, 0, 16, SKEY)
s.w('expect_ans 0 6A')
s.w('expect_ans 4 0')
s.cmd(INFO)
s.w('expect_ansbits 7 80 80')
s.play('.', '<00/04><00/05><00/06>')
s.play('-', '<00/1B>')
s.w('run 54000')
s.cmd(INFO)
s.w('expect_ansbits 7 80 80')
s.w('run 8000')
s.cmd(INFO)
s.w('expect_ansbits 7 80 0')
s.w('expect_ram0 _ZL3sec 10')
s.play('.', '<00/04>')
s.end()

s = S('t22c_key_wait_setting', '22c', ['secret'])
s.table(('.', secret_body([K('a')], 0, [K('b')])))
s.eekeys()
s.ee(KEY_WAIT, [1])
s.boot()
s.cmd(KEYAUTH, 0, 16, SKEY)
s.w('expect_ans 0 6A')
s.w('run 9500')
s.cmd(INFO)
s.w('expect_ansbits 7 80 80')
s.w('run 1500')
s.cmd(INFO)
s.w('expect_ansbits 7 80 0')
s.w('expect_ram0 _ZL3sec 10')
s.end()

s = S('t22c_forget_on_write', '22c', ['secret', 'commands'])
s.table(('.', secret_body([K('a')], 0, [K('b')])))
s.eekeys()
s.boot()
s.cmd(KEYAUTH, 0, 16, SKEY)
s.cmd(INFO)
s.w('expect_ansbits 7 80 80')
s.hold()
s.cmd(WRITE, 0x100, 2, b'xy')
s.w('expect_ee 100 78 79')
s.cmd(INFO)
s.w('expect_ansbits 7 80 0')
s.w('expect_ram0 _ZL3sec 10')
s.end()

s = S('t22d_wrong_key', '22d', ['secret', 'lockout'])
s.table(('.', secret_body([K('a')], 0, [K('b')])))
s.eekeys()
s.password()
s.lockout(2, 3, 1)
s.boot()
s.cmd(KEYAUTH, 0, 16, SBAD)
s.w('expect_ans 0 EE')
s.cmd(KEYAUTH, 0, 16, SKEY)
s.w('expect_ans 0 EE')
s.w('expect_ee 1ef FF')
s.w('expect_ram0 _ZL3sec 10')
s.w('run 1100')
s.cmd(KEYAUTH, 0, 15, SKEY)
s.w('expect_ans 0 EE')
s.cmd(KEYAUTH, 0, 16, SKEY)
s.w('expect_ans 0 6A')
s.end()

s = S('t22d2_pause_ends_by_loop', '22d', ['secret'])
s.table(('.', secret_body([K('a')], 0, [K('b')])))
s.eekeys()
s.boot()
s.cmd(KEYAUTH, 0, 16, SBAD)
s.w('run 1500')
s.w('note clocks_poll has expired it: the right key goes straight in')
s.cmd(KEYAUTH, 0, 16, SKEY)
s.w('expect_ans 0 6A')
s.end()

s = S('t22e_loud_refusal', '22e', ['secret', 'lockout'])
s.table(('.', secret_body([K('a')], 0, [K('b')])),
        ('-', secret_body([], 1, [K('x')])))
s.eekeys()
s.password()
s.lockout(2, 3, 1)
s.boot()
s.play('-', '')
s.w('expect_ee 1ef FE')
s.play('.', '<00/04>')
s.w('expect_ee 1ef FE')
s.play('-', '')
s.cmd(INFO)
s.w('expect_ansbits 7 18 8')
s.end()

s = S('t22e2_led_dark_on_loud', '22e2', ['secret', 'playback'])
s.table(('-', secret_body([], 1, [K('x')])), ('..', [K('a')]))
s.eekeys()
s.boot()
s.play('-', '')
s.w('note LED edges above are the three blinks only')
s.play('..', '<00/04>')
s.end()

s = S('t22f_secret_keeps_misses', '22f', ['secret', 'lockout'])
s.table(('.', secret_body([K('a')], 0, [K('b')])),
        ('-', secret_body([], 1, [K('x')])))
s.eekeys()
s.password()
s.lockout(4, 0xFF, 1)
s.boot()
miss(s)
s.play('.', '<00/04>')
s.w('expect_ee 1ef FE')
s.cmd(KEYAUTH, 0, 16, SKEY)
s.play('-', '<00/1B>')
s.w('expect_ee 1ef FE')
s.end()

s = S('t22g_jumps_and_secrets', '22g', ['secret', 'jump'])
s.table(('.', [K('a')] + jump_to('-')),
        ('-', secret_body([], 0, [K('x')] + jump_to('..'))),
        ('..', [K('y')]))
s.eekeys()
s.boot()
s.play('.', '<00/04>')
s.cmd(KEYAUTH, 0, 16, SKEY)
s.play('.', '<00/04><00/1B><00/1C>')
s.end()

s = S('t22h_everything_inside_secret', '22h', ['secret', 'repeats'])
s.table(('.', secret_body([K('q')], 0,
                          [K('a'), ESC_REPEAT, 2, ESC_MARK, K('b'), K('c'),
                           ESC_REPEAT, 1, ESC_MOD, 1, K('d'), ESC_PAUSE, 1, KS('e')])))
s.eekeys()
s.boot()
s.cmd(KEYAUTH, 0, 16, SKEY)
s.play('.', '<00/14><00/04><00/04><00/04><00/05><00/06><00/05><00/06><01/07><02/08>', wait=1500)
s.end()

s = S('t22i_key_runs_out', '22i', ['secret'])
s.table(('.', secret_body([], 0, [K('a'), ESC_PAUSE, 30, K('b')])))
s.eekeys()
s.ee(KEY_WAIT, [1])
s.boot()
s.cmd(KEYAUTH, 0, 16, SKEY)
s.w('run 8000')
s.play('.', '<00/04>', wait=4000)
s.w('expect_ram0 _ZL3sec 10')
s.end()

s = S('t22j_prove', '22j', ['secret', 'commands'])
s.blank()
s.eekeys()
c = bytes([0xC0, 0xFF, 0xEE, 0, 1, 2, 3, 4])
s.boot()
s.cmd(PROVE, 0, 8, c)
want = xtea(list(c), check_value(SKEY) * 2)
s.w('expect_ans 0 6B')
for i, b in enumerate(want):
    s.w('expect_ans %d %02X' % (4 + i, b))
s.cmd(PROVE, 0, 7, c)
s.w('expect_ans 0 EE')
s.end()

s = S('t22k_host_refused', '22k', ['secret', 'lockout'])
s.table(('.', [K('a'), ESC_HOST, HOST_SILENT, K('b')]),
        ('-', [ESC_HOST, HOST_LOUD, K('x')]))
s.eekeys()
s.password()
s.lockout(2, 3, 1)
s.boot()
s.cmd(KEYAUTH, 0, 16, SKEY)
s.play('.', '<00/04>')
s.play('-', '')
s.w('expect_ee 1ef FE')
s.end()

# ---- the remote --------------------------------------------------------

s = S('t24_remote', '24', ['ir'])
s.table(('.', [K('a')] + IRTAG(0x00, 0x45) + [K('b'), K('c')]),
        ('-', IRTAG(0x80, 0xC5) + [K('d')] + IRTAG(0x00, 0x46) + [K('e')]))
s.boot()
s.press(0x00, 0x45, '<00/05><00/06>')
s.press(0x00, 0x46, '<00/08>')
s.press(0x80, 0xC5, '<00/07><00/08>')
s.press(0x00, 0xC5, '')
s.press(0x80, 0x45, '')
s.press(0x01, 0x45, '')
s.play('.', '<00/04><00/05><00/06>')
s.end()

s = S('t24_remote_noise', '24', ['ir'])
s.table(('.', [K('a')] + IRTAG(0x00, 0x45) + [K('b'), K('c')]))
s.boot()
s.w('note the command is not its complement: noise')
s.w('ir 00 ff 45 45')
s.w('run 300')
s.w('expect_keys -')
s.w('note the address complement is not checked: extended NEC')
s.w('ir 00 12 45 ba')
s.w('run 300')
s.w('quiesce 300')
s.w('expect_keys <00/05><00/06>')
s.w('note a repeat plays nothing')
s.w('irraw 11250 1125')
s.w('run 300')
s.w('expect_keys -')
s.w('note a frame cut off, and stray bits after it')
s.w('irraw 13500 ' + ' '.join(['1125'] * 20))
s.w('run 200')
s.w('irraw ' + ' '.join(['1125'] * 12))
s.w('run 300')
s.w('expect_keys -')
s.w('note a glitch mid frame')
s.w('irraw 13500 ' + ' '.join(['1125'] * 16 + ['400'] + ['1125'] * 16))
s.w('run 300')
s.w('expect_keys -')
f = 0x80 | 0x7F << 8 | 0xC5 << 16 | 0x3A << 24
s.w('note a blip just before the last edge')
s.w('irraw 13500 ' + ' '.join(str(2250 if f >> i & 1 else 1125) for i in range(31)) + ' 400 725')
s.w('run 300')
s.w('expect_keys -')
s.end()

LG = [[11170, 1116, 1116, 2171, 1178, 1054, 1054, 1054, 1054, 2171, 2295,
       1116, 2171, 2233, 2295, 2233, 2233, 1116, 1116, 1054, 2171, 1116,
       1054, 1054, 1054, 2171, 2233, 2295, 1116, 2171, 2295, 2233, 2233],
      [11108, 1178, 1116, 2171, 1116, 1054, 1054, 1054, 1054, 2171, 2295,
       1116, 2171, 2233, 2295, 2233, 2233, 1116, 1116, 1054, 2171, 1116,
       1054, 1054, 1054, 2171, 2233, 2295, 1116, 2171, 2295, 2233, 2233]]
s = S('t24_lg_remote', '24', ['ir'])
s.table(('.', IRTAG(0x04, 0x08) + [K('l'), K('g')]))
s.boot()
for fr in LG:
    s.w('irraw ' + ' '.join(map(str, fr)))
    s.w('run 300')
    s.w('quiesce 300')
    s.w('expect_keys <00/0F><00/0A>')
s.end()

s = S('t24_ir_walk_knows_args', '24', ['ir', 'playback'])
s.table(('.', [ESC_PAUSE, 0x80, 0x85, 0x86, 0x82, K('z'), K('z')]),
        ('-', IRTAG(0x07, 0x08) + [K('y')]))
s.boot()
s.press(0x05, 0x86, '')
s.press(0x07, 0x08, '<00/1C>')
s.end()

s = S('t24_timing_edges', '24', ['ir'])
# the window for a leader (10-14.5 ms) and a bit (0.8-2.8 ms), its 0/1 split
s.table(('.', IRTAG(0x12, 0x34) + [K('a')]))
s.boot()
for lead, gap0, gap1, label in ((10000, 1125, 2250, 'short leader'),
                                (9500, 1125, 2250, 'too short'),
                                (14400, 1125, 2250, 'long leader'),
                                (14900, 1125, 2250, 'too long'),
                                (13500, 800, 2800, 'bit limits'),
                                (13500, 1650, 1750, 'split'),
                                (13500, 750, 2250, 'bit too short'),
                                (13500, 1125, 2850, 'bit too long'),
                                (13500, 1125, 3300, 'bit far too long'),
                                (28400, 1125, 2250, 'a gap past the wrap, leader-shaped')):
    f = 0x12 | 0xED << 8 | 0x34 << 16 | 0xCB << 24
    s.w('note ' + label)
    s.w('irraw %d ' % lead + ' '.join(str(gap1 if f >> i & 1 else gap0) for i in range(32)))
    s.w('run 300')
    s.w('quiesce 200')
    s.w('keys')
s.end()

s = S('t24b_remote_and_lockout', '24b', ['ir', 'lockout'])
s.table(('-', IRTAG(0x07, 0x08) + [K('y')]))
s.password()
s.lockout(2, 0xFF, 1)
s.boot()
s.press(1, 1, '')
s.press(1, 2, '')
s.press(1, 3, '')
s.cmd(INFO)
s.w('expect_ansbits 7 18 0')
miss(s)
miss(s)
s.cmd(INFO)
s.w('expect_ansbits 7 18 8')
s.press(0x07, 0x08, '')
s.end()

s = S('t24c_learn', '24c', ['ir', 'commands'])
s.table(('-', IRTAG(0x07, 0x08) + [K('y')]))
s.boot()
s.cmd(LEARN)
s.w('expect_ans 0 69')
s.press(0x07, 0xA8, '<00/27><00/24><00/2C><00/04><00/25>')
s.press(0x07, 0x08, '<00/1C>')
s.cmd(LEARN)
s.w('run 9500')
s.press(0x07, 0x08, '<00/27><00/24><00/2C><00/27><00/25>')
s.cmd(LEARN)
s.w('run 10600')
s.press(0x07, 0x08, '<00/1C>')
s.press(0x07, 0x08, '<00/1C>')
s.end()

s = S('t24c_hex_digits', '24c', ['ir'])
s.blank()
s.boot()
s.cmd(LEARN)
s.press(0x00, 0x9F, '<00/27><00/27><00/2C><00/26><00/09>')
s.cmd(LEARN)
s.press(0x10, 0xA5, '<00/1E><00/27><00/2C><00/04><00/22>')
s.end()

# ---- the touch pad: touch_test.cpp ----------------------------------------
# The rise per charge cycle in simulated cycles: the loop reads a pin every 7
# cycles, starting 4 after the pull-up comes on (see app/asm), so a cycle
# reading n counts when the rise is between 7n-3 and 7n+3.


def rise(n):
    return 7 * n + 2


def pad_level(s, n):
    """every charge cycle counts n"""
    s.w('pad %d %d' % (rise(n), rise(n)))


TOUCHQ = 8

s = S('p01_reading_is_max_of_groups', 'tp1', ['touch'])
s.blank()
s.w('note groups read 1,5,2,3 per cycle: 8 charge cycles each')
s.w('padseq ' + ' '.join(str(rise(g)) for g in (1, 5, 2, 3) for _ in range(TOUCHQ)))
s.boot()
s.w('ram touch_base 2')
s.w('run 100')
s.w('ram touch_base 2')
s.end()

s = S('p02_saturates', 'tp3', ['touch'])
s.blank()
s.w('pad 1000 1000')
s.boot()
s.w('ram touch_base 2')
s.end()

s = S('p04_average_settles', 'tp4', ['touch'])
s.blank()
pad_level(s, 2)
s.boot()
s.w('run 3000')
s.w('ram touch_base 2')
s.w('ram touch_state 1')
s.end()

s = S('p05_finger_at_power_up', 'tp5', ['touch', 'tap'])
s.blank()
pad_level(s, 6)
s.boot()
s.w('ramw touch_base 4')   # the fraction depends on when the 100 ms moves fell
pad_level(s, 2)
s.w('run 6000')
s.w('ramw touch_base 4')   # the fraction depends on when the 100 ms moves fell
s.w('ram touch_state 1')
s.w('run 200')
s.end()

s = S('p06_threshold', 'tp6', ['touch'])
s.blank()
pad_level(s, 2)
s.boot()
s.w('run 2000')
s.w('ram touch_base 2')
for reading in range(16, 34):
    s.w('note per-cycle counts summing to %d' % reading)
    q, r = divmod(reading, 8)
    s.w('padseq ' + ' '.join(str(rise(q + (i < r))) for i in range(8)))
    s.w('run 30')
    s.w('ram touch_state 1')
    pad_level(s, 2)
    s.w('run 400')
    s.w('ram touch_state 1')
s.end()

s = S('p07_average_ignores_finger', 'tp7', ['touch'])
s.blank()
pad_level(s, 2)
s.boot()
s.w('run 2000')
pad_level(s, 6)
s.w('run 500')
s.w('ram touch_state 1')
s.w('ram touch_base 2')
s.end()

s = S('p08_drift_follows', 'tp8', ['touch'])
s.blank()
pad_level(s, 12)
s.boot()
s.w('run 1000')
for want in (13, 14, 15, 16):
    pad_level(s, want)
    s.w('run 5000')
    s.w('ram touch_state 1')
    s.w('ramw touch_base 4')   # the fraction depends on when the 100 ms moves fell
s.end()

s = S('p09_stuck_touch', 'tp9', ['touch'])
s.blank()
pad_level(s, 2)
s.boot()
s.w('run 2000')
pad_level(s, 6)
s.w('run 200')
s.w('ram touch_state 1')
s.w('run 10500')
s.w('ram touch_base 2')
s.w('run 20')
s.w('ram touch_state 1')
s.end()

s = S('p10_release', 'tp10', ['touch'])
s.blank()
pad_level(s, 2)
s.boot()
s.w('run 2000')
pad_level(s, 6)
s.w('run 200')
s.w('ram touch_state 1')
pad_level(s, 2)
s.w('run 50')
s.w('ram touch_state 1')
s.end()

s = S('p12_slow_press', 'tp12 tp12b', ['touch', 'tap'])
s.blank()
pad_level(s, 1)
s.boot()
s.w('run 2000')
s.w('ramw touch_base 4')   # the fraction depends on when the 100 ms moves fell
for r in range(8, 17):
    q, m = divmod(r, 8)
    s.w('padseq ' + ' '.join(str(rise(q + (i < m))) for i in range(8)))
    s.w('run 40')
s.w('ram touch_state 1')
s.w('note the four-second hold, then a dip to the press threshold, then release')
s.w('run 4000')
s.w('ram touch_state 1')
s.w('ramw touch_base 4')   # the fraction depends on when the 100 ms moves fell
q, m = divmod(8 + 2 + 2, 8)
s.w('padseq ' + ' '.join(str(rise(q + (i < m))) for i in range(8)))
s.w('run 20')
s.w('ram touch_state 1')
pad_level(s, 1)
s.w('run 20')
s.w('ram touch_state 1')
s.end()

s = S('p13_pad_left_idle', 'tp13', ['touch'])
s.blank()
s.boot()
s.w('run 200')
s.w('peekm 37 fe')       # DDRB and PORTB but for the pad's bit, which a
s.w('peekm 38 fe')       # sample may catch mid-charge: see the inventory
s.end()

s = S('p14_touch_at_setup_seeds', 'tp4', ['touch'])
s.blank()
pad_level(s, 3)
s.boot()
s.w('ram touch_base 2')
s.end()

s = S('p15_four_groups_exactly', 'tp1', ['touch'])
# A fifth group of readings, or a third, would see the odd one out: four
# groups of eight charge cycles read low, the fifth (one reading in five, as
# the phase walks) reads high. Four groups never see it when the phase starts
# on it, so a finger reads as a finger only some of the time; five always do,
# which would pin touch_state and re-base the average when it looks stuck.
s.blank()
low, high = rise(2), rise(6)
s.w('padseq ' + ' '.join(str(low) for _ in range(32)) + ' ' +
    ' '.join(str(high) for _ in range(8)))
s.boot()
s.w('run 12000')
s.w('ramw touch_base 4')
s.end()

# ---- the mouse: pointer_test.c and app/hidusb.c --------------------------

s = S('m01_edges_diagonal_scale', 'pt', ['mouse'])
s.table(('.', [ESC_HOST, 0x80, ESC_HOST, 0x40]),        # to 0,0
        ('-', [ESC_HOST, 0x80 | 51, ESC_HOST, 0x40 | 57]),
        ('..', [ESC_HOST, 0x80 | 61, ESC_HOST, 0x40 | 50]),
        ('.-', [ESC_HOST, 8, ESC_HOST, 0x10 | 11, ESC_HOST, 0x80 | 71, ESC_HOST, 0x40 | 55]),
        ('-.', [ESC_HOST, 0x80 | 81, ESC_HOST, 0x40 | 55]),
        ('--', [ESC_HOST, 8, ESC_HOST, 0x80 | 0, ESC_HOST, 0x40 | 30]),
        ('...', [ESC_HOST, 0x80 | 10, ESC_HOST, 0x40 | 30]))
s.boot()
for c, w in (('.', 3000), ('-', 3000), ('..', 1500), ('.-', 1500), ('-.', 1500),
             ('--', 3000), ('...', 1500)):
    s.play(c, None, wait=w)
s.end()

s = S('m02_buttons', 'pt', ['mouse'])
s.table(('.', [ESC_HOST, 0x31]),
        ('-', [ESC_HOST, 0x22]),
        ('..', [ESC_HOST, 0x80 | 20, ESC_HOST, 0x40 | 30]),
        ('.-', [ESC_HOST, 0x20]),
        ('-.', [ESC_HOST, 3, ESC_HOST, 0x80 | 30, ESC_HOST, 0x40 | 30]))
s.boot()
for c in ('.', '-', '..', '.-', '-.'):
    s.play(c, None, wait=2500)
s.end()

# ---- what the app library answers: core_test.c and app_request ------------

s = S('c01_info', 'ct', ['commands'])
s.blank()
s.boot()
s.cmd(INFO)
s.w('expect_ans 0 40')
s.end()

s = S('c02_read_write_gates', 'ct', ['commands', 'password'])
s.table(('.', [K('a')]))
s.password(b'abc')
s.boot()
s.w('note no window')
s.cmd(READ, 0, 4)
s.w('expect_ans 0 EE')
s.cmd(WRITE, 0x100, 2, b'zz')
s.w('expect_ans 0 EE')
s.w('note window but no password')
s.hold()
s.cmd(READ, 0, 4)
s.w('expect_ans 0 EE')
s.w('note unlock')
s.cmd(UNLOCK, 0, 3, b'abc')
s.w('expect_ans 0 62')
s.cmd(READ, 0, 4)
s.w('expect_ans 0 60')
s.cmd(WRITE, 0x100, 2, b'xy')
s.w('expect_ans 0 61')
s.w('expect_ee 100 78 79')
s.w('expect_ans 4 0')
s.end()

s = S('c03_read_clamps', 'ct', ['commands'])
s.table(('.', [K('a')]))
s.boot()
s.hold()
s.w('note 32 bytes at most, and past the end reads FF')
s.cmd(READ, 0, 40)
s.w('peek 109 4')        # the core's variables after the report: left alone
s.cmd(READ, 0, 36)
s.w('peek 109 4')
s.cmd(READ, 0x1F0, 32)
s.cmd(READ, 0x1FF, 2)
s.cmd(READ, 0x200, 4)
s.cmd(READ, 0xFFFE, 4)
s.end()

s = S('c04_write_clamps', 'ct', ['commands'])
s.blank()
s.boot()
s.hold()
s.cmd(WRITE, 0x1FE, 4, b'ABCD')
s.w('expect_ee 1fe 41 42')
s.cmd(READ, 0x1FE, 2)
s.cmd(WRITE, 0x300, 2, b'ZZ')
s.cmd(WRITE, 0, 40, bytes(range(32)))
s.w('expect_ee 1f 1F')
s.end()

s = S('c05_refused_wipes', 'ct', ['commands'])
s.blank()
s.boot()
s.w('note a refused request may carry a secret: wiped from the report')
s.cmd(SETPASS, 0, 3, b'abc')
s.w('expect_ans 0 EE')
s.w('expect_ans 4 0')
s.cmd(0x5F, 0, 0)
s.w('expect_ans 0 EE')
s.end()

s = S('c06_update', 'ct', ['commands'])
s.blank()
s.boot()
s.w('run 7000')
s.w('peek 33 1')
s.cmd(UPDATE)
s.w('expect_ans 0 EE')
s.hold()
s.w('peek 33 1')
s.cmd(UPDATE)
s.w('expect_ans 0 68')
s.w('peek 33 1')
s.w('run 500')
s.w('peek 33 1')
s.end()

s = S('c07_restart', 'ct', ['commands'])
s.table(('.', [K('a')]))
s.eekeys()
s.boot()
s.cmd(KEYAUTH, 0, 16, SKEY)
s.cmd(INFO)
s.w('expect_ansbits 7 80 80')
s.hold()
s.cmd(RESTART)
s.w('expect_ans 0 42')
s.w('restart')
s.cmd(INFO)
s.w('expect_ansbits 7 81 0')
s.w('expect_ram0 _ZL3sec 10')
s.play('.', '<00/04>')
s.end()

s = S('c08_window_clock_wipes', 'ct', ['commands', 'window'])
s.blank()
s.password(b'abc')
s.boot()
s.hold()
s.cmd(UNLOCK, 0, 3, b'abc')
s.cmd(READ, 0x1F4, 4)
s.w('run 29000')
s.cmd(INFO)
s.w('expect_ansbits 7 7 7')
s.w('run 6000')
s.cmd(INFO)
s.w('expect_ansbits 7 7 2')
s.end()

s = S('c09_setpass_session', 'ct', ['password', 'commands'])
s.blank()
s.boot()
s.hold()
s.cmd(SETPASS, 0, 5, b'hello')
s.w('expect_ans 0 63')
s.cmd(READ, 0x1F4, 1)
s.w('expect_ans 0 60')
s.cmd(INFO)
s.w('expect_ansbits 7 6 6')
s.end()

s = S('c10_window_tenths', 'ct', ['clock', 'wtenths'])
s.blank()
s.boot()
s.w('peek 33 1')
s.w('run 1000')
s.w('peek 33 1')
s.w('run 25000')
s.w('peek 33 1')
s.end()

s = S('c11_serial_made_once', 'ct m3', ['setup'])
s.w('eefill FF')
s.w('bootwdt')
s.w('note the serial: entropy, so not byte for byte: see the inventory')
s.w('expect_serial')
s.w('run 1000')
s.w('expect_serial')
s.w('led')

s = S('c12_serial_kept', 'ct', ['setup'])
s.w('eefill FF')
s.w('ee 1e0 11 22 33 44')
s.w('boot')
s.w('eedump 1e0 4')
s.end()

s = S('c13_app_clock', 'm3', ['clock'])
s.blank()
s.boot()
s.w('pc_in')
s.w('run 1000')
s.w('pc_in')
s.w('run 1000')
s.w('pc_in')
s.end()

s = S('c14_macro_during_commands', 'ct', ['commands', 'stack'])
# USB commands while a macro is playing: the app_request path from inside the
# polls inside hidKey, with the macro's registers live.
s.table(('.', [K('a'), ESC_REPEAT, 60, K('b')]))
s.eekeys()
s.boot()
s.w('clearkeys')
s.w('code .')
s.w('run 800')
for i in range(5):
    s.cmd(INFO)
    s.cmd(KEYAUTH, 0, 16, SKEY)
    s.w('run 50')
s.w('quiesce 800')
s.w('keys')
s.end()

s = S('s_deep_stack', 'stack', ['stack'])
# Everything deep at once: a macro in a secret typing with the key held, the
# pad read, KEYAUTH and PROVE arriving during the playing, a window and a soft
# lock's clock running, IR frames arriving, and the host polling.
s.table(('.', secret_body([K('a'), ESC_MOD, 1], 0,
                          [K('b'), ESC_MARK, K('c'), ESC_HOST, 0x20, ESC_REPEAT, 2,
                           ESC_PAUSE, 2, K('d')] * 6)))
s.eekeys()
s.password(b'abc')
s.lockout(6, 0xFF, 1)
s.boot()
s.hold()
s.cmd(UNLOCK, 0, 3, b'abc')
s.cmd(KEYAUTH, 0, 16, SKEY)
s.w('clearkeys')
s.w('code .')
for i in range(8):
    s.cmd(PROVE, 0, 8, b'abcdefgh')
    s.cmd(KEYAUTH, 0, 16, SKEY)
    s.cmd(INFO)
    s.w('ir 00 ff 45 ba')
    s.w('run 400')
s.w('quiesce 800 8000')
s.w('keys')
s.w('stack')
s.end()

s = S('c15_unknown_commands', 'ct', ['commands'])
s.blank()
s.boot()
for c in (0x41, 0x43, 0x61 + 2, 0x64, 0xFF):
    s.cmd(c, 0, 0)
s.w('expect_ans 0 EE')
s.end()

# ---- web/table.js's encoder against the firmware's decoder: hexplay --------
# run.sh proves this with the C compiled for the PC (webtable.expected); here
# the same image on the simulated board, for the asm image's sake.

WEBTABLE = 'build/diff/webtable.txt'
s = S('w01_webtable_codes', 'webtable', ['playback', 'web'])
s.w('webtable')          # marks the use of the table run.py injects
s.boot()
for c in ('.', '-', '..', '.-', '-.', '--', '...', '..-', '...-'):
    s.play(c, None, wait=1500)
s.press(0x00, 0x45)
s.press(0x80, 0xC5)
s.press(0x01, 0x45)
s.press(0x00, 0x46)
s.end()

# ---- {totp} and the clock: the assembly app only --------------------------
#
# The expected digits come from Python's hmac and hashlib, not from the code
# under test; the midstates are made here with a plain SHA-1 compression.

import hashlib, hmac as _hmac, random as _random

_IV = (0x67452301, 0xEFCDAB89, 0x98BADCFE, 0x10325476, 0xC3D2E1F0)
_K = (0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xCA62C1D6)


def _rol(x, n):
    return (x << n | x >> (32 - n)) & 0xFFFFFFFF


def sha1_block(h, block, trace=None):
    """One compression. trace, if given, collects the schedule"""
    w = list(struct.unpack('>16I', block))
    for t in range(16, 80):
        w.append(_rol(w[t - 3] ^ w[t - 8] ^ w[t - 14] ^ w[t - 16], 1))
    a, b, c, d, e = h
    for t in range(80):
        if t < 20:
            f = (b & c) | (~b & d)
        elif t < 40:
            f = b ^ c ^ d
        elif t < 60:
            f = (b & c) | (b & d) | (c & d)
        else:
            f = b ^ c ^ d
        a, b, c, d, e = (_rol(a, 5) + (f & 0xFFFFFFFF) + e + _K[t // 20] + w[t]) & 0xFFFFFFFF, a, _rol(b, 30), c, d
    if trace is not None:
        trace.extend(w)
    return tuple((x + y) & 0xFFFFFFFF for x, y in zip(h, (a, b, c, d, e)))


def midstates(key):
    if len(key) > 64:
        key = hashlib.sha1(key).digest()
    key = key.ljust(64, b'\0')
    mi = sha1_block(_IV, bytes(b ^ 0x36 for b in key))
    mo = sha1_block(_IV, bytes(b ^ 0x5C for b in key))
    return mi, mo


def totp_blob(key):
    """The forty bytes in the table: each midstate reversed byte for byte"""
    mi, mo = midstates(key)
    b = struct.pack('>5I', *mi)[::-1] + struct.pack('>5I', *mo)[::-1]
    assert len(b) == 40
    return list(b)


def TOTP(key):
    return [ESC_IR, 0x7F] + totp_blob(key)


def hotp(key, t):
    """the reference: RFC 4226 through hashlib's own HMAC"""
    d = _hmac.new(key, struct.pack('>Q', t), hashlib.sha1).digest()
    o = d[19] & 15
    return (struct.unpack('>I', d[o:o + 4])[0] & 0x7FFFFFFF) % 1000000


def key_of(d):
    return 0x27 if d == 0 else 0x1D + d


def digits_str(code):
    out = ''
    for c in '%06d' % code:
        out += '<00/%02X>' % key_of(int(c))
    return out


def settime(s, t):
    s.cmd(KEYAUTH, 0, 20, SKEY + struct.pack('<I', t))
    s.w('expect_ans 0 6A')


def asm_only(name, src):
    return S(name, src, sorted(set(src.split()) | {'totp', 'asmonly'}))


CODES = [c for n in range(1, 6)
         for c in (''.join('-' if b >> i & 1 else '.' for i in range(n - 1, -1, -1))
                   for b in range(1 << n))]

# ---- correctness against the RFC vectors and random keys --------------

K20 = b'12345678901234567890'

s = asm_only('z01_rfc6238_sha1', 'totp')
times = [59, 1111111109, 1111111111, 1234567890, 2000000000]
s.table(*[(CODES[i], TOTP(K20)) for i in range(len(times))])
s.eekeys()
s.boot()
for i, t in enumerate(times):
    settime(s, t - t % 30 + 2)   # well inside its 30 s, whatever a tap takes
    s.play(CODES[i], digits_str(hotp(K20, t // 30)))
s.end()

s = asm_only('z02_rfc4226_hotp', 'totp')
s.table(('.', TOTP(K20)))
s.eekeys()
s.boot()
for c in range(10):
    settime(s, c * 30)
    s.play('.', digits_str(hotp(K20, c)))
s.end()

_r = _random.Random(6238)
for n, length in enumerate((1, 5, 10, 16, 20, 32, 64, 65, 70, 20, 20, 10)):
    s = asm_only('z03_random_%02d_keylen_%d' % (n, length), 'totp')
    keys = [bytes(_r.randrange(256) for _ in range(length)) for _ in range(10)]
    s.table(*[(CODES[i], TOTP(keys[i])) for i in range(10)])
    s.eekeys()
    s.boot()
    for i in range(10):
        t = _r.randrange(0, 2 ** 32 - 60)
        settime(s, t - t % 30 + 2)
        s.play(CODES[i], digits_str(hotp(keys[i], t // 30)))
    s.end()

s = asm_only('z04_leading_zeros', 'totp')
kz = bytes(range(1, 21))
zeros = [t for t in range(0, 200000) if hotp(kz, t) < 100000][:5]
s.table(('.', TOTP(kz)))
s.eekeys()
s.boot()
for t in zeros:
    settime(s, t * 30)
    s.play('.', digits_str(hotp(kz, t)))
s.end()

s = asm_only('z05_step_boundary', 'totp')
kb = b'boundary-test-key!!!'
s.table(('.', TOTP(kb)))
s.eekeys()
s.boot()
base = 1700000010               # a multiple of 30, where the step changes
settime(s, base - 25)
s.play('.', digits_str(hotp(kb, (base - 1) // 30)))
settime(s, base - 1)
s.w('run 1500')
s.play('.', digits_str(hotp(kb, base // 30)))
s.end()

# ---- the macro around it ------------------------------------------------

s = asm_only('z10_text_around_a_code', 'totp')
kc = b'macro-around-the-key'
s.table(('.', [K('a'), K('b')] + TOTP(kc) + [K('c'), ESC_PAUSE, 2, K('d')]),
        ('-', [K('z')]))
s.eekeys()
s.boot()
t = 1700000000
settime(s, t)
s.play('.', '<00/04><00/05>' + digits_str(hotp(kc, t // 30)) + '<00/06><00/07>', wait=1500)
s.play('-', '<00/1D>')
s.end()

s = asm_only('z11_several_and_the_walk_past_them', 'totp')
s.table(('.', TOTP(b'first-key-of-table!!')),
        ('-', [K('a')] + TOTP(b'second-key-of-table!') + TOTP(b'third-key-in-macro!!')),
        ('..', [K('z')]))
s.eekeys()
s.boot()
t = 1700000000
settime(s, t)
s.play('..', '<00/1D>')
s.play('-', '<00/04>' + digits_str(hotp(b'second-key-of-table!', t // 30)) +
       digits_str(hotp(b'third-key-in-macro!!', t // 30)))
s.end()

s = asm_only('z14_blob_past_the_top_of_the_table', 'totp')
# forty bytes would run into the serial and the password: refused, not read
s.blank()
s.ee(0, [HDR('.')] + [K('a')] * 459 + [ESC_IR, 0x7F] + [1] * 18)
s.eekeys()
s.boot()
settime(s, 1700000000)
s.play('.', '<00/04>' * 459, wait=1500)
s.end()

s = asm_only('z15_blob_exactly_at_the_top', 'totp')
ke = b'exactly-at-the-top!!'
s.blank()
s.ee(0, [HDR('.')] + [K('a')] * 437 + TOTP(ke))
s.eekeys()
s.boot()
t = 1700000000
settime(s, t)
s.play('.', '<00/04>' * 437 + digits_str(hotp(ke, t // 30)), wait=1500)
s.end()

s = asm_only('z12_jump_into_and_out_of_a_code', 'totp')
kj = b'jumpy-totp-secret-20'
s.table(('.', TOTP(kj) + jump_to('-')), ('-', [K('y')]))
s.eekeys()
s.boot()
t = 1700000123
settime(s, t)
s.play('.', digits_str(hotp(kj, t // 30)) + '<00/1C>')
s.end()

s = asm_only('z13_repeat_around_a_code', 'totp')
kr = b'repeat-test-key-1234'
s.table(('.', [K('a'), ESC_REPEAT, 1] + TOTP(kr) + [ESC_MOD, 1, K('c')]))
s.eekeys()
s.boot()
t = 1234567890
settime(s, t)
s.play('.', '<00/04><00/04>' + digits_str(hotp(kr, t // 30)) + '<01/06>')
s.end()

# ---- inside {secret} ------------------------------------------------------

ks = b'a-key-inside-secrets'
s = asm_only('z20_sealed_code', 'totp secret')
s.table(('.', secret_body([K('a')], 1, TOTP(ks) + [K('z')])))
s.eekeys()
s.boot()
t = 1700000420
settime(s, t)
s.play('.', '<00/04>' + digits_str(hotp(ks, t // 30)) + '<00/1D>', wait=1500)
s.end()

s = asm_only('z21_sealed_without_the_key', 'totp secret')
s.table(('.', secret_body([K('a')], 1, TOTP(ks) + [K('z')])),
        ('-', [K('y')]))
s.eekeys()
s.boot()
s.play('.', '<00/04>')
s.w('note {secret!} refused after the a: three blinks, and no digits')
s.play('-', '<00/1C>')
s.end()

s = asm_only('z22_sealed_key_lapses_before_it', 'totp secret')
s.table(('.', secret_body([], 0, [ESC_PAUSE, 30] + TOTP(ks) + [K('z')])))
s.eekeys()
s.ee(KEY_WAIT, [1])
s.boot()
settime(s, 1700000000)
s.w('run 8000')
s.play('.', '', wait=5000)
s.w('expect_ram0 _ZL3sec 10')
s.end()

# ---- the clock: unset, gates, moving ----------------------------------------

s = asm_only('z30_unset_types_zeros_and_stops', 'totp clock')
s.table(('.', [K('a')] + TOTP(kc) + [K('z')]), ('-', [K('c')]))
s.boot()
s.play('.', '<00/04>' + digits_str(0))
s.w('expect_zero 62 40')
s.play('-', '<00/06>')
s.end()

s = asm_only('z31_unset_inside_a_secret_stops', 'totp clock secret')
s.table(('.', secret_body([K('a')], 0, TOTP(ks) + [K('z')])))
s.eekeys()
s.boot()
s.cmd(KEYAUTH, 0, 16, SKEY)
s.w('expect_ans 0 6A')
s.play('.', '<00/04>' + digits_str(0))
s.end()

s = asm_only('z32_write_needs_window', 'clock')
s.blank()
s.boot()
s.w('checkclock 1000')
s.w('expect_ans 10 0')
s.cmd(0x6C, 0, 4, struct.pack('<I', 1700000000))
s.w('expect_ans 0 EE')
s.w('expect_ans 4 0')
s.cmd(0x6C)
s.w('expect_ans 10 0')
s.end()

s = asm_only('z33_write_with_window_no_password', 'clock')
s.blank()
s.boot()
s.hold()
s.cmd(0x6C, 0, 4, struct.pack('<I', 1700000000))
s.w('expect_ans 0 6C')
s.cmd(0x6C)
s.w('expect_ans 10 1')
s.cmd(0x6C)
for i, b in enumerate(struct.pack('<I', 1700000000)):
    s.w('expect_ans %d %02X' % (4 + i, b))
s.cmd(0x6C, 0, 3, b'abc')
s.w('expect_ans 0 EE')
s.end()

s = asm_only('z34_write_with_window_needs_password', 'clock')
s.blank()
s.password(b'abc')
s.boot()
s.hold()
s.cmd(0x6C, 0, 4, struct.pack('<I', 1700000000))
s.w('expect_ans 0 EE')
s.cmd(0x6C)
s.w('expect_ans 10 0')
s.cmd(UNLOCK, 0, 3, b'abc')
s.w('expect_ans 0 62')
s.cmd(0x6C, 0, 4, struct.pack('<I', 1700000000))
s.w('expect_ans 0 6C')
s.cmd(0x6C)
s.w('expect_ans 10 1')
s.end()

s = asm_only('z35_keyauth_time_wrong_key', 'clock secret')
s.blank()
s.eekeys()
s.boot()
s.cmd(KEYAUTH, 0, 20, SBAD + struct.pack('<I', 1700000000))
s.w('expect_ans 0 EE')
s.cmd(0x6C)
s.w('expect_ans 10 0')
s.w('run 1100')
s.w('note the right key sets it; a wrong one afterwards leaves it')
s.cmd(KEYAUTH, 0, 20, SKEY + struct.pack('<I', 1600000000))
s.w('expect_ans 0 6A')
s.w('expect_ans 4 0')
s.cmd(KEYAUTH, 0, 20, SBAD + struct.pack('<I', 1234567890))
s.w('expect_ans 0 EE')
s.cmd(0x6C)
for i, b in enumerate(struct.pack('<I', 1600000000)):
    s.w('expect_ans %d %02X' % (4 + i, b))
s.end()

s = asm_only('z36_keyauth_16_leaves_the_clock', 'clock secret')
s.blank()
s.eekeys()
s.boot()
s.cmd(KEYAUTH, 0, 16, SKEY)
s.w('expect_ans 0 6A')
s.cmd(0x6C)
s.w('expect_ans 10 0')
s.cmd(KEYAUTH, 0, 19, SKEY + b'abc')
s.w('expect_ans 0 EE')
s.end()

s = asm_only('z37_clock_lost_on_replug', 'clock')
s.blank()
s.eekeys()
s.boot()
settime(s, 1700000000)
s.cmd(0x6C)
s.w('expect_ans 10 1')
s.hold()
s.cmd(RESTART)
s.w('restart')
s.cmd(0x6C)
s.w('note RESTART is not a replug: it keeps the clock, as the core restarts only USB')
s.end()

s = asm_only('z38_moves_and_carries_past_the_16_bit_wrap', 'clock')
s.blank()
s.eekeys()
s.boot()
settime(s, 1700000000)
s.w('checkclock 6')
s.w('run 72000')
s.w('checkclock 6')
s.end()

s = asm_only('z38b_seconds_carry_past_sixteen_bits', 'clock')
s.blank()
s.eekeys()
s.boot()
settime(s, 0x1234FFF0)
s.w('run 20000')
s.cmd(0x6C)
s.w('expect_ans 7 12')
s.w('expect_ans 6 35')
s.end()

s = asm_only('z37b_clock_lost_on_power_cycle', 'clock')
s.table(('.', [K('a')] + TOTP(kc) + [K('z')]))
s.eekeys()
s.boot()
settime(s, 1700000000)
s.cmd(0x6C)
s.w('expect_ans 10 1')
s.w('powercycle')
s.cmd(0x6C)
s.w('expect_ans 10 0')
s.play('.', '<00/04>' + digits_str(0))
s.end()

s = asm_only('z39_moves_true_under_load', 'clock')
kt = b'load-test-seed-12345'
s.table(('.', [K('a'), ESC_REPEAT, 200] + TOTP(kt)))
s.eekeys()
s.boot()
settime(s, 1700000000)
s.w('checkclock 200')
s.w('code .')
for i in range(6):
    s.w('run 500')
    s.cmd(INFO)
    s.cmd(0x6C)
    s.w('ir 00 ff 45 ba')
s.w('quiesce 800 9000')
s.w('checkclock 200')
s.end()

# ---- a host that stops asking, mid-keystroke --------------------------------
s = asm_only('z64_no_press_lost_when_the_host_pauses', 'playback')
# The host polls nothing for 70 ms while a macro types, as s_deep_stack's
# host does across three commands: every press still arrives, and none is
# let go without having been pressed.
s.table(('.', [K(c) for c in 'abcdefghij']))
s.eekeys()
s.boot()
s.w('clearkeys')
s.w('code .')
s.w('run 615')                       # into the macro: its second key
s.w('poll 0')
s.w('run 70')
s.w('poll 1')
s.w('quiesce 1000 20000')
s.w('expect_keys ' + ''.join('<00/%02X>' % (4 + i) for i in range(10)))
s.end()

# ---- the frame lock, with the RC off and traffic ------------------------------
# The board's clock off by ppm: frames and bits that many longer in CPU
# cycles. Locked to the keep-alives, the clock moves with the host's time,
# not the CPU's, and its ticks keep step with the frames: none slips, with
# the host polling, asking and sending pages meanwhile.


def lock_traffic(s, seconds, ramp=None):
    """seconds of traffic: an INFO, a read and a half page every 500 ms;
    ramp: (from, to) ppm, stepped through over the middle third"""
    n = int(seconds * 2)
    for i in range(n):
        if ramp and n // 3 <= i < 2 * n // 3:
            f, t = ramp
            s.w('clock_error %d' % round(f + (t - f) * (i - n // 3 + 1) / (n // 3)))
        s.cmd(INFO)
        s.cmd(READ, 0x40, 16)
        s.w('page 0 ' + '5A' * 32)
        s.w('run 470')


def lock_scenario(name, ppm, ramp=None, seconds=30):
    s = asm_only(name, 'clock lock')
    s.blank()
    s.eekeys()
    s.boot()
    s.w('clock_error %d' % ppm)
    settime(s, 1700000000)
    s.w('run 3000')                     # lock acquired
    s.w('watch_lock')
    s.w('expect_hostclock 2')
    lock_traffic(s, seconds, ramp)
    s.w('expect_hostclock 2')
    s.w('expect_noslip')
    s.end()


lock_scenario('z60_lock_holds_3600_ppm_slow', -3600)
lock_scenario('z61_lock_holds_700_ppm_slow', -700)
lock_scenario('z62_lock_holds_1100_ppm_fast', 1100)
lock_scenario('z63_lock_holds_through_a_ramp', -700, ramp=(-3600, -1100), seconds=45)

# ---- a half-received page ---------------------------------------------------

PG = 0x1F00
pagedata = bytes(range(0x40, 0x60))

s = asm_only('z40_half_page_refuses_the_macro', 'totp')
s.table(('.', [K('a')] + TOTP(kc) + [K('z')]))
s.eekeys()
s.boot()
settime(s, 1700000000)
s.w('page %x %s' % (PG, pagedata.hex()))
s.w('expect_ans 0 65')
s.w('expect_eqbytes 62 ' + pagedata.hex())
s.play('.', '<00/04>')
s.w('note the page is intact: its second half now writes exactly what was sent')
s.w('expect_eqbytes 62 ' + pagedata.hex())
s.w('page %x %s' % (PG + 32, bytes(range(0x60, 0x80)).hex()))
s.w('run 400')
s.cmd(0x66, PG, 32)
for i, b in enumerate(pagedata):
    s.w('expect_ans %d %02X' % (4 + i, b))
s.end()

for d in (250, 300, 450, 650, 800, 880, 960):
    s = asm_only('z41_page_arrives_during_totp_%d' % d, 'totp')
    s.table(('.', TOTP(kc)))
    s.eekeys()
    s.boot()
    t = 1700000000
    settime(s, t)
    s.w('clearkeys')
    s.w('code .')
    s.w('run %d' % d)
    s.w('page %x %s' % (PG, pagedata.hex()))
    s.w('quiesce 600 3000')
    s.w('expect_keys_oneof %s|-' % digits_str(hotp(kc, t // 30)))
    s.w('expect_eqbytes 62 ' + pagedata.hex())
    s.end()

# ---- nothing left behind ---------------------------------------------------

def residue_patterns(key, t):
    """every 4-byte window of what the computation held"""
    mi, mo = midstates(key)
    blocks = [struct.pack('>Q', t // 30) + b'\x80' + bytes(53) + struct.pack('>H', 576)]
    inner_w = []
    d1 = sha1_block(mi, blocks[0], inner_w)
    blocks.append(struct.pack('>5I', *d1) + b'\x80' + bytes(41) + struct.pack('>H', 672))
    outer_w = []
    d2 = sha1_block(mo, blocks[1], outer_w)
    arrays = [struct.pack('>5I', *mi), struct.pack('>5I', *mo),
              struct.pack('>5I', *d1), struct.pack('>5I', *d2),
              struct.pack('>16I', *inner_w[-16:]), struct.pack('>16I', *outer_w[-16:])]
    arrays += [a[::-1] for a in arrays[:2]]
    pats = set()
    for a in arrays:
        for i in range(0, len(a) - 3):
            w = a[i:i + 4]
            if len(set(w)) >= 3:
                pats.add(bytes(w).hex())
    code = bytes(int(c) for c in '%06d' % hotp(key, t // 30))
    return sorted(pats), code.hex()


for name, sealed_ in (('plain', False), ('sealed', True)):
    s = asm_only('z50_residue_' + name, 'totp')
    kh = b'residue-test-seed-20'
    body = TOTP(kh) + [K('z')]
    s.table(('.', secret_body([], 1, body) if sealed_ else body))
    s.eekeys()
    s.boot()
    t = 1700000000
    settime(s, t)
    pats, code = residue_patterns(kh, t)
    s.w('watch_wipe')
    s.play('.', digits_str(hotp(kh, t // 30)) + '<00/1D>', wait=1500)
    s.w('expect_wiped')
    s.w('expect_zero 62 40')
    s.w('expect_ram0 totp_s 15')
    s.w('expect_clean ' + ' '.join(pats) + ' ' + code)
    s.end()

s = asm_only('z52_page_arrives_between_blocks', 'totp')
# deterministically: at the corePoll between the two SHA blocks the board
# sees half a page: W must by then hold nothing but zeros, and no code is typed
kh = b'residue-test-seed-20'
s.table(('.', TOTP(kh) + [K('z')]))
s.eekeys()
s.boot()
t = 1700000000
settime(s, t)
s.w('gap_page')
s.w('watch_wipe')
pats, code = residue_patterns(kh, t)
s.play('.', '')
s.w('expect_gap_hit')
s.w('expect_zero 62 40')
s.w('expect_wiped')
s.w('expect_clean ' + ' '.join(pats) + ' ' + code)
s.end()

s = asm_only('z65_wiped_with_an_interrupt_inside', 'totp')
# as z52, with an interrupt certain to come during the stack's wipe: its
# frame, left behind the wipe, holds main's registers and SREG, no secret
s.table(('.', TOTP(kh) + [K('z')]))
s.eekeys()
s.boot()
settime(s, 1700000000)
s.w('watch_wipe')
s.w('wipe_irq')
s.play('.', None)
s.w('expect_wipe_irq')
s.w('expect_wiped')
s.end()

s = asm_only('z53_key_forgotten_at_the_gap', 'totp secret')
# a sealed code whose key is forgotten by a WRITE or RESTART while it runs: the
# outer half would read garbage, so it types nothing rather than a wrong code
s.table(('.', secret_body([], 0, TOTP(ks) + [K('z')])))
s.eekeys()
s.boot()
t = 1700000000
settime(s, t)
s.w('gap_forget')
s.w('watch_wipe')
s.play('.', '')
s.w('expect_gap_hit')
s.w('expect_wiped')
s.end()

s = asm_only('z51_residue_when_stopped_part_way', 'totp')
kh = b'residue-test-seed-20'
s.table(('.', TOTP(kh) + [K('z')]))
s.eekeys()
s.boot()
t = 1700000000
settime(s, t)
pats, code = residue_patterns(kh, t)
s.w('clearkeys')
s.w('code .')
s.w('watch_wipe')
s.w('run 870')
s.w('touch 1')                 # a touch during the digits stops it
s.w('run 200')
s.w('touch 0')
s.w('quiesce 1000')
s.w('expect_zero 62 40')
s.w('expect_ram0 totp_s 15')
s.w('expect_clean ' + ' '.join(pats) + ' ' + code)
s.end()

# ---- deepest stack: a sealed code while USB commands arrive -----------------

s = asm_only('z60_deep_stack_totp', 'stack totp')
kd = b'deep-stack-test-seed'
s.table(('.', secret_body([K('a'), ESC_MOD, 1], 0,
                          [K('b'), ESC_MARK, K('c'), ESC_HOST, 0x20, ESC_REPEAT, 1]
                          + TOTP(kd) + [K('d')] * 3)))
s.eekeys()
s.password(b'abc')
s.lockout(6, 0xFF, 1)
s.boot()
s.hold()
s.cmd(UNLOCK, 0, 3, b'abc')
settime(s, 1700000000)
s.w('clearkeys')
s.w('code .')
for i in range(10):
    s.cmd(PROVE, 0, 8, b'abcdefgh')
    s.cmd(KEYAUTH, 0, 16, SKEY)
    s.cmd(0x6C)
    s.cmd(INFO)
    s.w('ir 00 ff 45 ba')
    s.w('run 150')
s.w('quiesce 800 8000')
s.w('keys')
s.w('stack')
s.end()

# A trimmed clock (tools/settrim.py on the image under test, by run.py): the
# clock gains what the trim says against the host's frames, 1 ms each. A
# losing trim and a gaining one, odd seconds one in 4 and one in 2.
for ppm, want, name in ((500, 1e6 / 1999, 'z41_trim_gains_500_ppm'),
                        (-250, -1e6 / 4001, 'z41b_trim_loses_250_ppm')):
    s = asm_only(name, 'clock')
    s.w('image_trim %d' % ppm)
    s.blank()
    s.eekeys()
    s.boot()
    settime(s, 1700000000)
    s.w('run 1500')
    s.w('expect_hostclock 2 %.3f' % want)
    s.w('run 40000')
    s.w('expect_hostclock 2 %.3f' % want)
    s.end()

# ---- back to the published C app, through the core ---------------------------

# Back to the C app (firmware 17, the reference build: make reference), which
# a board on the assembly app can always be given
s = asm_only('z70_reinstall_c_app', 'install')
s.subs = ['install', 'asmonly']
s.table(('.', [K('a')]))
s.boot()
s.w('reinstall %s' % os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', '..', 'build', 'v2', 'digimacro.hex'))
s.cmd(INFO)
s.w('expect_ans 0 40')
s.w('expect_ans 13 11')
s.w('expect_ansbits 12 1 1')
s.play('.', '<00/04>')
s.end()

# ---- the inventory ------------------------------------------------------

# Sections of the C-bound tests with no differential scenario, and why:
NOT_APPLICABLE = {
    '19': 'only asserts the C stub starts empty: t19_no_password covers the behaviour',
    '23': 'compile-time constants of the C; the asm uses the same addresses, '
          'and every scenario above would fail if one were moved',
    'tp2': 'compile-time parameters (TOUCH_REPS against reading widths)',
    'tp11': 'compile-time constants (TOUCH_STUCK_MS > HOLD_MS)',
    'tp13': 'see p13_pad_left_idle',
}


def source_sections():
    """every numbered section of the C-bound tests"""
    import re
    here = os.path.dirname(os.path.abspath(__file__))
    secs = []
    for line in open(os.path.join(here, '..', 'test.cpp')):
        m = re.match(r'\s*printf\("(\d+[a-z]*\d?)\. ', line)
        if m:
            secs.append(m.group(1))
    for line in open(os.path.join(here, '..', 'touch_test.cpp')):
        m = re.match(r'\s*printf\("(\d+[a-z]?)\. ', line)
        if m:
            secs.append('tp' + m.group(1))
    return secs


def main():
    if len(sys.argv) > 1 and sys.argv[1] == '--inventory':
        covered = {}
        for sc in ALL:
            for x in sc.src.split():
                covered.setdefault(x, []).append(sc.name)
        gaps = 0
        for x in source_sections():
            where = covered.get(x)
            if where:
                print('%-6s %s' % (x, ', '.join(where)))
            elif x in NOT_APPLICABLE:
                print('%-6s n/a: %s' % (x, NOT_APPLICABLE[x]))
            else:
                print('%-6s *** NOT COVERED' % x)
                gaps += 1
        for x in ('pt', 'ct', 'm3', 'webtable'):
            print('%-6s %s' % (x, ', '.join(covered.get(x, ['*** NOT COVERED']))))
        print('%d scenarios, %d gaps' % (len(ALL), gaps))
        return gaps != 0
    out = sys.argv[1]
    os.makedirs(out, exist_ok=True)
    index = {}
    for sc in ALL:
        with open(os.path.join(out, sc.name + '.txt'), 'w') as f:
            f.write('\n'.join(sc.l) + '\n')
        index[sc.name] = {'src': sc.src, 'subs': sc.subs}
    with open(os.path.join(out, 'index.json'), 'w') as f:
        json.dump(index, f, indent=1)
    print('%d scenarios in %s' % (len(ALL), out))


if __name__ == '__main__':
    sys.exit(main())
