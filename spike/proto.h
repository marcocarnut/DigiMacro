/*
 * v1's protocol, as the app now answers it: READ, WRITE, the password, the
 * host check, RESTART, and INFO's state bits. With it, what used to be the
 * USB library's and is now the app's: the clock, the window, typing.
 *
 * Linked into every app, and replaced with it, so a bug here is fixed by an
 * update. The firmware provides the hooks, as it did in v1.
 */
#ifndef PROTO_H
#define PROTO_H

#include <stdint.h>
#include "core.h"

#define DM_FUNC_RESTART     0x42    /* off the bus and back, as if replugged */
#define DM_FUNC_READ        0x60    /* address, count -> EEPROM */
#define DM_FUNC_WRITE       0x61    /* address, count, data */
#define DM_FUNC_UNLOCK      0x62    /* count, password -> accepted or not */
#define DM_FUNC_SETPASS     0x63    /* count, password; count 0 clears it */
#define DM_FUNC_HOSTAUTH    0x64    /* count, host key; needs no gate */

#define DM_INFO_EESIZE      5       /* [5..6] EEPROM size */
#define DM_INFO_STATE       7       /* [7] the state byte */

#define DM_ST_OPEN          0x01
#define DM_ST_PASSWORD      0x02
#define DM_ST_UNLOCKED      0x04
#define DM_ST_SOFTLOCK      0x08
#define DM_ST_HARDLOCK      0x10
#define DM_ST_HOST          0x20
#define DM_ST_HOSTKEY       0x40

#define DM_PASS_ADDR        0x1F4
#define DM_PASS_LEN         12
#define DM_HOST_ADDR        0x1E4
#define DM_HOST_LEN         8

#ifdef __cplusplus
extern "C" {
#endif

void     protoBegin(void);      /* from app_setup(): the clock, the serial */
void     protoPoll(void);       /* from app_loop(), and often: hidPoll()
                                   and what has to wait for the loop */
uint32_t millis(void);
void     hidAllow(uint16_t ms); /* somebody is at the board, briefly */
uint8_t  hidAllowed(void);      /* ask at least every 32 s: see proto.c */
void     hidKey(uint8_t modifier, uint8_t key);

/* The firmware's: the password, the lockout and the host key, as in v1. */
uint8_t hidPassSet(void);
uint8_t hidUnlocked(void);
uint8_t hidUnlock(const uint8_t *p, uint8_t n);
void    hidSetPass(const uint8_t *p, uint8_t n);
uint8_t hidLockState(void);
uint8_t hidHostState(void);
void    hidHostForget(void);
uint8_t hidHostAuth(const uint8_t *p, uint8_t n);

#ifdef __cplusplus
}
#endif
#endif /* PROTO_H */
