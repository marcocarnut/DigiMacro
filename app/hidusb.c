/*
 * The app's half of what v1's DigiHidUSB did: see hidusb.h.
 */
#include <stdint.h>
#include <string.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <avr/wdt.h>

#include "core.h"
#include "hidusb.h"
#include "keymap_us.h"

/* ===================================================================== */
/* Time                                                                  */
/* ===================================================================== */

/* Timer1, as on the Digistump core, so Timer0 stays free for entropy().
   CK/128 and a compare at 128 is a tick every 1.0007 ms. The interrupt
   reaches here through the core's vectors and the app's header.

   Not blocking: V-USB's interrupt has to start within a few cycles of the
   bus moving, so this one lets it in at once. */
static volatile uint16_t ms;    /* sixteen bits: all anything here compares,
                                   and half the interrupt of thirty-two */
static uint8_t tenth;

/* It also counts the core's update window, in tenths (layout.h). */
ISR(TIMER1_COMPA_vect, ISR_NOBLOCK)
{
    ms++;
    if (++tenth == 100) {
        tenth = 0;
        uint8_t w = DM_WINDOW + 1;
        if (w) DM_WINDOW = w;           /* stops at 0xFF */
    }
}

uint16_t millis(void)
{
    uint8_t sreg = SREG;
    cli();
    uint16_t m = ms;
    SREG = sreg;
    return m;
}

static inline uint16_t ticks(void) { return (uint16_t)millis(); }

/* ===================================================================== */
/* The window                                                            */
/* ===================================================================== */

static uint16_t allowUntil;
static uint8_t  allowOpen;
static uint8_t  restartWanted;
static uint16_t restartAsked;

/* Whatever the last request carried or the last answer held -- a password,
   a key, a chunk of EEPROM -- stays in the report until the next one,
   and anybody who can reach the device can read it back. So it is wiped
   wherever it might have held something, and when the window closes. */
static void wipe(void)
{
    memset(hidReport() + DM_VND_DATA, 0, DM_VND_CHUNK);
}

void hidAllow(uint16_t t)
{
    allowUntil = ticks() + t;
    allowOpen = 1;
}

/* Must be asked at least every 32 seconds or so while the window might be
   open: the comparison is sixteen bits of milliseconds, and asked later than
   that it would read an expired window as open again. */
uint8_t hidAllowed(void)
{
    if (allowOpen && (int16_t)(ticks() - allowUntil) >= 0) {
        allowOpen = 0;
        hidLock();              /* the session ends with the window */
        /* Not INFO's answer, which holds nothing secret -- and INFO asks
           this while filling itself in, so wiping would wipe the core's
           half of it: its version, the region, the page size. */
        if (hidReport()[0] != DM_FUNC_INFO) wipe();
    }
    return allowOpen;
}

/* ===================================================================== */
/* The serial                                                            */
/* ===================================================================== */

/* Random bits from the watchdog oscillator's jitter against the CPU clock,
   with interrupts off throughout: see v1. Runs from hidBegin(), before
   the core has connected to USB, so a second of it costs nothing. */
static uint32_t entropy(void)
{
    uint32_t s = OSCCAL;
    uint8_t sreg = SREG;

    cli();
    TCCR0B = (1 << CS00);
    WDTCR = (1 << WDCE) | (1 << WDE);
    WDTCR = (1 << WDIF) | (1 << WDIE);
    for (uint8_t i = 0; i < 64; i++) {
        while (!(WDTCR & (1 << WDIF)))
            ;
        WDTCR = (1 << WDIF) | (1 << WDIE);  /* a one clears the flag */
        /* rotated by three: s >> 29 taken from the top byte alone */
        s = ((s << 3) | ((uint8_t)(s >> 24) >> 5)) ^ TCNT0;
    }
    wdt_disable();
    TCCR0B = 0;
    SREG = sreg;
    return s;
}

void hidBegin(void)
{
    OCR1C = 128;
    OCR1A = 128;
    TCCR1 = (1 << CTC1) | (1 << CS13);
    TIMSK |= (1 << OCIE1A);

    /* The core reads the serial when the host asks for it; a chip that has
       none gets one now, once. Byte by byte, with what the app links
       anyway, rather than the block routines for the sake of four bytes. */
    uint8_t *e = (uint8_t *)DM_SERIAL_ADDR, none = 0xFF;
    for (uint8_t i = 4; i; i--) none &= eeprom_read_byte(e++);
    if (none == 0xFF) {
        uint32_t serial = entropy();
        for (uint8_t i = 4; i; i--, serial >>= 8)   /* e is past it now */
            eeprom_update_byte(e - i, (uint8_t)serial);
    }
}

/* ===================================================================== */
/* The loop                                                              */
/* ===================================================================== */

/* RESTART is noted when it arrives and done here, a moment later: it
   arrives inside the USB driver, and the host has not yet seen the
   request complete. */
void hidPoll(void)
{
    corePoll();
    if (restartWanted && (uint16_t)(ticks() - restartAsked) > 50) {
        restartWanted = 0;
        allowOpen = 0;
        hidKeyForget();         /* as if replugged: the key goes */
        wipe();
        hidRestart();
    }
}

/* ===================================================================== */
/* Typing                                                                */
/* ===================================================================== */

/* The keyboard's report or the mouse's: its id says which, and so how long */
static void sendRaw(const uint8_t *r)
{
    uint16_t t = ticks();

    while (!hidSend(r, r[0] == HID_REPORT_KBD ? HID_KBD_LEN : 4)) {
        corePoll();
        if ((uint16_t)(ticks() - t) > 50) return;   /* the host is not asking */
    }
}

static uint8_t kbd[HID_KBD_LEN];

static void __attribute__((noinline)) sendReport(uint8_t modifier, uint8_t key)
{
    kbd[0] = HID_REPORT_KBD;
    kbd[1] = modifier;
    kbd[3] = key;
    sendRaw(kbd);
}

/* The mouse. A TV takes only a relative one, so the board keeps where it
   put the cursor, in the page's steps -- 128 across, 64 down -- and moves by
   the difference: both axes together while each has a way to go, then
   straight on. A move to 0 slams against that edge instead, fast, so the
   cursor ends there wherever it was -- {0,0} is home, and a macro that
   starts there is right whatever moved the cursor meanwhile. A step is 8
   counts, as two reports of 4, slowly, so that acceleration scales every
   step alike; {x*a,y*b} makes them 4a and 4b for one move.
   test/pointer_test.c runs this code, from here to hidPoint()'s end. */
static uint8_t ptr[4];          /* id, buttons, dx, dy */
static uint8_t ptrX, ptrY, ptrToX;
static uint8_t ptrS[2];         /* the next move's counts a report, x and y, xor 4 */

static void ptrSend(int8_t dx, int8_t dy)
{
    ptr[0] = HID_REPORT_PTR;
    ptr[2] = dx;
    ptr[3] = dy;
    sendRaw(ptr);
    sendRaw(ptr);
}

/* One axis a step nearer, by s counts: what to send for it */
static int8_t __attribute__((noinline)) ptrStep(uint8_t *at, uint8_t to, uint8_t s)
{
    if (*at < to) { ++*at; return s; }
    if (*at > to) { --*at; return -s; }
    return 0;
}

void __attribute__((noinline)) hidPoint(uint8_t what)
{
    if (what & 0x80) { ptrToX = what & 0x7F; return; }  /* y moves */
    if (what & 0x40) {
        /* Both axes a step at a time, while each still has a way to go:
           a diagonal, then straight on */
        uint8_t toY = what & 0x3F;
        /* to an edge: 30 steps of 127, fast, which acceleration only
           lengthens */
        if (!ptrToX) { ptrX = 30; ptrS[0] = 127 ^ 4; }
        if (!toY) { ptrY = 30; ptrS[1] = 127 ^ 4; }
        while (ptrX != ptrToX || ptrY != toY) {
            int8_t dx = ptrStep(&ptrX, ptrToX, ptrS[0] ^ 4);
            ptrSend(dx, ptrStep(&ptrY, toY, ptrS[1] ^ 4));
        }
        ptrS[0] = ptrS[1] = 0;  /* the scale is the one move's */
        return;
    }
    if (!(what & PTR_BUTTONS)) {    /* {x*a,y*b}'s scale, before the move */
        ptrS[(what >> 4) & 1] = (what & 0x0F) ^ 4;
        return;
    }
    ptr[1] = what & 3;          /* the buttons down now: none is a release */
    ptrSend(0, 0);
    if (what & PTR_CLICK) {     /* ...and up again */
        ptr[1] = 0;
        ptrSend(0, 0);
    }
}

void hidKey(uint8_t modifier, uint8_t key)
{
    sendReport(modifier, key);
    sendReport(0, 0);
}

/* A character as a US keyboard types it; nothing for one it has no key for.
   The layout is the app's to choose, which is why it is up here and not in
   the core, which could never change it. */
void hidTypeChar(char c, uint8_t mod)
{
    uint8_t entry;

    switch (c) {
    case '\r':
    case '\n': hidKey(mod, 0x28); return;
    case '\t': hidKey(mod, 0x2B); return;
    case '\b': hidKey(mod, 0x2A); return;
    }
    if (c < KEYMAP_FIRST || c > KEYMAP_LAST) return;
    entry = pgm_read_byte(&keymapUS[(uint8_t)c - KEYMAP_FIRST]);
    if (entry == 0) return;
    if (entry & KEYMAP_SHIFT) mod |= 0x02;
    hidKey(mod, entry & 0x7F);
}

/* ===================================================================== */
/* Requests                                                              */
/* ===================================================================== */

/* Whether an address is in the EEPROM: it ends where a 256 does, so the
   high byte alone says. */
#if (E2END & 0xFF) != 0xFF
#error "IN_EEPROM() needs the EEPROM to end on a 256-byte boundary"
#endif
#define IN_EEPROM(a)    ((uint8_t)((a) >> 8) <= (E2END >> 8))

#if DM_ST_OPEN != 1
#error "INFO takes hidAllowed()'s 0 or 1 as DM_ST_OPEN"
#endif

/* From inside the USB driver: answer at once, and leave anything slow for
   hidPoll(). */
uint8_t app_request(uint8_t *v)
{
    uint16_t addr = v[1] | ((uint16_t)v[2] << 8);
    uint8_t  n    = v[3];
    uint8_t *d    = v + DM_VND_DATA;
    uint8_t  f    = v[0];

    if (n > DM_VND_CHUNK) n = DM_VND_CHUNK;

    /* No gate: the worst LEARN does is type the remote's next button as hex
       rather than play it, once, and it reads nothing. */
    if (f == DM_FUNC_LEARN) {
        hidLearn();
        return 1;
    }

    switch (f) {
    case DM_FUNC_INFO:
        /* The core has filled in what it knows. INFO answers whatever the
           gates say: it tells a page what it would have to do. */
        v[DM_INFO_EESIZE]     = (uint8_t)(E2END + 1);
        v[DM_INFO_EESIZE + 1] = (uint8_t)((E2END + 1) >> 8);
        v[DM_INFO_STATE] |= hidAllowed()     /* 0 or 1: DM_ST_OPEN */
                          | hidLockState()
                          | hidKeyState();
        return 1;

    case DM_FUNC_KEYAUTH:
        /* No gate: the point is a program on the host doing it with nobody
           at the board. It can do nothing else. */
        if (!hidKeyAuth(d, n)) goto refuse;
        goto done;              /* the key, whether it was right or not */

    case DM_FUNC_PROVE:
        /* No gate either: its answer is all it gives, and that is what a
           client checks before it sends the key above. */
        if (!hidProve(d, n)) goto refuseKept;
        return 1;


    case DM_FUNC_UPDATE:
    case DM_FUNC_RESTART:
    case DM_FUNC_READ:
    case DM_FUNC_WRITE:
    case DM_FUNC_UNLOCK:
    case DM_FUNC_SETPASS:
        break;

    default:
        return 0;               /* the core refuses it */
    }

    /* Two gates: the window says somebody is at the board, the password
       that they are the right somebody. Both, or neither if no password is
       set; UNLOCK is how the second is passed. */
    if (!hidAllowed() || (f != DM_FUNC_UNLOCK && !hidUnlocked()))
        goto refuse;            /* refused, it may still carry a password */

    switch (f) {
    case DM_FUNC_UPDATE:
        /* Somebody at the board, with the password: the core may take
           pages for the next six seconds. */
        DM_WINDOW = 0;
        break;

    case DM_FUNC_RESTART:
        restartWanted = 1;
        restartAsked = ticks();
        break;

    case DM_FUNC_UNLOCK: {
        /* The core checks it and keeps the session; what it means for the
           lockout is the firmware's. One turned away unread counts for
           nothing. */
        uint8_t r = hidUnlock(d, n);
        if (r == DM_PASS_RIGHT) {
            if (hidPassSet()) hidPassRight();
            goto done;
        }
        if (r == DM_PASS_WRONG) hidPassWrong();
        goto refuse;
    }

    case DM_FUNC_SETPASS:
        /* Zero bytes clears it. Reaching here at all meant getting past both
           gates; the new one is then given to the core, so the session goes
           on under it. */
        if (n > DM_PASS_LEN) goto refuse;
        for (uint8_t i = 0; i < DM_PASS_LEN; i++)
            eeprom_update_byte((uint8_t *)(DM_PASS_ADDR + i),
                               n == 0 ? 0xFF : (i < n ? d[i] : 0));
        hidPassChanged();
        hidUnlock(d, n);
        goto done;

    case DM_FUNC_READ:
        for (; n; n--, addr++)
            *d++ = IN_EEPROM(addr) ? eeprom_read_byte((const uint8_t *)addr)
                                   : 0xFF;
        break;

    case DM_FUNC_WRITE:
        for (; n && IN_EEPROM(addr); n--, addr++)
            eeprom_update_byte((uint8_t *)addr, *d++);
        hidKeyForget();         /* the secrets, or their check value, may
                                   not be what the key was for any more */
        goto done;              /* it might have been the password */
    }
    return 1;

    /* The ways out that wipe what the request carried, refused or not */
refuse:
    wipe();
refuseKept:                     /* PROVE's: it carried nothing secret */
    v[0] = DM_REFUSED;
    return 1;
done:
    wipe();
    return 1;
}
