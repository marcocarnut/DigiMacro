/*
 * The core's API, as an app sees it, and what both halves must agree on
 * about the USB device: report IDs, the vendor report's shape, the serial.
 * The protocol above that -- READ, WRITE, the password -- is the app's
 * (proto.h).
 */
#ifndef CORE_H
#define CORE_H

#include <stdint.h>
#include "layout.h"

#define HID_REPORT_KBD      1       /* the keyboard's reports */
#define HID_REPORT_VND      2       /* the feature report */
#define HID_KBD_LEN         6       /* id, modifier, reserved, three keys */

/*   [0] command   [1] address low   [2] address high   [3] count   [4..] data */
#define DM_VND_LEN          36
#define DM_VND_DATA         4
#define DM_VND_CHUNK        (DM_VND_LEN - DM_VND_DATA)
#define DM_REFUSED          0xEE    /* comes back in place of the command */

#define DM_MAGIC0           'D'
#define DM_MAGIC1           'M'
#define DM_PROTOCOL_VERSION 3       /* 2 was v1's, with Micronucleus */
#define DM_CORE_VERSION     1

/* The password: twelve bytes at the top of the EEPROM, zero-padded, the
   first 0xFF for none. The core checks it; the app sets it. */
#define DM_PASS_ADDR        0x1F4
#define DM_PASS_LEN         12

/* INFO's state byte [7]: the core's two bits. The app adds the rest. */
#define DM_ST_PASSWORD      0x02    /* a password is stored */
#define DM_ST_UNLOCKED      0x04    /* ...and has been given this session */

/* What hidUnlock() says */
#define DM_PASS_WRONG       0       /* compared, and wrong: costs a second */
#define DM_PASS_RIGHT       1       /* right, or there is no password */
#define DM_PASS_UNCHECKED   2       /* turned away unread: within a second
                                       of a wrong one, or too long */

/* Four bytes in EEPROM the core reads as the serial: see initSerial(). */
#define DM_SERIAL_ADDR      0x1E0
#define DM_SERIAL_PREFIX    "postcogito.org:"
#define DM_SERIAL_PREFIX_LEN 15
#define DM_SERIAL_DIGITS    8
#define DM_SERIAL_LEN       (DM_SERIAL_PREFIX_LEN + DM_SERIAL_DIGITS)

#ifdef __cplusplus
extern "C" {
#endif

/* The core's side: what an app may call, through the table. */
void     hidPoll(void);         /* often: at least every 45 ms or so */
uint8_t  hidReady(void);        /* enumerated */
uint8_t  hidAwake(void);        /* ...and the host has written the lock
                                   lights: it has a keyboard now */
uint8_t  hidSend(const uint8_t *r, uint8_t n);
                                /* one interrupt report, if the last has
                                   gone; 0 if not yet -- poll and retry */
void     hidRestart(void);      /* off the bus and back, the serial read
                                   again, as if replugged */
uint8_t *hidReport(void);       /* the vendor report, DM_VND_LEN bytes:
                                   for wiping it when a secret is done */
uint8_t  hidPassSet(void);      /* is there a password? */
uint8_t  hidUnlock(const uint8_t *p, uint8_t n);
                                /* check one: DM_PASS_*. Right unlocks the
                                   session; wrong pauses guessing a second */
uint8_t  hidUnlocked(void);     /* no password, or it has been given */
void     hidLock(void);         /* the session is over: the window closed */

/* The app's side: what the core calls, through the app's header.
 *
 * app_setup() runs before the core connects to USB, so the app can take
 * its time there -- making a serial, say -- with nobody on the bus to miss
 * it. It must not type. app_loop() runs from then on, over and over.
 *
 * app_request() is called from inside the USB driver, with the report in
 * v, for every request but PAGE, FREAD and COMMIT. For INFO, the core has
 * filled in its part, and the app adds its own. Anything else is the
 * app's to answer, gates and all, returning 1 -- or 0 for a command it
 * does not know, which the core then refuses. Never poll USB from here.
 *
 * The app has no say in updates beyond opening the window (layout.h).
 *
 * The watchdog runs while the app does, about two seconds, and the core's
 * hidPoll() resets it: an app must call hidPoll() at least every second or
 * so, and must leave the watchdog alone after app_setup(). An app that
 * hangs is reset by it, and the core then does not start it again -- the
 * board comes up core only, taking an update behind the password. A replug
 * runs it again.
 *
 * The app's interrupts go through its header (layout.h); before taking
 * any page the core switches off every interrupt source an app may have
 * enabled, the watchdog included, since the code they would jump to is
 * about to be rewritten. */
void     app_setup(void);
void     app_loop(void);
uint8_t  app_request(uint8_t *v);

#ifdef __cplusplus
}
#endif
#endif /* CORE_H */
