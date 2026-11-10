#!/usr/bin/env python3
"""
Flash by subsystem, from each image's own symbols: the C, the straightforward
assembly and the tuned assembly.

    sizemap.py nm C.elf straight.elf tuned.elf

A symbol's size is the distance to the next one in the code. The C's lines are
blurred by -flto: functions the compiler inlined are counted in whatever they
went into (app_loop holds handle_input, update_led and autoexec_run;
app_request holds hidKeyAuth and hidProve; play_header holds macro_play).
"""
import re, subprocess, sys

# label -> subsystem; anything unlisted is reported as "other"
GROUPS = {
    'header': '__vectors __app_reti __app_init',
    'clock/timer ISR': '__vector_3 millis t1tenth t1out',
    'eeprom/time helpers': 'eeprom_read_byte eeprom_update_byte eeread eeupdate ee1 ee eeput1 eeput '
                           'since stamp due10s zero waitval no one',
    'C runtime (prologues, bss)': '__prologue_saves__ __epilogue_restores__ __do_clear_bss',
    'window': 'hidAllowed hidAllow wipe',
    'setup + serial': 'app_setup hidBegin entropy entropy_step',
    'typing (reports, hex)': 'sendRaw sendReport hidKey send type_nib type_hex _ZL8type_nibh _ZL8type_hexh',
    'mouse': 'hidPoint ptrStep ptrSend',
    'requests': 'app_request reqRefuse reqRefuseKept reqDone reqOk reqOut reqRead info hidLearn',
    'lockout': 'hidLockState _ZL9lock_missv _ZL8count_upPh _ZL9lock_softv _ZL10lock_clearv '
               'lock_miss count_up lock_soft lock_clear lock_poll count_of count_put count_clr '
               'hidPassWrong hidPassRight hidPassChanged',
    'secrets (not xtea)': '_ZL6key_okv _ZL11secret_bytePh hidKeyForget key_ok secret_byte nextb hidKeyAuth '
                          'hidProve pause_over hidKeyState',
    'xtea': 'xtea half',
    'clocks_poll': '_ZL11clocks_pollv clocks_poll',
    'touch': '_Z10touch_pollv _Z10touch_readv _ZL10touch_seedh touch_poll touch_read touch_seed '
             'touch_once touch_seed2',
    'tapping + main loop': 'app_loop handle_input code_reset _Z10code_resetv update_led led_set led_on led_off '
                           'autoexec_run code_finish blink wait_poll wait70 _Z9wait_pollj.constprop.2',
    'playback': '_ZL11play_headerh _Z11find_headerh _ZL7touchedv touched find_header found not_found '
                'macro_play play_header play_loop play_end1 play_stop key loop stop stop2 stop3 escape '
                'jump host pause_poll pause_again hidPoll _ZL7hidPollv _ZL8touchedv',
    'remote (ISR, poll)': '__vector_1 int0 ir_poll',
    'totp': 'totp rolN acc compress rnd rd20 rdS addM wFill totpTypeZeros totpDigits '
            'totpStopped totpGiveUp totpWipe K P10 totp_gap totp_wiped',
    'clock': 'clock_poll clock_set',
    'clock readback': 'clock_read',
}
LOOKUP = {name: group for group, names in GROUPS.items() for name in names.split()}


def sizes(nm, elf):
    out = subprocess.check_output([nm, '-n', elf], universal_newlines=True)
    syms = []
    for line in out.splitlines():
        p = line.split()
        if len(p) == 3 and p[1] in 'tTwW' and int(p[0], 16) >= 0xF80 and int(p[0], 16) < 0x2000:
            syms.append((int(p[0], 16), p[2]))
    end = int(re.search(r'^([0-9a-f]+) \w __data_load_end$', out, re.M).group(1), 16)
    syms.sort()
    # distinct addresses: weak vector aliases collapse into the first
    merged = []
    for a, n in syms:
        if merged and merged[-1][0] == a:
            continue
        merged.append((a, n))
    res = {}
    for i, (a, n) in enumerate(merged):
        nxt = merged[i + 1][0] if i + 1 < len(merged) else end
        g = LOOKUP.get(n, 'other')
        res[g] = res.get(g, 0) + nxt - a
    return res, end - 0xF80


def main():
    nm, c, st, tu = sys.argv[1:5]
    rows = [sizes(nm, x) for x in (c, st, tu)]
    groups = sorted(set().union(*[r[0] for r in rows]))
    print('%-30s %6s %8s %6s' % ('', 'C', 'straight', 'tuned'))
    for g in groups:
        print('%-30s %6d %8d %6d' % (g, *[r[0].get(g, 0) for r in rows]))
    print('%-30s %6d %8d %6d' % ('total (no CRC)', *[r[1] for r in rows]))


if __name__ == '__main__':
    main()
