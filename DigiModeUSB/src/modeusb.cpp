#include <Arduino.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <util/delay.h>

extern "C" {
#include "vusb/usbdrv.h"
}

#include "modeusb.h"
#include "keymap_us.h"

#define USBDESCR_BOS            0x0F
#define MS_OS_20_SET_LEN        0xA2
#define MS_OS_20_DESCRIPTOR_INDEX 7

/* ===================================================================== */
/* HID keyboard personality                                              */
/* ===================================================================== */

static const uint8_t PROGMEM devDescHid[18] = {
    18, USBDESCR_DEVICE,
    0x10, 0x01,             /* bcdUSB 1.1: no BOS in this personality */
    0x00, 0x00, 0x00,       /* class deferred to the interface */
    8,
    0xc0, 0x16,             /* 16c0 */
    0xdb, 0x27,             /* 27db, obdev's shared keyboard id */
    0x00, 0x01,
    1, 2, 0,
    1
};

#define HID_REPORT_DESC_LEN 63

static const uint8_t PROGMEM cfgDescHid[34] = {
    9, USBDESCR_CONFIG, 34, 0, 1, 1, 0, 0x80, USB_CFG_MAX_BUS_POWER / 2,
    9, USBDESCR_INTERFACE, 0, 0, 1, 0x03, 0x01, 0x01, 0,
    9, USBDESCR_HID, 0x01, 0x01, 0x00, 1, USBDESCR_HID_REPORT,
       HID_REPORT_DESC_LEN, 0,
    7, USBDESCR_ENDPOINT, 0x81, 0x03, 8, 0, 10
};

static const uint8_t PROGMEM hidReportDesc[HID_REPORT_DESC_LEN] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01, 0x05, 0x07,
    0x19, 0xE0, 0x29, 0xE7, 0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08, 0x81, 0x02, 0x95, 0x01,
    0x75, 0x08, 0x81, 0x03, 0x95, 0x05, 0x75, 0x01,
    0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x91, 0x03, 0x95, 0x06,
    0x75, 0x08, 0x15, 0x00, 0x25, 0x65, 0x05, 0x07,
    0x19, 0x00, 0x29, 0x65, 0x81, 0x00, 0xC0
};

/* ===================================================================== */
/* Vendor-class personality, for WebUSB                                  */
/* ===================================================================== */

/* bcdUSB must be 2.10 or the host never asks for the BOS, and without the BOS
   Windows will not bind WinUSB and Chrome will not see a WebUSB device. */
static const uint8_t PROGMEM devDescVnd[18] = {
    18, USBDESCR_DEVICE,
    0x10, 0x02,             /* bcdUSB 2.10 */
    0xff, 0x00, 0x00,       /* vendor class at device level */
    8,
    0xc0, 0x16,             /* 16c0 */
    0xdc, 0x05,             /* 05dc, obdev's shared vendor-class id */
    0x00, 0x01,
    1, 2, 0,
    1
};

/* One interface, vendor class, and no endpoints at all: everything travels
   on control transfers, which is all V-USB would give us anyway. */
static const uint8_t PROGMEM cfgDescVnd[18] = {
    9, USBDESCR_CONFIG, 18, 0, 1, 1, 0, 0x80, USB_CFG_MAX_BUS_POWER / 2,
    9, USBDESCR_INTERFACE, 0, 0, 0, 0xff, 0, 0, 0
};

/* Binary Object Store: an MS OS 2.0 platform capability so Windows binds
   WinUSB with no driver to install, and a WebUSB one naming the landing page.
   Lifted from DigiISP, where it is known to work. */
static const uint8_t PROGMEM bosDescriptor[] = {
    5, USBDESCR_BOS, 64, 0, 3,
    /* USB 2.0 extension: no LPM (Linux complains if it is absent) */
    7, 0x10, 0x02, 0x00, 0x00, 0x00, 0x00,
    /* MS OS 2.0 platform capability */
    28, 0x10, 0x05, 0x00,
    0xDF, 0x60, 0xDD, 0xD8, 0x89, 0x45, 0xC7, 0x4C,
    0x9C, 0xD2, 0x65, 0x9D, 0x9E, 0x64, 0x8A, 0x9F,
    0x00, 0x00, 0x03, 0x06,                 /* Windows 8.1 and later */
    MS_OS_20_SET_LEN, 0x00,
    DM_FUNC_MS_OS_20,                       /* bMS_VendorCode */
    0x00,
    /* WebUSB platform capability */
    24, 0x10, 0x05, 0x00,
    0x38, 0xB6, 0x08, 0x34, 0xA9, 0x09, 0xA0, 0x47,
    0x8B, 0xFD, 0xA0, 0x76, 0x88, 0x15, 0xB6, 0x65,
    0x00, 0x01,
    DM_FUNC_WEBUSB,                         /* bVendorCode */
    1                                       /* iLandingPage */
};

#define LANDING_PAGE "digimacro.postcogito.org"
static const PROGMEM struct {
    uint8_t bLength, bDescriptorType, bScheme;
    char url[sizeof(LANDING_PAGE) - 1];
} landingPage = { 3 + sizeof(LANDING_PAGE) - 1, 3, 1 /* https:// */, LANDING_PAGE };

#define W(c) c, 0       /* UTF-16LE */

static const uint8_t PROGMEM msOs20DescriptorSet[] = {
    0x0A, 0x00, 0x00, 0x00, 0x00, 0x00, 0x03, 0x06, MS_OS_20_SET_LEN, 0x00,
    /* compatible ID: WinUSB */
    0x14, 0x00, 0x03, 0x00, 'W', 'I', 'N', 'U', 'S', 'B', 0, 0,
    0, 0, 0, 0, 0, 0, 0, 0,
    /* registry property: DeviceInterfaceGUIDs (REG_MULTI_SZ) */
    0x84, 0x00, 0x04, 0x00, 0x07, 0x00,
    0x2A, 0x00,
    W('D'), W('e'), W('v'), W('i'), W('c'), W('e'), W('I'), W('n'), W('t'),
    W('e'), W('r'), W('f'), W('a'), W('c'), W('e'), W('G'), W('U'), W('I'),
    W('D'), W('s'), W(0),
    0x50, 0x00,
    W('{'), W('A'), W('C'), W('0'), W('C'), W('7'), W('D'), W('D'), W('1'),
    W('-'), W('7'), W('0'), W('4'), W('B'), W('-'), W('4'), W('D'), W('6'),
    W('5'), W('-'), W('9'), W('C'), W('3'), W('D'), W('-'), W('1'), W('4'),
    W('D'), W('8'), W('6'), W('2'), W('D'), W('4'), W('E'), W('3'), W('4'),
    W('5'), W('}'), W(0), W(0),
};

/* ===================================================================== */
/* State                                                                 */
/* ===================================================================== */

static uint8_t mode = DUSB_HID;
static uint8_t hidReport[8];
static uint8_t hidIdleRate;
static uint8_t hidProtocol = 1;
static uint8_t replyBuffer[8];

static inline uint16_t ticks(void) { return (uint16_t)millis(); }

/* ===================================================================== */
/* V-USB callbacks                                                       */
/* ===================================================================== */

extern "C" usbMsgLen_t usbFunctionDescriptor(usbRequest_t *rq)
{
    /* The driver has already set usbMsgFlags to "in flash" for this path and
       overwrites whatever we put there, so everything here must be PROGMEM. */
    switch (rq->wValue.bytes[1]) {
    case USBDESCR_DEVICE:
        usbMsgPtr = (usbMsgPtr_t)(mode == DUSB_VENDOR ? devDescVnd : devDescHid);
        return 18;

    case USBDESCR_CONFIG:
        if (mode == DUSB_VENDOR) {
            usbMsgPtr = (usbMsgPtr_t)cfgDescVnd;
            return sizeof(cfgDescVnd);
        }
        usbMsgPtr = (usbMsgPtr_t)cfgDescHid;
        return sizeof(cfgDescHid);

    case USBDESCR_HID:
        if (mode != DUSB_HID) return 0;
        usbMsgPtr = (usbMsgPtr_t)(cfgDescHid + 18);
        return 9;

    case USBDESCR_HID_REPORT:
        if (mode != DUSB_HID) return 0;
        usbMsgPtr = (usbMsgPtr_t)hidReportDesc;
        return sizeof(hidReportDesc);

    case USBDESCR_BOS:
        /* Only the vendor personality claims USB 2.10, so only it is ever
           asked -- but answer for that one alone regardless. */
        if (mode != DUSB_VENDOR) return 0;
        usbMsgPtr = (usbMsgPtr_t)bosDescriptor;
        return sizeof(bosDescriptor);
    }
    return 0;
}

extern "C" usbMsgLen_t usbFunctionSetup(uchar data[8])
{
    usbRequest_t *rq = (usbRequest_t *)data;
    uchar type = rq->bmRequestType & USBRQ_TYPE_MASK;

    /* usbMsgFlags was zeroed before we were called, so a pointer returned
       from here is read out of RAM unless we say otherwise. */

    if (mode == DUSB_VENDOR) {
        if (type != USBRQ_TYPE_VENDOR) return 0;

        switch (rq->bRequest) {
        case DM_FUNC_INFO:
            replyBuffer[0] = DM_MAGIC0;
            replyBuffer[1] = DM_MAGIC1;
            replyBuffer[2] = DM_PROTOCOL_VERSION;
            replyBuffer[3] = DM_FW_VERSION;
            usbMsgPtr = (usbMsgPtr_t)replyBuffer;
            return 4;

        case DM_FUNC_WEBUSB:
            if (rq->wIndex.word != 2 || rq->wValue.bytes[0] != 1) return 0;
            usbMsgPtr = (usbMsgPtr_t)&landingPage;
            usbMsgFlags = USB_FLG_MSGPTR_IS_ROM;
            return sizeof(landingPage);

        case DM_FUNC_MS_OS_20:
            if (rq->wIndex.word != MS_OS_20_DESCRIPTOR_INDEX) return 0;
            usbMsgPtr = (usbMsgPtr_t)msOs20DescriptorSet;
            usbMsgFlags = USB_FLG_MSGPTR_IS_ROM;
            return sizeof(msOs20DescriptorSet);
        }
        return 0;
    }

    if (type != USBRQ_TYPE_CLASS) return 0;

    switch (rq->bRequest) {
    case USBRQ_HID_GET_REPORT:
        usbMsgPtr = (usbMsgPtr_t)hidReport;
        return sizeof(hidReport);
    case USBRQ_HID_SET_REPORT:
        return USB_NO_MSG;                  /* the LED report; discarded */
    case USBRQ_HID_GET_IDLE:
        usbMsgPtr = (usbMsgPtr_t)&hidIdleRate;
        return 1;
    case USBRQ_HID_SET_IDLE:
        hidIdleRate = rq->wValue.bytes[1];
        return 0;
    case USBRQ_HID_GET_PROTOCOL:
        usbMsgPtr = (usbMsgPtr_t)&hidProtocol;
        return 1;
    case USBRQ_HID_SET_PROTOCOL:
        hidProtocol = rq->wValue.bytes[0];
        return 0;
    }
    return 0;
}

extern "C" uchar usbFunctionWrite(uchar *data, uchar len)
{
    (void)data; (void)len;
    return 1;                               /* accept and drop, for now */
}

extern "C" uchar usbFunctionRead(uchar *data, uchar len)
{
    (void)data;
    return len;                             /* nothing streams out yet */
}

/* ===================================================================== */
/* Mode switching                                                        */
/* ===================================================================== */

static void reenumerate(uint8_t newMode)
{
    /* The driver's ISR hangs if it fires while D- is held low, so mask the
       pin-change interrupt for the whole disconnected window. */
    USB_INTR_ENABLE &= ~(1 << USB_INTR_ENABLE_BIT);
    usbDeviceDisconnect();

    mode = newMode;
    usbConfiguration = 0;
    for (uint8_t i = 0; i < sizeof(hidReport); i++) hidReport[i] = 0;

    for (uint8_t i = 30; i; i--) _delay_ms(10);      /* ~300 ms */

    usbDeviceConnect();
    USB_INTR_PENDING = (1 << USB_INTR_PENDING_BIT);
    usbInit();
}

void dusbBegin(uint8_t startMode)
{
    mode = startMode;
    usbInit();
    reenumerate(startMode);     /* make the host forget the bootloader */
}

void dusbSetMode(uint8_t newMode)
{
    if (newMode != mode) reenumerate(newMode);
}

uint8_t dusbMode(void)  { return mode; }
uint8_t dusbReady(void) { return usbConfiguration != 0; }
void    dusbPoll(void)  { usbPoll(); }

/* ===================================================================== */
/* HID typing                                                            */
/* ===================================================================== */

static void hidSendReport(uint8_t modifier, uint8_t key)
{
    hidReport[0] = modifier;
    hidReport[1] = 0;
    hidReport[2] = key;
    hidReport[3] = hidReport[4] = hidReport[5] = hidReport[6] = hidReport[7] = 0;

    uint16_t start = ticks();
    while (!usbInterruptIsReady()) {
        dusbPoll();
        if ((uint16_t)(ticks() - start) > 100) return;   /* host gone */
    }
    usbSetInterrupt(hidReport, sizeof(hidReport));
}

void dusbKey(uint8_t modifier, uint8_t key)
{
    hidSendReport(modifier, key);
    hidSendReport(0, 0);
}

void dusbTypeChar(char c, uint8_t mod)
{
    uint8_t entry;

    switch (c) {
    case '\r':
    case '\n': dusbKey(mod, 0x28); return;
    case '\t': dusbKey(mod, 0x2B); return;
    case '\b': dusbKey(mod, 0x2A); return;
    }
    if (c < KEYMAP_FIRST || c > KEYMAP_LAST) return;

    entry = pgm_read_byte(&keymapUS[(uint8_t)c - KEYMAP_FIRST]);
    if (entry == 0) return;
    if (entry & KEYMAP_SHIFT) mod |= 0x02;
    dusbKey(mod, entry & 0x7F);
}

void dusbType(const char *s)
{
    while (*s) dusbTypeChar(*s++, 0);
}

void dusbType_P(const char *s)
{
    char c;
    while ((c = (char)pgm_read_byte(s++)) != 0) dusbTypeChar(c, 0);
}
