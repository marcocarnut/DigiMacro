/*
 * The spike's app: types one line once the host has a keyboard, and blinks
 * the LED, so that which app is running shows at a glance and by ear. Two
 * builds of it, A and B, with different lines and blink rates, are what
 * the page swaps one for the other.
 *
 * The line lives in RAM (const data is .data on the AVR), so seeing it typed
 * whole also proves the app's own start-up copied .data from flash.
 */
#include <stdint.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include "core.h"
#include "hidusb.h"

#ifndef APP_LINE
#define APP_LINE    "DigiMacro v2 spike, app A"
#endif
#ifndef APP_HANG
#define APP_HANG    0                   /* ms before hanging on purpose */
#endif
#ifndef APP_TYPE
#define APP_TYPE    0
#endif
#ifndef APP_BLINK
#define APP_BLINK   500                 /* ms per half period */
#endif

#define LED         PB1

static const char line[] = APP_LINE "\n";
static uint8_t  typed;                  /* .bss: zero, if start-up cleared it */
static uint16_t since, blinked;

void app_setup(void)
{
    hidBegin();
    DDRB |= 1 << LED;
    since = blinked = millis();
}

void app_loop(void)
{
    uint16_t now = millis();

    hidPoll();
    hidAllow(10000);

    /* App C: a bad app, for the watchdog to catch. The LED stays lit. */
    if (APP_HANG && now - since > APP_HANG) {
        PORTB |= 1 << LED;
        for (;;)
            ;
    }                    /* SPIKE: no pad, so always open */

    if (now - blinked >= APP_BLINK) {
        blinked = now;
        PINB = 1 << LED;                /* toggles it */
    }
    /* As v1's start-up macro: a second after the host writes the lock
       lights, or three without -- a host with every light off never does. */
    /* Typing proved the app's start-up on the board, and then typed into
       whatever window had the focus: off unless built with APP_TYPE=1. */
    if (APP_TYPE && !typed && hidReady()
        && now - since > (hidAwake() ? 1000 : 3000)) {
        for (const char *p = line; *p; p++) hidTypeChar(*p, 0);
        typed = 1;
    }
}

/* No lockout and no key. The password is real: the core checks it,
   and SETPASS sets it. */
void    hidPassWrong(void)                       { }
void    hidPassRight(void)                       { }
void    hidPassChanged(void)                     { }
void    hidLearn(void)                           { }
uint8_t hidLockState(void)                       { return 0; }
uint8_t hidKeyState(void)                        { return 0; }
uint8_t hidKeyAuth(const uint8_t *p, uint8_t n)  { (void)p; (void)n; return 0; }
uint8_t hidProve(uint8_t *p, uint8_t n)          { (void)p; (void)n; return 0; }
void    hidKeyForget(void)                       { }
