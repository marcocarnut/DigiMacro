/* What core.c uses of V-USB. The test plays the driver: it calls the
   callbacks the way usbPoll() would, and usbPoll() itself only lets time
   pass. */
#pragma once
#include <stdint.h>
typedef unsigned char uchar;
typedef uchar usbMsgLen_t;
#define usbMsgPtr_t uchar *
typedef union { uint16_t word; uchar bytes[2]; } usbWord_t;
typedef struct { uchar bmRequestType, bRequest; usbWord_t wValue, wIndex, wLength; } usbRequest_t;
extern usbMsgPtr_t usbMsgPtr;
extern uchar usbConfiguration;
#define USB_NO_MSG ((usbMsgLen_t)-1)
#define USBRQ_TYPE_MASK 0x60
#define USBRQ_TYPE_CLASS 0x20
#define USBRQ_HID_GET_REPORT 0x01
#define USBRQ_HID_GET_IDLE 0x02
#define USBRQ_HID_SET_REPORT 0x09
#define USBRQ_HID_SET_IDLE 0x0a
#define USBDESCR_DEVICE 1
#define USBDESCR_CONFIG 2
#define USBDESCR_INTERFACE 4
#define USBDESCR_ENDPOINT 5
#define USBDESCR_HID 0x21
#define USBDESCR_HID_REPORT 0x22
#define USB_CFG_MAX_BUS_POWER 100
#define USB_PROP_IS_RAM (1u << 15)
#define USB_PROP_LENGTH(len) ((len) & 0x3fff)
#define USB_CFG_DESCR_PROPS_STRING_SERIAL_NUMBER (USB_PROP_IS_RAM | USB_PROP_LENGTH(2 + 2 * 23))
#define USB_STRING_DESCRIPTOR_HEADER(n) (((2 * (n) + 2) & 0xff) | (3 << 8))
extern uint8_t USB_INTR_ENABLE, USB_INTR_PENDING;
#define USB_INTR_ENABLE_BIT 5
#define USB_INTR_CFG_SET (1 << 4)
#define USB_INTR_PENDING_BIT 5
void usbPoll(void);
void usbInit(void);
void usbSetInterrupt(uchar *data, uchar len);
uchar usbInterruptIsReady(void);
void usbDeviceConnect(void);
void usbDeviceDisconnect(void);
