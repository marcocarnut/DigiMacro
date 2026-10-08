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
 * What it gives up is boot protocol. Two collections on one interface are
 * told apart by report IDs, a report ID is a byte in front of every report,
 * and a boot keyboard's report must have none -- so this one carries three key
 * slots rather than six, which keeps it inside the eight bytes
 * usbSetInterrupt() will send.
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

#define DM_FUNC_INFO        0x40    /* magic, versions, EEPROM size, state */
#define DM_FUNC_REBOOT      0x41    /* into the bootloader, for an upgrade;
                                       absent with DM_NO_BOOTLOADER */
#define DM_FUNC_READ        0x60    /* address, count -> data */
#define DM_FUNC_WRITE       0x61    /* address, count, data */
#define DM_FUNC_UNLOCK      0x62    /* count, password -> accepted or not */
#define DM_FUNC_SETPASS     0x63    /* count, password; count 0 clears it */
#define DM_FUNC_HOSTAUTH    0x64    /* count, host key: the host proves itself.
                                       Needs neither the hold nor the
                                       password -- see the firmware's "host" */
#define DM_REFUSED          0xEE    /* comes back in place of the command */

/* INFO's state byte */
#define DM_ST_OPEN          0x01    /* somebody held the pad recently */
#define DM_ST_PASSWORD      0x02    /* a password is stored */
#define DM_ST_UNLOCKED      0x04    /* ...and has been given this session */
#define DM_ST_SOFTLOCK      0x08    /* too many wrong codes: taps ignored */
#define DM_ST_HARDLOCK      0x10    /* ...until the password is given */
#define DM_ST_HOST          0x20    /* the host has proved itself lately */
#define DM_ST_HOSTKEY       0x40    /* ...and there is a host key to prove */
#define DM_ST_NOBOOT        0x80    /* built with no bootloader: no updates
                                       over USB, and no REBOOT */

/* Twelve bytes at the top of the EEPROM, zero-padded, 0xFF for none. The
 * firmware's map owns this address; it is here because guarding this channel
 * is the library's job and the two must agree.
 *
 * Having no password is the ordinary case and costs nothing: plenty of macros
 * are not secret, and a board that types eleven right-arrows at a television
 * should not ask anybody for anything. An erased chip is therefore an open
 * one, and the password only ever adds a gate. */
#define DM_PASS_ADDR        0x1F4
#define DM_PASS_LEN         12

/* Four bytes of randomness, made once on a virgin chip and kept. Chrome only
 * remembers a WebHID permission for a device that has a serial number, so
 * without one every replug means picking the board out of the chooser again;
 * and making it per board rather than per model means two boards get their
 * own grants instead of sharing one. Same trick as DigiISP, for the same
 * reason.
 *
 * The descriptor carries it after a domain and a colon, because 16c0:27db is
 * one of obdev's IDs for devices told apart by serial number, and their
 * terms say the serial must start with a domain the author owns, so that two
 * people's devices on the shared ID can never be confused. */
#define DM_SERIAL_ADDR      0x1E0
#define DM_SERIAL_PREFIX    "postcogito.org:"
#define DM_SERIAL_PREFIX_LEN 15
#define DM_SERIAL_DIGITS    8       /* the four bytes, as hex */
#define DM_SERIAL_LEN       (DM_SERIAL_PREFIX_LEN + DM_SERIAL_DIGITS)

/* Eight random bytes the host sends to prove it is the right one; all 0xFF
 * for none. Written like the table, through WRITE, behind both gates. */
#define DM_HOST_ADDR        0x1E4
#define DM_HOST_LEN         8

/* Reaching the table still needs someone at the board. The mode switch used
   to enforce that by accident of being a mode switch; now the sketch opens a
   window when the pad has been held, and INFO says whether one is open so the
   app can ask for it rather than fail at it. INFO itself always answers: it
   says nothing about what is stored. */

#define DM_MAGIC0           'D'
#define DM_MAGIC1           'M'
#define DM_PROTOCOL_VERSION 2       /* 1 was the WebUSB vendor-class one */
#define DM_FW_VERSION       6

#ifdef __cplusplus
extern "C" {
#endif

void    hidBegin(void);
void    hidPoll(void);
uint8_t hidReady(void);         /* enumerated: the host knows what this is */
uint8_t hidAwake(void);         /* ...and has spoken to the keyboard half */

/* The difference between those two matters, and costs a macro its first
   characters when it is missed. Enumeration is the host reading descriptors;
   it happens early, and keystrokes sent then go nowhere, because nothing has
   attached a keyboard to anything yet. The first lock-light report is the
   host talking TO the keyboard, which it only does once there is one. */

void    hidAllow(uint16_t ms);  /* somebody is at the board, briefly */
uint8_t hidAllowed(void);
/* Provided by the firmware, which owns the EEPROM map and is what the host
   tests compile. The library ships defaults that want no password at all. */
uint8_t hidPassSet(void);
uint8_t hidUnlocked(void);
uint8_t hidUnlock(const uint8_t *p, uint8_t n);
void    hidSetPass(const uint8_t *p, uint8_t n);
uint8_t hidLockState(void);     /* 0, DM_ST_SOFTLOCK or DM_ST_HARDLOCK */
uint8_t hidHostState(void);     /* DM_ST_HOST and DM_ST_HOSTKEY */
uint8_t hidHostAuth(const uint8_t *p, uint8_t n);

void    hidKey(uint8_t modifier, uint8_t key);
void    hidTypeChar(char c, uint8_t mod);

#ifdef __cplusplus
}
#endif
#endif /* HIDUSB_H */
