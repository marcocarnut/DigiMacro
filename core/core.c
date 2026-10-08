/*
 * The core: everything below the boundary, and the only part of a v2 board
 * that USB can never rewrite.
 *
 * It does what nothing above it could: it is the USB device -- V-USB, the
 * descriptors, the serial, the feature report's plumbing -- it reads flash
 * and fuses, it writes the app's pages and decides whether there is an app
 * to run, and it checks the password that guards those pages, app or no
 * app. Put on a board through Micronucleus, it takes its reset vector back
 * before its first page (ownResetVector()). Everything else, time included,
 * is the app's (layout.h), so that a bug there is one update away from
 * fixed.
 *
 * It cannot be fixed without a programmer, so it is kept small and dull,
 * and core/test/core_test.c compiles this very file for the host.
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
#define flashCopy(d, a, n)  memcpy_P((d), (const void *)(a), (n))
/* Function pointers on the AVR hold word addresses. */
#define APP(slot, type)     ((type)(uintptr_t)(APP_START / 2 + (slot)))
#define FOREVER             for (;;)
#define NORETURN            __attribute__((noreturn))
#endif

/* The driver's transmit state: idle when bit 4 is set. Not in usbdrv.h. */
extern volatile uchar usbTxLen;

typedef void    (*appVoid)(void);
typedef uint8_t (*appByte)(void);
typedef uint8_t (*appReq)(uint8_t *);

/* ===================================================================== */
/* Descriptors                                                           */
/* ===================================================================== */

/* V-USB builds the device and configuration descriptors itself, from
   vusb/usbconfig.h: one HID interface, one interrupt endpoint polled every
   10 ms, version 2.00. This is the rest of what the host reads.

   The keyboard, with a report ID in front of it, and three key slots
   rather than six: ID, modifier, reserved and three keys is six bytes, and
   usbSetInterrupt() will not send more than eight. Then a second
   collection on vendor page 0xFF00, carrying the one feature report, which
   is what WebHID will hand to a page. */
PROGMEM const uchar usbDescriptorHidReport[] = {
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
_Static_assert(sizeof usbDescriptorHidReport == (USB_CFG_DESCR_PROPS_HID_REPORT & 0x3FFF),
               "vusb/usbconfig.h has the report descriptor's length");

/* ===================================================================== */
/* Strings: the maker, the product, the serial                           */
/* ===================================================================== */

/* USB wants them in UTF-16, which for these names is every other byte a
   zero. So they are kept as ASCII and widened when the host asks, into one
   buffer the length of the longest: the maker's name. The serial has no
   text of its own -- "postcogito.org:", the domain obdev's rules want in
   front of it, is the tail of the maker's name -- and its eight digits come
   from the EEPROM as it is asked for, so a restored one is what the next
   enumeration sees. A board with no app and a blank EEPROM says FFFFFFFF
   until the app has made it one (hidusb.c). */
_Static_assert(USB_CFG_DESCR_PROPS_STRING_VENDOR && USB_CFG_DESCR_PROPS_STRING_PRODUCT
               && USB_CFG_DESCR_PROPS_STRING_SERIAL_NUMBER,
               "usbconfig.h must give each string a non-zero property, or the "
               "device descriptor tells the host there is none to ask for");
static const char PROGMEM vendorName[]  = "digimacro.postcogito.org";
static const char PROGMEM productName[] = "DigiMacro";
#define SERIAL_PREFIX_AT 10             /* "postcogito.org" in vendorName */
_Static_assert(sizeof vendorName - 1 - SERIAL_PREFIX_AT == DM_SERIAL_PREFIX_LEN - 1,
               "the serial's prefix is the maker's name from SERIAL_PREFIX_AT on");

/* UTF-16 by halves: only the low bytes are ever written, and the high
   ones stay the zeros .bss started them as. The language, whose high byte
   is not zero, has a descriptor of its own. */
static uint8_t strBuf[2 + 2 * (sizeof vendorName - 1)];
static uint8_t langDesc[4] = { 4, 3, 0x09, 0x04 };      /* US English */

usbMsgLen_t usbFunctionDescriptor(usbRequest_t *rq)
{
    uint8_t which = rq->wValue.bytes[0];
    uint8_t *p = strBuf + 2;
    const char *s = vendorName;

    if (which == 0) {
        usbMsgPtr = (usbMsgPtr_t)langDesc;
        return sizeof langDesc;
    }
    if (which == 2) s = productName;
    else if (which == 3) s = vendorName + SERIAL_PREFIX_AT;
    else if (which != 1) return 0;
    for (char c; (c = pgm_read_byte(s)); s++, p += 2) *p = c;
    if (which == 3) {
        /* The four bytes as v1 stored them, a little-endian word, written
           out most significant first. */
        *p = ':';
        for (uint8_t i = 0; i < DM_SERIAL_DIGITS; i++) {
            uint8_t b = eeprom_read_byte((const uint8_t *)(DM_SERIAL_ADDR + 3 - i / 2));
            uint8_t d = i & 1 ? b & 0x0F : b >> 4;
            p += 2;
            *p = d < 10 ? '0' + d : 'A' - 10 + d;
        }
        p += 2;
    }
    strBuf[0] = p - strBuf;
    strBuf[1] = 3;                      /* a string descriptor */
    usbMsgPtr = (usbMsgPtr_t)strBuf;
    return strBuf[0];
}

/* ===================================================================== */
/* State                                                                 */
/* ===================================================================== */

static uint8_t kbdIdle[HID_KBD_LEN] = { HID_REPORT_KBD };  /* no keys */
static uint8_t hidIdleRate;

static uint8_t vnd[DM_VND_LEN];
static uint8_t vndLeft;
static uint8_t vndAt;

static uint8_t  fuses[4];       /* low, high, extended, lock, read at boot */

/* The core's flags, as bits of GPIOR0: the compiler reaches a register in
   the low I/O space with SBI, CBI and SBIC, two bytes each, where a byte in
   RAM takes a four-byte LDS or STS and more. GPIOR0 is the core's, and main()
   clears it -- Micronucleus leaves the reset reason in it. GPIOR1 is the
   app's; GPIOR2 is the update window (layout.h). */
typedef struct {
    uint8_t app          : 1;   /* DM_CORE_APP: a valid app is running */
    uint8_t update       : 1;   /* DM_CORE_UPDATE: taking pages */
    uint8_t awake        : 1;   /* the host has written the lock lights */
    uint8_t unlocked     : 1;   /* this session; a replug locks */
    uint8_t passWait     : 1;   /* a wrong password: see passPause() */
    uint8_t pageHalf     : 1;   /* the first half of a page is in */
    uint8_t writingVnd   : 1;   /* the SET_REPORT in progress is ours */
    uint8_t commitWanted : 1;
} coreFlags;
#define F               (*(volatile coreFlags *)&GPIOR0)
#define mode            (GPIOR0 & (DM_CORE_APP | DM_CORE_UPDATE))
#define awake           F.awake
#define unlocked        F.unlocked
#define passWait        F.passWait
#define pageHalf        F.pageHalf
#define writingVnd      F.writingVnd
#define commitWanted    F.commitWanted

/* The page being received. Half a page per report, so the first half
   waits here for the second; the second asks for the write, which happens
   in the update's loop -- see updateStep(). */
static uint8_t  pageBuf[PAGE];
static uint16_t pageAt;
static uint8_t  pageWanted;     /* 1 asked; 2 the answer is on its way */
static uint8_t  pageWait;
static uint8_t  halted;         /* DM_CORE_HALTED: see mayRun() */

/* A board with Micronucleus still on it: the spike's builds, which stop
   below it. Everything else owns the whole chip. */
#define HAS_BOOTLOADER  (APP_END <= 0x1A00)

/* softReset()'s word to the next start: the reset was the core's own.
   In a section no start-up code clears, so it outlives the reset. */
#if !HAS_BOOTLOADER
#define RESTART_MARK    0xD1A6
static uint16_t restartMark __attribute__((section(".noinit")));
#endif
static uint16_t commitWait;

/* The core keeps no clock -- time is the app's -- so its few waits are
   counted in turns of this: a poll and a tenth of a millisecond. */
static void turn(void)
{
    wdt_reset();
    usbPoll();
    _delay_us(100);
}

/* ===================================================================== */
/* The password                                                          */
/* ===================================================================== */

/* The core checks it because it guards the pages, and with no app there
   would be nobody else to: a board left with no app by an update cut short
   still has every secret in its EEPROM. Setting it is the app's. */

/* Not inlined: asked from several places, it is smaller as one. */
uint8_t __attribute__((noinline)) hidPassSet(void)
{
    return eeprom_read_byte((const uint8_t *)DM_PASS_ADDR) != 0xFF;
}

uint8_t __attribute__((noinline)) hidUnlocked(void)
{
    return !hidPassSet() || unlocked;
}
void    hidLock(void)     { unlocked = 0; }

/* Compared in full whatever happens, so that how long it takes says nothing
   about how much of it was right. A wrong one costs a second, for which
   any other is turned away unread: a rate limit, not a lockout -- it holds
   up nobody but whoever is guessing. The counting of wrong ones toward a
   lockout is the app's, if it wants it (DM_PASS_WRONG). */
uint8_t __attribute__((noinline)) hidUnlock(const uint8_t *p, uint8_t n)
{
    uint8_t diff = 0;

    if (!hidPassSet()) return DM_PASS_RIGHT;
    if (passWait || n > DM_PASS_LEN) return DM_PASS_UNCHECKED;

    const uint8_t *e = (const uint8_t *)DM_PASS_ADDR;
    for (uint8_t i = 0; i < DM_PASS_LEN; i++, n--)
        diff |= eeprom_read_byte(e++) ^ ((int8_t)n > 0 ? *p++ : 0);
    if (diff) {
        passWait = 1;
        return DM_PASS_WRONG;
    }
    unlocked = 1;
    return DM_PASS_RIGHT;
}

/* The second a wrong password costs, taken here in the loop and never in
   the USB callback that heard it: polling USB from in there is what
   crashed v1. USB is served throughout, and guesses are turned away. */
static void passPause(void)
{
    if (!passWait) return;
    for (uint16_t i = 10000; i; i--) turn();
    passWait = 0;
}

/* ===================================================================== */
/* The app                                                               */
/* ===================================================================== */

/* Whether there is an app, and one built for this core: the magic, the
   API, a length that fits, and the CRC-16 that follows the image. The
   page writer erases the header before writing anything else, which makes
   an interrupted update certain to fail this; the CRC makes a mistaken
   host fail it too. */
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

/* Every interrupt source an app might have turned on, off -- before its
   code is rewritten under it, and before a restart that would run it
   before it was set up. The watchdog too: an app's would reset the board
   halfway through an update. */
static void quiet(void)
{
    wdt_disable();
    TIMSK = 0;
    GIMSK = 1 << USB_INTR_ENABLE_BIT;
    PCMSK = USB_INTR_CFG_SET;
    EECR = 0;
    ACSR &= ~(1 << ACIE);
    ADCSRA &= ~(1 << ADIE);
    USICR &= ~((1 << USISIE) | (1 << USIOIE));
}

/* ===================================================================== */
/* Requests                                                              */
/* ===================================================================== */

static void refuse(void) { vnd[0] = DM_REFUSED; }

/* INFO as the core knows it; the zeros are filled in, by the core or by
   the app. */
static const uint8_t PROGMEM infoFixed[DM_INFO_PAGE] = {
    DM_MAGIC0, DM_MAGIC1, DM_PROTOCOL_VERSION, DM_CORE_VERSION,
    0, 0, 0,                            /* [5..7] the app's */
    0, 0, 0, 0,                         /* [8..11] fuses */
    0, 0,                               /* [12] core state, [13] app version */
    CORE_API,
    (uint8_t)APP_START, APP_START >> 8,
    (uint8_t)APP_END, APP_END >> 8,
    PAGE,
};

static void takePage(uint16_t addr, uint8_t n)
{
    uint8_t second = addr & DM_VND_CHUNK;

    if (n != DM_VND_CHUNK || (addr & (DM_VND_CHUNK - 1))
        || addr < APP_START || addr >= APP_END || pageWanted
        || (second && (!pageHalf || pageAt != addr - DM_VND_CHUNK))) {
        refuse();
        return;
    }
    memcpy(pageBuf + second, vnd + DM_VND_DATA, DM_VND_CHUNK);
    pageAt = addr & ~(PAGE - 1);
    pageHalf = !second;
    if (second) {
        pageWanted = 1;
        vnd[0] = 0;             /* not written yet: see pageWrite() */
    }
}

static void vndAnswer(void)
{
    uint16_t addr = vnd[1] | ((uint16_t)vnd[2] << 8);
    uint8_t  n    = vnd[3];
    uint8_t  app  = F.app;

    if (n > DM_VND_CHUNK) n = DM_VND_CHUNK;

    switch (vnd[0]) {
    case DM_FUNC_INFO:
        memcpy_P(vnd + 1, infoFixed, sizeof infoFixed);
        memset(vnd + 1 + sizeof infoFixed, 0, DM_VND_LEN - 1 - sizeof infoFixed);
        memcpy(vnd + DM_INFO_FUSES, fuses, 4);
        vnd[7] = (hidPassSet()  ? DM_ST_PASSWORD : 0)
               | (hidUnlocked() ? DM_ST_UNLOCKED : 0);
        vnd[DM_INFO_CORE] = mode | halted;
        if (app) {
            vnd[DM_INFO_APPVER] = flashByte(APP_VERSION);
            APP(APP_REQUEST, appReq)(vnd);
        }
        break;

    case DM_FUNC_UNLOCK:
        /* With an app, the app's: it has the window and the lockout, and
           asks hidUnlock() itself. Without one, the core's alone. */
        if (app) goto toApp;
        if (hidUnlock(vnd + DM_VND_DATA, n) != DM_PASS_RIGHT) refuse();
        memset(vnd + DM_VND_DATA, 0, DM_VND_CHUNK);     /* the password */
        break;

    case DM_FUNC_PAGE:
        /* The password, app or no app; and while an app runs, the update
           window too (layout.h): the seconds after a replug, or after the
           app has opened it again. Beyond opening the window, the app has
           no say: an app that could refuse an update could keep itself on
           the board for good. Nothing after a commit: the app it checked
           is the one that starts. */
        if (!hidUnlocked() || commitWanted
            || (app && DM_WINDOW >= DM_WINDOW_TENTHS))
            refuse();
        else
            takePage(addr, n);
        break;

    case DM_FUNC_FREAD:
        /* Ungated: it is the published firmware, the core's or an app's,
           and the secrets are all in the EEPROM. Not while a page waits to
           be written, though: the flash still holds the old page then, and
           a host checking its write by reading it back would take an
           unchanged page for written and send the next too soon. */
        if (pageWanted) {
            refuse();
            break;
        }
        for (uint8_t i = 0; i < n; i++)
            vnd[DM_VND_DATA + i] = (addr + i) <= FLASHEND
                                 ? flashByte(addr + i) : 0xFF;
        break;

    case DM_FUNC_COMMIT:
        /* Only after pages, and only for an app that checks out. A refusal
           leaves the board taking pages, to try again. */
        if (!F.update || pageWanted || !appValid())
            refuse();
        else
            commitWanted = 1;
        break;

    default:
    toApp:
        if (!app || !APP(APP_REQUEST, appReq)(vnd)) refuse();
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
        usbMsgPtr = (usbMsgPtr_t)kbdIdle;
        return sizeof(kbdIdle);

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

/* Both the lock lights and the vendor report arrive here. A leading 2 can
   only be the report ID -- no command is 2 -- since Linux sends it and
   Windows does not. */
uchar usbFunctionWrite(uchar *data, uchar len)
{
    if (!writingVnd) {
        awake = 1;                      /* the host has a keyboard now */
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
/* For the app                                                           */
/* ===================================================================== */

uint8_t hidReady(void) { return usbConfiguration != 0; }
uint8_t hidAwake(void) { return awake; }
uint8_t *hidReport(void) { return vnd; }

uint8_t hidSend(const uint8_t *r, uint8_t n)
{
    if (!usbInterruptIsReady()) return 0;
    usbSetInterrupt((uchar *)r, n);
    return 1;
}

static void reconnect(void)
{
    USB_INTR_ENABLE &= ~(1 << USB_INTR_ENABLE_BIT);
    usbDeviceDisconnect();
    for (uint8_t i = 30; i; i--) _delay_ms(10);
    usbDeviceConnect();
    USB_INTR_PENDING = (1 << USB_INTR_PENDING_BIT);
    usbInit();
}

/* As if unplugged and plugged back in: the serial is read again, so a new
   one is what the host sees, and the host has to write the lock lights
   again before it counts as having a keyboard. Not from app_request():
   note it there, and call this from the loop. */
void hidRestart(void)
{
    awake = 0;
    unlocked = 0;
    USB_INTR_ENABLE &= ~(1 << USB_INTR_ENABLE_BIT);
    usbDeviceDisconnect();
    vnd[0] = DM_FUNC_INFO;
    vndAnswer();                /* so the first read after it is INFO's */
    reconnect();
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
   is room for one halt. A host that is not polling takes nothing, and then
   there is nothing to collide with either: 30 ms, and on. */
static void spmSlot(void)
{
    uint16_t i;
    for (i = 300; i && !hidSend(kbdIdle, sizeof kbdIdle); i--) turn();
    for (i = 300; i && !usbInterruptIsReady(); i--) turn();
}

#ifndef CORE_HOST_TEST
/* Erase the page at `a`, or write `p` to it, or both, with interrupts off
   throughout -- and after any EEPROM write the app left running, since SPM
   and an EEPROM write do not overlap. Both at once is for page 0 alone:
   between its erase and its write the interrupt vectors are blank, and an
   interrupt then -- V-USB takes one per packet -- jumps into erased flash. */
static void __attribute__((noinline)) spm(uint16_t a, const uint8_t *p, uint8_t erase)
{
    eeprom_busy_wait();
    cli();
    if (erase) {
        boot_page_erase(a);
        boot_spm_busy_wait();
    }
    if (p) {
        for (uint8_t i = 0; i < PAGE; i += 2)
            boot_page_fill(a + i, p[i] | ((uint16_t)p[i + 1] << 8));
        boot_page_write(a);
        boot_spm_busy_wait();
    }
    sei();
}

/* After a commit: off the bus, and a real reset, so that the new app
   starts on a chip in its power-on state -- every register, not only the
   ones the core knows to clear; an old app's clock prescaler or power
   reduction would otherwise outlive it. The watchdog does it, and leaves a
   mark in RAM no start-up clears, so that main() knows this reset was the
   core's own and not a hung app's (mayRun()).

   The spike's builds keep Micronucleus, which owns the reset vector and
   would take six seconds of every reset: there, a jump to the start. */
static void NORETURN softReset(void)
{
    cli();
    quiet();
    usbDeviceDisconnect();
#if HAS_BOOTLOADER
    GPIOR0 = 0;                 /* or main() reads the flags as a reset reason */
    asm volatile ("rjmp __init");
#else
    restartMark = RESTART_MARK;
    wdt_enable(WDTO_15MS);
    for (;;)
        ;
#endif
    __builtin_unreachable();
}
#endif

/* A page of the app erased and written, each in its own moment (spmSlot()),
   so that neither halt meets the host's next poll. */
static void spmPage(uint16_t a, const uint8_t *p)
{
    spmSlot();
    spm(a, 0, 1);
    spmSlot();
    spm(a, p, 0);
}

#if !HAS_BOOTLOADER
/* A core put on a board by Micronucleus, as the page's install does (stage
   one of two), starts with a reset vector that is not its own: Micronucleus
   points it at itself, at 0x1A00, and keeps the core's start in a jump just
   below. The app goes on top of Micronucleus, which would then leave a
   reset jumping into the middle of the app.

   So before its first page the core takes the vector back -- page 0 as it
   is, with its first word its own again -- and Micronucleus is never run
   again. It is the one page below the boundary the core ever writes, and
   only ever with that one word changed: nothing a host sends reaches it.
   Power lost in the few milliseconds of it would need a programmer, the
   same risk as every Micronucleus upload, which rewrites page 0 each time;
   it happens once in a board's life.

   Erased and written in one go, interrupts off throughout: page 0 holds the
   interrupt vectors. The first version of this waited between the two for
   the host's poll, as app pages do, and the next USB packet jumped through
   an erased vector: a bricked board. One halt meeting one poll costs the
   host a retry. */
#ifndef RESET_WORD
extern void __init(void);
#define RESET_WORD      (0xC000 | (((uint16_t)(uintptr_t)__init - 1) & 0x0FFF))
#endif

static void ownResetVector(void)
{
    uint8_t page[PAGE];
    uint16_t want = RESET_WORD;

    if (flashWord(0) == want) return;
    flashCopy(page, 0, PAGE);
    page[0] = (uint8_t)want;
    page[1] = want >> 8;
    spmSlot();
    spm(0, page, 1);
}
#endif

/* The header page goes last: anything else erases it first, so that the
   board cannot boot an app that is half old and half new. */
static void pageWrite(void)
{
    uint16_t a = pageAt;

#if !HAS_BOOTLOADER
    ownResetVector();
#endif
    if (a != APP_START && flashWord(APP_START) != 0xFFFF) {
        spmSlot();
        spm(APP_START, 0, 1);
    }
    spmPage(a, pageBuf);

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
   that took it, and once it has gone the driver is idle again -- or 20 ms
   have passed with no host to take it. */
static void updateStep(void)
{
    turn();
    passPause();
    if (pageWanted == 1) {
        pageWanted = 2;
        pageWait = 0;
    } else if (pageWanted == 2 && ((usbTxLen & 0x10) || ++pageWait > 200)) {
        pageWrite();
    }
    if (commitWanted && ++commitWait > 1000)    /* 100 ms: the host reads
                                                   the answer first */
        softReset();
}

static void NORETURN update(void)
{
    quiet();
    F.app = 0;
    F.update = 1;
    FOREVER updateStep();
}

/* The fuses and lock bits, for the app to say whether a board meant to be
   hardened really is. A read is a write to SPMCSR and an LPM within three
   cycles, so it runs before interrupts are on. They only change with a
   programmer, which means a power cycle, so once is enough. */
static void readFuses(void)
{
    fuses[0] = boot_lock_fuse_bits_get(GET_LOW_FUSE_BITS);
    fuses[1] = boot_lock_fuse_bits_get(GET_HIGH_FUSE_BITS);
    fuses[2] = boot_lock_fuse_bits_get(GET_EXTENDED_FUSE_BITS);
    fuses[3] = boot_lock_fuse_bits_get(GET_LOCK_BITS);
}

/* Called often, by the app or by the loop below. A page that has arrived
   means an update has begun, and an update never returns to the app: its
   code is about to be rewritten under it. */
void corePoll(void)
{
    wdt_reset();                /* the app is alive: see main() */
    usbPoll();
    passPause();
    if (pageWanted) update();
}

/* Whether the app may run, from why the chip started (MCUSR's flags).

   A watchdog reset means the app stopped calling corePoll(), unless the
   core caused it itself after a commit (softReset()'s mark). A start with
   no reason at all is a jump to address 0 -- a crash, which would only
   crash again. Either way the app is not started, and the core alone
   answers: it can be replaced, behind the password. A replug is a
   power-on, and runs it again.

   On the spike's boards Micronucleus runs first and takes the flags, so
   only its copy of them in GPIOR0 is to be had, and no crash can be told
   from a start. */
static uint8_t mayRun(uint8_t why)
{
#if HAS_BOOTLOADER
    return !(why & (1 << WDRF));
#else
    uint8_t own = restartMark == RESTART_MARK;
    restartMark = 0;
    if (why & (1 << WDRF)) return own;
    return why != 0;
#endif
}

/* The app is set up before USB: that is what gives it its variables, so
   nothing may call into it before -- INFO included -- and it may want a
   second with interrupts off for a serial. The watchdog watches that too,
   with longer to spare: a setup that hangs would otherwise hold the board
   for good, USB and all, replug after replug. */
static void appStart(uint8_t why)
{
    uint8_t run = mayRun(why);
    if (!appValid()) return;
    if (!run) {
        halted = DM_CORE_HALTED;
        return;
    }
    F.app = 1;
    wdt_enable(WDTO_8S);
    APP(APP_SETUP, appVoid)();
    wdt_enable(WDTO_2S);        /* setup may have borrowed it, for entropy */
}

#ifndef CORE_HOST_TEST
int main(void)
{
    /* Why we are here: in MCUSR -- or on the spike's boards, in GPIOR0,
       where a Micronucleus built with SAVE_MCUSR leaves it. */
    uint8_t why = MCUSR | (HAS_BOOTLOADER ? GPIOR0 : 0);
    MCUSR = 0;
    GPIOR0 = 0;
    DM_WINDOW = 0;              /* the seconds after a replug: layout.h */
    wdt_disable();
    readFuses();
    sei();

    appStart(why);
    hidRestart();
    for (;;) {
        corePoll();
        if (F.app) APP(APP_LOOP, appVoid)();
    }
}
#endif
