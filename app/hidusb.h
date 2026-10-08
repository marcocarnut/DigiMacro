/*
 * v1's protocol, as the app now answers it: READ, WRITE, the password, the
 * host check, RESTART, and INFO's state bits. With it, what used to be the
 * USB library's and is now the app's: the clock, the window, typing.
 *
 * Linked into every app, and replaced with it, so a bug here is fixed by an
 * update. The firmware provides the hooks, as it did in v1.
 */
#ifndef HIDUSB_H
#define HIDUSB_H

#include <stdint.h>
#include <avr/io.h>
#include <avr/pgmspace.h>
#include "core.h"

/* DigiMacro's own version, in INFO's [13] as the app's. v1 counted to 9
   in [4]; v2 goes on from there, and [4] is the core's now. */
#define DM_FW_VERSION       12

#define DM_FUNC_RESTART     0x42    /* off the bus and back, as if replugged */
#define DM_FUNC_READ        0x60    /* address, count -> EEPROM */
#define DM_FUNC_WRITE       0x61    /* address, count, data */
/* UNLOCK 0x62 is in layout.h: with no app, the core answers it */
#define DM_FUNC_SETPASS     0x63    /* count, password; count 0 clears it */
#define DM_FUNC_HOSTAUTH    0x64    /* count, host key; needs no gate */
#define DM_FUNC_UPDATE      0x68    /* open the update window: six seconds
                                       for the first page (layout.h) */

#define DM_INFO_EESIZE      5       /* [5..6] EEPROM size */
#define DM_INFO_STATE       7       /* [7] the state byte */

#define DM_ST_OPEN          0x01
/* PASSWORD 0x02 and UNLOCKED 0x04 are the core's: core.h */
#define DM_ST_SOFTLOCK      0x08
#define DM_ST_HARDLOCK      0x10
#define DM_ST_HOST          0x20
#define DM_ST_HOSTKEY       0x40

#define DM_HOST_ADDR        0x1E4
#define DM_HOST_LEN         8

#ifdef __cplusplus
extern "C" {
#endif

void     hidBegin(void);        /* from app_setup(): the clock, the serial */
void     hidPoll(void);         /* from app_loop(), and often: the core's
                                   corePoll() and what has to wait for the
                                   loop. The watchdog wants it every second */
uint16_t millis(void);         /* wraps every 65 s: compare differences */
void     hidAllow(uint16_t ms); /* somebody is at the board, briefly */
uint8_t  hidAllowed(void);      /* ask at least every 32 s: see hidusb.c */
void     hidKey(uint8_t modifier, uint8_t key);
void     hidTypeChar(char c, uint8_t mod);  /* US layout; mod adds to it */

/* The firmware's: the lockout and the host key, as in v1. Checking the
   password is the core's (core.h); what a right or wrong one means for the
   lockout, and a new one, are the firmware's to hear about. */
void    hidPassWrong(void);     /* a wrong password: a wrong code */
void    hidPassRight(void);     /* the right one: the lockout clears */
void    hidPassChanged(void);   /* a new one, or none: a clean slate */
uint8_t hidLockState(void);
uint8_t hidHostState(void);
void    hidHostForget(void);
uint8_t hidHostAuth(const uint8_t *p, uint8_t n);

#ifdef __cplusplus
}
#endif
#endif /* HIDUSB_H */
