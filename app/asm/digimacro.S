/*
 * DigiMacro's firmware, in hand-written AVR assembly
 * ==================================================
 *
 * WHAT IT DOES. A Digispark (an ATtiny85 at 16.5 MHz, no crystal) that is
 * a USB keyboard and mouse. Tap a code in dots and dashes on the touch pad
 * on P0, or press a remote's button at the IR receiver on P2, and it types
 * the macro its table in EEPROM holds for that code. Hold the pad four
 * seconds and the web page may read and write the table for half a minute
 * (the window). On top of what the C app did: a clock, locked to the
 * computer's USB frames, and {totp SECRET}, which types the six digits of
 * a one-time code.
 *
 * WHERE IT CAME FROM. A translation of the C app -- DigiMacro.ino, with
 * app/hidusb.c and app/xtea.S -- which was firmware 17 and is kept as the
 * reference: test/diff runs both on the simulated board and holds this to
 * the same EEPROM, protocol, header and CRC. app/asm/straight.S is the
 * line-by-line translation; this is the same program tuned by hand, which
 * is where the room for the clock and {totp} came from. Its version, INFO's
 * [13], is APP_VER, from the Makefile (APPVER_ASM). Each routine below
 * names its C counterpart where it has one. The C's comments give the long
 * reasons -- why a debounce filters, why a miss is counted before the
 * blinks -- and are not all repeated here: this says how, and what is new.
 *
 * WHERE IT RUNS. The flash is split at APP_START (core/layout.h, 0x0F80).
 * Below it, the core: V-USB, the descriptors, the password check, the page
 * writer. Above it, this. They meet only through core/layout.h: the core
 * calls the first three slots of the header below; this calls the core
 * through the rjmps of its table, which app/coreapi.S names (corePoll,
 * hidSend, hidReport ...). No C runtime is linked: app_setup clears the
 * variables itself, and there is no .data.
 *
 * ---- the file, in order ---------------------------------------------
 *
 *   defines        flags, EEPROM addresses, the frame lock's constants
 *   variables      all at V; the RAM map is below, the list after the
 *                  defines
 *   the header     the core's three entry points, the fourteen interrupt
 *                  slots -- holding the tick's and the comparator's code
 *                  in slots of interrupts never enabled -- then the magic,
 *                  length, version and API the core checks
 *   interrupts     acmore (ANA_COMP: the frame lock), t1more (TIMER1_COMPA:
 *                  the millisecond tick). int0, the remote's, is further on
 *   small things   ee, eeput, since, stamp, due10s, zero, waitval
 *   the window     wipe, hidAllowed
 *   typing         send, hidKey, sendReport, the mouse (hidPoint)
 *   the lockout    hidLockState, lock_miss, lock_clear, lock_poll ...
 *   secrets        the key: hidKeyForget, key_ok, hidKeyAuth, hidProve
 *   the clocks     clock_poll (seconds, CLOCK_TRIM), clock_set,
 *                  clock_read, probe_poll, clocks_poll
 *   XTEA           xtea, half
 *   requests       app_request: the protocol (README, "The protocol")
 *   touch          touch_read (cycle-counted), touch_poll, touch_seed
 *   tapping        code_reset, handle_input: dots, dashes, the hold
 *   playback       find_header, touched, hidPoll, pause_poll, nextb,
 *                  secret_byte, {totp} (SHA-1, HMAC, the six digits),
 *                  macro_play (the byte-code), play_header, code_finish
 *   the remote     int0 (the NEC decoder), type_nib, type_hex, ir_poll
 *   the rest       update_led, app_setup, app_loop
 *
 * ---- how it runs ----------------------------------------------------
 *
 * The core's main() turns interrupts on, calls APP_SETUP once, before USB
 * is connected, and then corePoll and APP_LOOP in turn, for ever.
 * app_loop does one round of everything and returns. The only long stays
 * are a macro playing and the waits inside it, and they keep everything
 * alive themselves: USB by calling corePoll (in send's retry loop, and in
 * hidPoll), and the clocks and the pad by calling touched between one byte
 * of the macro and the next. The core resets the watchdog in corePoll and
 * wants it every second or so (core/core.h).
 *
 * corePoll runs V-USB's usbPoll, which may call APP_REQUEST: app_request
 * runs in the main context, but inside whatever called corePoll -- the
 * middle of a macro, a send or a pause. So it leaves r2-r17 and Y as it
 * found them; and every caller of corePoll (and of hidPoll, send, hidKey,
 * pause_poll and the rest that reach it) must expect r18-r27, r30, r31
 * and r0 changed, as any C call changes them, and RAM changed as a request
 * may change it: the key forgotten, the clock set, the window shut. A page
 * of a firmware update never comes back: the first half sets GPIOR0's bit
 * 5, the second takes the board for good.
 *
 * The interrupts, in the chip's order of priority:
 *
 *   PCINT0         V-USB's: the core's own, never seen here. It must start
 *                  within a few cycles of the bus moving, so each of the
 *                  interrupts below turns interrupts back on as its first
 *                  instruction, or its second -- about ten cycles from the
 *                  event, with the chip's vector and the core's forwarding
 *                  rjmp (core/coretab.S) -- and saves nothing it does not
 *                  use.
 *   INT0           the remote: int0. SREG in r5. Owns IR_LEFT, IR_BITSL,
 *                  IR_BITSH, ir_addr and ir_cmd, and sets FLAG_IR.
 *   TIMER1_COMPA   the tick, at count 1 of every millisecond: t1more. SREG
 *                  in r4. Owns r2:r3 and r6, counts DM_WINDOW, writes
 *                  OCR1C and PLL_FRAC, counts PLL_MISS, clears F2_PLL and
 *                  F2_HIT.
 *   ANA_COMP       D- fell: the frame lock, acmore. SREG on the stack.
 *                  Moves OCR1C and pll_p, sets PLL_MISS, F2_PLL and F2_HIT.
 *
 * None of them can use Y: they may land inside the core, with Y the core's.
 * So their state is in registers nothing else writes (r2-r6), in spare I/O
 * registers one IN or OUT away, and in RAM by absolute address. None may
 * nest inside itself. The comparator's masks itself while it runs; the
 * tick's and the remote's keep SREG in a single register each and rely on
 * their events being far apart -- a millisecond; 1.1 ms between an NEC
 * frame's falling edges -- next to their few dozen cycles.
 *
 * ---- the registers --------------------------------------------------
 *
 * Global, for the whole program. The core never touches r2-r11, and its
 * main() holds nothing in r2-r17 or Y across APP_SETUP and APP_LOOP
 * (core/layout.h, which tools/checkcore.py checks at every core build), so
 * these keep their values from one app_loop to the next:
 *
 *   r0        scratch, as in the C ABI
 *   r1        zero, as in the core and the C ABI
 *   r2:r3     the milliseconds: sixteen bits, wrapping every 65.5 s.
 *             Written only by the tick's interrupt. Read with one movw,
 *             which no interrupt can split (since, stamp): two reads could
 *             straddle the tick that carries r2 into r3. A single bit or
 *             byte may be read alone (the LED's blinks; r3 for LEARN, in
 *             steps of 256 ms). The harness sees them as `ms`
 *   r4        SREG, saved by the tick's interrupt
 *   r5        SREG, saved by the remote's interrupt
 *   r6        the tick's countdown to the next tenth of a second, 100..1
 *   r7        macro_play: jumps since the last keystroke (JUMP_MAX)
 *   r8:r9     macro_play: the mark, where a repeated group starts, or 0
 *   r10:r11   send: the report it is sending, kept across hidSend and
 *             corePoll. r11 alone is a counter where nothing sends:
 *             pause_poll's tenths, code_finish's blinks, rd20's end
 *   r12       macro_play: modifiers latched for the next keystroke
 *   r13       macro_play: the last key, for a repeat
 *   r14       macro_play: the last key's modifiers
 *   r15       macro_play: times round a marked group still to go, or a
 *             plain repeat's count. totpDigits borrows it, saved
 *   r16:r17   macro_play: a, the address of the macro's next byte
 *   Y         V, the variables' base: ldd and std reach the first 64
 *             bytes of them as Y + Q(name) in one word. Set in app_setup
 *             and never moved for long: the core's C keeps it, being
 *             callee-saved, and main() holds nothing in it. app_request
 *             sets it again, since it runs inside the core; xtea borrows
 *             it for the key, and puts it back
 *
 * xtea needs eleven registers more than the C ABI gives it: it saves r7-r17
 * on the stack, uses them, and puts them back. r2-r6 it cannot borrow that
 * way, since the interrupts use them while it runs.
 *
 * Elsewhere, the C convention, more or less: arguments in r24, r22, r20,
 * results in r24 (r25:r24 for an address). But most routines here change
 * less than the C ABI would let them, and their callers count on it: the
 * header of each says what it takes, gives and changes. A few answer in the
 * zero or carry flag, which their callers branch on at once.
 *
 * The core's accessors hidPassSet, hidUnlocked, hidReport, hidLock,
 * hidReady and hidAwake change only r24, r25 and the flags (layout.h,
 * checked by tools/checkcore.py), so callers keep things in r18-r23 and X
 * across them. corePoll, hidSend, hidUnlock and hidRestart are plain C.
 *
 * ---- I/O registers kept as state ------------------------------------
 *
 * Bits and bytes the interrupts share with the main program, kept in I/O
 * registers below 0x20, where SBI, CBI, SBIC and SBIS reach them: one
 * instruction sets or clears one bit, and no interrupt can land inside it
 * (on the ATtiny85, SBI and CBI touch only their own bit). Each belongs to
 * hardware this program leaves off, so nothing but this program changes
 * it, and the core leaves them alone (layout.h, checkcore.py):
 *
 *   GPIOR1   FLAGS: the C's flags, the same eight (below)
 *   USIDR    F2: eight more. The USI is off (USICR is 0): nothing shifts it
 *   OCR0A    IR_LEFT; OCR0B IR_BITSL; OCR1B IR_BITSH: the remote's decoder.
 *            Timer0 runs for it, but neither of its compares, nor Timer1's
 *            B, is ever enabled or connected to a pin
 *   DT1A     PLL_FRAC; DT1B PLL_MISS: the frame lock's. Timer1's dead-time
 *            registers, which only its PWM modes use
 *   GPIOR0   the core's flags, only read here: bit 5, PAGE_HALF, says the
 *            page buffer holds half a page
 *   GPIOR2   DM_WINDOW, the update window in tenths (layout.h): zeroed by
 *            the core at boot and by UPDATE, counted up to 0xFF by the tick
 *
 * Confirmed plain read and write on hardware, in this configuration, as
 * the datasheet has them.
 *
 * ---- RAM ------------------------------------------------------------
 *
 *   0x060    the core's variables. In them, at 0x062, its page buffer,
 *            64 bytes, which {totp} borrows for SHA-1's schedule (see W)
 *   0x150    V, this program's variables, from APP_RAM (core/layout.h):
 *            listed after the defines, 0x72 bytes in the stock build
 *   ...      free, then the stack, shared with the core, from 0x25F down:
 *            73 bytes at the deepest scenario on the simulated board
 *            (git show b37194a:PROGRESS.md, before the frame lock), plus what an
 *            interrupt or two stacks on top
 *
 * All the variables are in one 256-byte page, which since and stamp rely
 * on: they take a variable's low byte in r30 and put hi8(V) in r31.
 */
/*
 * The file goes through the C preprocessor (it is a .S), so #define and
 * #if work as in C, and the I/O names come from avr/io.h. __SFR_OFFSET 0
 * makes them I/O addresses, as IN, OUT, SBI and CBI want; LDS and STS of
 * one would need 0x20 added.
 */
#define __SFR_OFFSET 0
#include <avr/io.h>
#include "layout.h"

/* The C's flags (DigiMacro.ino, "Flags"), by bit number rather than mask,
   in GPIOR1. Each is set and cleared by one SBI or CBI, so the remote's
   interrupt setting FLAG_IR can never be lost in another's update:
     0 CODE_OPEN   symbols tapped, waiting for the silence
     1 CODE_OVER   more symbols than any code can have
     2 CONSUMED    this press was a hold or a stop: no symbol
     3 AUTODONE    the power-on macro has had its turn
     4 AUTOWAIT    ...and is waiting for the host to settle
     5 HELD        the hold has already fired for this press
     6 IR          int0 has a button in ir_addr, ir_cmd
     7 LEARN       ...type the next one rather than play it */
#define FLAGS           GPIOR1
#define FLAG_CODE_OPEN  0
#define FLAG_CODE_OVER  1
#define FLAG_CONSUMED   2
#define FLAG_AUTODONE   3
#define FLAG_AUTOWAIT   4
#define FLAG_HELD       5
#define FLAG_IR         6
#define FLAG_LEARN      7

/* A second flag register: USIDR, which nothing shifts with the USI off.
   F2_OPEN is hidusb.c's allowOpen, F2_RESTART its restartWanted, F2_PAUSING
   DigiMacro.ino's key_pausing; the rest are new */
#define F2              USIDR
#define F2_OPEN         0       /* the window */
#define F2_RESTART      1       /* RESTART asked */
#define F2_PAUSING      2       /* a wrong key's second */
#define F2_CLOCK        3       /* the clock has been set: lost at a replug */
#define F2_PLL          4       /* this tick has had its edge */
#define F2_HIT          5       /* ...and it was the keep-alive, where due */

/* The remote's interrupt keeps its state in spare I/O registers: one word
   to reach, and it cannot use Y. Timer0's compares are never enabled.
   IR_LEFT is the C's ir_left, IR_BITSH:IR_BITSL its ir_bits */
#define IR_LEFT         OCR0A
#define IR_BITSL        OCR0B
#define IR_BITSH        OCR1B

/* The pins: PB0 the pad, PB1 the Digispark's LED, PB2 (INT0) an IR
   receiver's output. PB3 and PB4 are USB's D- and D+ */
#define PIN_TOUCH       0
#define PIN_LED         1
#define PIN_IR          2

/* The EEPROM (README, "The EEPROM"). The table is 0x000-0x1DF; the rest
   are the reserved bytes above it, given by their low byte: all are in the
   page at 0x100, which ee1 and eeput1 supply:
     TABLE_TOP     0x1E0, one past the table's last byte
     LOCK_SOFT     wrong codes before a soft lock; 0 or 0xFF, no lockout
     LOCK_HARD     soft locks before a hard one; 0 or 0xFF, never
     LOCK_WAIT     a soft lock's length, in tens of seconds
     LOCK_MISSES   wrong codes so far      (the three counts are stored
     LOCK_SOFTS    soft locks so far        inverted, so that an erased
     LOCK_CLEARED  hard locks lifted, ever  byte is 0: count_of)
     KEY_WAIT      how long the key is held, in tens of seconds
     AUTO_ADDR     the power-on macro's header
     PASS_ADDR     the password, twelve bytes (the core checks it)
     CHECK_AT      the secrets' check value, eight bytes */
#define TABLE_TOP       0x1E0
#define LOCK_SOFT       0xEC    /* EEPROM, low bytes: all in the page at 0x100 */
#define LOCK_HARD       0xED
#define LOCK_WAIT       0xEE
#define LOCK_MISSES     0xEF
#define LOCK_SOFTS      0xF0
#define LOCK_CLEARED    0xF1
#define KEY_WAIT        0xF2
#define AUTO_ADDR       0xF3
#define PASS_ADDR       0xF4
#define CHECK_AT        0xE4

/* ST_*: INFO's state bits (app/hidusb.h DM_ST_*). HOST_LOUD: what
   macro_play returns for "refused, as if the code were not there".
   REFUSED: core.h's DM_REFUSED, put in place of the verb. IR_WANT: asks
   find_header for the remote's button rather than a header, which none
   can be. TOTP_MARK: the byte after 0x80 that makes it {totp}, not {IR} */
#define ST_SOFT         0x08
#define ST_HARD         0x10
#define ST_KEY          0x80
#define HOST_LOUD       2
#define REFUSED         0xEE
#define IR_WANT         0xFF
#define TOTP_MARK       0x7F    /* after 0x80: no {IR} byte lacks bit 7 */

/* W, TOTP's 64 bytes of message schedule, borrowed from the core's page
   buffer (tools/checkcore.py checks its address): idle unless a page is
   half received, which GPIOR0's bit 5 says. It is in RAM's first page
   (0x62-0xA1), so its address fits a byte and the high byte is 0; but it
   is not 64-aligned, so a wrap within it is done on the offset (compress).
   Only usbPoll writes the page buffer, and only from corePoll: never from
   an interrupt. So W is safe from the moment PAGE_HALF is seen clear
   until the next corePoll, and {totp} zeroes it before that one */
#define W               CORE_SPARE_RAM
#define PAGE_HALF       5

/* The clock's seconds: 1000 ticks of Timer1, locked to the host's 1 ms
   frames. The comparator sees every keep-alive on D- (and the edges of
   packets). The lock keeps a period, pll_p (OCR1C in 8.8: Timer1's
   interrupt writes each tick's length from it, carrying the fraction in
   PLL_FRAC), and aims each keep-alive PLL_T counts into its tick:
   - Locked: only the first edge within PLL_W counts of PLL_T counts. It
     moves this tick by half its error and the period by a sixteenth of
     that half: a PI loop. Any other edge is a packet's, and ignored; a
     tick with none runs on the period, so the phase holds.
   - PLL_MISS counts the ticks since the last such edge. From PLL_LOST the
     lock is lost: the next edge anywhere is tried as the keep-alive, by
     moving the tick so that it falls at PLL_T, with two ticks to show it.
   -DNOPLL leaves the comparator off (the period then runs as set); -DPROBE
   counts what the lock does for tools/probe.py

   In numbers (README, "The clock", says why):

   The tick. Timer1 counts CK/128: 16.5 MHz / 128 = 128,906 counts a
   second, 7.76 us a count. In CTC1 mode it counts 0..OCR1C and clears, so
   a tick is OCR1C + 1 counts, and TIMER1_COMPA (OCR1A = 1) fires once a
   tick, one count after it starts. A true millisecond is 128.906 counts,
   OCR1C 127.906 -- which no byte holds. So the period is kept to 1/256 of
   a count, as pll_p: the high byte is OCR1C, the low byte a fraction that
   the tick adds into PLL_FRAC, and a carry out of PLL_FRAC makes that tick
   one count longer. Ticks of 128 and 129 counts in the right proportion
   average to the period exactly. app_setup starts it at 128.0, 129 counts,
   1.00072727 ms; the lock brings it to the host's, near 0x7FE8, and the
   period then follows the chip's RC oscillator as it warms and cools.

   The edge. A low-speed host sends a keep-alive at the start of every 1 ms
   frame: an end-of-packet, both lines low for about 1.3 us. The idle bus
   has D- high, so D- falls; the comparator, bandgap (1.1 V) against ADC3
   (D-, PB3), sees its output rise and interrupts. V-USB's interrupt, on
   D+, does not fire for a keep-alive, so the comparator's latency then is
   only the chip's: a dozen cycles, against 128 a count. Packets come
   later in the frame, and every bit of them that takes D- low is an edge
   too; those are what the window and "only the first" keep out.

   The error. acmore reads TCNT1 and takes PLL_T from it: e, in counts,
   positive if the edge came late. PLL_T 64 puts the keep-alive half way
   through its tick, as far as it can be from the tick's own interrupt at
   count 1 and from the clear at OCR1C.

   Locked, |e| <= PLL_W. This tick: OCR1C += e/2 (ASR: rounded down), so
   the tick ends that much later or sooner, and the next keep-alive lands
   half as far from PLL_T -- the proportional part. The period: pll_p +=
   (e/2) * 16, in 1/256 counts, a sixteenth of a count for each count of
   e/2 -- the integral part, which learns the frames' length so that the
   phase need not keep being pulled. e/2 is -2..1 at PLL_W 3; the code
   that makes (e/2) * 16 with a SWAP holds while it fits a nibble, up to
   PLL_W 15. What keeps the count right is the phase, not the period: while
   every keep-alive stays within PLL_W of PLL_T, no tick is gained or lost
   against the host's frames, whatever the period is doing. The period is
   a flywheel, for the ticks with no keep-alive seen.

   Lost, PLL_MISS >= PLL_LOST: 32 ticks with no edge in the window. The
   first edge of a tick, anywhere in it, is tried: OCR1C += e, which ends
   this tick a period less PLL_T counts after the edge, so that the next
   frame's keep-alive, a millisecond on, falls PLL_T into the next tick.
   PLL_MISS is set to PLL_LOST - 2: if the next two ticks see a keep-alive
   in the window, it is locked again; if not, the try was a packet's edge,
   and another is tried. A try makes one tick from about half to one and a
   half milliseconds long: up to half a millisecond on the clock, once.

   With no frames at all, as while the host is suspended, the ticks run on
   the period last learnt; tools/probe.py (make PROBE=1) shows all this. */
/* PLL_T: counts into its tick a keep-alive aims at. PLL_W: counts
   either side of it, the window. PLL_LOST: ticks with no keep-alive
   before the lock is lost. PLL_MISS: ticks since a keep-alive, up to 127.
   ACSR_ON: the comparator as app_setup starts it and acmore leaves it:
   the bandgap on its + input (ACBG), its interrupt on (ACIE), on the
   output's rising edge (ACIS1, ACIS0), which is D- falling below 1.1 V */
#define PLL_T           64
#define PLL_W           3
#define PLL_LOST        32
#define PLL_FRAC        DT1A    /* Timer1's dead time: unused, as no PWM */
#define PLL_MISS        DT1B
#define ACSR_ON         ((1 << ACBG) | (1 << ACIE) | (1 << ACIS1) | (1 << ACIS0))

/* ------------------------------------------------------------------ */
/* The variables                                                      */
/* ------------------------------------------------------------------ */

/*
 * All of them, at V (0x150, APP_RAM: the link puts .bss there). Nothing
 * clears them but app_setup, which zeroes the lot. Every time below is a
 * 16-bit stamp of r2:r3, compared as a difference (since), so none may go
 * unlooked-at for 65 s; clocks_poll sees to the ones that matter.
 *
 * The first 64 bytes are reached as Y + Q(name), one word each; from
 * totp_s on, by address, as ldd's reach ends at 63 (the assembler refuses
 * more). The .global names are for the tests (test/diff), which find the
 * C's variables by the same names in both images: _ZL3sec is how C++
 * mangles DigiMacro.ino's static `sec`.
 *
 * Addresses are the stock build's; PROBE=1 adds its 15 bytes before pll_p.
 */
    .section .bss
    .global V, _ZL3sec, touch_state, touch_base, touch_since, touch_moved
    .global code_bits, code_len, in_down, in_seen
V:
/* The window and RESTART (hidusb.c). allowUntil (0x150): when the window
   shuts, while F2_OPEN. restartAsked (152): when RESTART came, while
   F2_RESTART. sendT (154): when send began trying; also where pause_poll's
   tenth and wait70's wait began, which never overlap a send */
allowUntil:     .space 2
restartAsked:   .space 2
sendT:          .space 2
/* The reports, as hidSend takes them: kbd (156) id 1, modifiers, 0, the
   key, 0, 0; ptr (15C) id 3, buttons, dx, dy. app_setup puts the ids in */
kbd:            .space 6
ptr:            .space 4
/* The mouse (hidPoint): where the cursor is, ptrX (160) and ptrY (161),
   in the page's steps; ptrToX (162), where {x} said x is to go; ptrS
   (163), the next move's counts a report, x and y, xor 4, so 0 is 4 */
ptrX:           .space 1
ptrY:           .space 1
ptrToX:         .space 1
ptrS:           .space 2
/* The input, debounced (handle_input): in_down (165) the input once
   believed, in_seen (166) what it has read lately, in_since (167) since
   when; press_start (169) when this press began */
in_down:        .space 1
in_seen:        .space 1
in_since:       .space 2
press_start:    .space 2
/* The soft lock's clock (lock_poll): lock_left (16B) tens of seconds
   still to wait, 0 when not counting; lock_tick (16C) when this ten
   began */
lock_left:      .space 1
lock_tick:      .space 2
/* The secrets' key (key_ok, hidKeyAuth): key_left (16E) tens of seconds
   it is held for, 0 for no key; key_tick (16F) when this ten began;
   key_failed (171) when a wrong key came, while F2_PAUSING. sec_at (173):
   where the playing {secret}'s first encrypted byte, c0, is; 0 while the
   macro is in the clear */
key_left:       .space 1
key_tick:       .space 2
key_failed:     .space 2
sec_at:         .space 2
/* The pad (touch_poll): touch_state (175) 1 for a finger; touch_base
   (176) the running average, x16; touch_since (178) when the finger
   came; touch_moved (17A) when the average last moved */
touch_state:    .space 1
touch_base:     .space 2
touch_since:    .space 2
touch_moved:    .space 2
/* The code being tapped (handle_input, code_finish): last_release (17C)
   when the last symbol ended; code_bits (17E) its symbols, a dash 1, the
   first highest; code_len (17F) how many */
last_release:   .space 2
code_bits:      .space 1
code_len:       .space 1
/* ir_key (180): the remote's button as its {IR} holds it, for
   find_header(IR_WANT). auto_since (183): when the host enumerated, which
   the power-on macro waits from. ir_addr, ir_cmd (185, 186): the last good
   button, written by int0 (FLAG_IR). learn_at (187): r3 when LEARN came,
   in steps of 256 ms */
ir_key:         .space 3
auto_since:     .space 2
ir_addr:        .space 1
ir_cmd:         .space 1
learn_at:       .space 1
/* The clock (clock_poll): clk_secs (188) seconds since 1970, little-
   endian; clk_t (18C) the tick its second began at; clk_spare (18E),
   with CLOCK_TRIM, the trim's countdown of seconds, and unused without.
   totp_s (18F), {totp}'s working state, wiped after every code
   (totpWipe): [1..20] SHA-1's a, b, c, d, e, big-endian words, and then
   the digest; [1..6] the six digits, 0 to 9; [0] scratch, the count of
   millions, thrown away */
clk_secs:       .space 4
clk_t:          .space 2
clk_spare:      .space 1            /* was clk_frac: keeps totp_s where it was */
totp_s:         .space 21           /* [0] is scratch; a..e, then the digits */
    .global clk_secs, totp_s, totp_wiped, totp_gap, sec_ks, key_left
/* The secrets: DigiMacro.ino's struct sec, the same 28 bytes in the same
   order, zeroed whole by hidKeyForget. The key (1A4), held while
   key_left; the playing secret's nonce (1B4); and sec_ks (1B8), its nonce
   and counter, then XTEA of them, whose [0] is the keystream byte.
   sec_ks must follow sec_nonce: secret_byte writes it through Z =
   sec_nonce */
sec:
_ZL3sec:        .space 16
sec_nonce:      .space 4
sec_ks:         .space 8
#ifdef PROBE
/* The frame lock's probe: counted by ANA_COMP, taken whole each 1000 ticks
   into pr_snap by probe_poll, and read with the clock (0x6C). Kept in this
   order: clock_read sends pr_snap and pll_p as they lie */
pr_t:           .space 2            /* the probe's second began here */
ac_n:           .space 2            /* comparator interrupts */
ac_in:          .space 2            /* ...of them, keep-alives where due */
ac_j:           .space 2            /* edges tried while lost; one spare */
pr_snap:        .space 7            /* the six above, for the last second, */
#endif                              /* and the second's number */
/* pll_p (1C0; 1CF with PROBE): little-endian, [0] the fraction in 1/256
   of a count, [1] OCR1C. Written by acmore, read by t1more, and by the
   main program only in app_setup. Last, so that app_setup's zero ends at
   pll_p + 2 */
pll_p:          .space 2            /* the period, OCR1C in 8.8 */

/* A variable's displacement from Y, for ldd and std: 0 to 63 */
#define Q(v)    ((v) - V)

    .global ms
    .set ms, 0x800002           /* r2:r3, for the harness's eyes */

/* ------------------------------------------------------------------ */
/* The header. Slots of interrupts never enabled hold code instead.   */
/* ------------------------------------------------------------------ */

/*
 * At APP_START, in the shape core/layout.h fixes and app/apphdr.S, the C
 * app's header, has: seventeen one-word slots, then the magic, the
 * length, the version and the API. The core jumps to slot 0, 1 or 2 to
 * call the app, and the chip's interrupt n (1..14) reaches slot n + 2,
 * through the chip's vector and then the core's forwarding rjmp
 * (core/coretab.S): eight cycles from the interrupt to the slot.
 *
 * A slot is one word, room for an rjmp. But a slot whose interrupt is
 * never enabled is never jumped to, so the interrupt before it may run on
 * into it: the tick's first four instructions fill its own slot and the
 * next three, the comparator's first eight the last eight. That saves
 * their rjmp, and puts sei within a cycle or two of the slot.
 *
 *   slot  vector              holds
 *    0    APP_SETUP           rjmp app_setup
 *    1    APP_LOOP            rjmp app_loop
 *    2    APP_REQUEST         rjmp app_request
 *    3    1  INT0             sei, on into slot 4
 *    4    2  PCINT0           rjmp int0. PCINT0 is V-USB's, and the core's
 *                             vector goes straight to it: never jumped to
 *    5    3  TIMER1_COMPA     the tick, four words, on through
 *    6-8  4-6 TIMER1_OVF, TIMER0_OVF, EE_RDY: never enabled
 *    9    7  ANA_COMP         the comparator, eight words, on through
 *   10-16 8-14 ADC, TIMER1_COMPB, TIMER0_COMPA, TIMER0_COMPB, WDT,
 *             USI_START, USI_OVF: never enabled. (WDT's WDIE is set
 *             only in app_setup's entropy loop, with interrupts off, and
 *             cleared before they come back on)
 *
 * The CRC the core checks comes after the image, put there by
 * app/apphex.py; the length below is up to it.
 */
/* The clock's trim, as make CLOCK_TRIM=n starts it: see clock_poll */
#ifndef CLOCK_TRIM
#define CLOCK_TRIM      0
#endif
#if CLOCK_TRIM < 0
#define TRIM_ABS        (-(CLOCK_TRIM))
#define TRIM_DIR        1
#else
#define TRIM_ABS        (CLOCK_TRIM)
#define TRIM_DIR        0
#endif
#if CLOCK_TRIM && defined(PROBE)
#error "CLOCK_TRIM: the readout's build (PROBE) has no room for the trim"
#endif
#if CLOCK_TRIM && (TRIM_ABS < 4 || TRIM_ABS > 1000)
#error "CLOCK_TRIM: 0, or 4 to 1000 ppm either way (one second in 250 to 1)"
#endif
#if CLOCK_TRIM
#define TRIM_N          ((2000 / TRIM_ABS + 1) / 2)
#define TRIM_COUNT      (TRIM_N - 1)
#if TRIM_DIR
#define TRIM_ODD        0xE9
#define TRIM_ROLL       0xE9
#else
#define TRIM_ODD        0xE7
#define TRIM_ROLL       0xE8
#endif
#else
#define TRIM_COUNT      0
#define TRIM_ODD        0xE8
#define TRIM_ROLL       0xE8
#endif

    .section .vectors,"ax",@progbits
    .global __vectors
    /* Slots 0-2: APP_SETUP, once, before USB; APP_LOOP, over and over;
       APP_REQUEST, from inside usbPoll */
__vectors:
    rjmp app_setup
    rjmp app_loop
    rjmp app_request
    sei                         /* INT0's slot, and PCINT0's: V-USB's own */
    rjmp int0                   /* vector, so this one is never jumped to */
    /* TIMER1_COMPA's slot, and on through the three unused ones after it.
       sei first, so V-USB can get in; then SREG into r4, before the inc
       changes it. inc r2's zero flag, the carry into r3, is taken in
       t1more: rjmp changes no flag, and an interrupt in between puts
       SREG back as it found it */
    sei                         /* TIMER1_COMPA */
    in r4, SREG
    inc r2                      /* the clock */
    rjmp t1more
    /* ANA_COMP: D- fell -- a keep-alive, or a packet's edge. Not itself
       again until done: back-to-back packets, each holding V-USB's
       interrupt, would stack one more of it per packet */
    /* Slot 9. ACIE goes off before interrupts come back on. TCNT1 is read
       some 20 cycles after the edge, the same 20 each time: a sixth of a
       count, and a constant, which the lock's aim absorbs */
    cbi ACSR, ACIE
    sei
    push r16
    in r16, SREG
    push r16
    push r17
    in r16, TCNT1
    rjmp acmore
    .if (. - __vectors) != 2 * APP_SLOTS
    .error "the header's code does not end where the table must"
    .endif
    .byte APP_MAGIC0, APP_MAGIC1
    .word __data_load_end - APP_START
    .byte APP_VER, CORE_API
    /* APP_CODE: the clock's trim, two bytes tools/settrim.py may change
       in a built image (see clock_poll). Code starts after them */
trim:
    .byte TRIM_COUNT, TRIM_ODD, TRIM_ROLL, 0

    .text

/* ------------------------------------------------------------------ */
/* The frame lock and the tick: the rest of the two interrupts above  */
/* ------------------------------------------------------------------ */

/*
 * acmore: ANA_COMP, after its vector's eight words. The frame lock: see
 * PLL_T above for the numbers. No C counterpart (the C's clock ran free).
 *
 *   in:      r16 = TCNT1 at the edge; r16, SREG, r17 pushed, in that order;
 *            ACIE off, interrupts on
 *   changes: OCR1C, pll_p, PLL_MISS, F2_PLL, F2_HIT (and with PROBE,
 *            ac_n, ac_in, ac_j); nothing else
 *
 * The tick's interrupt may land in here, and V-USB's anywhere; both put
 * back what they use. Of what this writes, only OCR1C and F2's two bits
 * are the tick's too, and it writes them at count 1, long before a
 * keep-alive is due. (Lost, an edge can come at count 0, just before the
 * tick's interrupt, which then undoes the try: it is tried again two
 * ticks later.) The interrupts are off here for the vector's ten cycles
 * only; the rest runs with them on.
 */
/*
 * Step by step, as below:
 *   - e = TCNT1 - PLL_T, a signed byte: -64 at the tick's start, 0 where
 *     the keep-alive is due.
 *   - The probe's counts are +1 as "subi -1", AVR having no add-immediate;
 *     "sbci -1" then adds one to the high byte just when the low wrapped.
 *   - F2_PLL set: this tick has had its edge, and this one is ignored.
 *   - In the window, -PLL_W <= e <= PLL_W, is 0 <= e + PLL_W <= 2 PLL_W:
 *     one unsigned compare. Out of it, on at 5:.
 *   - This tick: OCR1C += e/2. OCR1C is about 128 and TCNT1 near 64, so
 *     the clear at OCR1C is still to come and moves with it.
 *   - The period: e/2 is -2..1; SWAP and ANDI 0xF0 make (e/2) * 16 of it
 *     as a signed byte, which holds while e/2 fits four bits (PLL_W <= 15).
 *     That is added to pll_p's fraction, its carry to OCR1C's byte, and
 *     one taken off that byte if it was negative (bit 7): the sign
 *     extended by hand.
 */
acmore:
    subi r16, PLL_T             /* the phase error */
#ifdef PROBE
    lds r17, ac_n               /* the probe: every edge */
    subi r17, -1
    sts ac_n, r17
    lds r17, ac_n + 1
    sbci r17, -1
    sts ac_n + 1, r17
#endif
    sbic F2, F2_PLL             /* one edge a tick */
    rjmp 9f
    mov r17, r16
    subi r17, -PLL_W
    cpi r17, 2 * PLL_W + 1
    brcc 5f
    sbi F2, F2_HIT              /* the keep-alive, where it was due */
    sbi F2, F2_PLL
#ifdef PROBE
    lds r17, ac_in
    subi r17, -1
    sts ac_in, r17
    lds r17, ac_in + 1
    sbci r17, -1
    sts ac_in + 1, r17
#endif
    asr r16                     /* this tick: half the error. OCR1C stays */
    in r17, OCR1C               /* far above TCNT1 */
    add r17, r16
    out OCR1C, r17
    swap r16                    /* the period: a sixteenth of the half */
    andi r16, 0xF0
    lds r17, pll_p
    add r17, r16
    sts pll_p, r17
    lds r17, pll_p + 1
    adc r17, r1
    sbrc r16, 7
    dec r17
    sts pll_p + 1, r17
    rjmp 9f
    /* Outside the window. Locked, it is a packet's. Lost, it is tried:
       OCR1C + e = OCR1C + TCNT1 - PLL_T puts the tick's end OCR1C - PLL_T
       counts after now, and PLL_MISS at PLL_LOST - 2 gives two ticks to
       find the keep-alive in the window before it is lost again */
5:  in r17, PLL_MISS
    cpi r17, PLL_LOST
    brcs 9f                     /* locked: a packet's, ignored */
    sbi F2, F2_PLL              /* lost: this one is tried. The tick ends */
    in r17, OCR1C               /* PLL_T counts short of the next frame's */
    add r17, r16                /* edge: 65 to 192, never below TCNT1 */
    out OCR1C, r17
    ldi r17, PLL_LOST - 2
    out PLL_MISS, r17
#ifdef PROBE
    lds r17, ac_j
    inc r17
    sts ac_j, r17
#endif
    /* Edges that came while this ran set ACI again, and are of no use: the
       first of this tick has been had, and their time is gone. On the
       ATtiny85, SBI writes only its own bit, so `sbi ACSR, ACIE` alone
       would leave ACI set, and the interrupt would run again at once with
       a stale TCNT1. ACI is cleared by writing a one to it, hence the
       first SBI. (simavr does SBI as a read and a write of the whole
       register, which writes back the one in ACI and so clears it anyway:
       there the first SBI changes nothing, which is how it was missed.)
       Interrupts are on throughout; ACIE back on is the last thing before
       the reti */
9:  pop r17
    pop r16
    out SREG, r16
    pop r16
    sbi ACSR, ACI               /* an edge that came meanwhile: forgotten. */
    sbi ACSR, ACIE              /* SBI sets only its bit: ACIE alone would */
    reti                        /* leave it pending, to run once more, late */

/*
 * t1more: TIMER1_COMPA, after its vector's four words. The tick: once a
 * millisecond (a tick of the period), at count 1 of it. hidusb.c's
 * ISR(TIMER1_COMPA_vect), which counted ms and the tenths, and now also
 * sets the tick's length for the frame lock.
 *
 *   in:      r2 just incremented, its zero flag still in SREG; SREG as it
 *            was in r4; interrupts on
 *   changes: r2:r3 (the milliseconds), r6, DM_WINDOW, OCR1C, PLL_FRAC,
 *            PLL_MISS, F2_PLL, F2_HIT. r16 and r17 are pushed and popped
 *
 * About 50 cycles; the comparator's interrupt and V-USB's may land in it.
 * r4 holds SREG till the end, so this must not run again inside itself:
 * it cannot, a tick being 16,500 cycles long.
 */
    /* The zero flag is inc r2's, from the vector: r2 wrapped, so r3 + 1 */
t1more:
    brne 1f
    inc r3
    /* This tick's length: PLL_FRAC += pll_p's fraction, and OCR1C =
       pll_p's whole counts plus that carry, so one tick in so many is a
       count longer. Written now, with TCNT1 at 1, far below it. Then
       PLL_MISS: back to 0 if the tick just ended had its keep-alive
       (F2_HIT), else one more, stopping at 127 (128 would be negative) */
1:  push r16
    push r17
    lds r16, pll_p              /* this tick's length: the period, and */
    in r17, PLL_FRAC            /* the fraction carried */
    add r17, r16
    out PLL_FRAC, r17
    lds r16, pll_p + 1
    adc r16, r1
    out OCR1C, r16
    in r16, PLL_MISS            /* ticks since a keep-alive, up to 127 */
    sbic F2, F2_HIT
    ldi r16, -1
    inc r16
    brmi 2f
    out PLL_MISS, r16
    /* A new tick: no edge yet. Then the tenths: r6 counts the ticks to the
       next one, and DM_WINDOW, the core's update window, counts tenths up
       to 0xFF and stops there (layout.h) */
2:  pop r17
    pop r16
    cbi F2, F2_PLL
    cbi F2, F2_HIT
    dec r6
    brne t1out
    in r6, DM_WINDOW            /* the tenth: 0xFF stays */
    inc r6
    breq t1tenth
    out DM_WINDOW, r6
    /* r6 takes no ldi (only r16-r31 do): 100 goes through r16 */
t1tenth:
    push r16
    ldi r16, 100
    mov r6, r16
    pop r16
t1out:
    out SREG, r4
    reti

/* ------------------------------------------------------------------ */
/* Small things everything uses                                       */
/* ------------------------------------------------------------------ */

/*
 * Two idioms run all through what follows. AVR has no add-immediate, so
 * "subi r, lo8(-k)" then "sbci r', hi8(-k)" adds k to a pair (and "subi
 * r, 0xFF" / "sbci r', 0xFF" adds one). And a local label, 1: to 9:, is
 * reached as 1b (back) or 1f (forward); 9: is usually a routine's ret.
 */

/* EEPROM. ee1 for an address in the page at 0x100; all keep everything
   but r24 and r25 (and r0, writing) */
/*
 * ee: the byte at EEPROM address r25:r24 -> r24 (eeprom_read_byte). ee1
 * is the same with r25 = 1, for the reserved bytes (LOCK_SOFT ...). Waits
 * first for a write still going (3.4 ms each): a read cannot overlap one.
 * Changes r24, r25 only, so callers keep X, Z and r18-r23 across it.
 */
ee1:
    ldi r25, 1
ee:
    sbic EECR, EEPE
    rjmp ee
    out EEARH, r25
    out EEARL, r24
    sbi EECR, EERE
    in r24, EEDR
    ret

/*
 * eeput: r22 into EEPROM address r25:r24, if it is not there already
 * (eeprom_update_byte: a table saved unchanged costs no wear). eeput1 is
 * the same in the page at 0x100. Changes r24 (to the old byte, not the
 * address: a loop must reload it), r25 and r0.
 *
 * The write starts and runs on its own; the next ee or eeput waits for
 * it. EEMPE must be followed by EEPE within four cycles, hence the cli:
 * interrupts are off for those two instructions only.
 */
eeput1:                         /* r24 low address, r22 the value */
    ldi r25, 1
    /* ee leaves the address in EEAR, where the write needs it */
eeput:
    rcall ee
    cp r24, r22
    breq 9f
    out EEDR, r22
    in r0, SREG
    cli
    sbi EECR, EEMPE
    sbi EECR, EEPE
    out SREG, r0
9:  ret

/* Milliseconds since the sixteen bits at Z's low byte (ZH is RAM's) */
/*
 * since: now - [r30], unsigned 16-bit -> r25:r24, and the stamp itself
 * -> r19:r18 (callers moving a stamp on use it: clock_poll, pause_poll,
 * probe_poll). r30 holds the variable's low byte; r31 is set to V's page,
 * and Z is left pointing at the stamp. Read with a movw, so atomic.
 * A difference is right across r2:r3's wrap as long as it is less than
 * 65.5 s: every caller compares against something far shorter.
 */
since:
    ldi r31, hi8(V)
    ld r18, Z
    ldd r19, Z+1
    movw r24, r2
    sub r24, r18
    sbc r25, r19
    ret

/* stamp: now into the variable whose low byte is r30. Changes r24, r25,
   r31; Z is left at the variable. ticks() and an assignment, in the C */
stamp:                          /* now, into the sixteen bits at r30 */
    ldi r31, hi8(V)
    movw r24, r2
    st Z, r24
    std Z+1, r25
    ret

/* Ten seconds since the time at r30? If so, moved on by them: carry clear */
/*
 * due10s: one step of the C's
 *     while ((uint16_t)(ticks() - t) >= LOCK_TICK_MS) { t += 10000; ... }
 * for lock_tick and key_tick. Carry set: not yet, nothing changed. Carry
 * clear: ten seconds have gone, and the stamp has moved on by exactly
 * ten, not to now, so the next ten are counted from where this one was
 * due to end and time spent away (a long macro) is caught up, not lost.
 * Changes r18, r19, r24, r25, r31.
 */
due10s:
    rcall since
    ldi r18, hi8(10000)
    cpi r24, lo8(10000)
    cpc r25, r18
    brcs 9f
    ld r18, Z
    ldd r19, Z+1
    subi r18, lo8(-10000)
    sbci r19, hi8(-10000)
    st Z, r18
    std Z+1, r19
    clc
9:  ret

/* zero: r24 bytes at Z, Z left past them, r24 left 0. (r24 = 0 would be
   256.) memset(..., 0, n) */
zero:                           /* r24 bytes at Z */
    st Z+, r1
    dec r24
    brne zero
    ret

/* A wait in tens of seconds, as stored: anything but 1..240 is a minute */
/*
 * waitval: r24 -> r24, for LOCK_WAIT and KEY_WAIT as read from EEPROM:
 * the C's (uint8_t)(w - 1) < LOCK_WAIT_MAX ? w : LOCK_WAIT_DEF. 0 and an
 * erased 0xFF both mean a minute. mov does not touch the flags, so the
 * compare survives the copy back. Changes r0.
 */
waitval:
    mov r0, r24
    dec r24
    cpi r24, 240
    mov r24, r0
    brcs 9f
    ldi r24, 6
9:  ret

/* ------------------------------------------------------------------ */
/* The window                                                         */
/* ------------------------------------------------------------------ */

/*
 * The window: somebody held the pad four seconds (handle_input sets
 * F2_OPEN and allowUntil, the C's hidAllow), so the page may reach the
 * table for half a minute. The vendor report is one buffer for everybody:
 * whatever a request carried or an answer held -- a password, a key, a
 * chunk of EEPROM -- stays in it to be read back. wipe clears it.
 */

/* wipe: the report's 32 data bytes to zero (hidusb.c wipe). Changes r24,
   r25, Z: hidReport changes only r24, r25 */
    /* From DM_VND_DATA, the report's 4th byte, DM_VND_CHUNK bytes. A tail
       call: zero's ret is ours */
wipe:
    rcall hidReport
    movw r30, r24
    adiw r30, 4
    ldi r24, 32
    rjmp zero

/*
 * hidAllowed -> r24: 1 if the window is open, 0 if not (hidusb.c). Asking
 * is what shuts it: past allowUntil, F2_OPEN goes, the core's session
 * ends (hidLock) and the report is wiped -- but not under INFO, which asks
 * this while filling in its answer and would lose the core's half. Must
 * be asked within 32 s of allowUntil, or the signed difference turns
 * round and an expired window reads as open: clocks_poll asks it all
 * through a macro. Keeps r20-r23 and X (callers rely on it); changes
 * r18, r19, r24, r25, Z.
 */
    /* now - allowUntil, signed: negative is still open */
hidAllowed:                     /* -> r24, 0 or 1 */
    sbis F2, F2_OPEN
    rjmp 8f
    ldi r30, lo8(allowUntil)
    rcall since
    tst r25
    brmi 8f
    cbi F2, F2_OPEN
    rcall hidLock
    rcall hidReport
    movw r30, r24
    ld r24, Z
    cpi r24, DM_FUNC_INFO
    breq 8f
    rcall wipe
8:  ldi r24, 0
    sbic F2, F2_OPEN
    ldi r24, 1
    ret

/* ------------------------------------------------------------------ */
/* Typing                                                             */
/* ------------------------------------------------------------------ */

/* The report at r25:r24, its length from its id: 1 is the keyboard's */
/*
 * send: hidusb.c's sendRaw. Hands the report to the core's hidSend until
 * it takes it (the last one has gone to the host), calling corePoll
 * between tries, and gives up after 250 ms: the host is not asking. id 1,
 * the keyboard's, is 6 bytes; id 3, the mouse's, is 4 (bit 1 tells them
 * apart). The report's address waits in r10:r11, which the C calls and
 * app_request leave alone.
 *
 * Calls corePoll: see "how it runs" at the top. Changes r10, r11, r18-r27,
 * r30, r31, r0. Every keystroke reaches here, so this is where a playing
 * macro keeps USB alive: a keystroke is two sends, about 20 ms.
 */
send:
    movw r10, r24
    ldi r30, lo8(sendT)
    rcall stamp
    /* The length from the report's id. hidSend non-zero: taken. Retries
       until 251 ms. The C gave up after 50 ms, which a host that pauses
       its polls a little longer (a burst of control transfers) turned
       into a lost press, its release still sent; 250 ms rides that out,
       and still fits one compare */
1:  movw r24, r10
    movw r30, r24
    ld r18, Z
    ldi r22, 6
    sbrc r18, 1
    ldi r22, 4
    rcall hidSend
    tst r24
    brne 9f
    rcall corePoll
    ldi r30, lo8(sendT)
    rcall since
    cpi r24, 251
    cpc r25, r1
    brcs 1b
9:  ret

/*
 * hidKey: a key pressed and let go: r24 the modifiers, r22 the key
 * (hidusb.c hidKey). It calls sendReport for the press, then falls into
 * it with zeros for the release, whose ret is its own. sendReport is the
 * C's: the keyboard report with these modifiers and this key, sent.
 * Both change what send changes.
 */
hidKey:                         /* r24 modifier, r22 key */
    rcall sendReport
    clr r24
    clr r22
sendReport:
    std Y + Q(kbd) + 1, r24
    std Y + Q(kbd) + 3, r22
    ldi r24, lo8(kbd)
    ldi r25, hi8(kbd)
    rjmp send

/*
 * The mouse (hidusb.c, "The mouse", says why it moves as it does). A TV
 * takes only a relative mouse, so the board keeps where it put the cursor
 * in the page's steps, 128 across and 64 down, and moves by the
 * difference. A step is two reports of ptrS's counts each.
 */

/* ptrSend: the mouse report with these dx, dy, twice: it calls the send at
   1: and then falls into it again. Changes what send changes */
ptrSend:                        /* r24 dx, r22 dy: the report twice */
    std Y + Q(ptr) + 2, r24
    std Y + Q(ptr) + 3, r22
    rcall 1f
1:  ldi r24, lo8(ptr)
    ldi r25, hi8(ptr)
    rjmp send

/* An axis a step nearer: Z the place, r22 the target, r20 the count -> r24 */
/* ptrStep (hidusb.c): +r20, -r20 or 0, the place moved on by one step.
   Changes r24 only */
ptrStep:
    ld r24, Z
    cp r24, r22
    brcc 1f
    inc r24
    st Z, r24
    mov r24, r20
    ret
1:  breq 2f
    dec r24
    st Z, r24
    mov r24, r20
    neg r24
    ret
2:  clr r24
    ret

/*
 * hidPoint: one {x,y}/{click} byte, r24, the argument of a 0x83 escape
 * from 3 on (hidusb.c hidPoint; README, "The table", for the encoding):
 *   1xxxxxxx   x's target, 0-127; nothing moves yet
 *   01yyyyyy   y's target, 0-63: move there, both axes a step at a time
 *              while each has a way to go, a diagonal then straight on.
 *              A target of 0 first puts the axis at 30 with 127 counts a
 *              report, so it drives into the edge whatever was there
 *   000y ssss  the next move's counts a report on axis y (bit 4), xor 4
 *   001c 00bb  buttons bb down (none: up), then up again if c: a click
 * Calls send: changes what it changes, and r20, r21, r24.
 */
hidPoint:                       /* r24 what */
    sbrs r24, 7
    rjmp 1f
    andi r24, 0x7F
    std Y + Q(ptrToX), r24
    ret
1:  sbrs r24, 6
    rjmp 5f
    andi r24, 0x3F
    mov r21, r24                /* toY */
    ldi r18, 30
    ldi r19, 127 ^ 4
    ldd r24, Y + Q(ptrToX)
    tst r24
    brne 2f
    std Y + Q(ptrX), r18
    std Y + Q(ptrS), r19
2:  tst r21
    brne 3f
    std Y + Q(ptrY), r18
    std Y + Q(ptrS) + 1, r19
    /* There yet, on both axes? */
3:  ldd r24, Y + Q(ptrX)
    ldd r25, Y + Q(ptrToX)
    cp r24, r25
    brne 4f
    ldd r24, Y + Q(ptrY)
    cp r24, r21
    breq 7f
    /* A step: x by ptrS[0] ^ 4 counts, y by ptrS[1] ^ 4. toY and then dx
       wait on the stack, past ptrStep and send */
4:  push r21
    ldd r20, Y + Q(ptrS)
    ldi r24, 4
    eor r20, r24
    ldd r22, Y + Q(ptrToX)
    ldi r30, lo8(ptrX)
    ldi r31, hi8(ptrX)
    rcall ptrStep
    push r24
    ldd r20, Y + Q(ptrS) + 1
    ldi r24, 4
    eor r20, r24
    mov r22, r21
    ldi r30, lo8(ptrY)
    ldi r31, hi8(ptrY)
    rcall ptrStep
    mov r22, r24
    pop r24
    rcall ptrSend
    pop r21
    rjmp 3b
    /* There: the scale was the one move's */
7:  std Y + Q(ptrS), r1
    std Y + Q(ptrS) + 1, r1
    ret
    /* A scale, before the move: bit 4, the axis, says which byte of ptrS */
5:  sbrc r24, 5
    rjmp 6f
    mov r30, r24                /* a scale, before the move */
    swap r30
    andi r30, 1
    andi r24, 0x0F
    ldi r25, 4
    eor r24, r25
    ldi r31, hi8(ptrS)
    subi r30, lo8(-(ptrS))
    st Z, r24
    ret
    /* Buttons; and with bit 4, a click, up again */
6:  push r24
    andi r24, 3
    std Y + Q(ptr) + 1, r24
    clr r24
    clr r22
    rcall ptrSend
    pop r24
    sbrs r24, 4
    ret
    std Y + Q(ptr) + 1, r1
    clr r24
    clr r22
    rjmp ptrSend

/* ------------------------------------------------------------------ */
/* The lockout                                                        */
/* ------------------------------------------------------------------ */

/*
 * DigiMacro.ino's "the lockout" says what it is for. In short: with a
 * password set, LOCK_SOFT wrong codes make a soft lock, every tap ignored
 * for LOCK_WAIT tens of seconds; LOCK_HARD soft locks make a hard one,
 * ignored until the password. All of it is in EEPROM, so a replug gets
 * nobody anywhere; whether it is locked is never stored but worked out
 * from the counts each time (hidLockState). The counts are stored
 * inverted, so an erased 0xFF reads as 0.
 *
 * Everything here keeps X, and changes only r18-r25, r0 and Z.
 */

count_of:                       /* r24 low address -> r24, the count */
    rcall ee1
    com r24
    ret

/* count_clr, count_put: a count into the reserved byte at r24, inverted.
   Change r22, r24, r25, r0 */
count_clr:                      /* r24 low address: an erased count */
    clr r22
count_put:                      /* r24 low address, r22 the count */
    com r22
    rjmp eeput1

/* lock_soft (DigiMacro.ino): no password, no lockout -- only the password
   lifts a hard lock. 0xFF, erased, is none; 0 is none already.
   Changes r24, r25 */
lock_soft:                      /* -> r24: the threshold, or 0 for no lockout */
    rcall hidPassSet
    tst r24
    breq 9f
    ldi r24, LOCK_SOFT
    rcall ee1
    cpi r24, 0xFF
    brne 9f
    clr r24
9:  ret

/*
 * hidLockState -> r24: 0, ST_SOFT or ST_HARD (DigiMacro.ino). Hard if
 * LOCK_HARD is set (1..254) and LOCK_SOFTS has reached it; else soft if
 * LOCK_MISSES has reached LOCK_SOFT. Changes r18, r19, r24, r25 only:
 * info, ir_poll and code_finish keep things in r20-r23 and X across it,
 * relying on hidPassSet's changing only r24 and r25.
 */
    /* At 1: below, the ldi between cp and brcs keeps the carry: ldi sets
       no flag */
hidLockState:                   /* -> r24, keeping r20-r23 and X */
    rcall lock_soft
    tst r24
    breq 9f
    mov r19, r24                /* s, past the calls that keep it */
    ldi r24, LOCK_HARD
    rcall ee1
    mov r18, r24
    inc r24
    cpi r24, 2                  /* 0 and 0xFF: never */
    brcs 1f
    ldi r24, LOCK_SOFTS
    rcall count_of
    cp r24, r18
    brcs 1f
    ldi r24, ST_HARD
    ret
1:  ldi r24, LOCK_MISSES
    rcall count_of
    cp r24, r19
    ldi r24, 0
    brcs 9f
    ldi r24, ST_SOFT
9:  ret

/* Counted before anything shows the result: DigiMacro.ino says why */
/*
 * lock_miss: a wrong code, or a wrong password (DigiMacro.ino lock_miss;
 * hidPassWrong is this). Nothing when there is no lockout or it is locked
 * already. The soft lock it causes is counted first, the miss after: a
 * plug pulled between the two costs the guesser, never the owner.
 * Changes r18-r25, r0, Z.
 */
    /* r20 = s; r21 = m, misses + 1, at most s since not yet locked, so
       it cannot wrap */
lock_miss:
    rcall lock_soft
    tst r24
    breq 9f
    mov r20, r24
    rcall hidLockState
    tst r24
    brne 9f
    ldi r24, LOCK_MISSES
    rcall count_of
    mov r21, r24
    inc r21
    cp r21, r20
    brcs 1f
    ldi r24, LOCK_SOFTS
    rcall count_up
1:  ldi r24, LOCK_MISSES
    mov r22, r21
    rjmp count_put
9:  ret

/* count_up (DigiMacro.ino): one more, stopping at 255. Changes r22-r25,
   r0 */
count_up:                       /* r24 low address, keeping r20 r21 */
    mov r23, r24
    rcall count_of
    cpi r24, 0xFF
    breq 9f
    mov r22, r24
    inc r22
    mov r24, r23
    rjmp count_put
9:  ret

/* lock_clear (DigiMacro.ino; hidPassChanged is this): the counts to zero,
   and the soft lock's clock stopped. eeput writes only what changed, so
   the usual case, nothing to clear, costs no wear. Changes r22, r24, r25,
   r0 */
lock_clear:
    ldi r24, LOCK_MISSES
    rcall count_clr
    ldi r24, LOCK_SOFTS
    rcall count_clr
    std Y + Q(lock_left), r1
    ret

/*
 * lock_poll (DigiMacro.ino): the soft lock's clock, from clocks_poll.
 * RAM only, deliberately: a board replugged while soft-locked starts the
 * whole wait again. The first poll that finds a soft lock starts the
 * count; each ten seconds gone (due10s, which catches up a long absence)
 * takes one off it, and the last clears LOCK_MISSES: served. Changes
 * r18-r25, r0, Z.
 */
lock_poll:
    rcall hidLockState
    cpi r24, ST_SOFT
    breq 1f
    std Y + Q(lock_left), r1
    ret
1:  ldd r24, Y + Q(lock_left)
    tst r24
    brne 2f
    ldi r24, LOCK_WAIT
    rcall ee1
    rcall waitval
    std Y + Q(lock_left), r24
    ldi r30, lo8(lock_tick)
    rcall stamp
2:  ldi r30, lo8(lock_tick)
    rcall due10s
    brcs 9f
    ldd r24, Y + Q(lock_left)
    dec r24
    std Y + Q(lock_left), r24
    brne 2b
    ldi r24, LOCK_MISSES
    rjmp count_clr
9:  ret

/* hidPassRight (DigiMacro.ino): the right password clears the lockout,
   and a hard lock it lifts is counted, for the page to show. Both ways
   end in lock_clear, the first by a branch straight into it */
hidPassRight:
    rcall hidLockState
    cpi r24, ST_HARD
    brne lock_clear
    ldi r24, LOCK_CLEARED
    rcall count_up
    rjmp lock_clear

/* ------------------------------------------------------------------ */
/* Secrets                                                            */
/* ------------------------------------------------------------------ */

/*
 * DigiMacro.ino's "secrets" says what they are. In short: a {secret}'s
 * bytes are XTEA in counter mode under a 16-byte key the board never
 * stores. A client on the computer sends it (KEYAUTH, hidKeyAuth below);
 * it is held in sec for KEY_WAIT tens of seconds (key_ok counts them) and
 * then forgotten, and at RESTART and any WRITE. The board knows the right
 * key by the check value at CHECK_AT, XTEA of zeros under it. A wrong key
 * costs a second (F2_PAUSING) and never counts toward the lockout.
 * Playing, secret_byte (with playback, below) decrypts byte by byte.
 */

/* hidKeyForget (DigiMacro.ino): no key, and all 28 bytes of sec zeroed:
   key, nonce, keystream. Changes r24, Z */
hidKeyForget:
    std Y + Q(key_left), r1
    ldi r30, lo8(sec)
    ldi r31, hi8(sec)
    ldi r24, 28
    rjmp zero

/*
 * key_ok -> r24: tens of seconds the key has left, 0 if none; the zero
 * flag set if none (DigiMacro.ino key_ok). Runs the key's clock, catching
 * up however long it went unasked, and forgets the key the moment its
 * time is up: so every use asks this first. From clocks_poll, info and
 * macro_play. Changes r18, r19, r24, r25, Z.
 */
key_ok:                         /* -> r24, the tens left; zero flag */
    /* Each ten gone takes one off; with the last, the key is forgotten,
       and round once more to answer 0 */
1:  ldd r24, Y + Q(key_left)
    tst r24
    breq 9f
    ldi r30, lo8(key_tick)
    rcall due10s
    brcs 8f
    ldd r24, Y + Q(key_left)
    dec r24
    std Y + Q(key_left), r24
    brne 1b
    rcall hidKeyForget
    rjmp 1b
8:  ldd r24, Y + Q(key_left)
    tst r24
9:  ret

/* A wrong key's second, up? */
/* pause_over: carry set while a wrong key's second still runs. Changes
   r18, r19, r24, r25, Z */
pause_over:                     /* -> carry clear if it is */
    ldi r30, lo8(key_failed)
    rcall since
    ldi r18, hi8(1000)
    cpi r24, lo8(1000)
    cpc r25, r18
    ret

/* Compared in full, as the password is. r25:r24 p, r22 n -> r24 */
/*
 * hidKeyAuth: KEYAUTH, from app_request (DigiMacro.ino hidKeyAuth).
 * p, the request's data; n, its count: 16, the key, or 20, the key and
 * then four bytes of time as clock_set takes them, taken only if the key
 * is right -- which is how digimacro-host sets the clock without the
 * window. -> r24: 1, the key is held; 0, refused (wrong length, within a
 * wrong key's second, or wrong).
 *
 * b = XTEA of eight zeros under p, on the stack; every byte compared
 * with the check value, the differences ORed together: the compare never
 * stops at the first one. Wrong: a second's pause. Right: the key
 * into sec, the clock for it started.
 *
 * In app_request, so inside corePoll. Changes r18-r27, r30, r31, r0, and
 * with n = 20 the clock. Stack: 8 bytes of b, and xtea's 21.
 */
hidKeyAuth:
    cpi r22, 20                 /* the key and the time */
    breq 0f
    cpi r22, 16
    brne no
0:  movw r26, r24
    sbis F2, F2_PAUSING
    rjmp 1f
    rcall pause_over
    brcs no
1:  cbi F2, F2_PAUSING
    ldi r24, 8
    /* SP is the next free byte, so b is at SP + 1. r18 gathers the
       differences */
2:  push r1                     /* b[8], zero */
    dec r24
    brne 2b
    in r24, SPL
    in r25, SPH
    adiw r24, 1
    movw r22, r26
    rcall xtea                  /* keeps X */
    clr r18
    ldi r20, 8
    ldi r19, CHECK_AT
    /* b[0] is popped first, and the pops take b off the stack. no: is
       shared, "0, refused", with hidProve */
3:  mov r24, r19
    rcall ee1
    pop r25
    eor r24, r25
    or r18, r24
    inc r19
    dec r20
    brne 3b
    tst r18
    breq 4f
    ldi r30, lo8(key_failed)
    rcall stamp
    sbi F2, F2_PAUSING
no: clr r24
    ret
    /* Right: the key, X's 16 bytes, into sec */
4:  ldi r30, lo8(sec)
    ldi r31, hi8(sec)
    ldi r24, 16
    /* Copy the key; then its clock starts from KEY_WAIT. After, X is past
       the key, and the request's count, [3], says whether the time follows
       it: r22 is long gone */
5:  ld r0, X+
    st Z+, r0
    dec r24
    brne 5b
    ldi r24, KEY_WAIT
    rcall ee1
    rcall waitval
    std Y + Q(key_left), r24
    ldi r30, lo8(key_tick)
    rcall stamp
    rcall hidReport             /* X is past the key: was the time sent? */
    movw r30, r24
    ldd r24, Z+3
    cpi r24, 20
    brne one
    rcall clock_set
    /* one: is shared, "1, done", with hidProve */
one:
    ldi r24, 1
    ret

/* XTEA under the check value, twice over: r25:r24 p, r22 n -> r24 */
/*
 * hidProve: PROVE, from app_request (DigiMacro.ino hidProve). p, eight
 * bytes of the client's choosing, encrypted in place under the key k =
 * check value twice over, so a client can tell this board from one that
 * only copied its serial number, before it sends the key. -> r24: 1, or
 * 0 if n is not 8. k is built on the stack, 16 bytes, pushed from k[15]
 * down so that k[0] ends lowest. Changes r18-r27, r30, r31, r0. Stack: 16
 * bytes of k, and xtea's 21.
 */
hidProve:
    cpi r22, 8
    brne no
    movw r26, r24
    ldi r18, 15                 /* k[16], from the top down, on the stack */
    /* k is at SP + 1 */
1:  mov r24, r18
    andi r24, 7
    subi r24, lo8(-CHECK_AT)
    rcall ee1
    push r24
    dec r18
    brpl 1b
    in r22, SPL
    in r23, SPH
    subi r22, 0xFF
    sbci r23, 0xFF
    movw r24, r26
    rcall xtea
    ldi r24, 16
2:  pop r0
    dec r24
    brne 2b
    rjmp one

/* ------------------------------------------------------------------ */
/* The clocks that keep running while a macro plays                   */
/* ------------------------------------------------------------------ */

/*
 * The time of day (README, "The clock"): clk_secs, seconds since 1970,
 * and clk_t, the tick its current second began at. Volatile: unset at
 * every power-up (F2_CLOCK clear), set by the page (0x6C) or with the key
 * (KEYAUTH of 20). The ticks are the frame lock's, so a second is 1000 of
 * the host's milliseconds. No C counterpart: the C app had no clock.
 */

/* Carry milliseconds into seconds: 1000 ticks. From clocks_poll, which
   every wait reaches well inside the sixteen bits' 65 seconds; only the
   main context writes these. */
/*
 * The trim: four bytes in flash, at a fixed place -- APP_CODE, right
 * after the header (core/layout.h), "trim" below -- so that a built image
 * can be trimmed without building it again: tools/settrim.py writes them
 * and stamps the CRC again, which the core checks. make CLOCK_TRIM=n (ppm,
 * default 0) only chooses what they start as.
 *
 *   trim[0]  the count: one second in every trim[0] + 1 is the odd one
 *   trim[1]  the odd second's length, low byte: 0xE7, 999 ticks (the
 *            clock gains); 0xE9, 1001 (it loses); 0xE8, 1000 (no trim)
 *   trim[2]  the length that ends a second, low byte: 0xE9 for a losing
 *            trim, else 0xE8 (1000)
 *   trim[3]  0, so the code after it starts on a word
 *
 * They are stored worked out, as clock_poll uses them, to keep it short.
 * One second in TRIM_N = round(1000/|n|) odd gains 1e6/(1000 TRIM_N - 1)
 * ppm, or loses 1e6/(1000 TRIM_N + 1): 25 is 40 s, 25.0 ppm; 30 is 33 s,
 * 30.3 ppm. 4 to 1000 ppm either way (TRIM_N 250 to 1). Untrimmed, every
 * second is "odd" and 1000 ticks long, which is no trim at all.
 */
/*
 * clock_poll: while a whole second has gone since clk_t, clk_t moves on
 * by the second's length (not to now, so nothing is lost to the poll's
 * lateness) and clk_secs counts it. A loop, so time spent away -- a
 * WRITE's 32 EEPROM bytes, inside corePoll, take about 110 ms -- is caught
 * up. Nothing while the clock is unset.
 *
 * The trim, in numbers: clk_spare counts the seconds down from TRIM_N - 1;
 * the one that finds it at 0 (borrow) is the odd one and restarts the
 * count. The first second after power-up is odd, clk_spare starting at 0.
 * Only r22 moves: 999, 1000 and 1001 share their high byte. A losing trim
 * rolls the second over at 1001 ticks, not 1000, so that its long second
 * cannot leave the next one at -1; an ordinary second still starts 1000
 * after the last, the one subtracted again below.
 *
 * Wrap: the seconds carry a byte at a time, the shortest way. In February
 * 2106, when the 32-bit count runs out, the carry has nowhere to stop and
 * adds one to clk_t's low byte, a millisecond, first.
 *
 * Changes r0, r18-r25, Z. Main context only (clocks_poll).
 */
clock_poll:
    sbis F2, F2_CLOCK
    ret
#ifdef PROBE
    /* The readout's build has no room for the trim: its bytes are there,
       and ignored. r0 = 0xE8: every second 1000 ticks */
    ldi r24, 0xE8
    mov r0, r24
#else
    ldi r30, lo8(trim)          /* r21 the count, r20 the odd second, */
    ldi r31, hi8(trim)          /* r0 the length that ends one: trim's */
    lpm r21, Z+                 /* bytes, which since leaves alone */
    lpm r20, Z+
    lpm r0, Z
#endif
    /* since: r25:r24 since clk_t, r19:r18 clk_t itself, Z at it. Less
       than r23:r0 ticks: done. 999, 1000 and 1001 share their high byte */
1:  ldi r23, hi8(1000)
    ldi r30, lo8(clk_t)
    rcall since
    cp r24, r0
    cpc r25, r23
    brcs 9f
    ldi r22, lo8(1000)          /* r23:r22, the second's length: 1000 */
#ifndef PROBE
    ldd r24, Y + Q(clk_spare)   /* the trim's count of seconds */
    subi r24, 1
    brcc 3f
    mov r24, r21                /* the odd one: the count starts again, */
    mov r22, r20                /* and this second is 999 or 1001 long */
3:  std Y + Q(clk_spare), r24
#endif
    /* clk_t moves on by the second's length */
    add r18, r22
    adc r19, r23
    st Z, r18
    std Z+1, r19
    .if clk_t - clk_secs - 4
    .error "the carry below needs clk_t right after clk_secs"
    .endif
    sbiw r30, 4                 /* clk_secs, a byte at a time: a byte that */
4:  ld r24, Z                   /* wrapped carries into the next (and at */
    inc r24                     /* 2^32 on into clk_t: 1 ms, in 2106) */
    st Z+, r24
    breq 4b
    rjmp 1b
9:  ret

/* Seconds from X, little-endian, from now */
/*
 * clock_set: clk_secs from the four bytes at X (X moves past them), the
 * second taken as beginning now, and the clock marked set. From
 * app_request (0x6C with four bytes, behind the gates) and hidKeyAuth (the
 * time after the key). The page and digimacro-host wait for a second to
 * begin before they send, so "now" is close to its start. Changes r24,
 * r25, Z.
 */
clock_set:
    ld r24, X+
    std Y + Q(clk_secs), r24
    ld r24, X+
    std Y + Q(clk_secs) + 1, r24
    ld r24, X+
    std Y + Q(clk_secs) + 2, r24
    ld r24, X+
    std Y + Q(clk_secs) + 3, r24
    ldi r30, lo8(clk_t)
    rcall stamp
    sbi F2, F2_CLOCK
    ret

/* Into X: seconds, ticks into this second, whether it is set */
/*
 * clock_read: 0x6C with a count of 0, no gate (README, "The protocol"):
 * into the report at X, [4..7] the seconds, [8..9] the milliseconds since
 * the second began, [10] 1 if set. The milliseconds come from clk_t, so
 * if clock_poll is late they may read 1000 or a little more; unset, they
 * count from power-up and mean nothing. With PROBE, then: [11..17]
 * pr_snap, [18..19] pll_p, [20] PLL_MISS, [21] OCR1C, all as they are now
 * (pll_p read without stopping the comparator's interrupt, which may
 * change it between its two bytes: a probe's glitch, nothing more). The
 * flag, or OCR1C, is stored by the shared last st. Changes r24, r25, Z.
 */
clock_read:
    ldd r24, Y + Q(clk_secs)
    st X+, r24
    ldd r24, Y + Q(clk_secs) + 1
    st X+, r24
    ldd r24, Y + Q(clk_secs) + 2
    st X+, r24
    ldd r24, Y + Q(clk_secs) + 3
    st X+, r24
    ldi r30, lo8(clk_t)
    rcall since
    st X+, r24
    st X+, r25
    ldi r24, 0
    sbic F2, F2_CLOCK
    ldi r24, 1
#ifdef PROBE
    st X+, r24
    ldi r30, lo8(pr_snap)       /* the probe's last second, the period, */
                                /* PLL_MISS and OCR1C now: tools/probe.py.
                                   r31 is hi8(V) from since, as for pr_t */
    ldi r24, 9
1:  ld r25, Z+
    st X+, r25
    dec r24
    brne 1b
    in r24, PLL_MISS
    st X+, r24
    in r24, OCR1C
#endif
    st X, r24
    ret

#ifdef PROBE
/* The probe's second: every 1000 ticks, the counts so far into pr_snap and
   back to zero. Two bytes at a time with interrupts off, as ANA_COMP writes
   them */
/* pr_t moves on by 1000 exactly; then ac_n, ac_in, ac_j each go to the
   pr_snap word six bytes on (Z + 4 once Z has passed the word), and Z
   ends at pr_snap. Interrupts are off for six instructions at a time.
   Changes r18, r19, r24, r25, Z */
probe_poll:
    ldi r30, lo8(pr_t)
    rcall since
    subi r24, lo8(1000)
    sbci r25, hi8(1000)
    brcs 9f
    subi r18, lo8(-1000)
    sbci r19, hi8(-1000)
    st Z+, r18
    st Z+, r19
    ldi r24, 3
1:  cli
    ld r18, Z
    ldd r19, Z + 1
    st Z+, r1
    st Z+, r1
    sei
    std Z + 4, r18
    std Z + 5, r19
    dec r24
    brne 1b
    ldd r24, Z + 6              /* Z is pr_snap: the second's number */
    inc r24
    std Z + 6, r24
9:  ret
#endif

/*
 * clocks_poll (DigiMacro.ino): every clock that compares sixteen bits of
 * milliseconds and so must be looked at within 32 or 65 s, and the ones
 * that must act on time -- the window shutting, the key forgotten -- even
 * while a macro plays and app_loop does not run. From app_loop and, all
 * through a macro, from touched. Does not call corePoll. Changes r18-r25,
 * r0, Z; keeps X.
 */
clocks_poll:
#ifdef PROBE
    rcall probe_poll
#endif
    rcall clock_poll
    rcall lock_poll
    rcall hidAllowed
    rcall key_ok
    sbis F2, F2_PAUSING
    ret
    rcall pause_over
    brcs 9f
    cbi F2, F2_PAUSING
9:  ret

/*
 * XTEA, encrypt, 32 cycles: r25:r24 v[8], r23:r22 key[16]. app/xtea.S with
 * the registers moved off r2-r6, the interrupts': v0 r20-23, v1 r24-27, sum
 * r16-19, T r8-11, U r12-15, cycles r7, the key at Y. Saved through the
 * register file, as there; v's address waits on the stack. Keeps X.
 *
 * v is v0 then v1, the key k[0..3], all little-endian 32-bit words, as
 * web/table.js has them; v is encrypted in place. Counter mode never
 * decrypts, so this only encrypts. Each half-cycle is v0 += F(v1), then
 * v0 and v1 swapped, by the same subroutine; two swaps a cycle leave them
 * where they started.
 *
 * The register file is at data addresses 0-31, so r7-r17 are saved by a
 * loop of ld Z+ / push, not eleven pushes, and put back the same way.
 * Keeps r1-r17, X and Y (and r2-r6, the interrupts', it never touches);
 * changes r18-r25, r30, r31, r0. Stack: 21 bytes, with its own return and
 * half's. About 6,500 cycles, 0.4 ms, all with interrupts on. Called from
 * secret_byte (main context), hidKeyAuth and hidProve (inside corePoll).
 */
    /* r7 to r17 pushed, by their data addresses 7..17 */
xtea:
    push r26
    push r27
    push r28
    push r29
    ldi r30, 7
    clr r31
    /* After this loop: Y is the key; v's address waits on the stack for
       the end; v0 into r20-r23, v1 into r24-r27; sum = 0; r7 counts the 32
       cycles */
1:  ld r0, Z+
    push r0
    cpi r30, 18
    brne 1b
    movw r28, r22
    push r24
    push r25
    movw r30, r24
    ld r20, Z+
    ld r21, Z+
    ld r22, Z+
    ld r23, Z+
    ld r24, Z+
    ld r25, Z+
    ld r26, Z+
    ld r27, Z
    clr r16
    clr r17
    movw r18, r16
    ldi r30, 32
    mov r7, r30
    /* A cycle: v0 += F(v1) with k[sum & 3]; sum += 0x9E3779B9, done as
       subtracting 0x61C88647; then v1 += F(v0) with k[(sum >> 11) & 3],
       sum's bits 11-12 being bits 3-4 of its second byte. After: v back
       to its address, and r17 down to r7 popped back into place */
2:  mov r30, r16
    rcall half
    subi r16, 0x47
    sbci r17, 0x86
    sbci r18, 0xC8
    sbci r19, 0x61
    mov r30, r17
    lsr r30
    lsr r30
    lsr r30
    rcall half
    dec r7
    brne 2b
    pop r31
    pop r30
    st Z+, r20
    st Z+, r21
    st Z+, r22
    st Z+, r23
    st Z+, r24
    st Z+, r25
    st Z+, r26
    st Z, r27
    ldi r30, 18
    clr r31
3:  pop r0
    st -Z, r0
    cpi r30, 7
    brne 3b
    pop r29
    pop r28
    pop r27
    pop r26
    ret

/* half: v0 += (((v1 << 4) ^ (v1 >> 5)) + v1) ^ (sum + k[r30 & 3]), then
   v0 and v1 swapped. T in r8-r11, U in r12-r15, the key at Y.
   Changes r8-r15, r20-r27, r30, r31 */
half:                           /* r30: which word of the key, in its low bits */
    movw r8, r24                /* T = v1 << 4 */
    movw r10, r26
    ldi r31, 4
1:  lsl r8
    rol r9
    rol r10
    rol r11
    dec r31
    brne 1b
    movw r12, r24               /* U = v1 >> 5 */
    movw r14, r26
    ldi r31, 5
2:  lsr r15
    ror r14
    ror r13
    ror r12
    dec r31
    brne 2b
    eor r8, r12
    eor r9, r13
    eor r10, r14
    eor r11, r15
    add r8, r24                 /* + v1 */
    adc r9, r25
    adc r10, r26
    adc r11, r27
    andi r30, 3                 /* U = sum + k[i] */
    lsl r30
    lsl r30
    add r30, r28
    mov r31, r29
    adc r31, r1
    ld r12, Z+
    ld r13, Z+
    ld r14, Z+
    ld r15, Z
    add r12, r16
    adc r13, r17
    adc r14, r18
    adc r15, r19
    eor r8, r12
    eor r9, r13
    eor r10, r14
    eor r11, r15
    add r20, r8                 /* v0 += ; then swap v0 and v1 */
    adc r21, r9
    adc r22, r10
    adc r23, r11
    movw r30, r20
    movw r20, r24
    movw r24, r30
    movw r30, r22
    movw r22, r26
    movw r26, r30
    ret

/* ------------------------------------------------------------------ */
/* Requests: from inside the USB driver, so the core's registers are  */
/* kept: only r18-r27, r30, r31 here. Y is put back.                  */
/* ------------------------------------------------------------------ */

/*
 * app_request: APP_REQUEST, slot 2 (hidusb.c app_request; core/core.h
 * says when the core calls it). v, r25:r24, is the 36-byte vendor report
 * the host set: [0] the verb f, [1..2] an address, [3] a count n, [4..35]
 * data. The answer goes back in the same report, which the host then
 * reads. -> r24: 1, answered (perhaps as REFUSED in [0]); 0, a verb this
 * does not know, which the core refuses.
 *
 *   no gate        0x69 LEARN, 0x40 INFO, 0x6A KEYAUTH, 0x6B PROVE,
 *                  0x6C with n = 0 (read the clock)
 *   window, then   0x62 UNLOCK
 *   window and     0x68 UPDATE, 0x42 RESTART, 0x60 READ, 0x61 WRITE,
 *   password       0x63 SETPASS, 0x6C with n = 4 (set the clock)
 *   anything else  0, for the core to refuse (0x64, v2's HOSTAUTH, too)
 *
 * The ways out: reqOk answers; reqDone wipes the data first, for anything
 * that carried or might have carried a secret; reqRefuse wipes it and puts
 * REFUSED in [0]; reqRefuseKept does the latter only (PROVE's: it carried
 * nothing secret, and the page needs nothing back). reqOut returns r24.
 *
 * Runs inside corePoll, in the main context, with interrupts on: whatever
 * the app was doing when it called corePoll is paused here. So it keeps
 * r2-r17 (xtea saves what it borrows) and puts Y back: inside the core, Y
 * is the core's. Changes r18-r27, r30, r31, r0, and RAM as each verb says.
 * Never calls corePoll. Slow only for WRITE: up to 32 EEPROM writes of
 * 3.4 ms, during which nothing else runs but the interrupts.
 *
 * Along the way: r21 f, r20 n clamped to 32, X the data (v + 4); r22 n and
 * r25:r24 the data again, for the verbs that take (p, n).
 */
    .global app_request
app_request:                    /* r25:r24 v -> r24 */
    push r28
    push r29
    ldi r28, lo8(V)
    ldi r29, hi8(V)
    movw r26, r24               /* v */
    ld r21, X                   /* f */
    adiw r26, 3
    ld r20, X+                  /* n, X now the data */
    cpi r20, 33
    brcs 1f
    ldi r20, 32
    /* LEARN (hidLearn): the next button is typed, not played, if it comes
       within ten seconds: see ir_poll */
1:  mov r22, r20
    movw r24, r26
    cpi r21, 0x69               /* LEARN */
    brne 2f
    std Y + Q(learn_at), r3
    sbi FLAGS, FLAG_LEARN
    rjmp reqOk
2:  cpi r21, DM_FUNC_INFO
    brne 3f
    rcall info
    rjmp reqOk
3:  cpi r21, 0x6A               /* KEYAUTH: no gate */
    brne 4f
    rcall hidKeyAuth
    tst r24
    brne 31f
    rjmp reqRefuse
    /* Right or wrong, the key is wiped from the report */
31: rjmp reqDone
4:  cpi r21, 0x6B               /* PROVE: no gate, and nothing to wipe */
    brne 5f
    rcall hidProve
    tst r24
    brne reqOk
    rjmp reqRefuseKept
5:  cpi r21, 0x6C               /* the clock: read it freely, set it gated */
    brne 51f
    tst r20
    brne 6f
    rcall clock_read
    rjmp reqOk
    /* 0x60..0x63 is f - 0x60 below 4, unsigned. Anything else is not
       ours: 0, and the core refuses it */
51: cpi r21, 0x68               /* the rest: UPDATE, RESTART, and READ, */
    breq 6f                     /* WRITE, UNLOCK, SETPASS, by their range */
    cpi r21, 0x42
    breq 6f
    mov r24, r21
    subi r24, 0x60
    cpi r24, 4
    brcs 6f
    clr r24
    rjmp reqOut
    /* The gates (hidusb.c): the window says somebody is at the board, the
       password that they are the right somebody. UNLOCK needs only the
       first, being how the second is passed. hidAllowed and hidUnlocked
       keep r20, r21 and X, which carry on past them */
    /* Refused, it is still wiped: it may carry a password. Setting the
       clock takes four bytes, no more */
6:  rcall hidAllowed
    tst r24
    breq reqRefuse
    cpi r21, DM_FUNC_UNLOCK
    breq 7f
    rcall hidUnlocked
    tst r24
    breq reqRefuse
    cpi r21, 0x6C
    brne 60f
    cpi r20, 4
    brne reqRefuse
    rcall clock_set
    rjmp reqOk
    /* r19:r18 = v[1..2]. UPDATE: the core may take pages for six seconds
       (layout.h) */
60: movw r30, r26               /* addr: the gates' calls took it */
    sbiw r30, 3
    ld r18, Z+
    ld r19, Z
    cpi r21, 0x68
    brne 61f
    out DM_WINDOW, r1
    rjmp reqOk
    /* RESTART: noted here, done in hidPoll 50 ms on, once the host has
       had its answer */
61: cpi r21, 0x42
    brne 62f
    sbi F2, F2_RESTART
    ldi r30, lo8(restartAsked)
    rcall stamp
    rjmp reqOk
62: cpi r21, 0x63
    breq 8f
    cpi r21, 0x60
    breq reqRead
    /* WRITE */
    /* n bytes from X to EEPROM from addr, stopping at its end (r19 < 2:
       512 bytes) */
21: tst r20
    breq 22f
    cpi r19, 2
    brcc 22f
    movw r24, r18
    ld r22, X+
    rcall eeput
    subi r18, 0xFF
    sbci r19, 0xFF
    dec r20
    rjmp 21b
    /* After a WRITE the secrets, or their check value, may not be what the
       key was for any more: it is forgotten */
22: rcall hidKeyForget
reqDone:
    rcall wipe
reqOk:
    ldi r24, 1
reqOut:
    pop r29
    pop r28
    ret

    /* UNLOCK (hidusb.c): the core checks it and keeps the session; what it
       means for the lockout is ours. Right, with a password: the lockout
       cleared. Wrong: a wrong code. Turned away unread (2: within a second
       of a wrong one, or too long): nothing */
    /* r25:r24 and r22 are the arguments again: hidAllowed took r24 */
7:  mov r22, r20                /* UNLOCK: r25:r24 and r22 still the arguments */
    movw r24, r26
    rcall hidUnlock
    cpi r24, 1
    brne 71f
    rcall hidPassSet
    tst r24
    breq reqDone
    rcall hidPassRight
    rjmp reqDone
71: tst r24
    brne reqRefuse
    rcall lock_miss
reqRefuse:
    rcall wipe
reqRefuseKept:
    rcall hidReport
    movw r30, r24
    ldi r24, REFUSED
    st Z, r24
    rjmp reqOk

    /* SETPASS (hidusb.c): twelve bytes at PASS_ADDR, the password and
       zeros; n = 0 clears it, 0xFF in all twelve. Then a clean slate for
       the lockout, and the new one given to the core, so the session goes
       on under it */
8:  cpi r20, 13                 /* SETPASS */
    brcc reqRefuse
    movw r30, r26               /* d, kept for hidUnlock */
    ldi r19, PASS_ADDR
    clr r18                     /* i */
81: ldi r22, 0xFF
    tst r20
    breq 82f
    ldi r22, 0
    cp r18, r20
    brcc 82f
    ld r22, X+
    /* eeput1 keeps Z, and so does lock_clear */
82: mov r24, r19
    rcall eeput1
    inc r19
    inc r18
    cpi r18, 12
    brne 81b
    rcall lock_clear
    movw r24, r30
    mov r22, r20
    rcall hidUnlock
    rjmp reqDone

    /* READ: n bytes of EEPROM from addr into the report, 0xFF past its end.
       Not wiped: the answer is the point */
reqRead:
    tst r20
    breq reqOk
    ldi r24, 0xFF
    cpi r19, 2
    brcc 1f
    movw r24, r18
    rcall ee
1:  st X+, r24
    subi r18, 0xFF
    sbci r19, 0xFF
    dec r20
    rjmp reqRead

/*
 * info: the app's half of INFO (hidusb.c's case DM_FUNC_INFO): the core
 * has filled in its own. [5..6] the EEPROM's size, 512; [7], the state
 * byte, ORed with the window (hidAllowed's 0 or 1 is DM_ST_OPEN), the lock
 * state and ST_KEY. hidAllowed, hidLockState and key_ok all keep r22 and X,
 * which this relies on. Changes r18-r26, Z.
 */
info:                           /* r26 is the data, v+4: INFO's bytes */
    adiw r26, 1
    st X+, r1                   /* EEPROM size: 512 */
    ldi r24, 2
    st X+, r24
    rcall hidAllowed
    mov r22, r24
    rcall hidLockState
    or r22, r24
    rcall key_ok
    breq 1f
    ori r22, ST_KEY
1:  ld r24, X
    or r24, r22
    st X, r24
    ret

/* ------------------------------------------------------------------ */
/* Touch                                                              */
/* ------------------------------------------------------------------ */

/*
 * The pad on P0 needs no parts (DigiMacro.ino, "Capacitive touch", says
 * how and why at length): drive it low to empty it, let the chip's own
 * pull-up fill it, and count until the pin reads high. A finger adds
 * capacitance, so the count goes up. One count is too coarse, so a
 * reading is the sum of eight charge cycles, each capped at 31; and the
 * highest of four readings wins, since an interrupt during a count lets
 * the pad charge uncounted and can only make a reading low.
 */

/*
 * touch_read -> r24: the highest of four readings, each the sum of eight
 * charge cycles of up to 31 counts: 0..248 (DigiMacro.ino touch_read and
 * touch_once, TOUCH_PULLUP 1). Interrupts stay on throughout. Changes
 * r18-r20, r24, r25; keeps r22, r23 (touch_poll's "now").
 *
 * Cycle-counted. The count's loop at 4: is 7 cycles a count (sbic skipping
 * 2, cpi 1, breq 1, subi 1, rjmp 2), 424 ns, exactly as the C compiles:
 * every reading is in those units, the thresholds and the test vectors
 * (test/diff's p-scenarios compare readings with the C's) depend on them,
 * and a pad tuned on one would read differently on another. The empty at
 * 3: is 50 cycles, 3.03 us, the C's _delay_us(3): ldi, 16 times dec and
 * brne less one, and an rjmp .+0, a two-cycle no-op in one word.
 */
touch_read:                     /* -> r24: the highest of four readings. */
    clr r24                     /* Cycle for cycle the C's: the readings */
    ldi r19, 4                  /* depend on it. Keeps r22 r23 */
    /* A reading: the sum of eight charge cycles */
1:  clr r25
    ldi r20, 8
    /* A cycle. PORTB's bit low before DDRB's goes up, or the pin would
       drive high for a moment: then it empties the pad */
2:  cbi PORTB, PIN_TOUCH
    sbi DDRB, PIN_TOUCH
    ldi r18, 0x10
    /* 50 cycles of emptying, 3.03 us; then an input again, and the
       pull-up fills the pad */
3:  dec r18
    brne 3b
    rjmp .+0
    cbi DDRB, PIN_TOUCH
    sbi PORTB, PIN_TOUCH
    ldi r18, 0
    /* Count while it reads low, up to 31: 7 cycles a count */
4:  sbic PINB, PIN_TOUCH
    rjmp 5f
    cpi r18, 0x1F
    breq 5f
    subi r18, 0xFF
    rjmp 4b
    /* The pull-up off between cycles; the highest of the four kept */
5:  cbi PORTB, PIN_TOUCH
    add r25, r18
    subi r20, 1
    brne 2b
    cp r24, r25
    brcc 6f
    mov r24, r25
6:  subi r19, 1
    brne 1b
    ret

/*
 * touch_poll (DigiMacro.ino): read the pad and decide touch_state. base is
 * the average / 16; a finger is a reading above base + base/4 + 2, and
 * once a finger, above base + base/8 + 1 still is (hysteresis). In 8 bits
 * as in the C: base + thr wraps past 255, so a pad that saturates at 248
 * reads as a finger. Untouched, the average closes 1/16 of the gap to the
 * reading once every 100 ms, a time constant of about 1.6 s. Touched for
 * more than 10 s, the "finger" is taken for drift and the average jumps to
 * meet it (touch_seed). Changes r0, r18-r25.
 */
touch_poll:
    movw r22, r2                /* now, from before the reading */
    rcall touch_read
    ldd r18, Y + Q(touch_base)
    ldd r19, Y + Q(touch_base) + 1
    ldi r20, 4
    /* base = touch_base >> 4 (the loop at 1:), thr = base/4. A finger
       already: thr = base + base/8 + 1; none (2:): base + base/4 + 2 */
1:  lsr r19
    ror r18
    dec r20
    brne 1b
    mov r20, r18                /* base */
    lsr r18
    lsr r18                     /* thr */
    ldd r19, Y + Q(touch_state)
    tst r19
    breq 2f
    lsr r18
    inc r18
    rjmp 3f
2:  subi r18, -2
    /* reading > thr: a finger */
3:  add r18, r20
    ldi r19, 0
    cp r18, r24
    brcc 4f
    ldi r19, 1
    /* Untouched: may the average move yet? If so, touch_base += reading
       - base */
4:  std Y + Q(touch_state), r19
    tst r19
    brne 6f
    movw r18, r22
    ldd r0, Y + Q(touch_moved)
    sub r18, r0
    ldd r0, Y + Q(touch_moved) + 1
    sbc r19, r0
    cpi r18, 100
    cpc r19, r1
    brcs 5f
    std Y + Q(touch_moved), r22
    std Y + Q(touch_moved) + 1, r23
    ldd r18, Y + Q(touch_base)
    ldd r19, Y + Q(touch_base) + 1
    add r18, r24
    adc r19, r1
    sub r18, r20
    sbc r19, r1
    std Y + Q(touch_base), r18
    std Y + Q(touch_base) + 1, r19
5:  std Y + Q(touch_since), r22
    std Y + Q(touch_since) + 1, r23
    ret
    /* Touched: for too long? 10001 is the C's "> TOUCH_STUCK_MS". The
       brcc is a tail call into touch_seed */
6:  movw r18, r22
    ldd r0, Y + Q(touch_since)
    sub r18, r0
    ldd r0, Y + Q(touch_since) + 1
    sbc r19, r0
    ldi r20, hi8(10001)
    cpi r18, lo8(10001)
    cpc r19, r20
    brcc touch_seed
    ret
/* touch_seed (DigiMacro.ino): touch_base = r24 << 4, the average put where
   a reading says the pad is: at power-up, and after a touch too long to be
   one. Changes r18, r24 */
touch_seed:                     /* r24: the average, at a reading */
    mov r18, r24
    swap r18
    andi r18, 0x0F
    swap r24
    andi r24, 0xF0
    std Y + Q(touch_base), r24
    std Y + Q(touch_base) + 1, r18
    ret

/* ------------------------------------------------------------------ */
/* Tapping                                                            */
/* ------------------------------------------------------------------ */

/*
 * The morse: tapping a code into code_bits and code_len, and the hold.
 * app_loop ends a code after 700 ms of silence (code_finish).
 */

/* code_reset (DigiMacro.ino): no symbols, and no code open */
code_reset:
    std Y + Q(code_bits), r1
    std Y + Q(code_len), r1
    cbi FLAGS, FLAG_CODE_OPEN
    cbi FLAGS, FLAG_CODE_OVER
    ret

/*
 * handle_input (DigiMacro.ino): touch_state, debounced, into dots, dashes
 * and the hold. A new level must last 25 ms before it is believed (in_seen
 * and in_since watch it), which filters a dropped sample, where locking
 * out after an edge did not. Then, on the believed level:
 *   pressed     press_start stamped; the press neither consumed nor held
 *   released    unless consumed: one symbol, a dash if held 250 ms or
 *               more, into code_bits; a sixth marks the code over
 *               (FLAG_CODE_OVER). last_release stamped and the code open:
 *               app_loop finishes it after 700 ms of silence
 *   held 4 s    once a press: the window opens for 30 s (the C's
 *               hidAllow), the code so far is dropped, and the press is
 *               consumed, so its release is no dash
 * Changes r18-r21, r24, r25, Z.
 */
handle_input:
    ldd r20, Y + Q(touch_state)
    ldd r21, Y + Q(in_seen)
    cp r20, r21
    breq 1f
    std Y + Q(in_seen), r20
    ldi r30, lo8(in_since)
    rcall stamp
1:  ldd r21, Y + Q(in_down)
    cp r20, r21
    breq 5f
    ldi r30, lo8(in_since)
    rcall since
    cpi r24, 25
    cpc r25, r1
    brcs 5f
    std Y + Q(in_down), r20
    tst r20
    breq 2f
    ldi r30, lo8(press_start)
    rcall stamp
    cbi FLAGS, FLAG_CONSUMED
    cbi FLAGS, FLAG_HELD
    ret
2:  sbic FLAGS, FLAG_CONSUMED
    ret
    ldd r20, Y + Q(code_len)
    cpi r20, 5
    brcs 3f
    sbi FLAGS, FLAG_CODE_OVER
    rjmp 4f
3:  ldi r30, lo8(press_start)
    rcall since
    ldd r18, Y + Q(code_bits)
    lsl r18
    ldi r19, hi8(250)
    cpi r24, lo8(250)
    cpc r25, r19
    brcs 31f
    ori r18, 1
31: std Y + Q(code_bits), r18
    inc r20
    std Y + Q(code_len), r20
    /* last_release: now, taken in one movw, as the register map asks. Two
       reads of r2 and r3 could straddle a tick's carry and leave it 255 ms
       ahead, which ended a tapped code at once */
4:  movw r24, r2
    std Y + Q(last_release), r24
    std Y + Q(last_release) + 1, r25
    sbi FLAGS, FLAG_CODE_OPEN
    ret
    /* No change believed. Held 4 s, and not yet fired for this press? */
5:  ldd r21, Y + Q(in_down)
    tst r21
    breq 9f
    sbic FLAGS, FLAG_HELD
    ret
    ldi r30, lo8(press_start)
    rcall since
    ldi r18, hi8(4000)
    cpi r24, lo8(4000)
    cpc r25, r18
    brcs 9f
    sbi FLAGS, FLAG_HELD
    sbi FLAGS, FLAG_CONSUMED
    rcall code_reset
    movw r24, r2
    subi r24, lo8(-30000)
    sbci r25, hi8(-30000)
    std Y + Q(allowUntil), r24
    std Y + Q(allowUntil) + 1, r25
    sbi F2, F2_OPEN
9:  ret

/* ------------------------------------------------------------------ */
/* Playback                                                           */
/* ------------------------------------------------------------------ */

/* r24 the header wanted -> r25:r24 just past it, zero flag if not there.
   X walks; ee keeps it */
/*
 * find_header (DigiMacro.ino): -> the zero flag clear and r25:r24 just
 * past the header, or the zero flag set if no entry has it. Walks the
 * table entry by entry -- [hdr][bytes][0x00], ended by any header whose
 * low three bits are 5 or more, 0xFF included (README, "The table") --
 * and each entry escape by escape, since an argument may be any byte, 0
 * and 0x80 included, and only a walk knows what it is reading:
 *   0x80 0x7F + 40   {totp}: the forty stepped over, whatever they hold
 *   0x80 a c x       {IR}: three bytes; asked for IR_WANT, compared with
 *                    ir_key, and a match answers just past them
 *   0x82             a mark: no argument
 *   other escapes    one argument
 * IR_WANT, 0xFF, can match no header, the walk stopping at any header
 * with a count above five first. Every step is bounded by TABLE_TOP, so a
 * table with no terminator stops there. Changes r18, r20, r22, r24-r27,
 * and Z when asked for IR_WANT; ee keeps X, the walk.
 */
find_header:
    mov r22, r24
    clr r26
    clr r27
1:  ldi r25, hi8(TABLE_TOP)
    cpi r26, lo8(TABLE_TOP)
    cpc r27, r25
    brcc not_found
    movw r24, r26
    adiw r26, 1
    rcall ee
    mov r18, r24
    andi r18, 7
    cpi r18, 5
    brcc not_found
    cp r24, r22
    breq found
2:  ldi r25, hi8(TABLE_TOP)
    cpi r26, lo8(TABLE_TOP)
    cpc r27, r25
    brcc 1b
    movw r24, r26
    adiw r26, 1
    rcall ee
    tst r24
    breq 1b
    cpi r24, 0x80               /* an {IR}: three bytes, and maybe the one */
    brne 5f
    movw r24, r26
    rcall ee
    cpi r24, TOTP_MARK          /* or a {totp}: its forty, whatever they hold */
    brne 21f
    adiw r26, 41
    rjmp 2b
21: clr r20
    cpi r22, IR_WANT
    brne 4f
    ldi r30, lo8(ir_key)
    ldi r31, hi8(ir_key)
3:  movw r24, r26
    add r24, r20
    adc r25, r1
    rcall ee
    ld r18, Z+
    cp r24, r18
    brne 4f
    inc r20
    cpi r20, 3
    brne 3b
4:  adiw r26, 3
    cpi r20, 3
    brne 2b
    /* found: and not_found: are shared exits: the zero flag says which */
found:
    movw r24, r26               /* never zero: a header comes before it */
    clz
    ret
5:  mov r18, r24
    andi r18, 0x7F
    cpi r18, 4
    brcc 2b
    cpi r24, 0x82
    breq 2b
    adiw r26, 1
    rjmp 2b
not_found:
    sez
    ret

/* Is anybody touching it? The clocks kept, the pad read. -> zero flag clear
   if so, and the press marked as seen */
/* touched (DigiMacro.ino touched, with stop_for_touch): asked between one
   thing and the next all through a macro. A touch stops the macro; the
   press is then marked seen and consumed, so that handle_input, which was
   not looking, makes no symbol of it -- but held on, it still opens the
   window, which is how a runaway macro gets mended. No corePoll here.
   Changes r0, r18-r25, Z; keeps X and r7-r17 */
touched:
    rcall clocks_poll
    rcall touch_poll
    ldd r24, Y + Q(touch_state)
    tst r24
    breq 9f
    std Y + Q(in_down), r24
    std Y + Q(in_seen), r24
    ldi r30, lo8(in_since)
    rcall stamp
    ldi r30, lo8(press_start)
    rcall stamp
    sbi FLAGS, FLAG_CONSUMED
    cbi FLAGS, FLAG_HELD
    clz
9:  ret

/*
 * hidPoll (hidusb.c): corePoll, and then what has to wait for the loop. A
 * RESTART came inside the USB driver, before the host saw its request
 * complete; 50 ms on, it is done here: the window shut, the key forgotten,
 * the report wiped, and the core off the bus and back (hidRestart), as if
 * replugged. Changes what corePoll changes: r18-r27, r30, r31, r0.
 */
hidPoll:
    rcall corePoll
    sbis F2, F2_RESTART
    ret
    ldi r30, lo8(restartAsked)
    rcall since
    cpi r24, 51
    cpc r25, r1
    brcs 9f
    cbi F2, F2_RESTART
    cbi F2, F2_OPEN
    rcall hidKeyForget
    rcall wipe
    rcall hidRestart
9:  ret

/* r24 tenths -> zero flag clear if a touch stopped it. Each tenth runs
   from where the last was due to end. sendT and r11 are free: nothing sends
   while a pause waits */
/* pause_poll (DigiMacro.ino): hidPoll and touched in turn, so USB, the
   clocks and the pad all run; since() leaves the tenth's start in
   r19:r18 and Z at sendT, and 2: moves it on by exactly 100. Changes r11,
   and what hidPoll and touched change */
pause_poll:
    mov r11, r24
    ldi r30, lo8(sendT)
    rcall stamp
1:  ldi r30, lo8(sendT)
    rcall since
    cpi r24, 100
    cpc r25, r1
    brcc 2f
    rcall hidPoll
    rcall touched
    breq 1b
    ret
2:  subi r18, -100
    sbci r19, -1
    st Z, r18
    std Z+1, r19
    dec r11
    brne 1b
    sez
    ret

/* Takes a byte of the macro being played -- at a, advancing it -> r24 */
/*
 * nextb falls through into secret_byte, with r24 the old a.
 *
 * secret_byte (DigiMacro.ino): plain while sec_at is 0, a tail jump into
 * ee. Inside a {secret}, byte j = a - sec_at is the stored byte XOR the
 * first byte of XTEA, under the key, of the block (nonce, j) -- two
 * little-endian words, j's only byte its lowest, a secret being at most
 * 256 bytes (web/table.js seals them so). The block is built in sec_ks
 * from sec_nonce, through Z, and encrypted in place. 0, which ends the
 * macro, if the key has gone since the secret began: key_ok runs from
 * clocks_poll between bytes. One XTEA a byte, 0.4 ms.
 * Changes r0, r18-r25, r30, r31; keeps X and r7-r17.
 */
nextb:
    movw r24, r16
    subi r16, 0xFF
    sbci r17, 0xFF

/* r25:r24 a -> r24: the byte, or decrypted; 0 if the key has gone */
secret_byte:
    ldd r18, Y + Q(sec_at)
    ldd r19, Y + Q(sec_at) + 1
    mov r0, r18
    or r0, r19
    brne 1f
    rjmp ee
1:  ldd r0, Y + Q(key_left)
    tst r0
    breq 9f
    push r24                    /* a */
    push r25
    sub r24, r18
    ldi r30, lo8(sec_nonce)
    ldi r31, hi8(sec_nonce)
    ldd r18, Z+0
    ldd r19, Z+1
    ldd r20, Z+2
    ldd r21, Z+3
    std Z+4, r18
    std Z+5, r19
    std Z+6, r20
    std Z+7, r21
    std Z+8, r24
    std Z+9, r1
    std Z+10, r1
    std Z+11, r1
    ldi r24, lo8(sec_ks)
    ldi r25, hi8(sec_ks)
    ldi r22, lo8(sec)
    ldi r23, hi8(sec)
    rcall xtea
    pop r25
    pop r24
    rcall ee
    lds r0, sec_ks
    eor r24, r0
    ret
9:  clr r24
    ret

/* ---- {totp} ----------------------------------------------------------
 *
 * In the table: 0x80 0x7F, then forty bytes: the HMAC midstates after the
 * key's ipad and opad blocks, each reversed byte for byte, inner first,
 * because this reads them from the top down. Whether plain or inside a
 * {secret}, they come through secret_byte, a byte at a time.
 *
 * SHA-1's schedule W sits in the core's page buffer, so nothing here calls
 * corePoll while W holds anything but zeros, and a half-received page -- whose
 * bytes share that memory -- refuses the macro rather than being overwritten.
 * a..e and the digits are in totp_s; all of it, and the stack and registers,
 * is wiped before the macro goes on.
 */

/*
 * How a code is made (RFC 4226, 6238; README, "One-time codes"):
 *
 *   T       seconds / 30, as eight bytes big-endian
 *   inner   SHA-1's compression from the ipad midstate, of T padded
 *   digest  the same from the opad midstate, of inner padded
 *   n       the 31 bits at digest[digest[19] & 15], big-endian
 *   code    n mod 10^6: six digits, leading zeros and all
 *
 * The midstates are what SHA-1 holds after the key's ipad and opad
 * blocks (RFC 2104's precomputation). The page works them out from the
 * base32 secret, so the board never holds the secret, only 40 bytes that
 * make codes and cannot be turned back into it. Each message after its
 * midstate is then one block: T's 8 bytes or inner's 20, 0x80, zeros, and
 * the length in bits counting the key's block before it: 576 or 672.
 *
 * Memory: a..e at totp_s + 1, five big-endian words; W, the schedule, in
 * the page buffer (see W at the top), as a ring of sixteen words, W[t &
 * 15] replaced in place from round 16 on.
 *
 * The flow, totp on:
 *   1. all forty bytes inside the table, or stop
 *   2. the clock unset: type the six zeros the last wipe left, and stop
 *   3. a..e = the inner midstate (rdS); a page half received: give up
 *   4. W = T padded, compress, a..e += the midstate (addM), W zeroed
 *   5. corePoll (totp_gap), the one place USB runs in all this; give up
 *      if a page has begun, or a sealed blob's key has gone
 *   6. W = inner padded, a..e = the outer midstate, compress, addM, the
 *      six digits, W zeroed
 *   7. the digits typed, each a chance to touch and stop; then totpWipe,
 *      and the macro goes on
 * Every way out wipes what held anything key-derived: totp_s, sec_ks, the
 * free stack below us, r18-r27.
 *
 * Time, by counting cycles: a compress is about 14 ms (the rotations go a
 * bit at a time); a sealed midstate is 20 XTEAs, 8 ms, read twice a half
 * (rdS, then addM). So 3-4 and 6 each run some 30 ms with no corePoll,
 * inside the 45 ms or so core.h asks for. The interrupts run throughout.
 *
 * Registers: r16:r17 walks the blob; r11 rd20's end; r18 the round t;
 * r21 t / 20, which picks K and f. r15 is the macro's: totpDigits saves it.
 *
 * K: SHA-1's four round constants, as .long lays them out, little-endian:
 * e's bytes are added from its last, least significant, back, with K's
 * read by lpm from its first. P10: 10^6 down to 10, three bytes each,
 * little-endian, for the digits by repeated subtraction.
 */
/* Reversed: little-endian words of K, read with lpm from the bottom up */
K:  .long 0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xCA62C1D6
P10: .byte 0x40, 0x42, 0x0F, 0xA0, 0x86, 0x01, 0x10, 0x27, 0x00, 0xE8, 0x03, 0x00
    .byte 0x64, 0x00, 0x00, 0x0A, 0x00, 0x00

/* The word at X, rotated left by r23 */
/* The word is big-endian: X[0] its top byte. lsl puts X[0]'s top bit in
   the carry, and rol from X[3] back to X[0] shifts it in at the bottom:
   one bit, about 40 cycles; r23 times. X is kept; r23 ends 0; changes
   r0, r22 */
rolN:
    ld r0, X
    adiw r26, 4
    lsl r0
    ldi r22, 4
1:  ld r0, -X
    rol r0
    st X, r0
    dec r22
    brne 1b
    dec r23
    brne rolN
    ret

/* acc: r25:r24 += r0, a column's sum with room for its carries */
acc:
    add r24, r0
    adc r25, r1
    ret

/* Eighty rounds over W, on totp_s+1. X, Z, r18-r25 and r0: no USB here */
/*
 * compress: SHA-1's 80 rounds, on a..e at totp_s + 1 and the schedule in
 * W; the midstate is added after, by addM. Round t (r18):
 *   - a = rotl5(a), in place: "a5"
 *   - e += K[t / 20]; r21 = t / 20, by taking 20s off
 *   - X = &W[t & 15]. From t = 16, W[s] = rotl1(W[s] ^ W[s+2] ^ W[s+8] ^
 *     W[s+13]): in the ring, W[t-16], W[t-14], W[t-8] and W[t-3]. As byte
 *     offsets, +8, +32 and +52, mod 64: W is not 64-aligned, so the wrap
 *     is done on the offset, r24 starting at it + 12 and going on by 20
 *     (32, 52, 72 = 8)
 *   - T = a5 + f(b, c, d) + e + W[s], one byte column at a time from the
 *     least significant, with a 16-bit carry (acc); and on the way each
 *     word moves down a place, [a5 b c d e] becoming [T a5 b c d]
 *   - f: t / 20 odd, parity b ^ c ^ d. 0: Ch = d ^ (b & (c ^ d)). 2: Maj,
 *     as Ch(b ^ c, c, d) ^ c ^ d, so the same instructions serve both
 *   - b = rotl27(a5) = a, rotl5 and rotl27 being a whole turn; then
 *     c = rotl30(b), the old b
 * Interrupts run; nothing here may call corePoll, W being the core's.
 */
compress:
    clr r18                     /* t */
rnd:
    ldi r26, lo8(totp_s + 1)    /* a = rotl5(a), undone below */
    ldi r27, hi8(totp_s + 1)
    ldi r23, 5
    rcall rolN
    ldi r30, lo8(K - 4)         /* e += K[t / 20] */
    ldi r31, hi8(K - 4)
    mov r22, r18
    ldi r21, -1
1:  adiw r30, 4
    inc r21
    subi r22, 20
    brcc 1b
    ldi r26, lo8(totp_s + 21)
    ldi r23, 4
    clc
2:  lpm r0, Z+
    ld r22, -X
    adc r0, r22
    st X, r0
    dec r23
    brne 2b
    clr r27                     /* X and Z now in W, in page 0 */
    clr r31
    mov r26, r18                /* X = &W[t & 15] */
    andi r26, 15
    lsl r26
    lsl r26
    subi r26, lo8(-(W))
    cpi r18, 16
    brcs 3f
    /* W[s] = rotl1(W[s] ^ W[s+2] ^ W[s+8] ^ W[s+13]) */
    ldi r23, 4
4:  ld r0, X
    mov r24, r26
    subi r24, W - 12            /* byte offsets +32, +52, +8, mod 64 */
    ldi r19, 3
5:  subi r24, -20
    andi r24, 63
    mov r30, r24
    subi r30, lo8(-(W))
    ld r22, Z
    eor r0, r22
    dec r19
    brne 5b
    st X+, r0
    dec r23
    brne 4b
    sbiw r26, 4
    ldi r23, 1
    rcall rolN
3:  adiw r26, 4
    /* T = rotl5(a) + f(b,c,d) + e + W, a column at a time with a 16-bit
       carry, shifting [a5 b c d e] to [T a5 b c d] on the way */
    ldi r30, lo8(totp_s + 5)
    ldi r31, hi8(totp_s + 5)
    clr r24
6:  sbiw r30, 1
    clr r25
    ldd r0, Z+16                /* e */
    rcall acc
    ldd r20, Z+4                /* b */
    ldd r22, Z+8                /* c */
    ldd r23, Z+12               /* d */
    std Z+16, r23
    std Z+12, r22
    std Z+8, r20
    ld r0, Z                    /* a5 */
    std Z+4, r0
    rcall acc
    ld r0, -X
    rcall acc
    mov r0, r20
    sbrc r21, 0
    rjmp 7f
    sbrc r21, 1                 /* Maj = Ch(b^c, c, d) ^ c ^ d */
    eor r0, r22
    eor r22, r23                /* Ch = d ^ (b & (c ^ d)) */
    and r0, r22
    eor r0, r23
    sbrc r21, 1
    eor r0, r22
    rjmp 8f
7:  eor r0, r22                 /* parity */
    eor r0, r23
8:  rcall acc
    st Z, r24
    mov r24, r25
    cpi r30, lo8(totp_s + 1)
    brne 6b
    ldi r26, lo8(totp_s + 5)    /* b = rotl27(a5) = a */
    ldi r27, hi8(totp_s + 5)
    ldi r23, 27
    rcall rolN
    adiw r26, 4                 /* c = rotl30(b) */
    ldi r23, 30
    rcall rolN
    inc r18
    cpi r18, 80
    breq 9f
    rjmp rnd
9:  ret

/* A midstate from the macro, into memory, top down: X one past its end, r11
   its start's low byte; the bytes from r16:r17 on */
/* Byte i of the blob, from r16:r17 + i, goes to start + 19 - i: the
   blob holds each midstate reversed, so it comes out in order, a first.
   Through secret_byte, so a sealed blob is decrypted on the way. X ends
   at the start. Changes r22, and what secret_byte changes */
rd20:
    mov r22, r11
    subi r22, -20
    sub r22, r26
    movw r24, r16
    add r24, r22
    adc r25, r1
    rcall secret_byte
    st -X, r24
    cp r26, r11
    brne rd20
    ret

/* rdS: a..e = the midstate at r16:r17 */
rdS:
    ldi r24, lo8(totp_s + 1)
    mov r11, r24
    ldi r26, lo8(totp_s + 21)
    ldi r27, hi8(totp_s + 21)
    rjmp rd20

/* The midstate back onto a..e, through W, which holds nothing live by now */
/* addM: a..e += the midstate at r16:r17, read again into W[0..19] (its
   bytes are not kept between: one less copy of them about), word by word
   from each word's last byte. The carry is cleared at the start of each
   word (r23 a multiple of 4); andi, dec and ld leave it alone between.
   Changes r0, r11, r22-r27, r30, r31, and what secret_byte changes */
addM:
    ldi r24, lo8(W)
    mov r11, r24
    ldi r26, lo8(W + 20)
    clr r27
    rcall rd20
    ldi r26, lo8(totp_s + 21)
    ldi r27, hi8(totp_s + 21)
    ldi r30, lo8(W + 20)
    clr r31
    ldi r23, 20
1:  mov r22, r23
    andi r22, 3
    brne 2f
    clc
2:  ld r0, -Z
    ld r22, -X
    adc r0, r22
    st X, r0
    dec r23
    brne 1b
    ret

/* W zero but for the 0x80 at r24's offset; its length ends 2, then r25 */
/* wFill: W = zeros, 0x80 at offset r24 (the message's end), and the
   length in bits big-endian in W[62..63]: 0x02, r25 -- 512 for the key's
   block, plus the message's 64 or 160. Changes r22, Z */
wFill:
    ldi r30, lo8(W)
    clr r31
    ldi r22, 64
1:  st Z+, r1
    dec r22
    brne 1b
    sbiw r30, 2
    ldi r22, 2
    st Z+, r22
    st Z, r25
    ldi r30, lo8(W)
    add r30, r24
    ldi r22, 0x80
    st Z, r22
    ret

/*
 * totp: {totp}, reached by a jump from macro_play's escape with r16:r17
 * at the 0x7F (the flow is in the block before K). Leaves by a jump
 * too: to loop, the macro going on past the blob, or to stop. The stack
 * is as loop has it: the refusal byte on top, which this never changes.
 */
totp:
    subi r16, 0xFF              /* past the mark, at the blob */
    sbci r17, 0xFF
    movw r24, r16               /* all forty inside the table, or none: above */
    adiw r24, 40                /* it are the serial and the password */
    ldi r18, hi8(TABLE_TOP + 1)
    cpi r24, lo8(TABLE_TOP + 1)
    cpc r25, r18
    brcs 0f
    rjmp stop
0:
    sbis F2, F2_CLOCK
    rjmp totpTypeZeros
1:  rcall rdS
    sbic GPIOR0, PAGE_HALF      /* W is another's: no code, and nothing typed */
    rjmp totpGiveUp
    /* W = T as 64 bits big-endian, then the padding: 64 + 8 bytes hashed */
    ldi r24, 8
    ldi r25, 0x40
    rcall wFill
    /* T = seconds / 30, by long division: nothing here to interrupt it */
    ldd r22, Y + Q(clk_secs)
    ldd r23, Y + Q(clk_secs) + 1
    ldd r24, Y + Q(clk_secs) + 2
    ldd r25, Y + Q(clk_secs) + 3
    clr r21
    ldi r20, 32
2:  lsl r22
    rol r23
    rol r24
    rol r25
    rol r21
    cpi r21, 30
    brcs 3f
    subi r21, 30
    inc r22
3:  dec r20
    brne 2b
    sts W + 4, r25
    sts W + 5, r24
    sts W + 6, r23
    sts W + 7, r22
    rcall compress
    rcall addM
    ldi r30, lo8(W)             /* zeros before USB may run again */
    clr r31
    ldi r24, 64
    rcall zero
    /* The one corePoll. A request may run in it: a WRITE forgets the key at
       once. (A RESTART only notes itself, and forgets the key later, in
       hidPoll, which nothing here calls: the "RESTART" in the comment
       below is more than can happen here.) A page's first half fills W:
       no code then. Otherwise the half done is in a..e, safe in totp_s */
totp_gap:                       /* for the test's eyes: no code here */
    rcall corePoll
    sbic GPIOR0, PAGE_HALF
    rjmp totpGiveUp
    ldd r24, Y + Q(sec_at)      /* a sealed code: USB may just have forgotten */
    ldd r25, Y + Q(sec_at) + 1  /* the key (WRITE, RESTART), and the half read */
    or r24, r25                 /* with it would be a wrong code */
    breq 11f
    ldd r24, Y + Q(key_left)
    tst r24
    brne 11f
    rjmp totpGiveUp
11:
    ldi r24, 20                 /* W = digest, padded: 64 + 20 bytes */
    ldi r25, 0xA0
    rcall wFill
    ldi r30, lo8(W)
    clr r31
    ldi r26, lo8(totp_s + 1)
    ldi r27, hi8(totp_s + 1)
    ldi r24, 20
4:  ld r0, X+
    st Z+, r0
    dec r24
    brne 4b
    subi r16, 0xEC              /* the outer midstate, 20 on */
    sbci r17, 0xFF
    rcall rdS
    rcall compress
    rcall addM
    subi r16, 0xEC              /* and past the blob */
    sbci r17, 0xFF
    /* truncate: 31 bits, at the digest's last nibble */
    lds r24, totp_s + 20
    andi r24, 15
    ldi r30, lo8(totp_s + 1)
    ldi r31, hi8(totp_s + 1)
    add r30, r24
    ldd r25, Z+0
    andi r25, 0x7F
    ldd r24, Z+1
    ldd r23, Z+2
    ldd r22, Z+3
    /* the six digits, 10^6 first to reduce it; [0] takes its count */
    ldi r26, lo8(totp_s)
    ldi r27, hi8(totp_s)
    ldi r30, lo8(P10)
    ldi r31, hi8(P10)
    ldi r20, 6
5:  lpm r18, Z+
    lpm r19, Z+
    lpm r21, Z+
    clr r0
6:  sub r22, r18
    sbc r23, r19
    sbc r24, r21
    sbc r25, r1
    brcs 7f
    inc r0
    rjmp 6b
7:  add r22, r18
    adc r23, r19
    adc r24, r21
    adc r25, r1
    st X+, r0
    dec r20
    brne 5b
    st X, r22
    ldi r30, lo8(W)             /* the schedule goes first: nothing in it */
    clr r31                     /* matters any more, and USB may run from here */
    ldi r24, 64
    rcall zero
    rjmp totpDigits

    /* The digits, [1..6] of totp_s, typed in order, each a chance for a
       touch to stop the macro. With the clock unset they are the zeros
       every wipe leaves, and the macro stops after them, so an {Enter}
       after the {totp} is not pressed. (F2_CLOCK is asked after the
       typing: a clock set by a request during the zeros lets the macro go
       on after them.) */
totpTypeZeros:                  /* nothing computed: the wipe left six zeros */
totpDigits:
    push r15                    /* the count of a repeat is the macro's */
    ldi r24, 6
    mov r15, r24
1:  rcall touched
    brne totpStopped
    ldi r30, lo8(totp_s + 7)    /* the digits start at [1] */
    sub r30, r15
    ldi r31, hi8(totp_s)
    ld r24, Z
    sbi PORTB, PIN_LED
    rcall type_nib
    dec r15
    brne 1b
    pop r15
    rcall totpWipe
    sbis F2, F2_CLOCK           /* unset: nothing after the zeros */
    rjmp stop
    rjmp loop
totpStopped:
    pop r15
totpGiveUp:
    rcall totpWipe
    rjmp stop

/* a..e, the digits, the keystream, the registers, the stack below us */
/* From _end, where the variables end, up to SP, the free stack: where
   compress, rd20 and xtea's frames were. r18-r27 by their data addresses,
   and r0. totp_wiped marks the end, for the tests. Changes r24-r27, r30,
   r31, and zeroes r0 and r18-r27 */
totpWipe:
    ldi r30, lo8(totp_s)
    ldi r31, hi8(totp_s)
    ldi r24, 21
    rcall zero
    ldi r30, lo8(sec_ks)
    ldi r31, hi8(sec_ks)
    ldi r24, 8
    rcall zero
    ldi r26, lo8(_end)
    ldi r27, hi8(_end)
1:  in r24, SPL
    in r25, SPH
    cp r24, r26                 /* SP itself: the next free byte */
    cpc r25, r27
    brcs 2f
    st X+, r1
    rjmp 1b
2:  ldi r30, 18                 /* r18-r27, through the register file */
    clr r31
3:  st Z+, r1
    cpi r30, 28
    brne 3b
    clr r0
totp_wiped:                     /* for the test's eyes: no code here */
    ret

/*
 * r25:r24 a -> r24, 0 or how a {secret} refused it. a r16:r17, mod r12,
 * lastkey r13, lastmod r14, reps r15 (or the count of a plain repeat, which
 * has no marks), mark r8:r9, jumps r7, refused on the way out in r24.
 */
/*
 * macro_play (DigiMacro.ino): play the macro whose bytes start at a, to
 * its 0x00, the table's top, a touch, or a refusal. The byte-code is
 * README's "The table":
 *   low 7 bits >= 4   a keystroke: bit 7 Shift, the rest a HID usage (key)
 *   0x82              a mark: a repeat's group starts here
 *   0x80 a c x        {IR}: nothing, played through; three bytes, bit 7 set
 *   0x80 0x7F + 40    {totp}
 *   0x01 nn           a pause of nn tenths
 *   0x02 mm           modifiers mm, for the next keystroke
 *   0x03 nn           the last keystroke nn more times; after a mark, the
 *                     group since the mark nn more times
 *   0x81 F7, F6       {clear}; the latched modifiers alone ({Gui})
 *   0x81 FE, FF       {secret}, {secret!}: the rest is encrypted
 *   0x81 hh           on with the macro whose header is hh - 1
 *   0x83 1, 2         v2's {host}, {host!}: refused, quietly, loudly
 *   0x83 nn, from 3   the mouse (hidPoint)
 * Any argument of 0, like any 0, ends the macro.
 *
 * -> r24: 0, or a refusal: 1, quiet (HOST_SILENT), or 2, as if the code
 * were not there (HOST_LOUD), which code_finish counts as a wrong code.
 * The answer lives on the stack from the start, one byte pushed as 0 and
 * replaced (pop, push) when a refusal sets it; stop pops it into r24, and
 * every way out goes through stop, the LED off.
 *
 * The LED comes on with the first thing that shows, a keystroke or a
 * pause, not when the macro is found: a {secret!} refused before anything
 * shows must look just like an empty code.
 *
 * Every pass of loop asks touched first: a touch stops any macro. A loop
 * of jumps that never types is bounded instead, by JUMP_MAX (r7).
 * Calls corePoll (through hidKey, pause_poll), and so app_request may run
 * in it; the state lives in r7-r17, which all of that keeps. Changes r0,
 * r7-r27, r30, r31. The {totp} path, with xtea inside it, is the deepest
 * stack there is.
 */
macro_play:
    movw r16, r24
    clr r12
    clr r13
    clr r14
    clr r15
    clr r7
    movw r8, r12
    std Y + Q(sec_at), r1
    std Y + Q(sec_at) + 1, r1
    ldi r30, 0                  /* refused */
    push r30
    /* The table's top, a touch, the next byte (0: the end) */
loop:
    ldi r25, hi8(TABLE_TOP)
    cpi r16, lo8(TABLE_TOP)
    cpc r17, r25
    brcc stop
    rcall touched
    brne stop
    rcall nextb
    tst r24
    breq stop
    mov r22, r24
    andi r24, 0x7F
    cpi r24, 4
    brcs escape
    /* A keystroke (from {Gui} too, with r22 0): bit 7 adds Shift; it
       becomes the last key for a repeat; the latched modifiers are spent;
       and the jumps start counting again, a keystroke being a chance to
       stop the macro */
key:                            /* r22 the byte */
    sbrs r22, 7
    rjmp 1f
    ldi r24, 2
    or r12, r24
1:  mov r14, r12
    mov r13, r22
    ldi r24, 0x7F
    and r13, r24
    clr r12
    clr r7
    sbi PORTB, PIN_LED
    mov r24, r14
    mov r22, r13
    rcall hidKey
    rjmp loop

    /* stop: the one way out, shared by every path, {totp}'s included */
stop:
    cbi PORTB, PIN_LED
    pop r24
    ret

    /* An escape, r19 = c. The mark and {IR}/{totp} take no argument byte;
       the rest take one, which must lie in the table and not be 0 */
escape:
    mov r19, r22                /* c */
    cpi r19, 0x82               /* mark: where it sits is its argument */
    brne 1f
    movw r8, r16
    rjmp loop
1:  cpi r19, 0x80               /* {IR}: three bytes, each with bit 7 set */
    brne 3f
    movw r24, r16               /* ...or {totp}, whose next byte has it clear */
    rcall secret_byte
    cpi r24, TOTP_MARK
    brne 11f
    rjmp totp
11: ldi r20, 3
2:  movw r24, r16
    push r20
    rcall secret_byte
    pop r20
    sbrs r24, 7
    rjmp stop
    subi r16, 0xFF
    sbci r17, 0xFF
    dec r20
    brne 2b
    rjmp loop
    /* An escape in the table's last byte would take its argument from the
       serial above it: stop. Then pause, modifiers, repeat, 0x81, 0x83 */
3:  ldi r25, hi8(TABLE_TOP)
    cpi r16, lo8(TABLE_TOP)
    cpc r17, r25
    brcc stop
    push r19
    rcall nextb
    pop r19
    tst r24
    breq stop
    mov r18, r24                /* arg */
    cpi r19, 1                  /* pause */
    brne 4f
    sbi PORTB, PIN_LED
    rcall pause_poll
    brne stop
    rjmp loop
4:  cpi r19, 2                  /* the modifier */
    brne 5f
    mov r12, r18
    rjmp loop
5:  cpi r19, 3                  /* repeat */
    brne 7f
    mov r0, r8
    or r0, r9
    brne 6f
    mov r15, r18
    sbi PORTB, PIN_LED
51: rcall touched
    brne stop2
    mov r24, r14
    mov r22, r13
    rcall hidKey
    dec r15
    brne 51b
    rjmp loop
    /* A plain repeat stopped by a touch. r15 is left as it is: nothing
       reads it again before macro_play clears it */
stop2:
    rjmp stop
    /* A repeat after a mark: r15 = 0 means the group has played once, so
       r15 = nn and back to the mark; then back again while --r15; at 0,
       the mark forgotten and on past the repeat */
6:  tst r15
    breq 61f
    dec r15
    breq 62f
    rjmp 63f
61: mov r15, r18
63: movw r16, r8
    rjmp loop
62: clr r8
    clr r9
    rjmp loop
    /* 0x81: {clear}, {Gui}, {secret}, or a jump. Without the key a
       {secret} is refused: FE - 0xFD is 1, quiet; FF, 2, loud. With it,
       the nonce is read in the clear and sec_at set: from here secret_byte
       decrypts */
7:  cpi r19, 0x81
    brne host
    cpi r18, 0xF7               /* {clear} */
    brne 71f
    rcall lock_clear
    rjmp loop
71: cpi r18, 0xF6               /* modifiers alone */
    brne 72f
    clr r22
    rjmp key
72: cpi r18, 0xFE
    brcs jump
    rcall key_ok
    brne 73f
    pop r0                      /* refused: 1 quiet, 2 loud */
    subi r18, 0xFD
    push r18
    rjmp stop
73: ldi r20, 4                  /* a secret: its nonce, read in the clear */
    ldi r30, lo8(sec_nonce)
    ldi r31, hi8(sec_nonce)
74: movw r24, r16
    subi r16, 0xFF
    sbci r17, 0xFF
    rcall ee
    st Z+, r24
    dec r20
    brne 74b
    std Y + Q(sec_at), r16
    std Y + Q(sec_at) + 1, r17
    rjmp loop
    /* A {goto}: on with the macro of header hh - 1, never to come back,
       in the clear, with no modifiers and no mark. Not there, or more than
       JUMP_MAX (100) jumps with no keystroke between: stop */
jump:
    mov r24, r18
    dec r24
    rcall find_header
    breq stop3
    inc r7
    ldi r18, 101
    cp r7, r18
    brcc stop3
    movw r16, r24
    std Y + Q(sec_at), r1
    std Y + Q(sec_at) + 1, r1
    clr r12
    clr r8
    clr r9
    clr r15
    rjmp loop
stop3:
    rjmp stop
    /* 0x83: c can be nothing else by here, escape having taken the other
       six (loop sends only c & 0x7F < 4 to it, 0 having stopped the
       macro). From 3 on, the mouse; 1 and 2, {host}, refused as it asked */
host:
    cpi r18, HOST_LOUD + 1
    brcs 81f
    mov r24, r18
    rcall hidPoint
    rjmp loop
81: pop r0
    push r18
    rjmp stop

/* play_header (DigiMacro.ino): find_header, then a tail jump into
   macro_play. Not there: HOST_LOUD, as a {secret!} that refused */
play_header:                    /* r24 -> r24 */
    rcall find_header
    breq 1f
    rjmp macro_play
1:  ldi r24, HOST_LOUD
    ret

/*
 * code_finish (DigiMacro.ino): the silence after a code. The header is
 * HDR_MAKE: (bits << 3) + len - 1. Locked, a code means nothing at all,
 * right or wrong. Otherwise it plays; a code that is not there, too long,
 * or whose {secret!} refused (HOST_LOUD), is a wrong code: lock_miss
 * BEFORE the three blinks, so pulling the plug at the blinks saves a
 * guesser nothing (DigiMacro.ino says more). A code that plays clears
 * nothing. r11 counts the blinks: nothing sends meanwhile.
 */
code_finish:
    ldd r20, Y + Q(code_len)
    ldd r24, Y + Q(code_bits)
    lsl r24
    lsl r24
    lsl r24
    add r24, r20
    dec r24                     /* the header */
    ldi r21, 1                  /* ok */
    sbic FLAGS, FLAG_CODE_OVER
    clr r21
    tst r20
    brne 1f
    clr r21
1:  push r24
    rcall code_reset
    rcall hidLockState
    pop r18
    tst r24
    brne 9f
    tst r21
    breq 2f
    mov r24, r18
    rcall play_header
    cpi r24, HOST_LOUD
    brne 9f
2:  rcall lock_miss
    ldi r24, 3                  /* three blinks, 70 ms each way */
    mov r11, r24
3:  sbi PORTB, PIN_LED
    rcall wait70
    cbi PORTB, PIN_LED
    rcall wait70
    dec r11
    brne 3b
9:  ret

/* wait70: 70 ms of hidPoll, for the blinks (the C's wait_poll) */
wait70:
    ldi r30, lo8(sendT)
    rcall stamp
1:  rcall hidPoll
    ldi r30, lo8(sendT)
    rcall since
    cpi r24, 70
    cpc r25, r1
    brcs 1b
    ret

/* ------------------------------------------------------------------ */
/* The remote                                                         */
/* ------------------------------------------------------------------ */

/*
 * int0: INT0, a falling edge on P2 (DigiMacro.ino ISR(INT0_vect); its
 * comment has the NEC format). An IR receiver's output goes low with the
 * carrier, so each falling edge starts a burst, and the time from one to
 * the next says what came. Timer0 runs free at CK/1024, 62 us a count,
 * and is read and cleared here, so each interval is one byte; one longer
 * than 255 counts (16 ms) sets TOV0, and is taken as 0xFF.
 *   161..233  10 to 14.5 ms: a frame starts, 32 bits to come (IR_LEFT).
 *             An LG remote's shortened burst gets in at 11.1; so does a
 *             repeat, 11.25, but no bits follow it
 *   12..45    0.8 to 2.8 ms, inside a frame: a bit, 1 from 28 (1.69 ms)
 *             up, shifted into IR_BITSH:IR_BITSL from the top: NEC sends
 *             the low bit first
 *   other     not a frame: IR_LEFT = 0
 * After 16 bits the low byte is the address: ir_addr. After 32 it is the
 * command, and the high byte must be its complement: if so, ir_cmd and
 * FLAG_IR, for ir_poll. The address's complement is not checked: extended
 * NEC uses that byte as more address.
 *
 * Interrupts are back on from the vector's first word, so they are off
 * for some ten cycles. SREG is kept in r5, so this must not nest: a real
 * frame's falling edges are 1.1 ms apart at least, against some 40 cycles
 * here, but edges microseconds apart (noise on P2) could land inside it
 * and corrupt the SREG it gives back. Pushes r16-r18, and r19 for a bit.
 */
int0:                           /* sei was in the vector */
    in r5, SREG
    push r16
    push r17
    in r16, TCNT0
    in r17, IR_LEFT
    out TCNT0, r1
    push r18
    in r18, TIFR
    sbrs r18, TOV0
    rjmp 1f
    ldi r18, 1 << TOV0
    out TIFR, r18
    ldi r16, 0xFF
    /* r16 the interval, r17 IR_LEFT, the bits still to come */
1:  cpi r16, 161
    brcs 2f
    cpi r16, 234
    brcc 2f
    ldi r17, 32
    rjmp 7f
2:  tst r17
    breq 3f
    cpi r16, 12
    brcs 3f
    cpi r16, 46
    brcs 4f
3:  clr r17
    rjmp 7f
4:  in r18, IR_BITSH
    lsr r18
    push r19
    in r19, IR_BITSL
    ror r19
    cpi r16, 28
    brcs 5f
    ori r18, 0x80
5:  out IR_BITSH, r18
    out IR_BITSL, r19
    dec r17
    cpi r17, 16
    brne 6f
    sts ir_addr, r19
    rjmp 65f
6:  tst r17
    brne 65f
    com r18
    cp r19, r18
    brne 65f
    sts ir_cmd, r19
    sbi FLAGS, FLAG_IR
65: pop r19
7:  out IR_LEFT, r17
    pop r18
    pop r17
    pop r16
    out SREG, r5
    reti

/* type_nib, type_hex (DigiMacro.ino): a nibble as a key, 0 to 9 and a to
   f, straight to HID usages, lower case: 0x27 is 0, 0x1E-0x26 are 1-9,
   0x04-0x09 a-f. For LEARN. Tail jumps into hidKey; change what it does */
type_nib:                       /* r24: its low four bits, as a key */
    andi r24, 0x0F
    mov r22, r24
    clr r24
    tst r22
    brne 1f
    ldi r22, 0x27
    rjmp hidKey
1:  cpi r22, 10
    brcc 2f
    subi r22, -0x1D
    rjmp hidKey
2:  subi r22, 6
    rjmp hidKey

type_hex:                       /* r24 */
    push r24
    swap r24
    rcall type_nib
    pop r24
    rjmp type_nib

/*
 * ir_poll (DigiMacro.ino): a button from int0 (FLAG_IR). After a LEARN,
 * within 40 x 256 ms, about ten seconds, it is typed as "aa cc" in hex,
 * for the page's {IR}; a LEARN nobody followed up is forgotten. Otherwise,
 * unless locked, it is played: ir_key gets the button as its {IR} holds
 * it -- a and c with bit 7 set, x = 0x80 plus their own top bits, a's in
 * bit 0 and c's in bit 1 -- and play_header(IR_WANT) types from just
 * after that {IR}, wherever it is, a tail jump. A button not in the table
 * does nothing and counts for nothing: rooms are full of other people's
 * remotes. ir_addr and ir_cmd are read without stopping int0, which a new
 * frame 108 ms on cannot race.
 */
ir_poll:
    lds r18, ir_addr
    lds r19, ir_cmd
    cbi FLAGS, FLAG_IR
    sbis FLAGS, FLAG_LEARN
    rjmp 2f
    cbi FLAGS, FLAG_LEARN
    ldd r20, Y + Q(learn_at)
    mov r24, r3
    sub r24, r20
    cpi r24, 40
    brcc 2f
    push r19
    mov r24, r18
    rcall type_hex
    clr r24
    ldi r22, 0x2C
    rcall hidKey
    pop r24
    rjmp type_hex
2:  push r18
    push r19
    rcall hidLockState
    pop r19
    pop r18
    tst r24
    brne 9f
    ldi r20, 0x80
    sbrc r18, 7
    ori r20, 1
    sbrc r19, 7
    ori r20, 2
    ori r18, 0x80
    ori r19, 0x80
    ldi r30, lo8(ir_key)
    ldi r31, hi8(ir_key)
    st Z+, r18
    st Z+, r19
    st Z, r20
    ldi r24, IR_WANT
    rjmp play_header
9:  ret

/* ------------------------------------------------------------------ */
/* The LED, setup, the loop                                           */
/* ------------------------------------------------------------------ */

/*
 * update_led (DigiMacro.ino), from app_loop only; a playing macro drives
 * the LED itself. The window first, so the owner sees a hold take even on
 * a locked board:
 *   window open   blinking, ticks & 0x200: about once a second
 *   hard lock     flicker, ticks & 0x40: eight times a second
 *   soft lock     a 60 ms blip every 2 s, (ticks & 0x7FF) < 60
 *   otherwise     dark, even while a code is tapped, so the LED does not
 *                 spell it out for a bystander
 * Bits of r2:r3 are read singly, which needs no movw. Changes r18-r25, Z.
 */
update_led:
    rcall hidAllowed
    tst r24
    breq 1f
    sbrs r3, 1                  /* a steady blink: ticks & 0x200 */
    rjmp led_on
    rjmp led_off
1:  rcall hidLockState
    cpi r24, ST_HARD
    brne 2f
    sbrs r2, 6                  /* fast: ticks & 0x40 */
    rjmp led_off
led_on:
    sbi PORTB, PIN_LED
    ret
2:  cpi r24, ST_SOFT
    brne led_off
    movw r24, r2                /* a blip: ticks & 0x7FF below 60 */
    andi r25, 7
    cpi r24, 60
    cpc r25, r1
    brcs led_on
led_off:
    cbi PORTB, PIN_LED
    ret

    .global app_setup
/*
 * app_setup: APP_SETUP, slot 0, once at power-up, before USB is connected
 * but with interrupts on (core.h; DigiMacro.ino app_setup and hidusb.c
 * hidBegin, ir_begin). Up to eight seconds of watchdog, so it may take its
 * time. In order: Y and every variable (no C runtime clears them); the
 * flags and the clock; the report ids; the pins and the pad's first
 * reading, which seeds the average, or every reading would look like a
 * touch; Timer1 and the comparator, so the tick and the frame lock run
 * from here; the serial, made once on a blank chip; the remote, after
 * the serial since the entropy borrows Timer0.
 *
 * TIMSK, MCUCR and GIMSK are written whole, not ORed: nothing has set them
 * since reset, and the core's usbInit, which comes later (hidRestart), ORs
 * its own PCIE into GIMSK. Comparator: ADCSRB's ACME routes the ADC's
 * multiplexer, ADMUX 3, ADC3 = PB3 = D-, to the comparator's - input (the
 * ADC stays off, as ACME needs).
 */
app_setup:
    ldi r28, lo8(V)
    ldi r29, hi8(V)
    movw r30, r28               /* the variables: nothing else clears them */
    ldi r24, pll_p + 2 - V
    rcall zero
    out FLAGS, r1
    out F2, r1
    clr r2
    clr r3
    ldi r24, 100
    mov r6, r24
    ldi r24, 1
    std Y + Q(kbd), r24
    ldi r24, 3
    std Y + Q(ptr), r24
    sbi DDRB, PIN_LED
    cbi DDRB, PIN_TOUCH
    cbi PORTB, PIN_TOUCH
    rcall touch_read
    rcall touch_seed
    ldi r24, 128                /* the period: 128.0, 1.00072727 ms */
    out OCR1C, r24
    sts pll_p + 1, r24
    ldi r24, 1                  /* the tick's interrupt: its start, so that */
    out OCR1A, r24              /* every edge in a tick comes after it */
#ifndef NOPLL
    ldi r24, 1 << ACME          /* comparator: bandgap against ADC3, D- */
    out ADCSRB, r24
    ldi r24, 3
    out ADMUX, r24
    ldi r24, ACSR_ON
    out ACSR, r24
#endif
    ldi r24, (1 << CTC1) | (1 << CS13)
    out TCCR1, r24
    ldi r24, 1 << OCIE1A        /* TIMSK, MCUCR, GIMSK: from reset, nothing */
    out TIMSK, r24              /* else has set them: the core's USB comes later */
    /* a chip with no serial makes one, once */
    ldi r20, 0xFF
    ldi r21, 0xE0
1:  mov r24, r21
    rcall ee1
    and r20, r24
    inc r21
    cpi r21, 0xE4
    brne 1b
    cpi r20, 0xFF
    brne 4f
    in r22, OSCCAL
    clr r23
    clr r24
    clr r25
    in r30, SREG
    cli
    ldi r18, 1
    out TCCR0B, r18
    ldi r18, (1 << WDCE) | (1 << WDE)
    out WDTCR, r18
    ldi r18, (1 << WDIF) | (1 << WDIE)
    out WDTCR, r18
    ldi r19, 64
    /* The serial (hidusb.c entropy): 64 times, wait for the watchdog's
       16 ms interrupt flag, its own 128 kHz oscillator jittering against
       the CPU's, then s = rotl3(s) ^ TCNT0, Timer0 at CK/1. s starts at
       OSCCAL. Interrupts off throughout, WDIE with them, so the WDT's slot
       is never jumped to; then the watchdog off, Timer0 stopped, SREG back,
       and the four bytes into 0x1E0, lowest first, where the core reads its
       serial number */
2:
5:  in r0, WDTCR
    sbrs r0, WDIF
    rjmp 5b
    ldi r18, (1 << WDIF) | (1 << WDIE)
    out WDTCR, r18
    ldi r18, 3
6:  mov r0, r25
    lsl r0
    rol r22
    rol r23
    rol r24
    rol r25
    dec r18
    brne 6b
    in r18, TCNT0
    eor r22, r18
    dec r19
    brne 2b
    wdr
    ldi r18, (1 << WDCE) | (1 << WDE)
    out WDTCR, r18
    out WDTCR, r1
    out TCCR0B, r1
    out SREG, r30
    push r25
    push r24
    push r23
    push r22
    ldi r21, 0xE0
3:  pop r22
    mov r24, r21
    rcall eeput1
    inc r21
    cpi r21, 0xE4
    brne 3b
    /* The remote (DigiMacro.ino ir_begin): P2's pull-up, so that with no
       receiver a floating pin is not a remote of noise; Timer0 free at
       CK/1024; INT0 on a falling edge */
4:  sbi PORTB, PIN_IR
    out TCCR0A, r1
    ldi r24, (1 << CS02) | (1 << CS00)
    out TCCR0B, r24
    ldi r24, 1 << ISC01
    out MCUCR, r24
    ldi r24, 1 << INT0
    out GIMSK, r24
    ret

    .global app_loop
/*
 * app_loop: APP_LOOP, slot 1, over and over (DigiMacro.ino app_loop). One
 * round of everything: USB, the pad, the morse, the clocks, the LED, the
 * remote. Then the power-on macro, once, when the host can take it: from
 * hidReady (enumerated), 250 ms after hidAwake (the host has written the
 * lock lights: it has a keyboard), or 1500 ms if it never does; not on a
 * hard-locked board, and not for an impossible header (none set). Then a
 * code 700 ms after its last symbol. Returns; the core calls corePoll and
 * then this again. Nothing is kept across calls but in RAM and the
 * global registers.
 */
app_loop:
    rcall hidPoll
    rcall touch_poll
    rcall handle_input
    rcall clocks_poll
    rcall update_led
    sbic FLAGS, FLAG_IR
    rcall ir_poll

    sbic FLAGS, FLAG_AUTODONE
    rjmp 3f
    rcall hidReady
    tst r24
    breq 3f
    sbic FLAGS, FLAG_AUTOWAIT
    rjmp 1f
    sbi FLAGS, FLAG_AUTOWAIT
    ldi r30, lo8(auto_since)
    rcall stamp
    rjmp 3f
1:  rcall hidAwake
    tst r24
    ldi r22, lo8(1500)
    ldi r23, hi8(1500)
    breq 2f
    ldi r22, lo8(250)
    ldi r23, hi8(250)
2:  ldi r30, lo8(auto_since)
    rcall since
    cp r24, r22
    cpc r25, r23
    brcs 3f
    sbi FLAGS, FLAG_AUTODONE
    ldi r24, AUTO_ADDR
    rcall ee1
    mov r18, r24
    andi r18, 7
    cpi r18, 5
    brcc 3f
    push r24
    rcall hidLockState
    pop r18
    cpi r24, ST_HARD
    breq 3f
    mov r24, r18
    rcall play_header

3:  sbis FLAGS, FLAG_CODE_OPEN
    ret
    ldd r24, Y + Q(in_down)
    tst r24
    brne 4f
    ldi r30, lo8(last_release)
    rcall since
    ldi r18, hi8(700)
    cpi r24, lo8(700)
    cpc r25, r18
    brcs 4f
    rjmp code_finish
4:  ret
