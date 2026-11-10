/*
 * DigiMacro firmware 17 in assembly: the straightforward translation.
 *
 * Function for function what DigiMacro.ino and app/hidusb.c are, with gcc's
 * register conventions -- arguments in r24 and r22, r18-r27 and r30-r31
 * scratch, r2-r17 and r28-r29 the callee's -- and every variable a byte of
 * RAM reached with lds and sts. No global registers, no tail merging, none
 * of the tricks digimacro.S goes on to play: this is the middle column of
 * the comparison, what a first hand translation comes to before tuning.
 *
 * Kept whole and working so that the size is measured, not remembered.
 * apphdr.S and xtea.S and coreapi.S are shared with the C build, unchanged.
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

#define PIN_TOUCH       0
#define PIN_LED         1
#define PIN_IR          2

#define HID_REPORT_KBD  1
#define HID_REPORT_PTR  3
#define DM_VND_DATA     4
#define DM_VND_CHUNK    32

#define DM_SERIAL 0x1E0
#define DM_PASS_AT 0x1F4
#define LOCK_SOFT       0x1EC
#define LOCK_HARD       0x1ED
#define LOCK_WAIT       0x1EE
#define LOCK_MISSES     0x1EF
#define LOCK_SOFTS      0x1F0
#define LOCK_CLEARED    0x1F1
#define DM_ST_SOFTLOCK  0x08
#define DM_ST_HARDLOCK  0x10
#define LOCK_WAIT_MAX 240
#define LOCK_WAIT_DEF 6
#define LOCK_TICK_MS 10000
#define PASS_PAUSE_MS 1000
#define CHECK_AT 0x1E4
#define KEY_WAIT 0x1F2
#define DM_ST_KEY 0x80
#define TABLE_TOP 0x1E0
#define ESC_IR 0x80
#define ESC_MARK 0x82
#define ESC_JUMP 0x81
#define ESC_HOST 0x83
#define ESC_PAUSE 1
#define ESC_MOD 2
#define ESC_REPEAT 3
#define HOST_SILENT 1
#define HOST_LOUD 2
#define SECRET_QUIET 0xFE
#define JUMP_CLEAR 0xF7
#define JUMP_ALONE 0xF6
#define IR_WANT 0xFF
#define JUMP_MAX 100

    .section .bss
    .global ms, tenth
ms:             .space 2
tenth:          .space 1
allowUntil:     .space 2
allowOpen:      .space 1
restartWanted:  .space 1
restartAsked:   .space 2
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
sec:            .space 16
sec_nonce:      .space 4
sec_ks:         .space 8
key_left:       .space 1
key_tick:       .space 2
key_failed:     .space 2
key_pausing:    .space 1
sec_at:         .space 2
    .global _ZL3sec
    .set _ZL3sec, sec
    .global touch_state, touch_base, touch_since, touch_moved, code_bits, code_len, in_down, in_seen
touch_raw:      .space 1
touch_state:    .space 1
touch_base:     .space 2
touch_since:    .space 2
touch_moved:    .space 2
last_release:   .space 2
code_bits:      .space 1
code_len:       .space 1
ir_key:         .space 3
auto_since:     .space 2
ir_left:        .space 1
ir_bits:        .space 2
ir_addr:        .space 1
ir_cmd:         .space 1
learn_at:       .space 1

    .text

/* ------------------------------------------------------------------ */
/* Time                                                               */
/* ------------------------------------------------------------------ */

    .global __vector_3
__vector_3:                     /* TIMER1_COMPA, not blocking */
    sei
    push r24
    in r24, SREG
    push r24
    push r25
    lds r24, ms
    lds r25, ms+1
    adiw r24, 1
    sts ms+1, r25
    sts ms, r24
    lds r24, tenth
    inc r24
    sts tenth, r24
    cpi r24, 100
    brne 1f
    sts tenth, r1
    in r24, GPIOR2              /* DM_WINDOW, stops at 0xFF */
    inc r24
    breq 1f
    out GPIOR2, r24
1:  pop r25
    pop r24
    out SREG, r24
    pop r24
    reti

millis:                         /* -> r25:r24, kept to itself */
    in r0, SREG
    cli
    lds r24, ms
    lds r25, ms+1
    out SREG, r0
    ret

/* ------------------------------------------------------------------ */
/* EEPROM                                                             */
/* ------------------------------------------------------------------ */

eeread:                         /* r25:r24 -> r24 */
    sbic EECR, EEPE
    rjmp eeread
    out EEARH, r25
    out EEARL, r24
    sbi EECR, EERE
    in r24, EEDR
    ret

eeupdate:                       /* r25:r24, r22 */
    sbic EECR, EEPE
    rjmp eeupdate
    out EEARH, r25
    out EEARL, r24
    sbi EECR, EERE
    in r24, EEDR
    cp r24, r22
    breq 2f
    out EECR, r1
    out EEDR, r22
    in r0, SREG
    cli
    sbi EECR, EEMPE
    sbi EECR, EEPE
    out SREG, r0
2:  ret

/* ------------------------------------------------------------------ */
/* The window                                                         */
/* ------------------------------------------------------------------ */

wipe:
    rcall hidReport
    movw r30, r24
    adiw r30, DM_VND_DATA
    ldi r24, DM_VND_CHUNK
1:  st Z+, r1
    dec r24
    brne 1b
    ret

hidAllow:                       /* r25:r24 ms */
    movw r22, r24
    rcall millis
    add r24, r22
    adc r25, r23
    sts allowUntil, r24
    sts allowUntil+1, r25
    ldi r24, 1
    sts allowOpen, r24
    ret

hidAllowed:                     /* -> r24 */
    lds r24, allowOpen
    tst r24
    breq 9f
    rcall millis
    lds r18, allowUntil
    lds r19, allowUntil+1
    sub r24, r18
    sbc r25, r19
    brmi 8f
    sts allowOpen, r1
    rcall hidLock
    rcall hidReport
    movw r30, r24
    ld r24, Z
    cpi r24, DM_FUNC_INFO
    breq 8f
    rcall wipe
8:  lds r24, allowOpen
9:  ret

/* ------------------------------------------------------------------ */
/* The serial                                                         */
/* ------------------------------------------------------------------ */

entropy:                        /* -> r25:r22, interrupts off throughout */
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
1:  in r0, WDTCR
    sbrs r0, WDIF
    rjmp 1b
    ldi r18, (1 << WDIF) | (1 << WDIE)
    out WDTCR, r18
    /* rotate left by three */
    ldi r18, 3
2:  mov r0, r25
    lsl r0
    rol r22
    rol r23
    rol r24
    rol r25
    dec r18
    brne 2b
    in r18, TCNT0
    eor r22, r18
    dec r19
    brne 1b
    wdr
    in r18, WDTCR
    ori r18, (1 << WDCE) | (1 << WDE)
    out WDTCR, r18
    out WDTCR, r1
    out TCCR0B, r1
    out SREG, r30
    ret

hidBegin:
    ldi r24, 128
    out OCR1C, r24
    out OCR1A, r24
    ldi r24, (1 << CTC1) | (1 << CS13)
    out TCCR1, r24
    in r24, TIMSK
    ori r24, (1 << OCIE1A)
    out TIMSK, r24
    push r16
    push r17
    push r28
    push r29
    ldi r28, lo8(DM_SERIAL)
    ldi r29, hi8(DM_SERIAL)
    ldi r16, 0xFF
    ldi r17, 4
1:  movw r24, r28
    rcall eeread
    and r16, r24
    adiw r28, 1
    dec r17
    brne 1b
    cpi r16, 0xFF
    brne 3f
    rcall entropy
    push r25
    push r24
    push r23
    push r22
    sbiw r28, 4
    ldi r17, 4
2:  pop r22
    movw r24, r28
    rcall eeupdate
    adiw r28, 1
    dec r17
    brne 2b
3:  pop r29
    pop r28
    pop r17
    pop r16
    ret

/* ------------------------------------------------------------------ */
/* The loop                                                           */
/* ------------------------------------------------------------------ */

hidPoll:
    rcall corePoll
    lds r24, restartWanted
    tst r24
    breq 9f
    rcall millis
    lds r18, restartAsked
    lds r19, restartAsked+1
    sub r24, r18
    sbc r25, r19
    cpi r24, 51
    cpc r25, r1
    brcs 9f
    sts restartWanted, r1
    sts allowOpen, r1
    rcall hidKeyForget
    rcall wipe
    rcall hidRestart
9:  ret

/* ------------------------------------------------------------------ */
/* Typing                                                             */
/* ------------------------------------------------------------------ */

sendRaw:                        /* r25:r24 the report */
    push r28
    push r29
    push r16
    push r17
    movw r28, r24
    rcall millis
    movw r16, r24
1:  movw r24, r28
    ld r18, Y
    ldi r22, 6
    cpi r18, HID_REPORT_KBD
    breq 2f
    ldi r22, 4
2:  rcall hidSend
    tst r24
    brne 9f
    rcall corePoll
    rcall millis
    sub r24, r16
    sbc r25, r17
    cpi r24, 51
    cpc r25, r1
    brcs 1b
9:  pop r17
    pop r16
    pop r29
    pop r28
    ret

sendReport:                     /* r24 modifier, r22 key */
    ldi r30, lo8(kbd)
    ldi r31, hi8(kbd)
    ldi r18, HID_REPORT_KBD
    st Z, r18
    std Z+1, r24
    std Z+3, r22
    movw r24, r30
    rjmp sendRaw

hidKey:                         /* r24 modifier, r22 key */
    rcall sendReport
    clr r24
    clr r22
    rjmp sendReport

ptrSend:                        /* r24 dx, r22 dy */
    sts ptr+2, r24
    sts ptr+3, r22
    ldi r24, HID_REPORT_PTR
    sts ptr, r24
    ldi r24, lo8(ptr)
    ldi r25, hi8(ptr)
    rcall sendRaw
    ldi r24, lo8(ptr)
    ldi r25, hi8(ptr)
    rjmp sendRaw

ptrStep:                        /* r25:r24 at, r22 to, r20 s -> r24 */
    movw r30, r24
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
    sts ptrToX, r24
    ret
1:  sbrs r24, 6
    rjmp 5f
    push r16
    push r17
    mov r17, r24
    andi r17, 0x3F              /* toY */
    lds r24, ptrToX
    tst r24
    brne 2f
    ldi r24, 30
    sts ptrX, r24
    ldi r24, 127 ^ 4
    sts ptrS, r24
2:  tst r17
    brne 3f
    ldi r24, 30
    sts ptrY, r24
    ldi r24, 127 ^ 4
    sts ptrS+1, r24
3:  lds r24, ptrX
    lds r25, ptrToX
    cp r24, r25
    brne 4f
    lds r24, ptrY
    cp r24, r17
    breq 7f
4:  lds r20, ptrS
    ldi r24, 4
    eor r20, r24
    lds r22, ptrToX
    ldi r24, lo8(ptrX)
    ldi r25, hi8(ptrX)
    rcall ptrStep
    mov r16, r24
    lds r20, ptrS+1
    ldi r24, 4
    eor r20, r24
    mov r22, r17
    ldi r24, lo8(ptrY)
    ldi r25, hi8(ptrY)
    rcall ptrStep
    mov r22, r24
    mov r24, r16
    rcall ptrSend
    rjmp 3b
7:  sts ptrS, r1
    sts ptrS+1, r1
    pop r17
    pop r16
    ret
5:  sbrc r24, 5
    rjmp 6f
    /* a scale, before the move */
    mov r25, r24
    swap r25
    andi r25, 1
    ldi r30, lo8(ptrS)
    ldi r31, hi8(ptrS)
    add r30, r25
    adc r31, r1
    andi r24, 0x0F
    ldi r25, 4
    eor r24, r25
    st Z, r24
    ret
6:  mov r25, r24
    andi r25, 3
    sts ptr+1, r25
    push r24
    clr r24
    clr r22
    rcall ptrSend
    pop r24
    sbrs r24, 4
    ret
    sts ptr+1, r1
    clr r24
    clr r22
    rjmp ptrSend

/* ------------------------------------------------------------------ */
/* Requests                                                           */
/* ------------------------------------------------------------------ */

    .global app_request
app_request:                    /* r25:r24 v -> r24 */
    push r28
    push r29
    push r16
    push r17
    push r15
    push r14
    movw r28, r24               /* v */
    ldd r14, Y+1                /* addr */
    ldd r15, Y+2
    ldd r16, Y+3                /* n */
    ld r17, Y                   /* f */
    cpi r16, DM_VND_CHUNK + 1
    brcs 1f
    ldi r16, DM_VND_CHUNK
1:  cpi r17, 0x69               /* LEARN */
    brne 2f
    rcall hidLearn
    rjmp reqOk
2:  cpi r17, DM_FUNC_INFO
    brne 3f
    ldi r24, 0x00
    std Y+5, r24
    ldi r24, 0x02
    std Y+6, r24
    rcall hidAllowed
    mov r15, r24
    rcall hidLockState
    or r15, r24
    rcall hidKeyState
    or r15, r24
    ldd r24, Y+7
    or r24, r15
    std Y+7, r24
    rjmp reqOk
3:  cpi r17, 0x6A               /* KEYAUTH */
    brne 4f
    movw r24, r28
    adiw r24, DM_VND_DATA
    mov r22, r16
    rcall hidKeyAuth
    tst r24
    breq 41f
    rjmp reqDone
41: rjmp reqRefuse
4:  cpi r17, 0x6B               /* PROVE */
    brne 5f
    movw r24, r28
    adiw r24, DM_VND_DATA
    mov r22, r16
    rcall hidProve
    tst r24
    breq 57f
    rjmp reqOk
57: rjmp reqRefuseKept
5:  cpi r17, 0x68               /* UPDATE */
    breq 7f
    cpi r17, 0x42               /* RESTART */
    breq 7f
    cpi r17, 0x60               /* READ */
    breq 7f
    cpi r17, 0x61               /* WRITE */
    breq 7f
    cpi r17, DM_FUNC_UNLOCK
    breq 7f
    cpi r17, 0x63               /* SETPASS */
    breq 7f
    clr r24                     /* the core refuses it */
    rjmp reqOut

7:  rcall hidAllowed
    tst r24
    breq 41b
    cpi r17, DM_FUNC_UNLOCK
    breq 10f
    rcall hidUnlocked
    tst r24
    breq 41b
10: cpi r17, 0x68
    brne 11f
    out DM_WINDOW, r1
    rjmp reqOk
11: cpi r17, 0x42
    brne 12f
    ldi r24, 1
    sts restartWanted, r24
    rcall millis
    sts restartAsked, r24
    sts restartAsked+1, r25
    rjmp reqOk
12: cpi r17, DM_FUNC_UNLOCK
    brne 13f
    movw r24, r28
    adiw r24, DM_VND_DATA
    mov r22, r16
    rcall hidUnlock
    cpi r24, 1
    brne 21f
    rcall hidPassSet
    tst r24
    breq 20f
    rcall hidPassRight
20: rjmp reqDone
21: tst r24
    brne 41b
    rcall hidPassWrong
    rjmp reqRefuse
13: cpi r17, 0x63
    brne 14f
    cpi r16, 13
    brcs 631f
    rjmp reqRefuse
631:
    movw r26, r28
    adiw r26, DM_VND_DATA
    clr r17                     /* i */
    ldi r30, lo8(DM_PASS_AT)
    ldi r31, hi8(DM_PASS_AT)
    movw r14, r30
22: ldi r22, 0xFF
    tst r16
    breq 24f
    ldi r22, 0
    cp r17, r16
    brcc 24f
    ld r22, X+
24: movw r24, r14
    push r26
    push r27
    rcall eeupdate
    pop r27
    pop r26
    ldi r24, 1
    add r14, r24
    adc r15, r1
    inc r17
    cpi r17, 12
    brne 22b
    rcall hidPassChanged
    movw r24, r28
    adiw r24, DM_VND_DATA
    mov r22, r16
    rcall hidUnlock
    rjmp reqDone
14: cpi r17, 0x60
    brne 15f
    movw r26, r28
    adiw r26, DM_VND_DATA
16: tst r16
    brne 161f
    rjmp reqOk
161:
    ldi r24, 0xFF               /* in the EEPROM: addr's high byte <= 1 */
    ldi r25, 1
    cp r25, r15
    brcs 18f
    movw r24, r14
    push r26
    rcall eeread
    pop r26
18: st X+, r24
    ldi r24, 1
    add r14, r24
    adc r15, r1
    dec r16
    rjmp 16b
15: /* WRITE */
    movw r26, r28
    adiw r26, DM_VND_DATA
25: tst r16
    breq 26f
    mov r24, r15
    cpi r24, 2
    brcc 26f
    movw r24, r14
    ld r22, X+
    push r26
    push r27
    rcall eeupdate
    pop r27
    pop r26
    ldi r24, 1
    add r14, r24
    adc r15, r1
    dec r16
    rjmp 25b
26: rcall hidKeyForget
    rjmp reqDone

reqRefuse:
    rcall wipe
reqRefuseKept:
    ldi r24, 0xEE
    st Y, r24
    rjmp reqOk
reqDone:
    rcall wipe
reqOk:
    ldi r24, 1
reqOut:
    pop r14
    pop r15
    pop r17
    pop r16
    pop r29
    pop r28
    ret


/* ------------------------------------------------------------------ */
/* Waiting                                                            */
/* ------------------------------------------------------------------ */

wait_poll:                      /* r25:r24 ms */
    push r28
    push r29
    push r16
    push r17
    movw r16, r24
    rcall millis
    movw r28, r24
1:  rcall hidPoll
    rcall millis
    sub r24, r28
    sbc r25, r29
    cp r24, r16
    cpc r25, r17
    brcs 1b
    pop r17
    pop r16
    pop r29
    pop r28
    ret

blink:                          /* r24 n, r23:r22 ms */
    push r28
    push r29
    mov r28, r24
    push r16
    push r17
    movw r16, r22
1:  sbi PORTB, PIN_LED
    movw r24, r16
    rcall wait_poll
    cbi PORTB, PIN_LED
    movw r24, r16
    rcall wait_poll
    dec r28
    brne 1b
    pop r17
    pop r16
    pop r29
    pop r28
    ret

/* ------------------------------------------------------------------ */
/* The lockout                                                        */
/* ------------------------------------------------------------------ */


count_of:                       /* r25:r24 -> r24, ~eeprom */
    rcall eeread
    com r24
    ret

count_put:                      /* r25:r24, r22 v */
    com r22
    rjmp eeupdate

count_up:                       /* r25:r24 */
    push r28
    push r29
    movw r28, r24
    rcall count_of
    cpi r24, 0xFF
    breq 1f
    mov r22, r24
    inc r22
    movw r24, r28
    rcall count_put
1:  pop r29
    pop r28
    ret

lock_soft:                      /* -> r24, the threshold or 0 */
    rcall hidPassSet
    tst r24
    breq 1f
    ldi r24, lo8(LOCK_SOFT)
    ldi r25, hi8(LOCK_SOFT)
    rcall eeread
    cpi r24, 0xFF
    brne 1f
    clr r24
1:  ret

hidLockState:                   /* -> r24 */
    push r17
    push r16
    rcall lock_soft
    mov r16, r24
    tst r24
    breq 9f
    ldi r24, lo8(LOCK_HARD)
    ldi r25, hi8(LOCK_HARD)
    rcall eeread
    mov r17, r24
    inc r24                     /* 1..254 becomes 2..255: THR_SET */
    cpi r24, 2
    brcs 1f
    ldi r24, lo8(LOCK_SOFTS)
    ldi r25, hi8(LOCK_SOFTS)
    rcall count_of
    cp r24, r17
    brcs 1f
    ldi r24, DM_ST_HARDLOCK
    rjmp 9f
1:  ldi r24, lo8(LOCK_MISSES)
    ldi r25, hi8(LOCK_MISSES)
    rcall count_of
    cp r24, r16
    ldi r24, 0
    brcs 8f
    ldi r24, DM_ST_SOFTLOCK
8:
9:  pop r16
    pop r17
    ret

lock_miss:
    push r16
    push r17
    rcall lock_soft
    mov r16, r24
    tst r24
    breq 9f
    rcall hidLockState
    tst r24
    brne 9f
    ldi r24, lo8(LOCK_MISSES)
    ldi r25, hi8(LOCK_MISSES)
    rcall count_of
    mov r17, r24
    inc r17
    cp r17, r16
    brcs 1f
    ldi r24, lo8(LOCK_SOFTS)
    ldi r25, hi8(LOCK_SOFTS)
    rcall count_up
1:  ldi r24, lo8(LOCK_MISSES)
    ldi r25, hi8(LOCK_MISSES)
    mov r22, r17
    rcall count_put
9:  pop r17
    pop r16
    ret

lock_clear:
    ldi r24, lo8(LOCK_MISSES)
    ldi r25, hi8(LOCK_MISSES)
    clr r22
    rcall count_put
    ldi r24, lo8(LOCK_SOFTS)
    ldi r25, hi8(LOCK_SOFTS)
    clr r22
    rcall count_put
    sts lock_left, r1
    ret

lock_poll:
    rcall hidLockState
    cpi r24, DM_ST_SOFTLOCK
    breq 1f
    sts lock_left, r1
    ret
1:  lds r24, lock_left
    tst r24
    brne 3f
    ldi r24, lo8(LOCK_WAIT)
    ldi r25, hi8(LOCK_WAIT)
    rcall eeread
    mov r18, r24
    dec r24
    cpi r24, LOCK_WAIT_MAX
    brcs 2f
    ldi r18, LOCK_WAIT_DEF
2:  sts lock_left, r18
    rcall millis
    sts lock_tick, r24
    sts lock_tick+1, r25
3:  rcall millis
    lds r18, lock_tick
    lds r19, lock_tick+1
    sub r24, r18
    sbc r25, r19
    ldi r18, hi8(LOCK_TICK_MS)
    cpi r24, lo8(LOCK_TICK_MS)
    cpc r25, r18
    brcs 9f
    lds r24, lock_tick
    lds r25, lock_tick+1
    subi r24, lo8(-LOCK_TICK_MS)
    sbci r25, hi8(-LOCK_TICK_MS)
    sts lock_tick, r24
    sts lock_tick+1, r25
    lds r24, lock_left
    dec r24
    sts lock_left, r24
    brne 3b
    ldi r24, lo8(LOCK_MISSES)
    ldi r25, hi8(LOCK_MISSES)
    clr r22
    rjmp count_put
9:  ret


hidPassWrong:
    rjmp lock_miss

hidPassRight:
    rcall hidLockState
    cpi r24, DM_ST_HARDLOCK
    brne 1f
    ldi r24, lo8(LOCK_CLEARED)
    ldi r25, hi8(LOCK_CLEARED)
    rcall count_up
1:  rjmp lock_clear

hidPassChanged:
    rjmp lock_clear

/* ------------------------------------------------------------------ */
/* Secrets                                                            */
/* ------------------------------------------------------------------ */


hidKeyForget:
    sts key_left, r1
    ldi r30, lo8(sec)
    ldi r31, hi8(sec)
    ldi r24, 28
1:  st Z+, r1
    dec r24
    brne 1b
    ret

key_ok:                         /* -> r24 */
1:  lds r24, key_left
    tst r24
    breq 9f
    rcall millis
    lds r18, key_tick
    lds r19, key_tick+1
    sub r24, r18
    sbc r25, r19
    ldi r18, hi8(LOCK_TICK_MS)
    cpi r24, lo8(LOCK_TICK_MS)
    cpc r25, r18
    lds r24, key_left
    brcs 9f
    lds r18, key_tick
    lds r19, key_tick+1
    subi r18, lo8(-LOCK_TICK_MS)
    sbci r19, hi8(-LOCK_TICK_MS)
    sts key_tick, r18
    sts key_tick+1, r19
    dec r24
    sts key_left, r24
    brne 1b
    rcall hidKeyForget
    rjmp 1b
9:  ret

hidKeyState:                    /* -> r24 */
    rcall key_ok
    tst r24
    breq 1f
    ldi r24, DM_ST_KEY
1:  ret

hidKeyAuth:                     /* r25:r24 p, r22 n -> r24 */
    cpi r22, 16
    breq 1f
    clr r24
    ret
1:  push r28
    push r29
    push r16
    push r17
    movw r28, r24               /* p */
    lds r24, key_pausing
    tst r24
    breq 2f
    rcall millis
    lds r18, key_failed
    lds r19, key_failed+1
    sub r24, r18
    sbc r25, r19
    cpi r24, lo8(PASS_PAUSE_MS)
    ldi r18, hi8(PASS_PAUSE_MS)
    cpc r25, r18
    brcs 8f
2:  sts key_pausing, r1
    ldi r24, 8
3:  push r1                     /* b[8], zero */
    dec r24
    brne 3b
    in r16, SPL
    in r17, SPH
    subi r16, 0xFF              /* b */
    sbci r17, 0xFF
    movw r24, r16
    movw r22, r28
    rcall xtea
    clr r18                     /* diff */
    ldi r26, lo8(CHECK_AT)
    ldi r27, hi8(CHECK_AT)
    ldi r20, 8
4:  movw r24, r26
    rcall eeread
    pop r25
    eor r24, r25
    or r18, r24
    adiw r26, 1
    dec r20
    brne 4b
    tst r18
    breq 5f
    rcall millis
    sts key_failed, r24
    sts key_failed+1, r25
    ldi r24, 1
    sts key_pausing, r24
    rjmp 8f
5:  ldi r26, lo8(sec)
    ldi r27, hi8(sec)
    movw r30, r28
    ldi r24, 16
6:  ld r0, Z+
    st X+, r0
    dec r24
    brne 6b
    ldi r24, lo8(KEY_WAIT)
    ldi r25, hi8(KEY_WAIT)
    rcall eeread
    mov r18, r24
    dec r24
    cpi r24, LOCK_WAIT_MAX
    brcs 7f
    ldi r18, LOCK_WAIT_DEF
7:  sts key_left, r18
    rcall millis
    sts key_tick, r24
    sts key_tick+1, r25
    ldi r24, 1
    rjmp 9f
8:  clr r24
9:  pop r17
    pop r16
    pop r29
    pop r28
    ret

hidProve:                       /* r25:r24 p, r22 n -> r24 */
    cpi r22, 8
    breq 1f
    clr r24
    ret
1:  push r28
    push r29
    push r16
    push r17
    movw r16, r24               /* p */
    ldi r24, 16
2:  push r1                     /* k[16] */
    dec r24
    brne 2b
    in r28, SPL
    in r29, SPH
    adiw r28, 1
    clr r18
3:  mov r24, r18
    andi r24, 7
    ldi r25, 0
    subi r24, lo8(-CHECK_AT)
    sbci r25, hi8(-CHECK_AT)
    rcall eeread
    movw r30, r28
    add r30, r18
    adc r31, r1
    st Z, r24
    inc r18
    cpi r18, 16
    brne 3b
    movw r24, r16
    movw r22, r28
    rcall xtea
    ldi r24, 16
4:  pop r0
    dec r24
    brne 4b
    ldi r24, 1
    pop r17
    pop r16
    pop r29
    pop r28
    ret

secret_byte:                    /* r25:r24 a -> r24 */
    push r28
    push r29
    movw r28, r24
    clr r22                     /* k */
    lds r18, sec_at
    lds r19, sec_at+1
    mov r0, r18
    or r0, r19
    breq 2f
    lds r24, key_left
    tst r24
    brne 1f
    clr r24
    pop r29
    pop r28
    ret
1:  ldi r26, lo8(sec_nonce)
    ldi r27, hi8(sec_nonce)
    ldi r30, lo8(sec_ks)
    ldi r31, hi8(sec_ks)
    ldi r24, 4
3:  ld r0, X+
    st Z+, r0
    dec r24
    brne 3b
    mov r24, r28
    sub r24, r18
    st Z+, r24
    st Z+, r1
    st Z+, r1
    st Z, r1
    ldi r24, lo8(sec_ks)
    ldi r25, hi8(sec_ks)
    ldi r22, lo8(sec)
    ldi r23, hi8(sec)
    rcall xtea
    lds r22, sec_ks
2:  movw r24, r28
    rcall eeread
    eor r24, r22
    pop r29
    pop r28
    ret

clocks_poll:
    rcall lock_poll
    rcall hidAllowed
    rcall key_ok
    lds r24, key_pausing
    tst r24
    breq 1f
    rcall millis
    lds r18, key_failed
    lds r19, key_failed+1
    sub r24, r18
    sbc r25, r19
    cpi r24, lo8(PASS_PAUSE_MS)
    ldi r18, hi8(PASS_PAUSE_MS)
    cpc r25, r18
    brcs 1f
    sts key_pausing, r1
1:  ret

/* ------------------------------------------------------------------ */
/* Capacitive touch                                                   */
/* ------------------------------------------------------------------ */

touch_once:                     /* -> r24: as the C's, cycle for cycle */
    clr r25                     /* sum */
    ldi r20, 8
1:  cbi PORTB, PIN_TOUCH
    sbi DDRB, PIN_TOUCH
    ldi r18, 0x10
2:  dec r18
    brne 2b
    rjmp .+0
    cbi DDRB, PIN_TOUCH
    sbi PORTB, PIN_TOUCH
    ldi r18, 0
3:  sbic PINB, PIN_TOUCH
    rjmp 4f
    cpi r18, 0x1F
    breq 4f
    subi r18, 0xFF
    rjmp 3b
4:  cbi PORTB, PIN_TOUCH
    add r25, r18
    subi r20, 1
    brne 1b
    mov r24, r25
    ret

touch_read:                     /* -> r24 */
    push r28
    ldi r28, 4
    clr r25
    mov r0, r25
1:  push r0
    rcall touch_once
    pop r0
    cp r0, r24
    brcc 2f
    mov r0, r24
2:  dec r28
    brne 1b
    mov r24, r0
    pop r28
    ret

touch_poll:
    push r28
    push r29
    rcall millis
    movw r28, r24               /* now */
    rcall touch_read
    sts touch_raw, r24
    lds r18, touch_base
    lds r19, touch_base+1
    ldi r20, 4
1:  lsr r19
    ror r18
    dec r20
    brne 1b
    mov r20, r18                /* base */
    mov r22, r18
    lsr r22
    lsr r22                     /* thr */
    lds r21, touch_state
    tst r21
    breq 2f
    lsr r22
    add r22, r20
    inc r22
    rjmp 3f
2:  add r22, r20
    ldi r21, 2
    add r22, r21
3:  ldi r21, 0
    cp r22, r24
    brcc 4f
    ldi r21, 1
4:  sts touch_state, r21
    tst r21
    brne 6f
    /* untouched: the average, every TOUCH_BASE_MS */
    lds r18, touch_moved
    lds r19, touch_moved+1
    movw r22, r28
    sub r22, r18
    sbc r23, r19
    cpi r22, 100
    cpc r23, r1
    brcs 5f
    sts touch_moved, r28
    sts touch_moved+1, r29
    lds r18, touch_base
    lds r19, touch_base+1
    add r18, r24
    adc r19, r1
    sub r18, r20
    sbc r19, r1
    sts touch_base+1, r19
    sts touch_base, r18
5:  sts touch_since, r28
    sts touch_since+1, r29
    pop r29
    pop r28
    ret
6:  lds r18, touch_since
    lds r19, touch_since+1
    movw r22, r28
    sub r22, r18
    sbc r23, r19
    ldi r18, hi8(10001)
    cpi r22, lo8(10001)
    cpc r23, r18
    brcs 7f
    rcall touch_seed2
7:  pop r29
    pop r28
    ret

touch_seed2:                    /* r24 = v: touch_base = v << 4 */
    mov r18, r24
    clr r19
    ldi r20, 4
1:  lsl r18
    rol r19
    dec r20
    brne 1b
    sts touch_base, r18
    sts touch_base+1, r19
    ret

/* ------------------------------------------------------------------ */
/* Hex                                                                */
/* ------------------------------------------------------------------ */

type_nib:                       /* r24 n */
    andi r24, 0x0F
    mov r22, r24
    ldi r24, 0
    tst r22
    brne 1f
    ldi r22, 0x27
    rjmp 3f
1:  cpi r22, 10
    brcc 2f
    subi r22, -0x1D
    rjmp 3f
2:  subi r22, 6
3:  rjmp hidKey

type_hex:                       /* r24 v */
    push r24
    swap r24
    rcall type_nib
    pop r24
    rjmp type_nib

/* ------------------------------------------------------------------ */
/* Tapping                                                            */
/* ------------------------------------------------------------------ */

code_reset:
    sts code_bits, r1
    sts code_len, r1
    cbi FLAGS, FLAG_CODE_OPEN
    cbi FLAGS, FLAG_CODE_OVER
    ret

handle_input:
    push r28
    push r29
    rcall millis
    movw r28, r24               /* now */
    lds r18, touch_state        /* raw */
    lds r19, in_seen
    cp r18, r19
    breq 1f
    sts in_seen, r18
    sts in_since, r28
    sts in_since+1, r29
1:  lds r19, in_down
    cp r18, r19
    breq 5f
    lds r20, in_since
    lds r21, in_since+1
    movw r22, r28
    sub r22, r20
    sbc r23, r21
    cpi r22, 25
    cpc r23, r1
    brcs 5f
    sts in_down, r18
    tst r18
    breq 2f
    sts press_start, r28
    sts press_start+1, r29
    cbi FLAGS, FLAG_CONSUMED
    cbi FLAGS, FLAG_HELD
    rjmp 9f
2:  sbic FLAGS, FLAG_CONSUMED
    rjmp 9f
    lds r24, code_len
    cpi r24, 5
    brcs 3f
    sbi FLAGS, FLAG_CODE_OVER
    rjmp 4f
3:  lds r18, code_bits
    lsl r18
    lds r20, press_start
    lds r21, press_start+1
    movw r22, r28
    sub r22, r20
    sbc r23, r21
    ldi r25, hi8(250)
    cpi r22, lo8(250)
    cpc r23, r25
    brcs 31f
    ori r18, 1
31: sts code_bits, r18
    inc r24
    sts code_len, r24
4:  sts last_release, r28
    sts last_release+1, r29
    sbi FLAGS, FLAG_CODE_OPEN
    rjmp 9f
5:  lds r19, in_down
    tst r19
    breq 9f
    sbic FLAGS, FLAG_HELD
    rjmp 9f
    lds r20, press_start
    lds r21, press_start+1
    movw r22, r28
    sub r22, r20
    sbc r23, r21
    ldi r25, hi8(4000)
    cpi r22, lo8(4000)
    cpc r23, r25
    brcs 9f
    sbi FLAGS, FLAG_HELD
    sbi FLAGS, FLAG_CONSUMED
    rcall code_reset
    ldi r24, lo8(30000)
    ldi r25, hi8(30000)
    rcall hidAllow
9:  pop r29
    pop r28
    ret

/* ------------------------------------------------------------------ */
/* Playback                                                           */
/* ------------------------------------------------------------------ */


find_header:                    /* r24 want -> r25:r24 where, or 0 */
    push r28
    push r29
    push r15
    mov r15, r24
    ldi r28, 0
    ldi r29, 0
1:  cpi r28, lo8(TABLE_TOP)
    ldi r24, hi8(TABLE_TOP)
    cpc r29, r24
    brcc 9f
    movw r24, r28
    adiw r28, 1
    rcall eeread
    mov r18, r24
    andi r18, 7
    cpi r18, 5
    brcc 9f
    cp r24, r15
    breq 8f
2:  cpi r28, lo8(TABLE_TOP)
    ldi r24, hi8(TABLE_TOP)
    cpc r29, r24
    brcc 1b
    movw r24, r28
    adiw r28, 1
    rcall eeread
    tst r24
    breq 1b
    cpi r24, ESC_IR
    brne 5f
    clr r20                     /* i */
    ldi r18, IR_WANT
    cp r15, r18
    brne 4f
3:  cpi r20, 3
    brcc 4f
    movw r24, r28
    add r24, r20
    adc r25, r1
    rcall eeread
    ldi r30, lo8(ir_key)
    ldi r31, hi8(ir_key)
    add r30, r20
    adc r31, r1
    ld r18, Z
    cp r24, r18
    brne 4f
    inc r20
    rjmp 3b
4:  adiw r28, 3
    cpi r20, 3
    brne 2b
8:  movw r24, r28
    rjmp 7f
5:  mov r18, r24
    andi r18, 0x7F
    cpi r18, 4
    brcc 2b
    cpi r24, ESC_MARK
    breq 2b
    adiw r28, 1
    rjmp 2b
9:  clr r24
    clr r25
7:  pop r15
    pop r29
    pop r28
    ret

touched:                        /* -> r24 */
    rcall clocks_poll
    rcall touch_poll
    lds r24, touch_state
    tst r24
    breq 1f
    rcall millis
    ldi r18, 1
    sts in_down, r18
    sts in_seen, r18
    sts in_since, r24
    sts in_since+1, r25
    sts press_start, r24
    sts press_start+1, r25
    sbi FLAGS, FLAG_CONSUMED
    cbi FLAGS, FLAG_HELD
    ldi r24, 1
1:  ret

pause_poll:                     /* r24 tenths -> r24 */
    push r28
    push r29
    push r17
    mov r17, r24
    rcall millis
    movw r28, r24
1:  rcall millis
    sub r24, r28
    sbc r25, r29
    cpi r24, 100
    cpc r25, r1
    brcc 2f
    rcall hidPoll
    rcall touched
    tst r24
    breq 1b
    ldi r24, 1
    rjmp 9f
2:  subi r28, -100
    sbci r29, -1
    dec r17
    brne 1b
    clr r24
9:  pop r17
    pop r29
    pop r28
    ret

macro_play:                     /* r25:r24 a -> r24 */
    push r2
    push r3
    push r4
    push r5
    push r6
    push r7
    push r8
    push r14
    push r15
    push r16
    push r17
    push r28
    push r29
    movw r28, r24               /* a */
    clr r2                      /* refused */
    clr r3                      /* jumps */
    clr r4                      /* mod */
    clr r5                      /* lastkey */
    clr r6                      /* lastmod */
    clr r14                     /* mark */
    clr r15
    clr r7                      /* reps */
    sts sec_at, r1
    sts sec_at+1, r1

play_loop:
    cpi r28, lo8(TABLE_TOP)
    ldi r24, hi8(TABLE_TOP)
    cpc r29, r24
    brcc play_end1
    rcall touched
    tst r24
    breq 1f
play_end1:
    rjmp play_stop
1:  movw r24, r28
    adiw r28, 1
    rcall secret_byte
    tst r24
    brne 11f
    rjmp play_stop
11: mov r16, r24                /* c */
    andi r24, 0x7F
    cpi r24, 4
    brcs 12f
    rjmp key
12: cpi r16, ESC_MARK
    brne 2f
    movw r14, r28
    rjmp play_loop
2:  cpi r16, ESC_IR
    brne 4f
    ldi r17, 3
3:  movw r24, r28
    rcall secret_byte
    sbrs r24, 7
    rjmp 31f
    adiw r28, 1
    dec r17
    brne 3b
    rjmp play_loop
31: rjmp play_stop
4:  cpi r28, lo8(TABLE_TOP)
    ldi r24, hi8(TABLE_TOP)
    cpc r29, r24
    brcc 31b
    movw r24, r28
    adiw r28, 1
    rcall secret_byte
    tst r24
    breq 31b
    mov r17, r24                /* arg */
    cpi r16, ESC_PAUSE
    brne 5f
    sbi PORTB, PIN_LED
    mov r24, r17
    rcall pause_poll
    tst r24
    brne 31b
    rjmp play_loop
5:  cpi r16, ESC_MOD
    brne 6f
    mov r4, r17
    rjmp play_loop
6:  cpi r16, ESC_REPEAT
    brne 7f
    mov r0, r14
    or r0, r15
    brne 61f
    sbi PORTB, PIN_LED
60: rcall touched
    tst r24
    brne 31b
    mov r24, r6
    mov r22, r5
    rcall hidKey
    dec r17
    brne 60b
    rjmp play_loop
61: tst r7
    brne 62f
    mov r7, r17
    movw r28, r14
    rjmp play_loop
62: dec r7
    breq 63f
    movw r28, r14
    rjmp play_loop
63: clr r14
    clr r15
    rjmp play_loop
7:  cpi r16, ESC_JUMP
    brne 8f
    cpi r17, JUMP_CLEAR
    brne 71f
    rcall lock_clear
    rjmp play_loop
71: cpi r17, JUMP_ALONE
    brne 72f
    clr r16
    rjmp key
72: cpi r17, SECRET_QUIET
    brcs 74f
    rcall key_ok
    tst r24
    brne 73f
    mov r2, r17
    ldi r24, SECRET_QUIET - HOST_SILENT
    sub r2, r24
    rjmp play_stop
73: ldi r24, 4
    mov r8, r24
    ldi r30, lo8(sec_nonce)
    ldi r31, hi8(sec_nonce)
731: movw r24, r28
    adiw r28, 1
    push r30
    rcall eeread
    pop r30
    st Z+, r24
    dec r8
    brne 731b
    sts sec_at, r28
    sts sec_at+1, r29
    rjmp play_loop
74: mov r24, r17
    dec r24
    rcall find_header
    mov r0, r24
    or r0, r25
    breq play_stop
    inc r3
    ldi r18, JUMP_MAX + 1
    cp r3, r18
    brcc play_stop
    movw r28, r24
    sts sec_at, r1
    sts sec_at+1, r1
    clr r4
    clr r14
    clr r15
    clr r7
    rjmp play_loop
8:  cpi r16, ESC_HOST
    brne play_stop
    cpi r17, HOST_LOUD + 1
    brcs 81f
    mov r24, r17
    rcall hidPoint
    rjmp play_loop
81: mov r2, r17
    rjmp play_stop

key:
    sbrs r16, 7
    rjmp 1f
    ldi r24, 2
    or r4, r24
1:  mov r6, r4
    mov r5, r16
    ldi r24, 0x7F
    and r5, r24
    clr r4
    clr r3
    sbi PORTB, PIN_LED
    mov r24, r6
    mov r22, r5
    rcall hidKey
    rjmp play_loop

play_stop:
    cbi PORTB, PIN_LED
    mov r24, r2
    pop r29
    pop r28
    pop r17
    pop r16
    pop r15
    pop r14
    pop r8
    pop r7
    pop r6
    pop r5
    pop r4
    pop r3
    pop r2
    ret

play_header:                    /* r24 h -> r24 */
    rcall find_header
    mov r0, r24
    or r0, r25
    breq 1f
    rjmp macro_play
1:  ldi r24, HOST_LOUD
    ret

code_finish:
    lds r22, code_len
    lds r24, code_bits
    lsl r24
    lsl r24
    lsl r24
    mov r18, r22
    dec r18
    or r18, r24                 /* h */
    ldi r19, 1                  /* ok */
    sbic FLAGS, FLAG_CODE_OVER
    clr r19
    tst r22
    brne 1f
    clr r19
1:  push r18
    push r19
    rcall code_reset
    rcall hidLockState
    pop r19
    pop r18
    tst r24
    breq 11f
    ret
11: tst r19
    breq 2f
    mov r24, r18
    rcall play_header
    cpi r24, HOST_LOUD
    brne 3f
2:  rcall lock_miss
    ldi r24, 3
    ldi r22, 70
    ldi r23, 0
    rjmp blink
3:  ret

autoexec_run:
    ldi r24, lo8(0x1F3)
    ldi r25, hi8(0x1F3)
    rcall eeread
    mov r18, r24
    andi r18, 7
    cpi r18, 5
    brcc 9f
    push r24
    rcall hidLockState
    pop r18
    cpi r24, DM_ST_HARDLOCK
    breq 9f
    mov r24, r18
    rjmp play_header
9:  ret

/* ------------------------------------------------------------------ */
/* The remote                                                         */
/* ------------------------------------------------------------------ */

    .global __vector_1
__vector_1:                     /* INT0, not blocking */
    sei
    push r24
    in r24, SREG
    push r24
    push r25
    push r30
    push r31
    push r18
    in r24, TCNT0               /* t */
    lds r25, ir_left            /* n */
    out TCNT0, r1
    in r18, TIFR
    sbrs r18, TOV0
    rjmp 1f
    ldi r18, 1 << TOV0
    out TIFR, r18
    ldi r24, 0xFF
1:  cpi r24, 161
    brcs 2f
    cpi r24, 234
    brcc 2f
    ldi r25, 32
    rjmp 7f
2:  tst r25
    breq 3f
    cpi r24, 12
    brcs 3f
    cpi r24, 46
    brcs 4f
3:  clr r25
    rjmp 7f
4:  lds r30, ir_bits
    lds r31, ir_bits+1
    lsr r31
    ror r30
    cpi r24, 28
    brcs 5f
    ori r31, 0x80
5:  sts ir_bits, r30
    sts ir_bits+1, r31
    dec r25
    cpi r25, 16
    brne 6f
    sts ir_addr, r30
    rjmp 7f
6:  tst r25
    brne 7f
    mov r18, r31
    com r18
    cp r30, r18
    brne 7f
    sts ir_cmd, r30
    sbi FLAGS, FLAG_IR
7:  sts ir_left, r25
    pop r18
    pop r31
    pop r30
    pop r25
    pop r24
    out SREG, r24
    pop r24
    reti

hidLearn:
    rcall millis
    sts learn_at, r25
    sbi FLAGS, FLAG_LEARN
    ret

ir_poll:
    lds r18, ir_addr
    lds r19, ir_cmd
    sbis FLAGS, FLAG_IR
    nop
    cbi FLAGS, FLAG_IR
    sbis FLAGS, FLAG_LEARN
    rjmp 2f
    cbi FLAGS, FLAG_LEARN
    push r18
    push r19
    rcall millis
    pop r19
    pop r18
    lds r20, learn_at
    mov r24, r25
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
    sts ir_key, r18
    sts ir_key+1, r19
    sts ir_key+2, r20
    ldi r24, IR_WANT
    rjmp play_header
9:  ret

/* ------------------------------------------------------------------ */
/* LED, setup, the loop                                               */
/* ------------------------------------------------------------------ */

led_set:                        /* r24 */
    tst r24
    breq 1f
    sbi PORTB, PIN_LED
    ret
1:  cbi PORTB, PIN_LED
    ret

update_led:
    rcall hidAllowed
    tst r24
    breq 1f
    rcall millis
    com r25
    mov r24, r25
    andi r24, 2
    rjmp led_set
1:  rcall hidLockState
    mov r18, r24
    cpi r18, DM_ST_HARDLOCK
    brne 2f
    rcall millis
    andi r24, 0x40
    rjmp led_set
2:  cpi r18, DM_ST_SOFTLOCK
    brne 3f
    rcall millis
    andi r25, 0x07
    ldi r18, 1
    cpi r24, 60
    cpc r25, r1
    brcs 21f
    clr r18
21: mov r24, r18
    rjmp led_set
3:  clr r24
    rjmp led_set

    .global app_setup
app_setup:
    out FLAGS, r1
    sbi DDRB, PIN_LED
    cbi DDRB, PIN_TOUCH
    cbi PORTB, PIN_TOUCH
    rcall touch_read
    rcall touch_seed2
    rcall hidBegin
    sbi PORTB, PIN_IR
    out TCCR0A, r1
    ldi r24, (1 << CS02) | (1 << CS00)
    out TCCR0B, r24
    in r24, MCUCR
    ori r24, (1 << ISC01)
    out MCUCR, r24
    in r24, GIMSK
    ori r24, (1 << INT0)
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
    rcall millis
    sts auto_since, r24
    sts auto_since+1, r25
    rjmp 3f
1:  rcall hidAwake
    tst r24
    ldi r22, lo8(1500)
    ldi r23, hi8(1500)
    breq 2f
    ldi r22, lo8(250)
    ldi r23, hi8(250)
2:  push r22
    push r23
    rcall millis
    lds r18, auto_since
    lds r19, auto_since+1
    sub r24, r18
    sbc r25, r19
    pop r23
    pop r22
    cp r24, r22
    cpc r25, r23
    brcs 3f
    sbi FLAGS, FLAG_AUTODONE
    rcall autoexec_run

3:  sbis FLAGS, FLAG_CODE_OPEN
    rjmp 4f
    lds r24, in_down
    tst r24
    brne 4f
    rcall millis
    lds r18, last_release
    lds r19, last_release+1
    sub r24, r18
    sbc r25, r19
    ldi r18, hi8(700)
    cpi r24, lo8(700)
    cpc r25, r18
    brcs 4f
    rcall code_finish
4:  ret
