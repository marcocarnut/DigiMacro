/*
 * The core: everything below the boundary, and the only part of a v2 board
 * that USB can never rewrite.
 *
 * It is v1's DigiHidUSB -- the keyboard, the vendor collection, the serial,
 * the fuses, the window, INFO, READ/WRITE and the gates -- with three
 * things added: its own millisecond timer and main(), since there is no
 * Arduino core underneath any more; a page writer for the region above the
 * boundary; and the check that decides whether there is an app to run.
 *
 * Once it is on a board it cannot be fixed without a programmer, so it is
 * kept small and dull, and test/core_test.c compiles this very file for the
 * host and drives its requests.
 */
#include <stdint.h>
#include <string.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/pgmspace.h>
#include <avr/eeprom.h>
#include <avr/wdt.h>
#include <avr/boot.h>
#include <util/delay.h>
#include <util/crc16.h>

#include "usbdrv.h"
#include "layout.h"
#include "core.h"

/* What differs between the chip and the host tests, which define their own
   before including this file. */
#ifndef CORE_HOST_TEST
#define flashByte(a)        pgm_read_byte((const uint8_t *)(a))
#define flashWord(a)        pgm_read_word((const uint16_t *)(a))
/* Function pointers on the AVR hold word addresses. */
#define APP(slot, type)     ((type)(uintptr_t)(APP_START / 2 + (slot)))
#define FOREVER             for (;;)
#define NORETURN            __attribute__((noreturn))
#endif

/* The driver's transmit state: idle when bit 4 is set. Not in usbdrv.h. */
extern volatile uchar usbTxLen;

typedef void    (*appVoid)(void);
typedef uint8_t (*appByte)(void);
typedef uint8_t (*appTake)(const uint8_t *, uint8_t);
typedef void    (*appGive)(const uint8_t *, uint8_t);

/* ===================================================================== */
/* Time                                                                  */
/* ===================================================================== */

/* Timer1, as on the Digistump core, so that Timer0 stays the app's: v1's
   entropy and anything else that wants a free-running counter. CK/128 and a
   compare at 128 is a tick every 1.0007 ms.

   Not blocking: V-USB's interrupt has to start within a few cycles of the
   bus moving, so this one lets it in at once. */
static volatile uint32_t ms;

ISR(TIMER1_COMPA_vect, ISR_NOBLOCK)
{
    ms++;
}

uint32_t millis(void)
{
    uint8_t sreg = SREG;
    cli();
    uint32_t m = ms;
    SREG = sreg;
    return m;
}

static inline uint16_t ticks(void) { return (uint16_t)millis(); }

static void timerBegin(void)
{
    OCR1C = 128;
    OCR1A = 128;
    TCCR1 = (1 << CTC1) | (1 << CS13);
    TIMSK |= (1 << OCIE1A);
}

/* ===================================================================== */
/* Descriptors -- as v1's                                                */
/* ===================================================================== */

static const uint8_t PROGMEM devDesc[18] = {
    18, USBDESCR_DEVICE,
    0x10, 0x01,
    0x00, 0x00, 0x00,
    8,
    0xc0, 0x16,
    0xdb, 0x27,
    0x00, 0x02,             /* v2 */
    1, 2, 3,
    1
};

#define HID_REPORT_DESC_LEN 88

static const uint8_t PROGMEM hidReportDesc[HID_REPORT_DESC_LEN] = {
    0x05, 0x01, 0x09, 0x06, 0xA1, 0x01,
    0x85, HID_REPORT_KBD,
    0x05, 0x07, 0x19, 0xE0, 0x29, 0xE7,
    0x15, 0x00, 0x25, 0x01, 0x75, 0x01, 0x95, 0x08, 0x81, 0x02,
    0x95, 0x01, 0x75, 0x08, 0x81, 0x03,
    0x95, 0x05, 0x75, 0x01, 0x05, 0x08, 0x19, 0x01, 0x29, 0x05, 0x91, 0x02,
    0x95, 0x01, 0x75, 0x03, 0x91, 0x03,
    0x95, 0x03, 0x75, 0x08, 0x15, 0x00, 0x25, 0x65,
    0x05, 0x07, 0x19, 0x00, 0x29, 0x65, 0x81, 0x00,
    0xC0,

    0x06, 0x00, 0xFF, 0x09, 0x01, 0xA1, 0x01,
    0x85, HID_REPORT_VND,
    0x09, 0x02, 0x15, 0x00, 0x26, 0xFF, 0x00,
    0x75, 0x08, 0x95, DM_VND_LEN, 0xB1, 0x02,
    0xC0
};

static const uint8_t PROGMEM cfgDesc[34] = {
    9, USBDESCR_CONFIG, 34, 0, 1, 1, 0, 0x80, USB_CFG_MAX_BUS_POWER / 2,
    9, USBDESCR_INTERFACE, 0, 0, 1, 0x03, 0x00, 0x00, 0,
    9, USBDESCR_HID, 0x01, 0x01, 0x00, 1, USBDESCR_HID_REPORT,
       HID_REPORT_DESC_LEN, 0,
    7, USBDESCR_ENDPOINT, 0x81, 0x03, 8, 0, 10
};

usbMsgLen_t usbFunctionDescriptor(usbRequest_t *rq)
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
/* Serial number -- as v1's                                              */
/* ===================================================================== */

int usbDescriptorStringSerialNumber[1 + DM_SERIAL_LEN] = {
    USB_STRING_DESCRIPTOR_HEADER(DM_SERIAL_LEN),
};

/* Watchdog jitter against Timer0, with interrupts off: see v1. */
static uint32_t entropy(void)
{
    uint32_t s = OSCCAL;
    uint8_t sreg = SREG;

    cli();
    TCCR0B = (1 << CS00);
    WDTCR = (1 << WDCE) | (1 << WDE);
    WDTCR = (1 << WDIF) | (1 << WDIE);
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

static const char PROGMEM serialPrefix[] = DM_SERIAL_PREFIX;
_Static_assert(sizeof DM_SERIAL_PREFIX - 1 == DM_SERIAL_PREFIX_LEN,
               "DM_SERIAL_PREFIX_LEN must match DM_SERIAL_PREFIX");
_Static_assert((USB_CFG_DESCR_PROPS_STRING_SERIAL_NUMBER & 0x3FFF)
               == 2 * (sizeof usbDescriptorStringSerialNumber
                       / sizeof usbDescriptorStringSerialNumber[0]),
               "usbconfig.h's serial descriptor length must match the serial");

static void initSerial(void)
{
    uint32_t serial = eeprom_read_dword((uint32_t *)DM_SERIAL_ADDR);

    if (serial == 0xFFFFFFFF) {
        serial = entropy();
        eeprom_write_dword((uint32_t *)DM_SERIAL_ADDR, serial);
    }
    for (uint8_t i = 0; i < DM_SERIAL_PREFIX_LEN; i++)
        usbDescriptorStringSerialNumber[1 + i] = pgm_read_byte(&serialPrefix[i]);
    for (uint8_t i = 0; i < DM_SERIAL_DIGITS; i++) {
        uint8_t n = (serial >> (28 - 4 * i)) & 0x0F;
        usbDescriptorStringSerialNumber[1 + DM_SERIAL_PREFIX_LEN + i] =
            n < 10 ? '0' + n : 'A' - 10 + n;
    }
}

/* ===================================================================== */
/* State                                                                 */
/* ===================================================================== */

#define KBD_REPORT_LEN  6

static uint8_t kbdReport[KBD_REPORT_LEN] = { HID_REPORT_KBD };
static uint8_t hidIdleRate;
static uint8_t awake;

static uint8_t vnd[DM_VND_LEN];
static uint8_t vndLeft;
static uint8_t vndAt;
static uint8_t writingVnd;

static uint16_t allowUntil;
static uint8_t  allowOpen;

static uint8_t  fuses[4];
static uint8_t  restartWanted;
static uint16_t restartAsked;

static uint8_t  mode;           /* DM_CORE_APP, DM_CORE_UPDATE */

/* The page being received. Half a page per report, so the first half
   waits here for the second; the second asks for the write, which happens
   in the main loop -- see updateStep(). */
static uint8_t  pageBuf[PAGE];
static uint16_t pageAt;
static uint8_t  pageHalf;       /* the first half is in */
static uint8_t  pageWanted;     /* 1 asked; 2 the answer is on its way */
static uint16_t pageAsked;
static uint8_t  commitWanted;
static uint16_t commitAsked;

void hidAllow(uint16_t ms)
{
    allowUntil = ticks() + ms;
    allowOpen = 1;
}

static void wipe(void)
{
    memset(vnd + DM_VND_DATA, 0, DM_VND_CHUNK);
}

uint8_t hidAllowed(void)
{
    if (allowOpen && (int16_t)(ticks() - allowUntil) >= 0) {
        allowOpen = 0;
        wipe();
    }
    return allowOpen;
}

/* ===================================================================== */
/* The app                                                               */
/* ===================================================================== */

/* Whether there is an app, and one built for this core: the magic, the
   API, a length that fits, and the CRC-16 that follows the image. The
   length and the CRC are what catch an update cut short; the magic alone
   would not, since the header page is written last and an interrupted
   update can leave the old header over half a new app -- except that the
   page writer erases the header before writing anything else. Both, then:
   the erase makes an interrupted update certain to fail, and the CRC makes
   a mistaken host fail too. */
static uint8_t appValid(void)
{
    if (flashByte(APP_MAGIC) != APP_MAGIC0
        || flashByte(APP_MAGIC + 1) != APP_MAGIC1
        || flashByte(APP_API) != CORE_API)
        return 0;

    uint16_t len = flashWord(APP_LENGTH);
    if (len < APP_CODE - APP_START || len > APP_END - APP_START - 2)
        return 0;

    uint16_t crc = 0xFFFF;
    for (uint16_t a = APP_START; a < APP_START + len; a++)
        crc = _crc16_update(crc, flashByte(a));
    return crc == flashWord(APP_START + len);
}

static void appHostForget(void)
{
    if (mode & DM_CORE_APP) APP(APP_HOSTFORGET, appVoid)();
}

/* ===================================================================== */
/* Requests                                                              */
/* ===================================================================== */

static void refuse(void) { vnd[0] = DM_REFUSED; }

/* Taking pages is what an update is, and nothing else may touch the app
   while it happens: once this has said yes, the app is never called again
   until a new one is committed. */
static void takePage(uint16_t addr, uint8_t n)
{
    uint8_t second = addr & DM_VND_CHUNK;

    if (n != DM_VND_CHUNK || (addr & (DM_VND_CHUNK - 1))
        || addr < APP_START || addr >= APP_END || pageWanted)
        { refuse(); return; }
    if (second && (!pageHalf || pageAt != addr - DM_VND_CHUNK))
        { refuse(); return; }

    memcpy(pageBuf + second, vnd + DM_VND_DATA, DM_VND_CHUNK);
    pageAt = addr & ~(PAGE - 1);
    pageHalf = !second;
    if (second) {
        pageWanted = 1;
        pageAsked = ticks();
        vnd[0] = 0;             /* not written yet: see pageWrite() */
    }
}

/* INFO from [1] on, as far as it never changes; the zeros are filled in. */
static const uint8_t PROGMEM infoFixed[DM_VND_LEN - 1] = {
    DM_MAGIC0, DM_MAGIC1, DM_PROTOCOL_VERSION, DM_CORE_VERSION,
    (uint8_t)(E2END + 1), (uint8_t)((E2END + 1) >> 8),
    0,                                  /* [7] state */
    0, 0, 0, 0,                         /* [8..11] fuses */
    0,                                  /* [12] core state */
    0,                                  /* [13] app version */
    CORE_API,
    (uint8_t)APP_START, APP_START >> 8,
    (uint8_t)APP_END, APP_END >> 8,
    PAGE,
};
_Static_assert(DM_INFO_PAGE == 19, "infoFixed must match layout.h's INFO");

static void vndAnswer(void)
{
    uint16_t addr = vnd[1] | ((uint16_t)vnd[2] << 8);
    uint8_t  n    = vnd[3];
    uint8_t  cmd  = vnd[0];

    if (n > DM_VND_CHUNK) n = DM_VND_CHUNK;

    if (!(mode & DM_CORE_APP)) {
        /* No app: an update under way, or one that never finished. Only
           what an update needs, and no gate -- the app owned the gates.
           SPIKE: which gate the core keeps for itself here is one of
           PLAN-v2's open questions. */
        switch (cmd) {
        case DM_FUNC_INFO: case DM_FUNC_PAGE:
        case DM_FUNC_FREAD: case DM_FUNC_COMMIT:
            break;
        default:
            refuse();
            return;
        }
    } else {
        if (cmd == DM_FUNC_HOSTAUTH) {
            if (!APP(APP_HOSTAUTH, appTake)(vnd + DM_VND_DATA, n)) refuse();
            wipe();
            return;
        }
        if (cmd != DM_FUNC_INFO && !hidAllowed()) {
            refuse();
            return;
        }
        if (cmd != DM_FUNC_INFO && cmd != DM_FUNC_UNLOCK
            && !APP(APP_UNLOCKED, appByte)()) {
            refuse();
            return;
        }
    }

    switch (cmd) {
    case DM_FUNC_INFO:
        /* What never changes comes from a template; what does is filled
           in over it. */
        memcpy_P(vnd + 1, infoFixed, sizeof infoFixed);
        memcpy(vnd + DM_INFO_FUSES, fuses, 4);
        vnd[7] = hidAllowed() ? DM_ST_OPEN : 0;
        if (mode & DM_CORE_APP) {
            vnd[7] |= (APP(APP_PASSSET, appByte)()  ? DM_ST_PASSWORD : 0)
                    | (APP(APP_UNLOCKED, appByte)() ? DM_ST_UNLOCKED : 0)
                    | APP(APP_LOCKSTATE, appByte)()
                    | APP(APP_HOSTSTATE, appByte)();
            vnd[DM_INFO_APPVER] = flashByte(APP_VERSION);
        }
        vnd[DM_INFO_CORE] = mode;
        break;

    case DM_FUNC_UNLOCK:
        if (!APP(APP_UNLOCK, appTake)(vnd + DM_VND_DATA, n)) refuse();
        wipe();
        break;

    case DM_FUNC_SETPASS:
        APP(APP_SETPASS, appGive)(vnd + DM_VND_DATA, n);
        wipe();
        break;

    case DM_FUNC_RESTART:
        restartWanted = 1;
        restartAsked = ticks();
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
        if (addr < DM_HOST_ADDR + DM_HOST_LEN && addr + n > DM_HOST_ADDR)
            appHostForget();
        wipe();
        break;

    case DM_FUNC_PAGE:
        takePage(addr, n);
        break;

    case DM_FUNC_FREAD:
        /* Any of it, the core included: it is the published firmware, and
           the secrets are all in the EEPROM. */
        for (uint8_t i = 0; i < n; i++)
            vnd[DM_VND_DATA + i] = (addr + i) <= FLASHEND
                                 ? flashByte(addr + i) : 0xFF;
        break;

    case DM_FUNC_COMMIT:
        /* Only after pages, and only for an app that checks out. A refusal
           leaves the board where it was: taking pages, for another try. */
        if (!(mode & DM_CORE_UPDATE) || pageWanted || !appValid()) {
            refuse();
            break;
        }
        commitWanted = 1;
        commitAsked = ticks();
        break;

    default:
        refuse();
        break;
    }
}

usbMsgLen_t usbFunctionSetup(uchar data[8])
{
    usbRequest_t *rq = (usbRequest_t *)data;

    if ((rq->bmRequestType & USBRQ_TYPE_MASK) != USBRQ_TYPE_CLASS) return 0;

    switch (rq->bRequest) {
    case USBRQ_HID_GET_REPORT:
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
        return USB_NO_MSG;

    case USBRQ_HID_GET_IDLE:
        usbMsgPtr = (usbMsgPtr_t)&hidIdleRate;
        return 1;
    case USBRQ_HID_SET_IDLE:
        hidIdleRate = rq->wValue.bytes[1];
        return 0;
    }
    return 0;
}

uchar usbFunctionWrite(uchar *data, uchar len)
{
    if (!writingVnd) {
        awake = 1;
        return 1;
    }

    while (len--) {
        uint8_t b = *data++;
        if (vndAt == 0 && b == HID_REPORT_VND) continue;
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
        if ((uint16_t)(ticks() - t) > 50) return;
    }
    usbSetInterrupt(kbdReport, sizeof(kbdReport));
}

void hidKey(uint8_t modifier, uint8_t key)
{
    sendReport(modifier, key);
    sendReport(0, 0);
}

/* ===================================================================== */
/* Writing flash                                                         */
/* ===================================================================== */

/* An erase or a write halts the CPU for about 4.5 ms, and the host polls
   the keyboard's interrupt endpoint every 8 or 10. A poll that meets a
   halted CPU is an error on the host's side, and Linux's usbhid resets a
   device whose errors go on for more than a second -- which, at one or two
   per page, an update would manage.

   So each one waits for its moment: hand the host an empty report, and the
   instant it has been taken, the next poll is a whole interval away, which
   is room for one halt. A host that is not polling at all takes nothing,
   and then there is nothing to collide with either. */
static void spmSlot(void)
{
    sendReport(0, 0);
    uint16_t t = ticks();
    while (!usbInterruptIsReady()) {
        usbPoll();
        if ((uint16_t)(ticks() - t) > 30) return;
    }
}

#ifndef CORE_HOST_TEST
static void spmErase(uint16_t a)
{
    cli();
    boot_page_erase(a);
    boot_spm_busy_wait();
    sei();
}

static void spmWrite(uint16_t a, const uint8_t *p)
{
    cli();
    for (uint8_t i = 0; i < PAGE; i += 2)
        boot_page_fill(a + i, p[i] | ((uint16_t)p[i + 1] << 8));
    boot_page_write(a);
    boot_spm_busy_wait();
    sei();
}

/* As if from power-on, without the reset that would hand the board to
   Micronucleus -- which on the spike's boards is still there, and owns the
   reset vector. Off the bus first; main() comes back onto it. */
static void NORETURN softReset(void)
{
    cli();
    USB_INTR_ENABLE &= ~(1 << USB_INTR_ENABLE_BIT);
    usbDeviceDisconnect();
    TIMSK = 0;
    asm volatile ("rjmp __init");
    __builtin_unreachable();
}
#endif

/* The header page goes last: anything else erases it first, so that the
   board cannot boot an app that is half old and half new. */
static void pageWrite(void)
{
    uint16_t a = pageAt;

    if (a != APP_START && flashWord(APP_START) != 0xFFFF) {
        spmSlot();
        spmErase(APP_START);
    }
    spmSlot();
    spmErase(a);
    spmSlot();
    spmWrite(a, pageBuf);

    /* The answer the host reads next: the page, or a refusal if the flash
       does not hold what was sent. */
    uint8_t ok = 1;
    for (uint8_t i = 0; i < PAGE; i++)
        if (flashByte(a + i) != pageBuf[i]) ok = 0;
    vnd[0] = ok ? DM_FUNC_PAGE : DM_REFUSED;
    vnd[1] = (uint8_t)a;
    vnd[2] = a >> 8;
    pageWanted = 0;
    pageHalf = 0;
}

/* One turn of the loop that runs while pages arrive, and nothing of the
   app with it. The write waits for the host to have seen the request
   complete: the second half's status packet is queued by the usbPoll()
   that took it, and once it has gone the driver is idle again. */
static void updateStep(void)
{
    usbPoll();
    if (pageWanted == 1)
        pageWanted = 2;
    else if (pageWanted == 2
             && ((usbTxLen & 0x10) || (uint16_t)(ticks() - pageAsked) > 20))
        pageWrite();
    if (commitWanted && (uint16_t)(ticks() - commitAsked) > 100)
        softReset();
}

static void NORETURN update(void)
{
    mode = DM_CORE_UPDATE;
    allowOpen = 0;
    FOREVER updateStep();
}

/* ===================================================================== */

static void reconnect(void)
{
    USB_INTR_ENABLE &= ~(1 << USB_INTR_ENABLE_BIT);
    usbDeviceDisconnect();
    for (uint8_t i = 30; i; i--) _delay_ms(10);
    usbDeviceConnect();
    USB_INTR_PENDING = (1 << USB_INTR_PENDING_BIT);
    usbInit();
}

static void restart(void)
{
    restartWanted = 0;
    allowOpen = 0;
    awake = 0;
    appHostForget();
    wipe();
    USB_INTR_ENABLE &= ~(1 << USB_INTR_ENABLE_BIT);
    usbDeviceDisconnect();
    initSerial();
    vnd[0] = DM_FUNC_INFO;
    vndAnswer();
    reconnect();
}

static void hidBegin(void)
{
    MCUSR = 0;
    wdt_disable();

    uint8_t sreg = SREG;
    cli();
    fuses[0] = boot_lock_fuse_bits_get(GET_LOW_FUSE_BITS);
    fuses[1] = boot_lock_fuse_bits_get(GET_HIGH_FUSE_BITS);
    fuses[2] = boot_lock_fuse_bits_get(GET_EXTENDED_FUSE_BITS);
    fuses[3] = boot_lock_fuse_bits_get(GET_LOCK_BITS);
    SREG = sreg;

    initSerial();

    vnd[0] = DM_FUNC_INFO;
    vndAnswer();                /* before the app is set up: so no hooks */

    usbInit();
    reconnect();
}

/* The app calls this, and so does the loop below. A page that has arrived
   means an update has begun, and an update never returns to the app: its
   code is about to be rewritten under it. */
void hidPoll(void)
{
    usbPoll();
    if (pageWanted) update();
    if (restartWanted && (uint16_t)(ticks() - restartAsked) > 50) restart();
}

uint8_t hidReady(void) { return usbConfiguration != 0; }
uint8_t hidAwake(void) { return awake; }

#ifndef CORE_HOST_TEST
int main(void)
{
    timerBegin();
    sei();
    uint8_t ok = appValid();
    hidBegin();
    /* Setting the app up is what gives it its variables, so nothing may
       call into it before this -- INFO included. */
    if (ok) {
        mode = DM_CORE_APP;
        APP(APP_SETUP, appVoid)();
    }
    for (;;) {
        hidPoll();
        if (mode & DM_CORE_APP) APP(APP_LOOP, appVoid)();
    }
}
#endif
