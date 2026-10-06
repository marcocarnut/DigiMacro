#include <Arduino.h>
#include <avr/eeprom.h>
#include <avr/pgmspace.h>
#include <avr/interrupt.h>
#include <avr/wdt.h>
#include <util/delay.h>

extern "C" {
#include "vusb/usbdrv.h"
}

#include "hidusb.h"
#include "keymap_us.h"

/* ===================================================================== */
/* Descriptors                                                           */
/* ===================================================================== */

static const uint8_t PROGMEM devDesc[18] = {
    18, USBDESCR_DEVICE,
    0x10, 0x01,             /* bcdUSB 1.1: no BOS wanted here */
    0x00, 0x00, 0x00,       /* class deferred to the interface */
    8,
    0xc0, 0x16,             /* 16c0 */
    0xdb, 0x27,             /* 27db, obdev's shared keyboard id */
    0x00, 0x01,
    1, 2, 3,                /* manufacturer, product, serial -- and the third
                               is what makes a WebHID permission stick */
    1
};

/* The keyboard, with a report ID in front of it, and three key slots rather
   than six: ID, modifier, reserved and three keys is six bytes, and
   usbSetInterrupt() will not send more than eight. Nothing here ever holds
   more than one key down anyway.

   Then a second application collection on usage page 0xFF00, carrying one
   feature report. A protected usage page is what WebHID refuses; a vendor one
   it will hand over. */
#define HID_REPORT_DESC_LEN 88

static const uint8_t PROGMEM hidReportDesc[HID_REPORT_DESC_LEN] = {
    0x05, 0x01,                     /* Usage Page (Generic Desktop)     */
    0x09, 0x06,                     /* Usage (Keyboard)                 */
    0xA1, 0x01,                     /* Collection (Application)         */
    0x85, HID_REPORT_KBD,           /*   Report ID (1)                  */
    0x05, 0x07,                     /*   Usage Page (Keyboard)          */
    0x19, 0xE0, 0x29, 0xE7,         /*   Usages (the eight modifiers)   */
    0x15, 0x00, 0x25, 0x01,
    0x75, 0x01, 0x95, 0x08,
    0x81, 0x02,                     /*   Input: the modifier byte       */
    0x95, 0x01, 0x75, 0x08,
    0x81, 0x03,                     /*   Input: the reserved byte       */
    0x95, 0x05, 0x75, 0x01,
    0x05, 0x08,                     /*   Usage Page (LEDs)              */
    0x19, 0x01, 0x29, 0x05,
    0x91, 0x02,                     /*   Output: five lock lights       */
    0x95, 0x01, 0x75, 0x03,
    0x91, 0x03,                     /*   Output: three bits of padding  */
    0x95, 0x03, 0x75, 0x08,         /*   three key slots                */
    0x15, 0x00, 0x25, 0x65,
    0x05, 0x07, 0x19, 0x00, 0x29, 0x65,
    0x81, 0x00,                     /*   Input: the keys                */
    0xC0,

    0x06, 0x00, 0xFF,               /* Usage Page (Vendor 0xFF00)       */
    0x09, 0x01,                     /* Usage (1)                        */
    0xA1, 0x01,                     /* Collection (Application)         */
    0x85, HID_REPORT_VND,           /*   Report ID (2)                  */
    0x09, 0x02,                     /*   Usage (2)                      */
    0x15, 0x00, 0x26, 0xFF, 0x00,
    0x75, 0x08, 0x95, DM_VND_LEN,
    0xB1, 0x02,                     /*   Feature (Data, Var, Abs)       */
    0xC0
};

static const uint8_t PROGMEM cfgDesc[34] = {
    9, USBDESCR_CONFIG, 34, 0, 1, 1, 0, 0x80, USB_CFG_MAX_BUS_POWER / 2,
    /* Not a boot keyboard: subclass and protocol are zero, because report
       IDs and boot protocol do not go together. */
    9, USBDESCR_INTERFACE, 0, 0, 1, 0x03, 0x00, 0x00, 0,
    9, USBDESCR_HID, 0x01, 0x01, 0x00, 1, USBDESCR_HID_REPORT,
       HID_REPORT_DESC_LEN, 0,
    7, USBDESCR_ENDPOINT, 0x81, 0x03, 8, 0, 10
};

extern "C" usbMsgLen_t usbFunctionDescriptor(usbRequest_t *rq)
{
    switch (rq->wValue.bytes[1]) {
    case USBDESCR_DEVICE:
        usbMsgPtr = (usbMsgPtr_t)devDesc;        return sizeof(devDesc);
    case USBDESCR_CONFIG:
        usbMsgPtr = (usbMsgPtr_t)cfgDesc;        return sizeof(cfgDesc);
    case USBDESCR_HID:
        usbMsgPtr = (usbMsgPtr_t)(cfgDesc + 18); return 9;
    case USBDESCR_HID_REPORT:
        usbMsgPtr = (usbMsgPtr_t)hidReportDesc;  return sizeof(hidReportDesc);
    }
    return 0;
}

/* ===================================================================== */
/* Serial number                                                         */
/* ===================================================================== */

int usbDescriptorStringSerialNumber[1 + DM_SERIAL_LEN] = {
    USB_STRING_DESCRIPTOR_HEADER(DM_SERIAL_LEN),
};

/* Random bits from the watchdog oscillator's jitter against the CPU clock:
 * two independent RC oscillators, so the low bits of Timer0 at each watchdog
 * timeout are unpredictable. Takes about a second, and runs once in a board's
 * life -- on a chip whose EEPROM has never held one.
 *
 * Interrupts stay off throughout, because the watchdog is put in interrupt
 * mode to stop it resetting the part and there is no handler for it. Nothing
 * is lost: this runs before the USB driver starts. */
static uint32_t entropy(void)
{
    uint32_t s = OSCCAL;
    uint8_t sreg = SREG;

    cli();
    TCCR0B = (1 << CS00);
    WDTCR = (1 << WDCE) | (1 << WDE);
    WDTCR = (1 << WDIF) | (1 << WDIE);          /* 16 ms, and no reset */
    for (uint8_t i = 0; i < 64; i++) {
        while (!(WDTCR & (1 << WDIF)))
            ;
        WDTCR |= (1 << WDIF);
        s = ((s << 3) | (s >> 29)) ^ TCNT0;
    }
    wdt_disable();
    TCCR0B = 0;
    SREG = sreg;
    return s;
}

static void initSerial(void)
{
    uint32_t serial = eeprom_read_dword((uint32_t *)DM_SERIAL_ADDR);

    if (serial == 0xFFFFFFFF) {                 /* never had one */
        serial = entropy();
        eeprom_write_dword((uint32_t *)DM_SERIAL_ADDR, serial);
    }
    for (uint8_t i = 0; i < DM_SERIAL_LEN; i++) {
        uint8_t n = (serial >> (28 - 4 * i)) & 0x0F;
        usbDescriptorStringSerialNumber[1 + i] =
            n < 10 ? '0' + n : 'A' - 10 + n;
    }
}

/* ===================================================================== */
/* State                                                                 */
/* ===================================================================== */

#define KBD_REPORT_LEN  6       /* id, modifier, reserved, three keys */

static uint8_t kbdReport[KBD_REPORT_LEN] = { HID_REPORT_KBD };
static uint8_t hidIdleRate;
static uint8_t leds;            /* what the host last said its lights are */
static uint8_t awake;           /* ...and that it said anything at all */

static uint8_t vnd[DM_VND_LEN]; /* the feature report, both ways */
static uint8_t vndLeft;         /* bytes of it still to come */
static uint8_t vndAt;           /* where the next ones go */
static uint8_t writingVnd;      /* tells usbFunctionWrite which it is serving */

static uint16_t allowUntil;     /* the configuration window */
static uint8_t  allowOpen;

static inline uint16_t ticks(void) { return (uint16_t)millis(); }

void hidAllow(uint16_t ms)
{
    allowUntil = ticks() + ms;
    allowOpen = 1;
}

uint8_t hidAllowed(void)
{
    if (allowOpen && (int16_t)(ticks() - allowUntil) >= 0) allowOpen = 0;
    return allowOpen;
}

/* The password belongs to the firmware, not to the transport: the firmware
   owns the EEPROM map, and -- more to the point -- the firmware is what the
   host tests compile, so putting the checking there is what lets it be
   tested. These are the defaults, for a sketch that wants none of it. */
uint8_t __attribute__((weak)) hidPassSet(void)  { return 0; }
uint8_t __attribute__((weak)) hidUnlocked(void) { return 1; }
uint8_t __attribute__((weak)) hidUnlock(const uint8_t *p, uint8_t n)
                                                { (void)p; (void)n; return 1; }
void    __attribute__((weak)) hidSetPass(const uint8_t *p, uint8_t n)
                                                { (void)p; (void)n; }

/* ===================================================================== */
/* Requests                                                              */
/* ===================================================================== */

static void vndAnswer(void)
{
    uint16_t addr = vnd[1] | ((uint16_t)vnd[2] << 8);
    uint8_t  n    = vnd[3];

    if (n > DM_VND_CHUNK) n = DM_VND_CHUNK;

    /* Two gates, and they answer different questions. The window says somebody
       is at the board, which a web page cannot arrange; the password says
       they are the right somebody, which someone holding a stolen board
       cannot. Both, or neither if no password is set.

       INFO answers whatever the gates say: it tells a page what this is and
       what it would have to do, which gives nothing away. */
    if (vnd[0] != DM_FUNC_INFO && !hidAllowed()) {
        vnd[0] = DM_REFUSED;
        return;
    }
    if (vnd[0] != DM_FUNC_INFO && vnd[0] != DM_FUNC_UNLOCK && !hidUnlocked()) {
        vnd[0] = DM_REFUSED;
        return;
    }

    switch (vnd[0]) {
    case DM_FUNC_INFO:
        vnd[1] = DM_MAGIC0;
        vnd[2] = DM_MAGIC1;
        vnd[3] = DM_PROTOCOL_VERSION;
        vnd[4] = DM_FW_VERSION;
        vnd[5] = (uint8_t)(E2END + 1);
        vnd[6] = (uint8_t)((E2END + 1) >> 8);
        vnd[7] = (hidAllowed()  ? DM_ST_OPEN     : 0)
               | (hidPassSet()  ? DM_ST_PASSWORD : 0)
               | (hidUnlocked() ? DM_ST_UNLOCKED : 0);
        break;

    case DM_FUNC_UNLOCK:
        if (!hidUnlock(vnd + DM_VND_DATA, n)) vnd[0] = DM_REFUSED;
        break;

    case DM_FUNC_SETPASS:
        /* Zero bytes clears it, which is how a board goes back to needing
           nothing. Reaching here at all meant getting past both gates. */
        hidSetPass(vnd + DM_VND_DATA, n);
        break;

    case DM_FUNC_READ:
        for (uint8_t i = 0; i < n; i++)
            vnd[DM_VND_DATA + i] = (addr + i) <= E2END
                                 ? eeprom_read_byte((uint8_t *)(addr + i)) : 0xFF;
        break;

    case DM_FUNC_WRITE:
        for (uint8_t i = 0; i < n; i++) {
            if ((addr + i) > E2END) break;
            eeprom_update_byte((uint8_t *)(addr + i), vnd[DM_VND_DATA + i]);
        }
        break;
    }
}

extern "C" usbMsgLen_t usbFunctionSetup(uchar data[8])
{
    usbRequest_t *rq = (usbRequest_t *)data;

    if ((rq->bmRequestType & USBRQ_TYPE_MASK) != USBRQ_TYPE_CLASS) return 0;

    switch (rq->bRequest) {
    case USBRQ_HID_GET_REPORT:
        /* wValue is the report type above and the report ID below. */
        if (rq->wValue.bytes[0] == HID_REPORT_VND) {
            usbMsgPtr = (usbMsgPtr_t)vnd;
            return sizeof(vnd);
        }
        usbMsgPtr = (usbMsgPtr_t)kbdReport;
        return sizeof(kbdReport);

    case USBRQ_HID_SET_REPORT:
        writingVnd = (rq->wValue.bytes[0] == HID_REPORT_VND);
        vndAt = 0;
        vndLeft = sizeof(vnd);
        return USB_NO_MSG;              /* usbFunctionWrite() takes it */

    case USBRQ_HID_GET_IDLE:
        usbMsgPtr = (usbMsgPtr_t)&hidIdleRate;
        return 1;
    case USBRQ_HID_SET_IDLE:
        hidIdleRate = rq->wValue.bytes[1];
        return 0;
    }
    return 0;
}

/* Both the lock lights and the vendor report arrive here.
 *
 * Whether the host puts the report ID in front of the payload or only in
 * wValue is the one thing nobody agrees on: Linux sends it both ways round,
 * Windows strips it. Since no command is 2, a leading 2 can only be the ID,
 * which makes the question answerable rather than argued about. */
extern "C" uchar usbFunctionWrite(uchar *data, uchar len)
{
    if (!writingVnd) {
        if (len) leds = data[len - 1];  /* the lock lights, ID or no ID */
        awake = 1;                      /* and the host has a keyboard now */
        return 1;
    }

    while (len--) {
        uint8_t b = *data++;
        if (vndAt == 0 && b == HID_REPORT_VND) continue;   /* the report ID */
        if (vndAt < sizeof(vnd)) vnd[vndAt++] = b;
        if (vndLeft) vndLeft--;
    }
    if (vndAt < sizeof(vnd) && vndLeft) return 0;

    vndAnswer();
    return 1;
}

/* ===================================================================== */
/* Keyboard                                                              */
/* ===================================================================== */

static void sendReport(uint8_t modifier, uint8_t key)
{
    kbdReport[0] = HID_REPORT_KBD;
    kbdReport[1] = modifier;
    kbdReport[2] = 0;
    kbdReport[3] = key;
    kbdReport[4] = kbdReport[5] = 0;

    uint16_t t = ticks();
    while (!usbInterruptIsReady()) {
        usbPoll();
        if ((uint16_t)(ticks() - t) > 50) return;   /* the host is not asking */
    }
    usbSetInterrupt(kbdReport, sizeof(kbdReport));
}

void hidKey(uint8_t modifier, uint8_t key)
{
    sendReport(modifier, key);
    sendReport(0, 0);
}

void hidTypeChar(char c, uint8_t mod)
{
    uint8_t entry;

    switch (c) {
    case '\r':
    case '\n': hidKey(mod, 0x28); return;
    case '\t': hidKey(mod, 0x2B); return;
    case '\b': hidKey(mod, 0x2A); return;
    }
    if (c < KEYMAP_FIRST || c > KEYMAP_LAST) return;
    entry = pgm_read_byte(&keymapUS[(uint8_t)c - KEYMAP_FIRST]);
    if (entry == 0) return;
    if (entry & KEYMAP_SHIFT) mod |= 0x02;
    hidKey(mod, entry & 0x7F);
}

void hidType_P(const char *s)
{
    char c;
    while ((c = (char)pgm_read_byte(s++)) != 0) hidTypeChar(c, 0);
}

/* ===================================================================== */

void hidBegin(void)
{
    initSerial();

    vnd[0] = DM_FUNC_INFO;
    vndAnswer();                /* so a first read answers something */

    usbInit();
    /* Make the host forget the bootloader. */
    USB_INTR_ENABLE &= ~(1 << USB_INTR_ENABLE_BIT);
    usbDeviceDisconnect();
    for (uint8_t i = 30; i; i--) _delay_ms(10);
    usbDeviceConnect();
    USB_INTR_PENDING = (1 << USB_INTR_PENDING_BIT);
    usbInit();
}

void    hidPoll(void)  { usbPoll(); }
uint8_t hidReady(void) { return usbConfiguration != 0; }
uint8_t hidAwake(void) { return awake; }
uint8_t hidLeds(void)  { return leds; }
