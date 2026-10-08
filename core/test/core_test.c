/*
 * The core's request handling, and the app's protocol over it, on the host.
 *
 * Compiles ../core.c and ../../app/hidusb.c themselves -- not copies -- against
 * stubs for the chip and for V-USB, with an array for the flash, and
 * hidusb.c's functions in the app's slots as an app's header would put
 * them. The core is the part that can never be fixed in the field, so
 * everything a host can ask of it is asked here: the gates, the clamps, the
 * page writer's refusals, a whole update, and updates that go wrong.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define CORE_HOST_TEST 1
#include "layout.h"

uint8_t FLASH[8192];
#define flashByte(a)    FLASH[(uint16_t)(a)]
#define flashWord(a)    (FLASH[(uint16_t)(a)] | FLASH[(uint16_t)((a) + 1)] << 8)
#define flashCopy(d, a, n) memcpy((d), FLASH + (a), (n))
#define FOREVER         /* once: the test turns the loop itself */
#define NORETURN

/* The app's slots, as the test's functions */
static void *appSlots[16];
#define APP(slot, type) ((type)appSlots[slot])

/* The core's own reset vector, as the linker would put it at address 0:
   an rjmp to its start-up code. */
#define RESET_WORD      0xC083

/* Flash writes, checked: nothing may ever land below the boundary -- but
   page 0, rewritten whole with only its reset vector changed back to the
   core's own (core.c, ownResetVector()). */
static int erases, writes, softResets, belowBoundary, page0Writes;
static int vectorsBlank;                /* page 0 erased and left so, between
                                           calls, interrupts on: a brick */
static uint8_t page0Before[PAGE];
static int headerErasedFirst = -1;      /* was the header gone before page 1? */
static void spmErase(uint16_t a);
static void spmWrite(uint16_t a, const uint8_t *p);

/* core.c's spm(): what it may do to page 0 is erase and write it in the
   same call, interrupts off throughout. Erased alone, the vectors stay
   blank until the next call, and V-USB's interrupt finds them so. */
static void spm(uint16_t a, const uint8_t *p, uint8_t erase)
{
    if (a == 0 && !(erase && p)) vectorsBlank++;
    if (erase) spmErase(a);
    if (p) spmWrite(a, p);
}

static void spmErase(uint16_t a)
{
    if (a == 0) { memcpy(page0Before, FLASH, PAGE); memset(FLASH, 0xFF, PAGE); erases++; return; }
    if (a < APP_START || a % PAGE) belowBoundary++;
    else memset(FLASH + a, 0xFF, PAGE);
    erases++;
}
static void spmWrite(uint16_t a, const uint8_t *p)
{
    if (a == 0) {
        /* everything as it was but the first word, which is the core's own */
        if ((p[0] | p[1] << 8) != RESET_WORD
            || memcmp(p + 2, page0Before + 2, PAGE - 2)) belowBoundary++;
        memcpy(FLASH, p, PAGE);
        page0Writes++;
        return;
    }
    if (a < APP_START || a % PAGE) { belowBoundary++; return; }
    if (headerErasedFirst < 0 && a != APP_START)
        headerErasedFirst = FLASH[APP_MAGIC] == 0xFF;
    for (int i = 0; i < PAGE; i++) FLASH[a + i] &= p[i];   /* as flash does */
    writes++;
}
static void softReset(void) { softResets++; }

#include "../core.c"
#include "../../app/hidusb.c"

/* ---- the stubs' state ---- */
uint8_t SREG, OCR1C, OCR1A, TCCR1, TIMSK, OSCCAL, TCCR0B, TCNT0, WDTCR, MCUSR,
        GIMSK, GIFR, PCMSK, EECR, ACSR, ADCSRA, USICR, GPIOR0, GPIOR1, GPIOR2,
        USB_INTR_ENABLE, USB_INTR_PENDING;
uint8_t EE[512], FUSES[4] = { 0xF1, 0xFF, 0xFE, 0xDF };
usbMsgPtr_t usbMsgPtr;
uchar usbConfiguration;
volatile uchar usbTxLen = 0x5a;         /* USBPID_NAK: idle */
static int reports;
int wdtResets, wdtEnabled;
void usbPoll(void) { ms++; }            /* time passes as the core waits */
void usbInit(void) { }
void usbSetInterrupt(uchar *d, uchar n) { (void)d; (void)n; reports++; }
uchar usbInterruptIsReady(void) { return 1; }
void usbDeviceConnect(void) { }
void usbDeviceDisconnect(void) { }

/* ---- the firmware's hooks, as hidusb.c calls them ---- */
static uint8_t lockState, hostState, hostAuthOk;
static int hostForgot, passWrongs, passRights, passChanges;
static void    tVoid(void) { }
static int setupSawWatchdog;            /* was it armed when setup ran? */
static void    tSetup(void) { setupSawWatchdog = wdtEnabled; }
void    hidPassWrong(void) { passWrongs++; }
void    hidPassRight(void) { passRights++; }
void    hidPassChanged(void) { passChanges++; }
uint8_t hidLockState(void) { return lockState; }
uint8_t hidHostState(void) { return hostState; }
void    hidHostForget(void) { hostForgot++; }
uint8_t hidHostAuth(const uint8_t *p, uint8_t n) { (void)p; (void)n; return hostAuthOk; }

/* ---- harness ---- */
static int fails, checks;
#define CHECK(c) do { checks++; if (!(c)) { fails++; \
    printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); } } while (0)

/* A SET_REPORT of the vendor report, delivered in 8-byte packets as the
   driver would, optionally with the report ID in front as Linux sends it. */
static void setReport(const uint8_t *r, int withId)
{
    uint8_t setup[8] = { 0x21, USBRQ_HID_SET_REPORT, HID_REPORT_VND, 3, 0, 0,
                         DM_VND_LEN + withId, 0 };
    uint8_t buf[DM_VND_LEN + 1];
    int n = 0;
    if (withId) buf[n++] = HID_REPORT_VND;
    memcpy(buf + n, r, DM_VND_LEN);
    n += DM_VND_LEN;
    CHECK(usbFunctionSetup(setup) == USB_NO_MSG);
    for (int at = 0; at < n; at += 8)
        usbFunctionWrite(buf + at, n - at < 8 ? n - at : 8);
}

static const uint8_t *getReport(void)
{
    uint8_t setup[8] = { 0xA1, USBRQ_HID_GET_REPORT, HID_REPORT_VND, 3, 0, 0,
                         DM_VND_LEN, 0 };
    CHECK(usbFunctionSetup(setup) == DM_VND_LEN);
    return usbMsgPtr;
}

static const uint8_t *ask(uint8_t cmd, uint16_t addr, uint8_t n, const uint8_t *data)
{
    uint8_t r[DM_VND_LEN] = { cmd, addr & 0xFF, addr >> 8, n };
    if (data) memcpy(r + DM_VND_DATA, data, n > DM_VND_CHUNK ? DM_VND_CHUNK : n);
    setReport(r, 1);
    return getReport();
}

static uint8_t image[APP_END - APP_START];
static int imageLen;

/* An app image as the build makes one: header, body, CRC. */
static void makeImage(int bodyLen, uint8_t version, uint8_t seed)
{
    memset(image, 0xFF, sizeof image);
    for (int i = 0; i < 2 * APP_SLOTS; i++) image[i] = 0xC0 + i;
    int len = APP_CODE - APP_START + bodyLen;
    image[APP_MAGIC - APP_START] = APP_MAGIC0;
    image[APP_MAGIC - APP_START + 1] = APP_MAGIC1;
    image[APP_LENGTH - APP_START] = len;
    image[APP_LENGTH - APP_START + 1] = len >> 8;
    image[APP_VERSION - APP_START] = version;
    image[APP_API - APP_START] = CORE_API;
    for (int i = 0; i < bodyLen; i++)
        image[APP_CODE - APP_START + i] = (uint8_t)(seed + i * 7);
    uint16_t crc = 0xFFFF;
    for (int i = 0; i < len; i++) crc = _crc16_update(crc, image[i]);
    image[len] = crc;
    image[len + 1] = crc >> 8;
    imageLen = len + 2;
}

static void install(void)               /* as a programmer would */
{
    memset(FLASH + APP_START, 0xFF, APP_END - APP_START);
    memcpy(FLASH + APP_START, image, imageLen);
}

static void boot(void)                  /* main(), without the loop */
{
    memset(vnd, 0, sizeof vnd);
    GPIOR0 = 0;                         /* every flag, as main() does */
    DM_WINDOW = 0;
    pageWanted = 0;
    commitWait = 0; allowOpen = 0; restartWanted = 0;
    readFuses();
    halted = 0; wdtEnabled = 0;
    appStart(1 << PORF);                /* plugged in */
    hidRestart();
}

/* Run the loop, as the app's hidPoll() would and then the update's own,
   until nothing is pending. */
static void spin(void)
{
    corePoll();
    for (int i = 0; i < 2000 && (pageWanted || commitWanted); i++) {
        if (pageWanted == 2) usbTxLen = 0x5a;  /* the host took the status */
        updateStep();
    }
}

/* One page in two halves; 1 if the core wrote it. */
static int sendPage(uint16_t a, const uint8_t *p)
{
    if (ask(DM_FUNC_PAGE, a, 32, p)[0] != DM_FUNC_PAGE) return 0;
    usbTxLen = 4;                       /* the status packet, queued */
    if (ask(DM_FUNC_PAGE, a + 32, 32, p + 32)[0] != 0) return 0;
    spin();
    return getReport()[0] == DM_FUNC_PAGE;
}

static int sendImage(int headerLast, int stopAfter)
{
    int pages = (imageLen + PAGE - 1) / PAGE, sent = 0;
    for (int i = 0; i < pages; i++) {
        int pg = headerLast ? (i + 1) % pages : i;
        if (sent == stopAfter) return sent;
        if (!sendPage(APP_START + pg * PAGE, image + pg * PAGE)) return -1;
        sent++;
    }
    return sent;
}

static void setup(void)
{
    appSlots[APP_SETUP] = (void *)tSetup;       appSlots[APP_LOOP] = (void *)tVoid;
    appSlots[APP_REQUEST] = (void *)app_request;
    memset(FLASH, 0xFF, sizeof FLASH);
    memset(FLASH, 0x5A, APP_START);     /* the core, which must never change */
    FLASH[0] = RESET_WORD & 0xFF;       /* its reset vector, as a programmer */
    FLASH[1] = RESET_WORD >> 8;         /* would have left it */
    page0Writes = 0;
    vectorsBlank = 0;
    memset(EE, 0xFF, sizeof EE);
    EE[DM_SERIAL_ADDR] = 0x78; EE[DM_SERIAL_ADDR + 1] = 0x56;
    EE[DM_SERIAL_ADDR + 2] = 0x34; EE[DM_SERIAL_ADDR + 3] = 0x12;
    lockState = hostState = hostAuthOk = 0;
    passWrongs = passRights = passChanges = 0;
    GPIOR0 = 0;
    erases = writes = softResets = belowBoundary = 0;
    headerErasedFirst = -1;
    makeImage(300, 1, 0x11);
    install();
    boot();
}

/* A password as the app stores it: zero-padded, or 0xFF for none. */
static void storePass(const char *p)
{
    for (int i = 0; i < DM_PASS_LEN; i++)
        EE[DM_PASS_ADDR + i] = !p ? 0xFF : i < (int)strlen(p) ? p[i] : 0;
}

static const uint8_t *askPass(uint8_t cmd, const char *p)
{
    return ask(cmd, 0, strlen(p), (const uint8_t *)p);
}

/* A string descriptor as the host would read it: UTF-16LE, its length and
   type in front. */
static int strDesc(uint8_t which, const char *want)
{
    usbRequest_t rq = { 0x80, 6, { .bytes = { which, 3 } } };
    uint8_t len = usbFunctionDescriptor(&rq);
    const uint8_t *d = usbMsgPtr;
    size_t n = strlen(want);
    if (len != 2 + 2 * n || d[0] != len || d[1] != 3) return 0;
    for (size_t i = 0; i < n; i++)
        if (d[2 + 2 * i] != (uint8_t)want[i] || d[3 + 2 * i] != 0) return 0;
    return 1;
}

static int coreIntact(void)
{
    if ((FLASH[0] | FLASH[1] << 8) != RESET_WORD) return 0;
    for (int i = 2; i < APP_START; i++) if (FLASH[i] != 0x5A) return 0;
    return 1;
}

int main(void)
{
    const uint8_t *r;
    uint8_t data[32];

    printf("boot and INFO\n");
    setup();
    CHECK(mode == DM_CORE_APP);
    r = ask(DM_FUNC_INFO, 0, 0, NULL);
    CHECK(r[0] == DM_FUNC_INFO && r[1] == 'D' && r[2] == 'M');
    CHECK(r[3] == DM_PROTOCOL_VERSION && r[4] == DM_CORE_VERSION);
    CHECK(r[5] == 0 && r[6] == 2);                      /* 512 bytes */
    CHECK(r[8] == 0xF1 && r[9] == 0xDF && r[10] == 0xFE && r[11] == 0xFF);
    CHECK(r[DM_INFO_CORE] == DM_CORE_APP && r[DM_INFO_APPVER] == 1);
    CHECK(r[DM_INFO_API] == CORE_API);
    CHECK((r[DM_INFO_START] | r[DM_INFO_START + 1] << 8) == APP_START);
    CHECK((r[DM_INFO_END] | r[DM_INFO_END + 1] << 8) == APP_END);
    CHECK(r[DM_INFO_PAGE] == PAGE);
    storePass("test"); lockState = DM_ST_SOFTLOCK; hostState = DM_ST_HOSTKEY;
    r = ask(DM_FUNC_INFO, 0, 0, NULL);
    CHECK(r[7] == (DM_ST_PASSWORD | DM_ST_SOFTLOCK | DM_ST_HOSTKEY));
    hidAllow(1000);
    r = ask(DM_FUNC_INFO, 0, 0, NULL);
    CHECK(r[7] & DM_ST_OPEN);
    /* the serial, from EEPROM */
    CHECK(strDesc(3, "postcogito.org:12345678"));
    CHECK(strDesc(1, "digimacro.postcogito.org"));
    CHECK(strDesc(2, "DigiMacro"));
    {
        usbRequest_t rq = { 0x80, 6, { .bytes = { 0, 3 } } };
        CHECK(usbFunctionDescriptor(&rq) == 4
              && !memcmp(usbMsgPtr, "\x04\x03\x09\x04", 4));       /* US English */
        rq.wValue.bytes[0] = 4;
        CHECK(usbFunctionDescriptor(&rq) == 0);                     /* none */
    }
    /* A new serial is what the next enumeration sees, no restart of the
       core's own needed */
    EE[DM_SERIAL_ADDR] = 0xEF; EE[DM_SERIAL_ADDR + 3] = 0xAB;
    CHECK(strDesc(3, "postcogito.org:AB3456EF"));
    CHECK(strDesc(2, "DigiMacro"));                     /* nothing left over */

    printf("an app that does not check out is not run\n");
    setup(); FLASH[APP_CODE + 5] ^= 1; boot();
    CHECK(mode == 0);
    setup(); FLASH[APP_API] = CORE_API + 1; boot();
    CHECK(mode == 0);
    setup(); FLASH[APP_LENGTH + 1] = 0x40; boot();      /* longer than the region */
    CHECK(mode == 0);
    setup(); FLASH[APP_MAGIC] = 'X'; boot();
    CHECK(mode == 0);
    setup(); memset(FLASH + APP_START, 0xFF, PAGE); boot();
    CHECK(mode == 0);
    r = ask(DM_FUNC_INFO, 0, 0, NULL);
    CHECK(r[0] == DM_FUNC_INFO && r[DM_INFO_CORE] == 0 && r[DM_INFO_APPVER] == 0);

    printf("the gates, with an app\n");
    setup();
    CHECK(ask(DM_FUNC_READ, 0, 4, NULL)[0] == DM_REFUSED);     /* no window */
    /* pages need no window: with no password, nothing at all, as with
       Micronucleus -- the app has no say */
    CHECK(ask(DM_FUNC_PAGE, APP_START + PAGE, 32, data)[0] == DM_FUNC_PAGE);
    pageHalf = 0;
    CHECK(ask(DM_FUNC_FREAD, 0, 4, NULL)[0] == DM_FUNC_FREAD);  /* public */
    CHECK(ask(DM_FUNC_RESTART, 0, 0, NULL)[0] == DM_REFUSED);
    CHECK(askPass(DM_FUNC_UNLOCK, "test")[0] == DM_REFUSED);  /* no window */
    hidAllow(1000);
    CHECK(ask(DM_FUNC_READ, 0, 4, NULL)[0] == DM_FUNC_READ);
    CHECK(ask(0x41, 0, 0, NULL)[0] == DM_REFUSED);             /* no REBOOT */
    CHECK(ask(0x99, 0, 0, NULL)[0] == DM_REFUSED);
    CHECK(ask(DM_FUNC_COMMIT, 0, 0, NULL)[0] == DM_REFUSED);   /* not updating */

    printf("the password: the core checks it, with an app\n");
    /* A long window: the stub's clock runs a millisecond a poll, and a
       wrong password's pause is ten thousand of them. */
    setup(); storePass("test"); hidAllow(30000);
    r = ask(DM_FUNC_INFO, 0, 0, NULL);
    CHECK((r[7] & (DM_ST_PASSWORD | DM_ST_UNLOCKED)) == DM_ST_PASSWORD);
    CHECK(ask(DM_FUNC_READ, 0, 4, NULL)[0] == DM_REFUSED);
    CHECK(askPass(DM_FUNC_SETPASS, "x")[0] == DM_REFUSED && passChanges == 0);
    /* the window is open, and still no pages: the password decides */
    CHECK(hidAllowed());
    CHECK(ask(DM_FUNC_PAGE, APP_START + PAGE, 32, data)[0] == DM_REFUSED);
    r = askPass(DM_FUNC_UNLOCK, "tesT");
    CHECK(r[0] == DM_REFUSED && passWrongs == 1 && r[4] == 0);  /* wiped */
    /* within the second, even the right one is turned away, uncounted */
    CHECK(askPass(DM_FUNC_UNLOCK, "test")[0] == DM_REFUSED && passWrongs == 1);
    CHECK(!hidUnlocked());
    corePoll();                                  /* the second passes */
    CHECK(askPass(DM_FUNC_UNLOCK, "tes")[0] == DM_REFUSED && passWrongs == 2);
    hidPoll();
    CHECK(askPass(DM_FUNC_UNLOCK, "test\0")[0] == DM_FUNC_UNLOCK);  /* padded */
    CHECK(passRights == 1 && hidUnlocked());
    hidLock(); hidAllow(30000);
    CHECK(askPass(DM_FUNC_UNLOCK, "0123456789abc")[0] == DM_REFUSED);  /* 13 */
    CHECK(passWrongs == 2 && !passWait);        /* unread, uncounted */
    CHECK(askPass(DM_FUNC_UNLOCK, "test")[0] == DM_FUNC_UNLOCK);
    r = ask(DM_FUNC_INFO, 0, 0, NULL);
    CHECK((r[7] & (DM_ST_PASSWORD | DM_ST_UNLOCKED)) == (DM_ST_PASSWORD | DM_ST_UNLOCKED));
    CHECK(ask(DM_FUNC_READ, 0, 4, NULL)[0] == DM_FUNC_READ);
    CHECK(ask(DM_FUNC_PAGE, APP_START + PAGE, 32, data)[0] == DM_FUNC_PAGE);
    pageHalf = 0;
    /* a new password: written, and the session goes on under it */
    CHECK(askPass(DM_FUNC_SETPASS, "newpass")[0] == DM_FUNC_SETPASS);
    CHECK(EE[DM_PASS_ADDR] == 'n' && EE[DM_PASS_ADDR + 7] == 0 && passChanges == 1);
    CHECK(hidUnlocked() && unlocked);
    CHECK(getReport()[4] == 0);                 /* wiped */
    CHECK(ask(DM_FUNC_SETPASS, 0, 13, data)[0] == DM_REFUSED);
    CHECK(EE[DM_PASS_ADDR] == 'n');
    /* the window closing ends the session */
    ms += 40000;
    CHECK(!hidAllowed() && !unlocked && !hidUnlocked());
    /* and none at all */
    hidAllow(1000);
    CHECK(askPass(DM_FUNC_UNLOCK, "newpass")[0] == DM_FUNC_UNLOCK);
    CHECK(ask(DM_FUNC_SETPASS, 0, 0, NULL)[0] == DM_FUNC_SETPASS);
    CHECK(EE[DM_PASS_ADDR] == 0xFF && !hidPassSet() && hidUnlocked());
    /* a restart, as a replug, locks */
    storePass("test");
    CHECK(askPass(DM_FUNC_UNLOCK, "test")[0] == DM_FUNC_UNLOCK);
    hidRestart();
    CHECK(!hidUnlocked());

    printf("the password: the core checks it with no app\n");
    setup(); storePass("test");
    memset(FLASH + APP_START, 0xFF, PAGE); boot();
    CHECK(mode == 0);
    r = ask(DM_FUNC_INFO, 0, 0, NULL);
    CHECK((r[7] & (DM_ST_PASSWORD | DM_ST_UNLOCKED)) == DM_ST_PASSWORD);
    makeImage(900, 7, 0x77);
    CHECK(sendImage(1, -1) < 0);                /* no pages */
    CHECK(erases == 0 && writes == 0);
    CHECK(askPass(DM_FUNC_UNLOCK, "nope")[0] == DM_REFUSED);
    CHECK(passWrongs == 0);                     /* no app to count it */
    CHECK(askPass(DM_FUNC_UNLOCK, "test")[0] == DM_REFUSED);  /* the second */
    hidPoll();
    CHECK(askPass(DM_FUNC_UNLOCK, "test")[0] == DM_FUNC_UNLOCK);
    CHECK(getReport()[4] == 0);                 /* wiped */
    CHECK(ask(DM_FUNC_READ, 0, 4, NULL)[0] == DM_REFUSED);  /* still the app's */
    CHECK(sendImage(1, -1) > 0);
    CHECK(ask(DM_FUNC_COMMIT, 0, 0, NULL)[0] == DM_FUNC_COMMIT);
    boot();
    CHECK(mode == DM_CORE_APP && !hidUnlocked());   /* a new session */

    printf("with no firmware, the session ends by itself\n");
    setup(); storePass("test");
    memset(FLASH + APP_START, 0xFF, PAGE); boot();
    CHECK(askPass(DM_FUNC_UNLOCK, "test")[0] == DM_FUNC_UNLOCK);
    for (long i = 0; i < 65535; i++) turn();
    CHECK(hidUnlocked());                               /* not quite yet */
    turn();
    CHECK(!hidUnlocked());                              /* about seven seconds */
    /* ...and an update left hanging, page closed, board still plugged in */
    CHECK(askPass(DM_FUNC_UNLOCK, "test")[0] == DM_FUNC_UNLOCK);
    makeImage(900, 3, 0x33);
    CHECK(sendImage(1, 3) == 3);
    for (long i = 0; i < 65536; i++) turn();
    CHECK(!hidUnlocked());
    CHECK(ask(DM_FUNC_PAGE, APP_START + 5 * PAGE, 32, data)[0] == DM_REFUSED);
    /* while pages keep coming, it never ends */
    CHECK(askPass(DM_FUNC_UNLOCK, "test")[0] == DM_FUNC_UNLOCK);
    for (int k = 0; k < 4; k++) {
        for (long i = 0; i < 40000; i++) turn();
        CHECK(sendPage(APP_START + (3 + k) * PAGE, image + (3 + k) * PAGE));
    }
    CHECK(hidUnlocked());
    /* with firmware running, the window is what ends it, not this */
    setup(); storePass("test"); hidAllow(30000);
    CHECK(askPass(DM_FUNC_UNLOCK, "test")[0] == DM_FUNC_UNLOCK);
    for (int k = 0; k < 4; k++) {                       /* 80000 turns */
        for (long i = 0; i < 20000; i++) turn();
        hidAllow(30000);                                /* as a held pad would */
    }
    CHECK(hidUnlocked());

    printf("no password, no app: open, as v1 was to Micronucleus\n");
    setup(); memset(FLASH + APP_START, 0xFF, PAGE); boot();
    makeImage(900, 8, 0x88);
    CHECK(sendImage(1, -1) > 0);

    printf("the update window, while an app runs\n");
    setup();
    DM_WINDOW = DM_WINDOW_TENTHS - 1;
    CHECK(ask(DM_FUNC_PAGE, APP_START + PAGE, 32, data)[0] == DM_FUNC_PAGE);
    pageHalf = 0;
    DM_WINDOW = DM_WINDOW_TENTHS;                       /* six seconds on */
    CHECK(ask(DM_FUNC_PAGE, APP_START + PAGE, 32, data)[0] == DM_REFUSED);
    /* the app's clock counts it, in tenths, and stops at 255 */
    DM_WINDOW = 0;
    for (int i = 0; i < 1000; i++) TIMER1_COMPA_vect();
    CHECK(DM_WINDOW == 10);
    DM_WINDOW = 250;
    for (int i = 0; i < 2000; i++) TIMER1_COMPA_vect();
    CHECK(DM_WINDOW == 255);
    /* the app opens it again: behind both of its gates */
    CHECK(ask(DM_FUNC_UPDATE, 0, 0, NULL)[0] == DM_REFUSED);  /* no window */
    CHECK(DM_WINDOW == 255);
    hidAllow(30000); storePass("test");
    CHECK(ask(DM_FUNC_UPDATE, 0, 0, NULL)[0] == DM_REFUSED);  /* locked */
    CHECK(askPass(DM_FUNC_UNLOCK, "test")[0] == DM_FUNC_UNLOCK);
    CHECK(ask(DM_FUNC_UPDATE, 0, 0, NULL)[0] == DM_FUNC_UPDATE);
    CHECK(DM_WINDOW == 0);
    CHECK(ask(DM_FUNC_PAGE, APP_START + PAGE, 32, data)[0] == DM_FUNC_PAGE);
    pageHalf = 0;
    /* with no app, no window: only the password */
    setup(); memset(FLASH + APP_START, 0xFF, PAGE); boot();
    DM_WINDOW = 255;
    CHECK(ask(DM_FUNC_PAGE, APP_START + PAGE, 32, data)[0] == DM_FUNC_PAGE);

    printf("an app the watchdog had to reset is not started again\n");
    setup();
    CHECK(F.app && wdtEnabled);                 /* armed once it is set up */
    CHECK(setupSawWatchdog);                    /* ...and while it was */
    wdtResets = 0; corePoll();
    CHECK(wdtResets > 0);                       /* and fed by corePoll() */
    GPIOR0 = 0; halted = 0; wdtEnabled = 0;
    appStart(1 << WDRF);                        /* from MCUSR, or GPIOR0 */
    CHECK(!F.app && halted == DM_CORE_HALTED && !wdtEnabled);
    r = ask(DM_FUNC_INFO, 0, 0, NULL);
    CHECK(r[DM_INFO_CORE] == DM_CORE_HALTED && r[DM_INFO_APPVER] == 0);
    CHECK(ask(DM_FUNC_READ, 0, 4, NULL)[0] == DM_REFUSED);  /* app's verbs */
    storePass("test");
    CHECK(ask(DM_FUNC_PAGE, APP_START + PAGE, 32, data)[0] == DM_REFUSED);
    CHECK(askPass(DM_FUNC_UNLOCK, "test")[0] == DM_FUNC_UNLOCK);
    DM_WINDOW = 255;                            /* no window: no app */
    CHECK(ask(DM_FUNC_PAGE, APP_START + PAGE, 32, data)[0] == DM_FUNC_PAGE);
    pageHalf = 0;
    /* a reset for any other reason runs it */
    GPIOR0 = 0; halted = 0;
    appStart(1 << EXTRF);
    CHECK(F.app && !halted);
#if !HAS_BOOTLOADER
    printf("a crash is not run again; the core's own restart is\n");
    /* No reason at all: a jump to address 0. */
    GPIOR0 = 0; halted = 0;
    appStart(0);
    CHECK(!F.app && halted == DM_CORE_HALTED);
    /* The watchdog, with softReset()'s mark: the core's own, after a commit */
    GPIOR0 = 0; halted = 0;
    restartMark = RESTART_MARK;
    appStart(1 << WDRF);
    CHECK(F.app && !halted && restartMark != RESTART_MARK);  /* used up */
    /* ...and only once: the next watchdog reset is the app's again */
    GPIOR0 = 0; halted = 0;
    appStart(1 << WDRF);
    CHECK(!F.app && halted == DM_CORE_HALTED);
#else
    printf("beside Micronucleus, no crash can be told from a start\n");
    GPIOR0 = 0; halted = 0;
    appStart(0);
    CHECK(F.app && !halted);
#endif

    printf("a refusal wipes what it carried; INFO survives the window closing\n");
    setup(); storePass("test");
    r = askPass(DM_FUNC_UNLOCK, "test");               /* no window */
    CHECK(r[0] == DM_REFUSED && r[4] == 0);
    r = askPass(DM_FUNC_SETPASS, "newsecret");
    CHECK(r[0] == DM_REFUSED && r[4] == 0);
    hidAllow(1000);
    ms += 2000;                                         /* expires unasked */
    r = ask(DM_FUNC_INFO, 0, 0, NULL);
    CHECK(r[DM_INFO_API] == CORE_API && r[DM_INFO_PAGE] == PAGE);
    CHECK(!allowOpen);

    printf("nothing read back before a page is written, nothing after a commit\n");
    setup(); hidAllow(1000);
    ask(DM_FUNC_PAGE, APP_START + PAGE, 32, image + PAGE);
    usbTxLen = 4;
    ask(DM_FUNC_PAGE, APP_START + PAGE + 32, 32, image + PAGE + 32);
    CHECK(pageWanted);
    /* the flash still holds the old page: no answer from it yet */
    CHECK(ask(DM_FUNC_FREAD, APP_START + PAGE, 32, NULL)[0] == DM_REFUSED);
    spin();
    CHECK(!pageWanted && ask(DM_FUNC_FREAD, APP_START + PAGE, 32, NULL)[0] == DM_FUNC_FREAD);
    makeImage(900, 9, 0x99);
    CHECK(sendImage(1, -1) > 0);
    CHECK(ask(DM_FUNC_COMMIT, 0, 0, NULL)[0] == DM_FUNC_COMMIT);
    CHECK(ask(DM_FUNC_PAGE, APP_START + PAGE, 32, data)[0] == DM_REFUSED);
    CHECK(!pageHalf && FLASH[APP_MAGIC] == APP_MAGIC0);

#if !HAS_BOOTLOADER
    printf("a core Micronucleus put here takes its reset vector back, once\n");
    setup(); memset(FLASH + APP_START, 0xFF, PAGE); boot();
    FLASH[0] = 0xFF; FLASH[1] = 0xCC;                   /* rjmp 0x1A00 */
    makeImage(900, 10, 0xAA);
    CHECK(sendImage(1, 1) == 1);                        /* the first page */
    CHECK(page0Writes == 1 && belowBoundary == 0 && coreIntact());
    CHECK(vectorsBlank == 0);                           /* never left blank */
    CHECK(sendImage(1, -1) > 0);                        /* and no more */
    CHECK(page0Writes == 1 && coreIntact());
    CHECK(ask(DM_FUNC_COMMIT, 0, 0, NULL)[0] == DM_FUNC_COMMIT);
    printf("...and a core flashed by a programmer never touches page 0\n");
    setup(); hidAllow(1000);
    makeImage(900, 10, 0xAB);
    CHECK(sendImage(1, -1) > 0 && page0Writes == 0);
#endif

    printf("the host check needs no gate, and does nothing else\n");
    setup();
    CHECK(ask(DM_FUNC_HOSTAUTH, 0, 8, data)[0] == DM_REFUSED);
    hostAuthOk = 1;
    r = ask(DM_FUNC_HOSTAUTH, 0, 8, (const uint8_t *)"secretXY");
    CHECK(r[0] == DM_FUNC_HOSTAUTH && r[4] == 0 && r[11] == 0);  /* wiped */
    CHECK(!allowOpen);

    printf("READ and WRITE clamp, and secrets are wiped\n");
    setup(); hidAllow(1000);
    r = ask(DM_FUNC_READ, 508, 40, NULL);               /* 40 -> 32; past the end */
    CHECK(r[0] == DM_FUNC_READ && r[4 + 4] == 0xFF && r[4 + 31] == 0xFF);
    memset(data, 0xAB, 32);
    ask(DM_FUNC_WRITE, 500, 32, data);
    CHECK(EE[511] == 0xAB && EE[499] == 0xFF);
    CHECK(getReport()[4] == 0);                          /* wiped */
    /* that wrote a password over the last twelve bytes, and it holds at
       once: the session was not unlocked under it */
    CHECK(hidPassSet() && !hidUnlocked());
    CHECK(ask(DM_FUNC_READ, 0, 4, NULL)[0] == DM_REFUSED);
    storePass(NULL);
    hostForgot = 0;
    ask(DM_FUNC_WRITE, DM_HOST_ADDR + 7, 1, data);
    CHECK(hostForgot == 1);
    ask(DM_FUNC_WRITE, DM_HOST_ADDR + 8, 1, data);
    CHECK(hostForgot == 1);
    ask(DM_FUNC_WRITE, DM_HOST_ADDR - 1, 1, data);
    CHECK(hostForgot == 1);
    ask(DM_FUNC_WRITE, DM_HOST_ADDR - 1, 2, data);
    CHECK(hostForgot == 2);
    r = ask(DM_FUNC_READ, 0, 4, NULL);
    ms += 2000;                                          /* the window closes */
    CHECK(!hidAllowed() && getReport()[4] == 0);

    printf("the report ID, with or without\n");
    setup();
    { uint8_t q[DM_VND_LEN] = { DM_FUNC_INFO }; setReport(q, 0); }
    CHECK(getReport()[1] == 'D');

    printf("FREAD reads any flash, the core included, with no gate\n");
    setup();
    r = ask(DM_FUNC_FREAD, 0, 4, NULL);
    CHECK(r[0] == DM_FUNC_FREAD && (r[4] | r[5] << 8) == RESET_WORD && r[6] == 0x5A);
    r = ask(DM_FUNC_FREAD, APP_MAGIC, 2, NULL);
    CHECK(r[4] == 'D' && r[5] == 'M');
    r = ask(DM_FUNC_FREAD, 8190, 4, NULL);
    CHECK(r[6] == 0xFF);

    printf("PAGE refuses what it must\n");
    setup(); hidAllow(1000);
    memset(data, 0, 32);
    CHECK(ask(DM_FUNC_PAGE, APP_START - PAGE, 32, data)[0] == DM_REFUSED);
    CHECK(ask(DM_FUNC_PAGE, 0, 32, data)[0] == DM_REFUSED);
    CHECK(ask(DM_FUNC_PAGE, APP_END, 32, data)[0] == DM_REFUSED);
    CHECK(ask(DM_FUNC_PAGE, 0xFFE0, 32, data)[0] == DM_REFUSED);
    CHECK(ask(DM_FUNC_PAGE, APP_START + PAGE + 16, 32, data)[0] == DM_REFUSED);
    CHECK(ask(DM_FUNC_PAGE, APP_START + PAGE, 31, data)[0] == DM_REFUSED);
    CHECK(ask(DM_FUNC_PAGE, APP_START + PAGE + 32, 32, data)[0] == DM_REFUSED);
    CHECK(ask(DM_FUNC_PAGE, APP_START + PAGE, 32, data)[0] == DM_FUNC_PAGE);
    CHECK(ask(DM_FUNC_PAGE, APP_START + 2 * PAGE + 32, 32, data)[0] == DM_REFUSED);
    CHECK(!pageWanted && mode == DM_CORE_APP && erases == 0);

    printf("a whole update\n");
    setup(); hidAllow(1000);
    TIMSK = 0x40; GIMSK = 0x60; PCMSK = 0x3F; EECR = 0x08;  /* the app's */
    makeImage(900, 2, 0x22);
    CHECK(sendImage(1, -1) > 0);
    CHECK(mode == DM_CORE_UPDATE);
    CHECK(TIMSK == 0 && GIMSK == 1 << USB_INTR_ENABLE_BIT
          && PCMSK == USB_INTR_CFG_SET && EECR == 0);    /* quiet() */
    CHECK(headerErasedFirst == 1);
    CHECK(memcmp(FLASH + APP_START, image, imageLen) == 0);
    CHECK(belowBoundary == 0 && coreIntact());
    CHECK(reports > 0);                                 /* it waited for polls */
    r = ask(DM_FUNC_INFO, 0, 0, NULL);
    CHECK(r[DM_INFO_CORE] == DM_CORE_UPDATE && r[DM_INFO_APPVER] == 0);
    CHECK(ask(DM_FUNC_READ, 0, 4, NULL)[0] == DM_REFUSED);   /* no app, no gates */
    CHECK(ask(DM_FUNC_HOSTAUTH, 0, 8, data)[0] == DM_REFUSED);
    CHECK(ask(DM_FUNC_COMMIT, 0, 0, NULL)[0] == DM_FUNC_COMMIT);
    spin();
    CHECK(softResets > 0);      /* the stub returns, so it counts again */
    boot();
    CHECK(mode == DM_CORE_APP);
    CHECK(ask(DM_FUNC_INFO, 0, 0, NULL)[DM_INFO_APPVER] == 2);

    printf("the write waits for the host to have its answer\n");
    setup(); hidAllow(1000);
    ask(DM_FUNC_PAGE, APP_START + PAGE, 32, image + PAGE);
    usbTxLen = 4;
    ask(DM_FUNC_PAGE, APP_START + PAGE + 32, 32, image + PAGE + 32);
    {
        /* enter the update by hand, holding the status packet back */
        hidPoll();
        int before = erases;
        for (int i = 0; i < 10; i++) updateStep();
        CHECK(erases == before);
        usbTxLen = 0x5a;
        updateStep();
        CHECK(erases > before && !pageWanted);
    }
    CHECK(ask(DM_FUNC_PAGE, APP_START + PAGE, 32, data)[0] == DM_FUNC_PAGE);
    CHECK(getReport()[0] == DM_FUNC_PAGE);
    {
        /* and gives up waiting for a host that never asks */
        usbTxLen = 4;
        ask(DM_FUNC_PAGE, APP_START + PAGE + 32, 32, data);
        int before = writes;
        for (int i = 0; i < 250; i++) updateStep();
        CHECK(writes == before + 1);
    }

    printf("an update cut short leaves no app to run\n");
    for (int stop = 1; stop < 6; stop++) {
        setup(); hidAllow(1000);
        makeImage(900, 3, 0x33);
        CHECK(sendImage(1, stop) == stop);
        boot();                                         /* the cable, pulled */
        CHECK(mode == 0);
        /* ...and the board takes the update again, with no gate */
        CHECK(sendImage(1, -1) > 0);
        CHECK(ask(DM_FUNC_COMMIT, 0, 0, NULL)[0] == DM_FUNC_COMMIT);
        boot();
        CHECK(mode == DM_CORE_APP && coreIntact());
    }

    printf("a header sent first is undone, and the commit refused\n");
    setup(); hidAllow(1000);
    makeImage(900, 4, 0x44);
    CHECK(sendImage(0, -1) > 0);
    CHECK(ask(DM_FUNC_COMMIT, 0, 0, NULL)[0] == DM_REFUSED);
    CHECK(mode == DM_CORE_UPDATE && softResets == 0);
    sendPage(APP_START, image);                          /* put it right */
    CHECK(ask(DM_FUNC_COMMIT, 0, 0, NULL)[0] == DM_FUNC_COMMIT);

    printf("a bad CRC is refused\n");
    setup(); hidAllow(1000);
    makeImage(900, 5, 0x55);
    image[APP_CODE - APP_START + 100] ^= 0x80;
    CHECK(sendImage(1, -1) > 0);
    CHECK(ask(DM_FUNC_COMMIT, 0, 0, NULL)[0] == DM_REFUSED);
    boot();
    CHECK(mode == 0);

    printf("an app filling the region to the last byte\n");
    setup(); hidAllow(1000);
    makeImage(APP_END - APP_CODE - 2, 6, 0x66);
    CHECK(imageLen == APP_END - APP_START);
    CHECK(sendImage(1, -1) > 0);
    CHECK(ask(DM_FUNC_COMMIT, 0, 0, NULL)[0] == DM_FUNC_COMMIT);
    CHECK(belowBoundary == 0 && coreIntact());
    CHECK(APP_END == sizeof FLASH || FLASH[APP_END] == 0xFF);  /* nothing past */

    printf("RESTART\n");
    setup(); hidAllow(1000);
    hostForgot = 0;
    CHECK(ask(DM_FUNC_RESTART, 0, 0, NULL)[0] == DM_FUNC_RESTART);
    for (int i = 0; i < 60; i++) hidPoll();
    CHECK(!restartWanted && !allowOpen && hostForgot == 1);
    CHECK(getReport()[0] == DM_FUNC_INFO);

    printf("%d checks, %d failed\n", checks, fails);
    return fails != 0;
}
