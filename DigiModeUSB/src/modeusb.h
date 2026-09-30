/*
 * modeusb -- one V-USB stack, two personalities: an HID keyboard, or a
 * vendor-class device a WebUSB page can talk to.
 *
 * It has to be one or the other, never both. Chrome's WebUSB refuses to claim
 * interfaces of protected classes, HID among them, and Windows binds a HID
 * device to HIDClass rather than to WinUSB -- so a keyboard is not reachable
 * from WebUSB at all. Rather than fight that, the device changes what it is:
 * a four-second hold re-enumerates it as the vendor-class device, and another
 * puts the keyboard back.
 *
 * Which turns out to be the better arrangement anyway. Reaching the macro
 * table needs someone holding the pad, so no web page can reconfigure the
 * device on its own; and while it is configurable it is not a keyboard, so it
 * cannot be made to type.
 *
 * The mechanism is the one DigiMacroCDC used for HID and CDC: a single driver
 * with USB_CFG_DESCR_PROPS_* dynamic, answering descriptor requests from
 * whichever set of PROGMEM arrays the mode selects, and forcing a
 * re-enumeration to change over.
 */
#ifndef MODEUSB_H
#define MODEUSB_H

#include <stdint.h>

#define DUSB_HID    0       /* boot-protocol keyboard      16c0:27db */
#define DUSB_VENDOR 1       /* vendor class, for WebUSB     16c0:05dc */

/* Vendor requests. 0x4D and 0x57 keep DigiISP's numbers because they are
   named inside the BOS descriptor, and there is no reason for them to differ
   between two devices that share a web toolchain. The EEPROM pair sits in its
   own block above everything DigiISP uses, so a tool that meets the wrong
   device gets a stall rather than an action.

   Both are answered in vendor mode only. That is what makes the four-second
   hold a physical-presence check rather than a convenience: in keyboard mode
   the device answers nothing but HID class requests, so a program on the host
   cannot read the table out of a board nobody is touching. */
#define DM_FUNC_INFO        0x40    /* IN:  magic, versions, EEPROM size */
#define DM_FUNC_MS_OS_20    0x4D    /* IN, wIndex 7: the MS OS 2.0 set */
#define DM_FUNC_WEBUSB      0x57    /* IN, wIndex 2: the landing page URL */
#define DM_FUNC_READ        0x60    /* IN:  wValue = address, wLength = count */
#define DM_FUNC_WRITE       0x61    /* OUT: wValue = address, payload = bytes */

#define DM_INFO_LEN         6

#define DM_MAGIC0           'D'
#define DM_MAGIC1           'M'
#define DM_PROTOCOL_VERSION 1
#define DM_FW_VERSION       1

#ifdef __cplusplus
extern "C" {
#endif

void    dusbBegin(uint8_t mode);
void    dusbPoll(void);
uint8_t dusbMode(void);
void    dusbSetMode(uint8_t mode);
uint8_t dusbReady(void);

/* ---- HID keyboard, DUSB_HID only ---------------------------------- */
void    dusbKey(uint8_t modifier, uint8_t key);
void    dusbTypeChar(char c, uint8_t mod);
void    dusbType(const char *s);
void    dusbType_P(const char *s);

#ifdef __cplusplus
}
#endif
#endif /* MODEUSB_H */
