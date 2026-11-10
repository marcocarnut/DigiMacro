# Differential test inventory

What each scenario in the C-bound tests (test/test.cpp, test/touch_test.cpp,
test/pointer_test.c, test/hexplay.cpp, core/test/core_test.c as far as it runs
app/hidusb.c) maps to in test/diff/scenarios.py, on the simulated board. The
expected keystrokes in test.cpp are carried over as `expect_*` lines, so a
scenario also has to do what test.cpp asked of the C.

Regenerate the table with `python3 test/diff/scenarios.py --inventory`.

## Covered only in part, or not at all, and why

| What | Status | Why |
|---|---|---|
| Build options BUTTON=1, RC=1, LEDECHO=1, TOUCHDEBUG=1 (`make` flags) | not covered | the assembly is the stock firmware only; the C keeps the options |
| TOUCH_PULLUP=0 (bleed-resistor method), touch_test.cpp's second build | not covered | same: a build option the assembly does not have |
| Serial from entropy (c11) | properties only | values depend on the exact cycle of each TCNT0 sample, which the two images do not share: checked that a serial is made, is neither all 0 nor all 1, and is kept |
| tp13, pad left idle between readings | partly | a sample can land inside a charge cycle, so only DDRB/PORTB's other bits are compared; touch_once itself is cycle-identical to the C's |
| test.cpp 19, 23; touch_test.cpp tp2, tp11 | n/a | assertions on compile-time constants or on the stub's start state |
| Real USB hosts, real pad, real IR receiver, EEPROM write timing, OSCCAL | not covered | simulator only: no hardware in this exercise. simavr's EEPROM writes are instant |
| hexplay's secret round trip through the C (run.sh) | through w01 and secret.c | run.sh still does it for the C; `make simtest` does it for the assembly image |

Timing: LED edges and HID reports are compared by the gap from the previous
event of their stream (the first of each by absolute time), within 20 ms (the host polls every 10 ms); the app's
clock within 4 ms; everything else by value only.

## Phase 3: {totp} and the clock (assembly app only)

The `z*` scenarios run on the assembly image alone and are checked against
Python's hmac/hashlib: RFC 4226 and 6238 vectors (the 2e10 s vector cannot be
held by the 32-bit clock), random keys of lengths 1-70, step boundaries,
leading zeros, the page's own compile path (`totp_e2e.py`), the gates, the
clock under load and across 16-bit wraps, half a page arriving before and during
a code, a forgotten key, nothing left in RAM or on the stack (pattern search and
a check at the instant the wipe ends), and reinstalling the published C app.

Deliberate differences from the C app when the table holds no {totp}: INFO's
[13] is 18, not 17; verb 0x6C exists; KEYAUTH also accepts n = 20. In the C
comparison INFO's [13] is masked; nothing else is exempted.

## Mapping

Section numbers are test.cpp's own; `tpN` are touch_test.cpp's, `pt`
pointer_test.c, `ct` core_test.c's app-library parts, `m3` the harness's
m3_digimacro, `webtable` web/table.js against the firmware.

```
1      t01_virgin
2      t02_length_is_part_of_code
3      t02_length_is_part_of_code
4      t04_shift
5      t05_mod
5b     t05_mod
6      t06_repeat
6b     t06b_gui_alone
7      t07_pause
8      t08_zero_arg
9      t09_ir_tag, t09_ir_tag_short
9b     t09b_pointer_escapes, t09b_host_stops, t09b_scale_escapes
10     t10_walk_bounded, t10b_end_of_table_byte, t10c_end_of_table_byte_high
11     t11_play_bounded
11b    t11b_escape_last_byte
12     t12_too_many_symbols, t12b_five_symbols
13     t13_tapping, t13c_dot_dash_boundary, t13d_end_of_code_timing
13b    t13b_dropout
14     t14_hold, t14c_hold_almost
14b    t14_hold
15     t15_window_closes
16     t16_groups, t16d_group_pause_inside
16b    t16b_group_carries_on, t16e_mark_forgotten_after_group
16c    t16c_mark_harmless
17     t17_power_on
17a    t17a_settle, t17e_not_enumerated
17b2   t17b_never_says_hello
17b    t17b_once
17c    t17c_nothing_set, t17f_power_on_impossible_header
17d    t17d_dangling
18     t18_jump, t18i_jump_clears_group_mod
18b    t18b_jump_absent
18c    t18c_self_jump_touch_stops
18d    t18c_self_jump_touch_stops
18e    t18c_self_jump_touch_stops
18f    t18f_jump_loop_bounded, t18f2_hundred_jumps, t18f3_long_chain_is_allowed
18g    t18g_long_macro_window
18h    t18h_repeat_stoppable
19     t19_no_password
19b    t19b_setpass_gates, t19b_unlock_rules
19b2   t19b_setpass_gates, t19b_unlock_rules, t19b2_second_after_wrong
19c    t19b_setpass_gates
19d    t19d_password_plays_anyway
21     t21_no_lockout_erased, t21_count_saturates, t21_miss_before_blink
21b    t21b_soft_lock
21c    t21b_soft_lock
21d    t21d_replug_restarts_wait, t21d_replug_real
21e    t21e_hard_lock
21f    t21e_hard_lock, t21f_hard_stops_power_on
21g    t21e_hard_lock
21h    t21h_wrong_password_counts
21i    t21i_wait_nonsense_0, t21i_wait_nonsense_241, t21i_wait_nonsense_ff, t21i_forty_minutes
21j    t21j_no_hard_threshold
21j2   t21j2_clear
21k    t21k_new_password_clean_slate
22     t22_no_key, t22_no_check_value
22b    t22b_right_key
22c    t22b_right_key, t22c_key_wait_setting, t22c_forget_on_write
22d    t22d_wrong_key, t22d2_pause_ends_by_loop
22e    t22e_loud_refusal
22e2   t22e2_led_dark_on_loud
22f    t22f_secret_keeps_misses
22g    t22g_jumps_and_secrets
22h    t22h_everything_inside_secret
22i    t22i_key_runs_out
22j    t22j_prove
22k    t22k_host_refused
23     n/a: compile-time constants of the C; the asm uses the same addresses, and every scenario above would fail if one were moved
24     t24_remote, t24_remote_noise, t24_lg_remote, t24_ir_walk_knows_args, t24_timing_edges
24b    t24b_remote_and_lockout
24c    t24c_learn, t24c_hex_digits
tp1    p01_reading_is_max_of_groups, p15_four_groups_exactly
tp2    n/a: compile-time parameters (TOUCH_REPS against reading widths)
tp3    p02_saturates
tp4    p04_average_settles, p14_touch_at_setup_seeds
tp5    p05_finger_at_power_up
tp6    p06_threshold
tp7    p07_average_ignores_finger
tp8    p08_drift_follows
tp9    p09_stuck_touch
tp10   p10_release
tp11   n/a: compile-time constants (TOUCH_STUCK_MS > HOLD_MS)
tp12   p12_slow_press
tp12b  p12_slow_press
tp13   p13_pad_left_idle
pt     m01_edges_diagonal_scale, m02_buttons
ct     c01_info, c02_read_write_gates, c03_read_clamps, c04_write_clamps, c05_refused_wipes, c06_update, c07_restart, c08_window_clock_wipes, c09_setpass_session, c10_window_tenths, c11_serial_made_once, c12_serial_kept, c14_macro_during_commands, c15_unknown_commands
m3     c11_serial_made_once, c13_app_clock
webtable w01_webtable_codes
177 scenarios, 0 gaps
```
