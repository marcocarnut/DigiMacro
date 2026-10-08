/*
 * The core's API, as an app sees it, and the protocol both halves share.
 *
 * v1's DigiHidUSB library, split in two: what it did with USB stays in the
 * core, and every call the sketch made into it now goes through the core's
 * table (layout.h). What the sketch provided as weak hooks, the app now
 * provides through its header.
 */
#ifndef CORE_H
#define CORE_H

#include <stdint.h>
#include "layout.h"

#define HID_REPORT_KBD      1       /* the keyboard's reports */
#define HID_REPORT_VND      2       /* the feature report */

/*   [0] command   [1] address low   [2] address high   [3] count   [4..] data */
#define DM_VND_LEN          36
#define DM_VND_DATA         4
#define DM_VND_CHUNK        (DM_VND_LEN - DM_VND_DATA)

#define DM_FUNC_INFO        0x40
#define DM_FUNC_RESTART     0x42
#define DM_FUNC_READ        0x60
#define DM_FUNC_WRITE       0x61
#define DM_FUNC_UNLOCK      0x62
#define DM_FUNC_SETPASS     0x63
#define DM_FUNC_HOSTAUTH    0x64
/* PAGE, FREAD and COMMIT are in layout.h, with the rest of the update */
#define DM_REFUSED          0xEE

#define DM_INFO_FUSES       8

#define DM_ST_OPEN          0x01
#define DM_ST_PASSWORD      0x02
#define DM_ST_UNLOCKED      0x04
#define DM_ST_SOFTLOCK      0x08
#define DM_ST_HARDLOCK      0x10
#define DM_ST_HOST          0x20
#define DM_ST_HOSTKEY       0x40

#define DM_PASS_ADDR        0x1F4
#define DM_PASS_LEN         12
#define DM_SERIAL_ADDR      0x1E0
#define DM_SERIAL_PREFIX    "postcogito.org:"
#define DM_SERIAL_PREFIX_LEN 15
#define DM_SERIAL_DIGITS    8
#define DM_SERIAL_LEN       (DM_SERIAL_PREFIX_LEN + DM_SERIAL_DIGITS)
#define DM_HOST_ADDR        0x1E4
#define DM_HOST_LEN         8

#define DM_MAGIC0           'D'
#define DM_MAGIC1           'M'
#define DM_PROTOCOL_VERSION 3       /* 2 was v1's, with Micronucleus */
#define DM_CORE_VERSION     1

#ifdef __cplusplus
extern "C" {
#endif

/* The core's side: what an app may call. */
void     hidPoll(void);
uint8_t  hidReady(void);
uint8_t  hidAwake(void);
void     hidAllow(uint16_t ms);
uint8_t  hidAllowed(void);
void     hidKey(uint8_t modifier, uint8_t key);
uint32_t millis(void);

/* The app's side: what the core calls, through the app's header. */
void     app_setup(void);
void     app_loop(void);
uint8_t  hidPassSet(void);
uint8_t  hidUnlocked(void);
uint8_t  hidUnlock(const uint8_t *p, uint8_t n);
void     hidSetPass(const uint8_t *p, uint8_t n);
uint8_t  hidLockState(void);
uint8_t  hidHostState(void);
void     hidHostForget(void);
uint8_t  hidHostAuth(const uint8_t *p, uint8_t n);

#ifdef __cplusplus
}
#endif
#endif /* CORE_H */
