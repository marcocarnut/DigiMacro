/*
 * DigiMacro firmware 17 in hand-tuned assembly. Same EEPROM, same protocol,
 * same header and CRC as the C; test/diff holds it to that on the simulated
 * board. The whole app is one program, so its conventions are its own:
 *
 *   r2:r3   the millisecond clock: read with one movw, which nothing can
 *           interrupt. The core never touches r2-r11 (tools/checkcore.py)
 *   r4 r5   SREG saved by the timer's and the remote's interrupts
 *   r6      tenths: the timer's countdown to the next tick of DM_WINDOW
 *   r7-r9, r12-r17   the macro being played (see macro_play)
 *   r10:r11 the report send is working on
 *   Y       the variables, set in app_setup and never moved: the core's
 *           code keeps it for us, and main() holds nothing in it. Interrupts
 *           cannot use it: they may land inside the core, with Y its own
 *   r1      zero, as in the core
 *
 * Interrupts save nothing they do not use, but start with sei: V-USB has
 * to get in within a few cycles. Only the interrupts' own registers (r2-r6)
 * are out of bounds to xtea, which runs with them on.
 */
#define __SFR_OFFSET 0
#include <avr/io.h>
#include "layout.h"

#define FLAGS           GPIOR1
#define FLAG_CODE_OPEN  0
#define FLAG_CODE_OVER  1
#define FLAG_CONSUMED   2
#define FLAG_AUTODONE   3
#define FLAG_AUTOWAIT   4
#define FLAG_HELD       5
#define FLAG_IR         6
#define FLAG_LEARN      7

/* A second flag register: USIDR, which nothing shifts with the USI off */
#define F2              USIDR
#define F2_OPEN         0       /* the window */
#define F2_RESTART      1       /* RESTART asked */
#define F2_PAUSING      2       /* a wrong key's second */
#define F2_CLOCK        3       /* the clock has been set: lost at a replug */

/* The remote's interrupt keeps its state in spare I/O registers: one word
   to reach, and it cannot use Y. Timer0's compares are never enabled */
#define IR_LEFT         OCR0A
#define IR_BITSL        OCR0B
#define IR_BITSH        OCR1B

#define PIN_TOUCH       0
#define PIN_LED         1
#define PIN_IR          2

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

#define ST_SOFT         0x08
#define ST_HARD         0x10
#define ST_KEY          0x80
#define HOST_LOUD       2
#define REFUSED         0xEE
#define IR_WANT         0xFF
#define TOTP_MARK       0x7F    /* after 0x80: no {IR} byte lacks bit 7 */

/* W, TOTP's 64 bytes of message schedule, borrowed from the core's page
   buffer (tools/checkcore.py checks its address): idle unless a page is
   half received, which GPIOR0's bit 5 says */
#define W               CORE_SPARE_RAM
#define PAGE_HALF       5

/* The clock's seconds, in ticks of Timer1: nominally 1.00072727 ms, so
   999 + CLK_FRAC/256 ticks a second. Build-time: make CLK_FRAC=n */
#ifndef CLK_FRAC
#define CLK_FRAC        70
#endif

    .section .bss
    .global V, _ZL3sec, touch_state, touch_base, touch_since, touch_moved
    .global code_bits, code_len, in_down, in_seen
V:
allowUntil:     .space 2
restartAsked:   .space 2
sendT:          .space 2
kbd:            .space 6
ptr:            .space 4
ptrX:           .space 1
ptrY:           .space 1
ptrToX:         .space 1
ptrS:           .space 2
in_down:        .space 1
in_seen:        .space 1
in_since:       .space 2
press_start:    .space 2
lock_left:      .space 1
lock_tick:      .space 2
key_left:       .space 1
key_tick:       .space 2
key_failed:     .space 2
sec_at:         .space 2
touch_state:    .space 1
touch_base:     .space 2
touch_since:    .space 2
touch_moved:    .space 2
last_release:   .space 2
code_bits:      .space 1
code_len:       .space 1
ir_key:         .space 3
auto_since:     .space 2
ir_addr:        .space 1
ir_cmd:         .space 1
learn_at:       .space 1
clk_secs:       .space 4
clk_t:          .space 2
clk_frac:       .space 1
totp_s:         .space 21           /* [0] is scratch; a..e, then the digits */
    .global clk_secs, totp_s, totp_wiped, totp_gap, sec_ks, key_left
sec:
_ZL3sec:        .space 16
sec_nonce:      .space 4
sec_ks:         .space 8

#define Q(v)    ((v) - V)

    .global ms
    .set ms, 0x800002           /* r2:r3, for the harness's eyes */

/* ------------------------------------------------------------------ */
/* The header. Slots of interrupts never enabled hold code instead.   */
/* ------------------------------------------------------------------ */

    .section .vectors,"ax",@progbits
    .global __vectors
__vectors:
    rjmp app_setup
    rjmp app_loop
    rjmp app_request
    sei                         /* INT0's slot, and PCINT0's: V-USB's own */
    rjmp int0                   /* vector, so this one is never jumped to */
    /* TIMER1_COMPA's slot, and on through the eleven unused ones */
    sei
    in r4, SREG
    inc r2                      /* the clock */
    brne 1f
    inc r3
1:  dec r6
    brne t1out
    in r6, DM_WINDOW            /* the tenth: 0xFF stays */
    inc r6
    breq 2f
    out DM_WINDOW, r6
2:  rjmp t1tenth
    .if (. - __vectors) != 2 * APP_SLOTS
    .error "the header's code does not end where the table must"
    .endif
    .byte APP_MAGIC0, APP_MAGIC1
    .word __data_load_end - APP_START
    .byte APP_VER, CORE_API

    .text
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

/* EEPROM. ee1 for an address in the page at 0x100; all keep everything
   but r24 and r25 (and r0, writing) */
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

eeput1:                         /* r24 low address, r22 the value */
    ldi r25, 1
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
since:
    ldi r31, hi8(V)
    ld r18, Z
    ldd r19, Z+1
    movw r24, r2
    sub r24, r18
    sbc r25, r19
    ret

stamp:                          /* now, into the sixteen bits at r30 */
    ldi r31, hi8(V)
    movw r24, r2
    st Z, r24
    std Z+1, r25
    ret

/* Ten seconds since the time at r30? If so, moved on by them: carry clear */
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

zero:                           /* r24 bytes at Z */
    st Z+, r1
    dec r24
    brne zero
    ret

/* A wait in tens of seconds, as stored: anything but 1..240 is a minute */
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

wipe:
    rcall hidReport
    movw r30, r24
    adiw r30, 4
    ldi r24, 32
    rjmp zero

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
send:
    movw r10, r24
    ldi r30, lo8(sendT)
    rcall stamp
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
    cpi r24, 51
    cpc r25, r1
    brcs 1b
9:  ret

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

ptrSend:                        /* r24 dx, r22 dy: the report twice */
    std Y + Q(ptr) + 2, r24
    std Y + Q(ptr) + 3, r22
    rcall 1f
1:  ldi r24, lo8(ptr)
    ldi r25, hi8(ptr)
    rjmp send

/* An axis a step nearer: Z the place, r22 the target, r20 the count -> r24 */
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
3:  ldd r24, Y + Q(ptrX)
    ldd r25, Y + Q(ptrToX)
    cp r24, r25
    brne 4f
    ldd r24, Y + Q(ptrY)
    cp r24, r21
    breq 7f
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
7:  std Y + Q(ptrS), r1
    std Y + Q(ptrS) + 1, r1
    ret
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

count_of:                       /* r24 low address -> r24, the count */
    rcall ee1
    com r24
    ret

count_clr:                      /* r24 low address: an erased count */
    clr r22
count_put:                      /* r24 low address, r22 the count */
    com r22
    rjmp eeput1

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

lock_clear:
    ldi r24, LOCK_MISSES
    rcall count_clr
    ldi r24, LOCK_SOFTS
    rcall count_clr
    std Y + Q(lock_left), r1
    ret

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

hidKeyForget:
    std Y + Q(key_left), r1
    ldi r30, lo8(sec)
    ldi r31, hi8(sec)
    ldi r24, 28
    rjmp zero

key_ok:                         /* -> r24, the tens left; zero flag */
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
pause_over:                     /* -> carry clear if it is */
    ldi r30, lo8(key_failed)
    rcall since
    ldi r18, hi8(1000)
    cpi r24, lo8(1000)
    cpc r25, r18
    ret

/* Compared in full, as the password is. r25:r24 p, r22 n -> r24 */
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
4:  ldi r30, lo8(sec)
    ldi r31, hi8(sec)
    ldi r24, 16
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
one:
    ldi r24, 1
    ret

/* XTEA under the check value, twice over: r25:r24 p, r22 n -> r24 */
hidProve:
    cpi r22, 8
    brne no
    movw r26, r24
    ldi r18, 15                 /* k[16], from the top down, on the stack */
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

/* The clocks that keep running while a macro plays */
/* Carry milliseconds into seconds: 999 ticks, and one more whenever the
   fraction overflows. From clocks_poll, which every wait reaches well inside
   the sixteen bits' 65 seconds; only the main context writes these */
clock_poll:
    sbis F2, F2_CLOCK
    ret
1:  ldd r20, Y + Q(clk_frac)
    ldi r21, CLK_FRAC
    add r20, r21
    ldi r22, lo8(999)
    ldi r23, hi8(999)
    adc r22, r1
    adc r23, r1
    ldi r30, lo8(clk_t)
    rcall since
    cp r24, r22
    cpc r25, r23
    brcs 9f
    add r18, r22                /* the second's start moves on by what it took */
    adc r19, r23
    st Z, r18
    std Z+1, r19
    std Y + Q(clk_frac), r20
    ldd r24, Y + Q(clk_secs)
    ldd r25, Y + Q(clk_secs) + 1
    adiw r24, 1
    std Y + Q(clk_secs), r24
    std Y + Q(clk_secs) + 1, r25
    brcc 1b
    ldd r24, Y + Q(clk_secs) + 2
    ldd r25, Y + Q(clk_secs) + 3
    adiw r24, 1
    std Y + Q(clk_secs) + 2, r24
    std Y + Q(clk_secs) + 3, r25
    rjmp 1b
9:  ret

/* Seconds from X, little-endian, from now */
clock_set:
    ld r24, X+
    std Y + Q(clk_secs), r24
    ld r24, X+
    std Y + Q(clk_secs) + 1, r24
    ld r24, X+
    std Y + Q(clk_secs) + 2, r24
    ld r24, X+
    std Y + Q(clk_secs) + 3, r24
    std Y + Q(clk_frac), r1
    ldi r30, lo8(clk_t)
    rcall stamp
    sbi F2, F2_CLOCK
    ret

/* Into X: seconds, ticks into this second, whether it is set */
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
    st X, r24
    ret

clocks_poll:
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
 */
xtea:
    push r26
    push r27
    push r28
    push r29
    ldi r30, 7
    clr r31
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
60: movw r30, r26               /* addr: the gates' calls took it */
    sbiw r30, 3
    ld r18, Z+
    ld r19, Z
    cpi r21, 0x68
    brne 61f
    out DM_WINDOW, r1
    rjmp reqOk
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
22: rcall hidKeyForget
reqDone:
    rcall wipe
reqOk:
    ldi r24, 1
reqOut:
    pop r29
    pop r28
    ret

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

touch_read:                     /* -> r24: the highest of four readings. */
    clr r24                     /* Cycle for cycle the C's: the readings */
    ldi r19, 4                  /* depend on it. Keeps r22 r23 */
1:  clr r25
    ldi r20, 8
2:  cbi PORTB, PIN_TOUCH
    sbi DDRB, PIN_TOUCH
    ldi r18, 0x10
3:  dec r18
    brne 3b
    rjmp .+0
    cbi DDRB, PIN_TOUCH
    sbi PORTB, PIN_TOUCH
    ldi r18, 0
4:  sbic PINB, PIN_TOUCH
    rjmp 5f
    cpi r18, 0x1F
    breq 5f
    subi r18, 0xFF
    rjmp 4b
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

touch_poll:
    movw r22, r2                /* now, from before the reading */
    rcall touch_read
    ldd r18, Y + Q(touch_base)
    ldd r19, Y + Q(touch_base) + 1
    ldi r20, 4
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
3:  add r18, r20
    ldi r19, 0
    cp r18, r24
    brcc 4f
    ldi r19, 1
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

code_reset:
    std Y + Q(code_bits), r1
    std Y + Q(code_len), r1
    cbi FLAGS, FLAG_CODE_OPEN
    cbi FLAGS, FLAG_CODE_OVER
    ret

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
4:  std Y + Q(last_release), r2
    std Y + Q(last_release) + 1, r3
    sbi FLAGS, FLAG_CODE_OPEN
    ret
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

/* Reversed: little-endian words of K, read with lpm from the bottom up */
K:  .long 0x5A827999, 0x6ED9EBA1, 0x8F1BBCDC, 0xCA62C1D6
P10: .byte 0x40, 0x42, 0x0F, 0xA0, 0x86, 0x01, 0x10, 0x27, 0x00, 0xE8, 0x03, 0x00
    .byte 0x64, 0x00, 0x00, 0x0A, 0x00, 0x00

/* The word at X, rotated left by r23 */
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

acc:
    add r24, r0
    adc r25, r1
    ret

/* Eighty rounds over W, on totp_s+1. X, Z, r18-r25 and r0: no USB here */
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

rdS:
    ldi r24, lo8(totp_s + 1)
    mov r11, r24
    ldi r26, lo8(totp_s + 21)
    ldi r27, hi8(totp_s + 21)
    rjmp rd20

/* The midstate back onto a..e, through W, which holds nothing live by now */
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

stop:
    cbi PORTB, PIN_LED
    pop r24
    ret

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
stop2:
    clr r15
    rjmp stop
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
host:
    cpi r19, 0x83
    brne stop3
    cpi r18, HOST_LOUD + 1
    brcs 81f
    mov r24, r18
    rcall hidPoint
    rjmp loop
81: pop r0
    push r18
    rjmp stop

play_header:                    /* r24 -> r24 */
    rcall find_header
    breq 1f
    rjmp macro_play
1:  ldi r24, HOST_LOUD
    ret

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
app_setup:
    ldi r28, lo8(V)
    ldi r29, hi8(V)
    movw r30, r28               /* the variables: nothing else clears them */
    ldi r24, sec_ks + 8 - V
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
    ldi r24, 128
    out OCR1C, r24
    out OCR1A, r24
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
