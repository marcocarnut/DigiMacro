#!/usr/bin/env python3
"""
Do the differential scenarios catch bugs in the assembly? Each mutation below
changes one thing in app/asm/digimacro.S; it is built, checked to have changed
the binary, and compared against the C on the scenarios of its subsystem.

    mutate.py [-j N] [name ...]

A mutation whose text is not found, or that leaves the binary as it was, is an
error, not a pass. Prints a table; exit 1 if any mutant survived.
"""
import argparse, hashlib, os, re, subprocess, sys
from concurrent.futures import ThreadPoolExecutor

HERE = os.path.dirname(os.path.abspath(__file__))
ROOT = os.path.abspath(os.path.join(HERE, '..', '..'))
BIN = os.path.expanduser('~/.arduino15/packages/arduino/tools/avr-gcc/7.3.0-atmel3.6.1-arduino7/bin/')
SRC = os.path.join(ROOT, 'app/asm/digimacro.S')
O = os.path.join(ROOT, 'build/v2')
REF = os.path.join(O, 'digimacro.hex')

# name, subsystems to run, old text, new text
M = [
 ('tap: dash threshold 250 -> 300', 'tap', 'ldi r19, hi8(250)\n    cpi r24, lo8(250)', 'ldi r19, hi8(300)\n    cpi r24, lo8(300)'),
 ('tap: debounce 25 -> 60', 'tap', 'cpi r24, 25\n', 'cpi r24, 60\n'),
 ('tap: silence 700 -> 900', 'tap', 'ldi r18, hi8(700)\n    cpi r24, lo8(700)', 'ldi r18, hi8(900)\n    cpi r24, lo8(900)'),
 ('tap: five symbols -> four', 'tap', 'cpi r20, 5\n    brcs 3f\n    sbi FLAGS, FLAG_CODE_OVER', 'cpi r20, 4\n    brcs 3f\n    sbi FLAGS, FLAG_CODE_OVER'),
 ('tap: hold 4000 -> 3000', 'tap,window', 'ldi r18, hi8(4000)\n    cpi r24, lo8(4000)', 'ldi r18, hi8(3000)\n    cpi r24, lo8(3000)'),
 ('tap: dash and dot swapped in the code', 'tap', 'brcs 31f\n    ori r18, 1\n31:', 'brcc 31f\n    ori r18, 1\n31:'),
 ('touch: threshold +2 -> +4', 'touch', 'subi r18, -2\n', 'subi r18, -4\n'),
 ('touch: hysteresis lost', 'touch', 'lsr r18\n    inc r18\n    rjmp 3f', 'subi r18, -2\n    rjmp 3f'),
 ('touch: average every 100 -> 200 ms', 'touch', 'cpi r18, 100\n    cpc r19, r1', 'cpi r18, 200\n    cpc r19, r1'),
 ('touch: stuck 10001 -> 3001 ms', 'touch', 'ldi r20, hi8(10001)\n    cpi r18, lo8(10001)', 'ldi r20, hi8(3001)\n    cpi r18, lo8(3001)'),
 ('touch: lowest reading wins', 'touch', 'cp r24, r25\n    brcc 6f', 'cp r25, r24\n    brcc 6f'),
 ('playback: shift bit ignored', 'playback', 'sbrs r22, 7\n    rjmp 1f\n    ldi r24, 2', 'rjmp 1f\n    rjmp 1f\n    ldi r24, 2'),
 ('playback: modifier sticks', 'playback', 'and r13, r24\n    clr r12\n', 'and r13, r24\n'),
 ('playback: table top not checked', 'playback', 'loop:\n    ldi r25, hi8(TABLE_TOP)\n    cpi r16, lo8(TABLE_TOP)', 'loop:\n    ldi r25, hi8(TABLE_TOP + 16)\n    cpi r16, lo8(TABLE_TOP + 16)'),
 ('playback: last-byte escape takes its argument', 'playback', '3:  ldi r25, hi8(TABLE_TOP)\n    cpi r16, lo8(TABLE_TOP)\n    cpc r17, r25\n    brcc stop', '3:  ldi r25, hi8(TABLE_TOP + 1)\n    cpi r16, lo8(TABLE_TOP + 1)\n    cpc r17, r25\n    brcc stop'),
 ('playback: pause of tenths is 50 ms', 'playback', 'cpi r24, 100\n    cpc r25, r1\n    brcc 2f\n    rcall hidPoll', 'cpi r24, 50\n    cpc r25, r1\n    brcc 2f\n    rcall hidPoll'),
 ('playback: {IR} tag not skipped', 'playback,ir', 'ldi r20, 3\n2:  movw r24, r16', 'ldi r20, 2\n2:  movw r24, r16'),
 ('playback: LED not lit by a keystroke', 'playback,secret', 'clr r7\n    sbi PORTB, PIN_LED\n', 'clr r7\n'),
 ('walk: end-of-table test off', 'playback', 'cpi r18, 5\n    brcc not_found', 'cpi r18, 6\n    brcc not_found'),
 ('walk: an escape\'s argument not stepped over', 'playback,jump', 'cpi r24, 0x82\n    breq 2b\n    adiw r26, 1', 'cpi r24, 0x82\n    breq 2b\n    nop'),
 ('repeat: one short', 'repeats', 'dec r15\n    brne 51b', 'dec r15\n    dec r15\n    brne 51b'),
 ('repeat: loses the modifier', 'repeats', '51: rcall touched\n    brne stop2\n    mov r24, r14', '51: rcall touched\n    brne stop2\n    clr r24'),
 ('repeat: not stoppable', 'repeats,stop', '51: rcall touched\n    brne stop2', '51: rcall touched\n    nop'),
 ('group: mark forgotten at the end', 'repeats', '62: clr r8\n    clr r9', '62: nop\n    nop'),
 ('group: one round too few', 'repeats', '61: mov r15, r18\n', '61: mov r15, r18\n    dec r15\n'),
 ('goto: jump limit 100 -> 5', 'jump', 'ldi r18, 101\n', 'ldi r18, 6\n'),
 ('goto: jump does not reset the modifier', 'jump', 'clr r12\n    clr r8\n    clr r9\n    clr r15\n    rjmp loop\nstop3', 'clr r8\n    clr r9\n    clr r15\n    rjmp loop\nstop3'),
 ('goto: header off by one', 'jump', 'mov r24, r18\n    dec r24\n    rcall find_header', 'mov r24, r18\n    rcall find_header'),
 ('stop: touch not marked consumed', 'stop,jump', 'sbi FLAGS, FLAG_CONSUMED\n    cbi FLAGS, FLAG_HELD\n    clz', 'cbi FLAGS, FLAG_HELD\n    clz'),
 ('lockout: wrong code not counted', 'lockout', '2:  rcall lock_miss\n', '2:\n'),
 ('lockout: counts with no password', 'lockout', 'lock_soft:                      /* -> r24: the threshold, or 0 for no lockout */\n    rcall hidPassSet\n    tst r24\n    breq 9f\n', 'lock_soft:                      /* -> r24: the threshold, or 0 for no lockout */\n    rcall hidPassSet\n    tst r24\n'),
 ('lockout: hard threshold ignored', 'lockout', 'ldi r24, ST_HARD\n    ret\n1:  ldi r24, LOCK_MISSES', 'ldi r24, ST_SOFT\n    ret\n1:  ldi r24, LOCK_MISSES'),
 ('lockout: wait default 6 -> 3', 'lockout', 'brcs 9f\n    ldi r24, 6\n', 'brcs 9f\n    ldi r24, 3\n'),
 ('lockout: ten seconds -> eight', 'lockout,secret', 'ldi r18, hi8(10000)\n    cpi r24, lo8(10000)', 'ldi r18, hi8(8000)\n    cpi r24, lo8(8000)'),
 ('lockout: right password does not count a lifted hard lock', 'lockout,password', 'ldi r24, LOCK_CLEARED\n    rcall count_up\n', ''),
 ('lockout: soft lock not counted', 'lockout', 'ldi r24, LOCK_SOFTS\n    rcall count_up\n1:  ldi r24, LOCK_MISSES', '1:  ldi r24, LOCK_MISSES'),
 ('lockout: remote plays while locked', 'ir,lockout', 'rcall hidLockState\n    pop r19\n    pop r18\n    tst r24\n    brne 9f\n    ldi r20, 0x80', 'rcall hidLockState\n    pop r19\n    pop r18\n    tst r24\n    nop\n    ldi r20, 0x80'),
 ('lockout: power-on plays through a hard lock', 'autoexec,lockout', 'cpi r24, ST_HARD\n    breq 3f\n    mov r24, r18', 'cpi r24, ST_HARD\n    nop\n    mov r24, r18'),
 ('{clear}: does nothing', 'clear', 'rcall lock_clear\n    rjmp loop\n71:', 'nop\n    rjmp loop\n71:'),
 ('{Gui}: modifier dropped', 'gui', 'cpi r18, 0xF6               /* modifiers alone */\n    brne 72f\n    clr r22\n', 'cpi r18, 0xF6               /* modifiers alone */\n    brne 72f\n    clr r12\n    clr r22\n'),
 ('{Gui}: types a key', 'gui', 'brne 72f\n    clr r22\n', 'brne 72f\n    ldi r22, 4\n'),
 ('secret: refused quiet is loud', 'secret', 'subi r18, 0xFD\n', 'subi r18, 0xFC\n'),
 ('secret: counter byte wrong', 'secret', 'sub r24, r18\n    ldi r30, lo8(sec_nonce)', 'sub r24, r18\n    inc r24\n    ldi r30, lo8(sec_nonce)'),
 ('secret: key not forgotten when it lapses', 'secret', 'brne 1b\n    rcall hidKeyForget\n    rjmp 1b', 'brne 1b\n    nop\n    rjmp 1b'),
 ('secret: jump stays in the secret', 'secret,jump', 'movw r16, r24\n    std Y + Q(sec_at), r1\n    std Y + Q(sec_at) + 1, r1\n    clr r12', 'movw r16, r24\n    clr r12'),
 ('secret: wrong key costs no second', 'secret', 'sbi F2, F2_PAUSING\nno:', 'nop\nno:'),
 ('secret: key compare stops at 7 bytes', 'secret', 'ldi r20, 8\n    ldi r19, CHECK_AT', 'ldi r20, 7\n    ldi r19, CHECK_AT'),
 ('xtea: 31 cycles', 'secret', 'ldi r30, 32\n    mov r7, r30', 'ldi r30, 31\n    mov r7, r30'),
 ('xtea: wrong delta', 'secret', 'subi r16, 0x47', 'subi r16, 0x46'),
 ('xtea: shift by 3', 'secret', 'ldi r31, 4\n1:  lsl r8', 'ldi r31, 3\n1:  lsl r8'),
 ('mouse: x and y swapped', 'mouse', 'std Y + Q(ptr) + 2, r24\n    std Y + Q(ptr) + 3, r22', 'std Y + Q(ptr) + 3, r24\n    std Y + Q(ptr) + 2, r22'),
 ('mouse: one report per step', 'mouse', 'rcall 1f\n1:  ldi r24, lo8(ptr)', 'nop\n1:  ldi r24, lo8(ptr)'),
 ('mouse: no slam to the edge', 'mouse', 'ldi r18, 30\n', 'ldi r18, 3\n'),
 ('mouse: scale survives the move', 'mouse', '7:  std Y + Q(ptrS), r1\n    std Y + Q(ptrS) + 1, r1\n', '7:\n'),
 ('mouse: click not released', 'mouse', 'sbrs r24, 4\n    ret\n    std Y + Q(ptr) + 1, r1', 'ret\n    ret\n    std Y + Q(ptr) + 1, r1'),
 ('mouse: steps backwards', 'mouse', 'mov r24, r20\n    neg r24\n    ret', 'mov r24, r20\n    ret\n    ret'),
 ('IR: leader window too narrow', 'ir', 'cpi r16, 161\n', 'cpi r16, 190\n'),
 ('IR: bit threshold moved', 'ir', 'cpi r16, 28\n', 'cpi r16, 34\n'),
 ('IR: complement not checked', 'ir', 'com r18\n    cp r19, r18\n    brne 65f', 'com r18\n    cp r19, r18\n    nop'),
 ('IR: bit max 45 -> 60', 'ir', 'cpi r16, 46\n', 'cpi r16, 61\n'),
 ('IR: overflow not treated as a gap', 'ir', 'ldi r18, 1 << TOV0\n    out TIFR, r18\n    ldi r16, 0xFF', 'ldi r18, 1 << TOV0\n    out TIFR, r18\n    nop'),
 ('IR: high bit of a button lost', 'ir', 'sbrc r19, 7\n    ori r20, 2', 'nop\n    nop'),
 ('IR: learn window 10 s -> 5 s', 'ir', 'cpi r24, 40\n', 'cpi r24, 20\n'),
 ('IR: learn types no space', 'ir', 'ldi r22, 0x2C\n', 'ldi r22, 0x2D\n'),
 ('hex: letters off by one', 'ir', 'subi r22, 6\n', 'subi r22, 5\n'),
 ('INFO: wrong EEPROM size', 'commands', 'ldi r24, 2\n    st X+, r24\n    rcall hidAllowed', 'ldi r24, 1\n    st X+, r24\n    rcall hidAllowed'),
 ('INFO: key bit never set', 'commands,secret', 'breq 1f\n    ori r22, ST_KEY\n1:  ld r24, X', 'rjmp 1f\n    ori r22, ST_KEY\n1:  ld r24, X'),
 ('gates: window not checked', 'commands', '6:  rcall hidAllowed\n    tst r24\n    breq reqRefuse', '6:  rcall hidAllowed\n    tst r24\n    nop'),
 ('gates: password not checked', 'commands,password', 'rcall hidUnlocked\n    tst r24\n    breq reqRefuse', 'rcall hidUnlocked\n    tst r24\n    nop'),
 ('READ: past the EEPROM reads it', 'commands', 'ldi r24, 0xFF\n    cpi r19, 2\n    brcc 1f\n    movw r24, r18', 'ldi r24, 0xFF\n    cpi r19, 4\n    brcc 1f\n    movw r24, r18'),
 ('READ: count not clamped', 'commands', 'cpi r20, 33\n', 'cpi r20, 40\n'),
 ('WRITE: past the end allowed', 'commands', 'tst r20\n    breq 22f\n    cpi r19, 2\n', 'tst r20\n    breq 22f\n    cpi r19, 4\n'),
 ('WRITE: key not forgotten', 'commands,secret', '22: rcall hidKeyForget\n', '22:\n'),
 ('UNLOCK: wrong one not counted', 'password,lockout', '71: tst r24\n    brne reqRefuse\n    rcall lock_miss', '71: tst r24\n    brne reqRefuse\n    nop'),
 ('SETPASS: over-long allowed', 'password,commands', 'cpi r20, 13                 /* SETPASS */', 'cpi r20, 14                 /* SETPASS */'),
 ('SETPASS: not zero-padded', 'password,commands', 'ldi r22, 0\n    cp r18, r20', 'ldi r22, 0xFF\n    cp r18, r20'),
 ('SETPASS: clears no counts', 'password,lockout', 'rcall lock_clear\n    movw r24, r30', 'nop\n    movw r24, r30'),
 ('UPDATE: window not reopened', 'commands', 'out DM_WINDOW, r1\n    rjmp reqOk', 'nop\n    rjmp reqOk'),
 ('RESTART: key kept', 'commands,secret', 'cbi F2, F2_OPEN\n    rcall hidKeyForget', 'cbi F2, F2_OPEN\n    nop'),
 ('LEARN: forgets the flag', 'ir,commands', 'std Y + Q(learn_at), r3\n    sbi FLAGS, FLAG_LEARN', 'std Y + Q(learn_at), r3\n    nop'),
 ('KEYAUTH: key not wiped from the report', 'secret,commands', '31: rjmp reqDone', '31: rjmp reqOk'),
 ('PROVE: seven bytes pass', 'secret,commands', 'cpi r22, 8\n    brne no', 'cpi r22, 7\n    brne no'),
 ('refusal does not wipe', 'commands', 'reqRefuse:\n    rcall wipe', 'reqRefuse:\n    nop'),
 ('window: thirty seconds -> ten', 'window', 'subi r24, lo8(-30000)\n    sbci r25, hi8(-30000)', 'subi r24, lo8(-10000)\n    sbci r25, hi8(-10000)'),
 ('window: session survives its end', 'window,commands', 'cbi F2, F2_OPEN\n    rcall hidLock', 'cbi F2, F2_OPEN\n    nop'),
 ('LED: blink pattern', 'window,lockout', 'sbrs r3, 1                  /* a steady blink: ticks & 0x200 */', 'sbrs r3, 2                  /* a steady blink: ticks & 0x200 */'),
 ('LED: soft-lock blip 60 -> 120 ms', 'lockout', 'cpi r24, 60\n    cpc r25, r1\n    brcs led_on', 'cpi r24, 120\n    cpc r25, r1\n    brcs led_on'),
 ('LED: three blinks -> two', 'lockout', 'ldi r24, 3                  /* three blinks', 'ldi r24, 2                  /* three blinks'),
 ('power-on: settle 250 -> 500', 'autoexec', 'ldi r22, lo8(250)\n    ldi r23, hi8(250)', 'ldi r22, lo8(500)\n    ldi r23, hi8(500)'),
 ('power-on: wait 1500 -> 800', 'autoexec', 'ldi r22, lo8(1500)\n    ldi r23, hi8(1500)', 'ldi r22, lo8(800)\n    ldi r23, hi8(800)'),
 ('clock: window tenths off', 'wtenths,commands', 'ldi r16, 100\n', 'ldi r16, 50\n'),
 ('serial: made every boot', 'setup', 'cpi r20, 0xFF\n    brne 4f', 'cpi r20, 0xFF\n    nop'),
 ('touch: five groups a reading', 'touch', 'ldi r19, 4                  /* depend on it.', 'ldi r19, 5                  /* depend on it.'),
 ('totp: the wipe does nothing', 'totp', 'totpWipe:\n    ldi r30, lo8(totp_s)', 'totpWipe:\n    ret\n    ldi r30, lo8(totp_s)'),
 ('totp: the wipe leaves the stack', 'totp', 'st X+, r1\n    rjmp 1b\n2:  ldi r30, 18', 'nop\n    rjmp 2f\n2:  ldi r30, 18'),
 ('totp: W not wiped before USB', 'totp', 'ldi r30, lo8(W)             /* zeros before USB may run again */\n    clr r31\n    ldi r24, 64\n    rcall zero\n', 'nop\n    nop\n    nop\n    nop\n'),
 ('totp: W not wiped after the code', 'totp', 'ldi r30, lo8(W)             /* the schedule goes first: nothing in it */\n    clr r31                     /* matters any more, and USB may run from here */\n    ldi r24, 64\n    rcall zero\n', 'nop\n    nop\n    nop\n    nop\n'),
 ('totp: the keystream kept', 'totp', 'ldi r30, lo8(sec_ks)\n    ldi r31, hi8(sec_ks)\n    ldi r24, 8\n    rcall zero\n', 'nop\n    nop\n    nop\n    nop\n'),
 ('totp: unset does not stop', 'totp,clock', 'sbis F2, F2_CLOCK           /* unset: nothing after the zeros */\n    rjmp stop\n', 'nop\n    nop\n'),
 ('totp: unset types nothing', 'totp,clock', 'sbis F2, F2_CLOCK\n    rjmp totpTypeZeros', 'sbis F2, F2_CLOCK\n    rjmp stop'),
 ('totp: half page not looked at', 'totp', 'sbic GPIOR0, PAGE_HALF      /* W is another\'s: no code, and nothing typed */\n    rjmp totpGiveUp', 'nop\n    nop'),
 ('totp: half page arrives between blocks', 'totp', 'rcall corePoll\n    sbic GPIOR0, PAGE_HALF\n    rjmp totpGiveUp', 'rcall corePoll\n    nop\n    nop'),
 ('hmac: round constant', 'totp', 'K:  .long 0x5A827999', 'K:  .long 0x5A82799A'),
 ('hmac: rotl30 is 29', 'totp', 'ldi r23, 30\n    rcall rolN\n    inc r18', 'ldi r23, 29\n    rcall rolN\n    inc r18'),
 ('hmac: 79 rounds', 'totp', 'cpi r18, 80\n    breq 9f', 'cpi r18, 79\n    breq 9f'),
 ('hmac: schedule rotate lost', 'totp', 'ldi r23, 1\n    rcall rolN\n3:  adiw r26, 4', 'nop\n    nop\n3:  adiw r26, 4'),
 ('hmac: Maj written as Ch', 'totp', 'sbrc r21, 1                 /* Maj = Ch(b^c, c, d) ^ c ^ d */', 'rjmp .+0                    /* */'),
 ('hmac: the inner block\'s length', 'totp', 'ldi r24, 8\n    ldi r25, 0x40', 'ldi r24, 8\n    ldi r25, 0x48'),
 ('hmac: the outer block\'s length', 'totp', 'ldi r24, 20                 /* W = digest, padded: 64 + 20 bytes */\n    ldi r25, 0xA0', 'ldi r24, 20                 /* W = digest, padded: 64 + 20 bytes */\n    ldi r25, 0xA8'),
 ('hmac: padding byte in the wrong place', 'totp', 'ldi r30, lo8(W)\n    add r30, r24\n    ldi r22, 0x80', 'ldi r30, lo8(W)\n    inc r30\n    add r30, r24\n    ldi r22, 0x80'),
 ('hmac: midstate added back wrongly', 'totp', 'mov r22, r23\n    andi r22, 3\n    brne 2f\n    clc\n2:  ld r0, -Z', 'nop\n    nop\n    nop\n    clc\n2:  ld r0, -Z'),
 ('hmac: outer midstate read twice from the inner', 'totp', 'subi r16, 0xEC              /* the outer midstate, 20 on */\n    sbci r17, 0xFF\n    rcall rdS', 'nop\n    nop\n    rcall rdS'),
 ('totp: thirty second step is thirty one', 'totp', 'cpi r21, 30\n    brcs 3f\n    subi r21, 30', 'cpi r21, 31\n    brcs 3f\n    subi r21, 31'),
 ('totp: truncation offset of three bits', 'totp', 'lds r24, totp_s + 20\n    andi r24, 15', 'lds r24, totp_s + 20\n    andi r24, 7'),
 ('totp: sign bit not masked', 'totp', 'andi r25, 0x7F\n    ldd r24, Z+1', 'nop\n    ldd r24, Z+1'),
 ('totp: a digit\'s place wrong', 'totp', 'P10: .byte 0x40, 0x42, 0x0F, 0xA0, 0x86, 0x01', 'P10: .byte 0x40, 0x42, 0x0F, 0xA1, 0x86, 0x01'),
 ('totp: five digits typed', 'totp', 'ldi r24, 6\n    mov r15, r24', 'ldi r24, 5\n    mov r15, r24'),
 ('totp: blob not skipped after the code', 'totp', 'subi r16, 0xEC              /* and past the blob */\n    sbci r17, 0xFF', 'nop\n    nop'),
 ('totp: table top not checked', 'totp', 'ldi r18, hi8(TABLE_TOP + 1)\n    cpi r24, lo8(TABLE_TOP + 1)', 'ldi r18, hi8(TABLE_TOP + 41)\n    cpi r24, lo8(TABLE_TOP + 41)'),
 ('totp: a walk steps one short', 'totp', 'adiw r26, 41\n', 'adiw r26, 40\n'),
 ('totp: forgotten key not rechecked', 'totp', 'ldd r24, Y + Q(key_left)\n    tst r24\n    brne 11f\n    rjmp totpGiveUp\n11:', 'ldd r24, Y + Q(key_left)\n    tst r24\n    rjmp 11f\n    rjmp totpGiveUp\n11:'),
 ('clock: a tick too long a second', 'clock', 'ldi r22, lo8(999)\n    ldi r23, hi8(999)', 'ldi r22, lo8(1000)\n    ldi r23, hi8(1000)'),
 ('clock: no fractional correction', 'clock', 'ldi r21, CLK_FRAC\n', 'ldi r21, 0\n'),
 ('clock: seconds do not carry past 16 bits', 'clock', 'std Y + Q(clk_secs) + 1, r25\n    brcc 1b\n    ldd r24, Y + Q(clk_secs) + 2', 'std Y + Q(clk_secs) + 1, r25\n    rjmp 1b\n    ldd r24, Y + Q(clk_secs) + 2'),
 ('clock: wrong byte order set', 'clock', 'ld r24, X+\n    std Y + Q(clk_secs), r24\n    ld r24, X+\n    std Y + Q(clk_secs) + 1, r24', 'ld r24, X+\n    std Y + Q(clk_secs) + 1, r24\n    ld r24, X+\n    std Y + Q(clk_secs), r24'),
 ('clock: read gated', 'clock', 'cpi r21, 0x6C               /* the clock: read it freely, set it gated */\n    brne 51f\n    tst r20\n    brne 6f\n    rcall clock_read', 'cpi r21, 0x6C               /* the clock: read it freely, set it gated */\n    brne 51f\n    rjmp 6f\n    rcall clock_read'),
 ('clock: page write without the window', 'clock', 'tst r20\n    brne 6f\n    rcall clock_read\n    rjmp reqOk', 'tst r20\n    breq 53f\n    rcall clock_set\n53: rcall clock_read\n    rjmp reqOk'),
 ('clock: page write skips the password', 'clock', 'rcall hidUnlocked\n    tst r24\n    breq reqRefuse\n    cpi r21, 0x6C', 'rcall hidUnlocked\n    tst r24\n    nop\n    cpi r21, 0x6C'),
 ('clock: the wrong count is taken', 'clock', 'cpi r20, 4\n    brne reqRefuse', 'cpi r20, 4\n    nop'),
 ('clock: time with the wrong key', 'clock,secret', 'cpi r22, 16\n    brne no\n0:  movw r26, r24', 'cpi r22, 16\n    brne no\n0:  movw r26, r24\n    push r24\n    push r25\n    push r26\n    push r27\n    adiw r26, 16\n    rcall clock_set\n    pop r27\n    pop r26\n    pop r25\n    pop r24'),
 ('clock: accepts any key', 'clock,secret', 'or r18, r24\n    inc r19\n    dec r20\n    brne 3b\n    tst r18\n    breq 4f', 'or r18, r24\n    inc r19\n    dec r20\n    brne 3b\n    tst r18\n    rjmp 4f'),
 ('clock: replug keeps it', 'clock', 'out F2, r1\n', 'nop\n'),
]


def sh(*a, **k):
    return subprocess.run(a, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, universal_newlines=True, **k)


def base_flags():
    return ['-mmcu=attiny85', '-DF_CPU=16500000L', '-Icore', '-Iapp', '-Icore/vusb',
            '-x', 'assembler-with-cpp', '-DAPP_VER=18']


def build(d, text):
    os.makedirs(d, exist_ok=True)
    s = os.path.join(d, 'digimacro.S')
    with open(s, 'w') as f:
        f.write(text)
    r = sh(BIN + 'avr-gcc', *base_flags(), '-c', '-o', os.path.join(d, 'm.o'), s, cwd=ROOT)
    if r.returncode:
        return r.stdout
    elf = os.path.join(d, 'digimacro-asm.elf')
    r = sh(BIN + 'avr-gcc', '-mmcu=attiny85', '-nostartfiles', '-Wl,--section-start=.text=0xf80',
           '-Wl,--section-start=.data=0x800150', '-o', elf, os.path.join(d, 'm.o'),
           os.path.join(O, 'coreapi.o'), cwd=ROOT)
    if r.returncode:
        return r.stdout
    b = os.path.join(d, 'digimacro-asm.bin')
    sh(BIN + 'avr-objcopy', '-O', 'binary', '-R', '.eeprom', elf, b)
    r = sh('python3', os.path.join(ROOT, 'app/apphex.py'), 'app', '3968', '34', b,
           os.path.join(d, 'digimacro-asm.hex'))
    return None if r.returncode == 0 else r.stdout


def binhash(d):
    return hashlib.sha256(open(os.path.join(d, 'digimacro-asm.bin'), 'rb').read()).hexdigest()


def one(item, baseline):
    i, (name, subs, old, new) = item
    src = open(SRC).read()
    if src.count(old) != 1:
        return name, 'BAD (text found %d times)' % src.count(old), ''
    text = src.replace(old, new)
    d = os.path.join(ROOT, 'build/mut/%02d' % i)
    err = build(d, text)
    if err:
        return name, 'BUILD FAILED', err[:300]
    if binhash(d) == baseline:
        return name, 'NO CHANGE', ''
    asmonly = any(x in ('totp', 'clock', 'install') for x in subs.split(','))
    images = [os.path.join(d, 'digimacro-asm.hex')] if asmonly else [REF, os.path.join(d, 'digimacro-asm.hex')]
    r = sh('python3', os.path.join(HERE, 'run.py'), *images,
           '-j', '2', '--stop', '-s', subs, '-o', os.path.join(ROOT, 'build/diff/mut%02d' % i), cwd=ROOT)
    first = next((l for l in r.stdout.splitlines() if l.startswith('FAIL')), '')
    return name, 'caught' if r.returncode else 'SURVIVED', first.replace('FAIL ', '')


def main():
    p = argparse.ArgumentParser()
    p.add_argument('-j', type=int, default=10)
    p.add_argument('names', nargs='*')
    a = p.parse_args()
    d = os.path.join(ROOT, 'build/mut/base')
    err = build(d, open(SRC).read())
    if err:
        print(err)
        return 2
    baseline = binhash(d)
    items = [x for x in enumerate(M) if not a.names or any(n in x[1][0] for n in a.names)]
    bad = 0
    with ThreadPoolExecutor(a.j) as ex:
        for name, res, info in ex.map(lambda it: one(it, baseline), items):
            print('%-55s %-10s %s' % (name, res, info))
            sys.stdout.flush()
            bad += res != 'caught'
    print('%d mutations, %d not caught' % (len(items), bad))
    return 1 if bad else 0


if __name__ == '__main__':
    sys.exit(main())
