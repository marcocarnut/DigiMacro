/*
 * The app's half of what v1's DigiHidUSB did: see proto.h.
 */
#include <stdint.h>
#include <string.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <avr/wdt.h>

#include "core.h"
#include "proto.h"

/* ===================================================================== */
/* Time                                                                  */
/* ===================================================================== */

/* Timer1, as on the Digistump core, so Timer0 stays free for entropy().
   CK/128 and a compare at 128 is a tick every 1.0007 ms. The interrupt
   reaches here through the core's vectors and the app's header.

   Not blocking: V-USB's interrupt has to start within a few cycles of the
   bus moving, so this one lets it in at once. */
static volatile uint32_t ms;

ISR(TIMER1_COMPA_vect, ISR_NOBLOCK)
{
    ms++;
}

uint32_t millis(void)
{
    uint8_t sreg = SREG;
    cli();
    uint32_t m = ms;
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
   a host key, a chunk of EEPROM -- stays in the report until the next one,
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
        wipe();
    }
    return allowOpen;
}

/* ===================================================================== */
/* The serial                                                            */
/* ===================================================================== */

/* Random bits from the watchdog oscillator's jitter against the CPU clock,
   with interrupts off throughout: see v1. Runs from protoBegin(), before
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
        WDTCR |= (1 << WDIF);
        s = ((s << 3) | (s >> 29)) ^ TCNT0;
    }
    wdt_disable();
    TCCR0B = 0;
    SREG = sreg;
    return s;
}

void protoBegin(void)
{
    OCR1C = 128;
    OCR1A = 128;
    TCCR1 = (1 << CTC1) | (1 << CS13);
    TIMSK |= (1 << OCIE1A);

    /* The core reads the serial as it connects; a chip that has none gets
       one now, once. */
    uint32_t serial;
    eeprom_read_block(&serial, (const void *)DM_SERIAL_ADDR, 4);
    if (serial == 0xFFFFFFFF) {
        serial = entropy();
        eeprom_write_block(&serial, (void *)DM_SERIAL_ADDR, 4);
    }
}

/* ===================================================================== */
/* The loop                                                              */
/* ===================================================================== */

/* RESTART is noted when it arrives and done here, a moment later: it
   arrives inside the USB driver, and the host has not yet seen the
   request complete. */
void protoPoll(void)
{
    hidPoll();
    if (restartWanted && (uint16_t)(ticks() - restartAsked) > 50) {
        restartWanted = 0;
        allowOpen = 0;
        hidHostForget();        /* as if replugged: trust starts again */
        wipe();
        hidRestart();
    }
}

/* ===================================================================== */
/* Typing                                                                */
/* ===================================================================== */

static void sendReport(uint8_t modifier, uint8_t key)
{
    uint8_t r[HID_KBD_LEN] = { HID_REPORT_KBD, modifier, 0, key };
    uint16_t t = ticks();

    while (!hidSend(r, sizeof r)) {
        hidPoll();
        if ((uint16_t)(ticks() - t) > 50) return;   /* the host is not asking */
    }
}

void hidKey(uint8_t modifier, uint8_t key)
{
    sendReport(modifier, key);
    sendReport(0, 0);
}

/* ===================================================================== */
/* Requests                                                              */
/* ===================================================================== */

/* The core asks this before it takes the first page of an update, having
   checked the password itself: is somebody at the board? */
uint8_t app_may_update(void)
{
    return hidAllowed();
}

/* From inside the USB driver: answer at once, and leave anything slow for
   protoPoll(). */
uint8_t app_request(uint8_t *v)
{
    uint16_t addr = v[1] | ((uint16_t)v[2] << 8);
    uint8_t  n    = v[3];
    uint8_t *d    = v + DM_VND_DATA;

    if (n > DM_VND_CHUNK) n = DM_VND_CHUNK;

    switch (v[0]) {
    case DM_FUNC_INFO:
        /* The core has filled in what it knows. INFO answers whatever the
           gates say: it tells a page what it would have to do. */
        v[DM_INFO_EESIZE]     = (uint8_t)(E2END + 1);
        v[DM_INFO_EESIZE + 1] = (uint8_t)((E2END + 1) >> 8);
        v[DM_INFO_STATE] |= (hidAllowed() ? DM_ST_OPEN : 0)
                          | hidLockState()
                          | hidHostState();
        return 1;

    case DM_FUNC_HOSTAUTH:
        /* The one request with no gate: the point is a program on the host
           doing it with nobody at the board. It can do nothing else. */
        if (!hidHostAuth(d, n)) v[0] = DM_REFUSED;
        wipe();                 /* the key, whether it was right or not */
        return 1;

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
    if (!hidAllowed() || (v[0] != DM_FUNC_UNLOCK && !hidUnlocked())) {
        v[0] = DM_REFUSED;
        return 1;
    }

    switch (v[0]) {
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
        } else {
            if (r == DM_PASS_WRONG) hidPassWrong();
            v[0] = DM_REFUSED;
        }
        wipe();
        break;
    }

    case DM_FUNC_SETPASS:
        /* Zero bytes clears it. Reaching here at all meant getting past both
           gates; the new one is then given to the core, so the session goes
           on under it. */
        if (n > DM_PASS_LEN) {
            v[0] = DM_REFUSED;
        } else {
            for (uint8_t i = 0; i < DM_PASS_LEN; i++)
                eeprom_update_byte((uint8_t *)(DM_PASS_ADDR + i),
                                   n == 0 ? 0xFF : (i < n ? d[i] : 0));
            hidPassChanged();
            hidUnlock(d, n);
        }
        wipe();
        break;

    case DM_FUNC_READ:
        for (uint8_t i = 0; i < n; i++)
            d[i] = (addr + i) <= E2END
                 ? eeprom_read_byte((const uint8_t *)(addr + i)) : 0xFF;
        break;

    case DM_FUNC_WRITE:
        for (uint8_t i = 0; i < n; i++) {
            if ((addr + i) > E2END) break;
            eeprom_update_byte((uint8_t *)(addr + i), d[i]);
        }
        /* A new host key, or none: whatever trust the old one earned goes. */
        if (addr < DM_HOST_ADDR + DM_HOST_LEN && addr + n > DM_HOST_ADDR)
            hidHostForget();
        wipe();                 /* it might have been the password */
        break;
    }
    return 1;
}
