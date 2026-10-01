/*
 * hidusb -- one HID keyboard with two top-level collections: the keyboard
 * itself, and a vendor one that WebHID is allowed to talk to.
 *
 * The spike behind it: if a vendor collection can carry bytes, it can carry
 * the EEPROM protocol, and the device never has to stop being a keyboard to
 * be configured. Chrome's WebHID refuses *protected* usage pages -- keyboard
 * among them -- but not vendor ones, which is how QMK's Vial configures a
 * keyboard that stays a keyboard the whole time.
 *
 * What it gives up against DigiModeUSB: no second personality, so no BOS, no
 * MS OS 2.0 set, no re-enumeration, and no landing page. What it gives up
 * against the keyboard we have now: boot protocol. Two collections on one
 * interface are told apart by report IDs, a report ID is a byte in front of
 * every report, and a boot keyboard's report must have none -- so this one
 * carries three key slots rather than six, which keeps it inside the eight
 * bytes usbSetInterrupt() will send.
 */
#ifndef HIDUSB_H
#define HIDUSB_H

#include <stdint.h>

#define HID_REPORT_KBD      1       /* the keyboard's reports */
#define HID_REPORT_VND      2       /* the feature report below */

/* The vendor report: a command, where it applies, and up to 32 bytes.
 *
 *   [0] command   [1] address low   [2] address high   [3] count   [4..] data
 *
 * Set the feature report to ask; get it to see the answer. A read is two
 * exchanges, which is what a control transfer is anyway. */
#define DM_VND_LEN          36
#define DM_VND_DATA         4       /* where the data starts */
#define DM_VND_CHUNK        (DM_VND_LEN - DM_VND_DATA)

#define DM_FUNC_INFO        0x40    /* magic, versions, EEPROM size, open? */
#define DM_FUNC_READ        0x60    /* address, count -> data */
#define DM_FUNC_WRITE       0x61    /* address, count, data */
#define DM_REFUSED          0xEE    /* comes back in place of the command */

/* Reaching the table still needs someone at the board. The mode switch used
   to enforce that by accident of being a mode switch; now the sketch opens a
   window when the pad has been held, and INFO says whether one is open so the
   app can ask for it rather than fail at it. INFO itself always answers: it
   says nothing about what is stored. */

#define DM_MAGIC0           'D'
#define DM_MAGIC1           'M'
#define DM_PROTOCOL_VERSION 2       /* 1 was the WebUSB vendor-class one */
#define DM_FW_VERSION       1

#ifdef __cplusplus
extern "C" {
#endif

void    hidBegin(void);
void    hidPoll(void);
uint8_t hidReady(void);
uint8_t hidLeds(void);          /* the host's lock lights, as it last said */

void    hidAllow(uint16_t ms);  /* let the table be read and written, briefly */
uint8_t hidAllowed(void);

void    hidKey(uint8_t modifier, uint8_t key);
void    hidTypeChar(char c, uint8_t mod);
void    hidType_P(const char *s);

#ifdef __cplusplus
}
#endif
#endif /* HIDUSB_H */
