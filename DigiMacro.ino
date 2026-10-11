/*
 * DigiMacro -- a morse-tapped macro keyboard on an ATtiny85 Digispark.
 *
 * A touch pad on P0, and optionally an IR remote on P2. Tap a code in dots
 * and dashes; if the table in EEPROM has an entry for that code, the board
 * types it as keystrokes.
 * Hold the pad four seconds and the app may read and write the table for the
 * next half minute.
 *
 * It stays a keyboard throughout. The app reaches it through a second
 * top-level collection on a vendor usage page, which is the one kind of HID
 * collection a browser will hand over -- so there is no second personality,
 * no re-enumeration, and nothing to install on either platform. The hold is
 * not a mode switch any more; it is the part a web page cannot forge, which
 * is somebody standing at the board.
 *
 * Sibling of DigiMacroCDC, which does the same job with an on-device command
 * line over a CDC serial port. Everything that edited the table there has
 * moved to the app, so the firmware here only ever *reads* it, and the table
 * format is the interface between the two halves of the project -- see below,
 * and keep the app in step with it.
 *
 *   plugging it in    types the macro nominated as the power-on one, if any
 *   short tap         one symbol; a press under 250 ms is a dot, longer a dash
 *   700 ms of silence the code is finished: look it up and type it
 *   4 s hold          let the app in, for half a minute
 *
 *   LED  off          idle -- deliberately dark while a code is tapped, so it
 *                     does not spell the code out for anyone watching
 *        on           a macro is typing
 *        3 quick      the code matched nothing
 *        blinking     the app may configure it, for half a minute
 *        blip every 2 s   soft lock: too many wrong codes, taps ignored
 *        fast flicker     hard lock: taps ignored until the password
 *
 * The pad wants no external parts. When it misbehaves, build with
 * TOUCH_DEBUG=1 (make TOUCHDEBUG=1) and the board types its own readings
 * twice a second instead of staying quiet -- the only diagnostic channel a
 * device with no serial port has:
 *
 *      1A 18 . - 0834   raw, running average, touched?, press open?, and
 *      30 18 T # 0821   how many times the pad was read since the last line
 *
 * Read them like this:
 *
 *   raw sits at F8    the pad never crosses: nothing is charging it
 *   raw sits at 00    it crosses within one loop pass: lower TOUCH_REPS
 *   raw == base, flat a pad that is connected but not moving
 *   raw rises on touch working; a quarter above base trips it
 *
 * and for a hold that will not take, watch the last two columns while you
 * hold: the four seconds only accumulate while both stay "T #". A single
 * line of ". -" in the middle means the press ended and started again, which
 * is what a marginal pad does to a long press long before it spoils a tap.
 */

#include <hidusb.h>
#include <xtea.h>
#include <string.h>
#include <avr/eeprom.h>
#include <avr/interrupt.h>
#include <util/delay.h>

/* ---- options -------------------------------------------------------- */

#ifndef HAVE_TOUCH
#define HAVE_TOUCH  1       /* 0 = a pushbutton from P0 to ground instead */
#endif

#ifndef LED_ECHO
#define LED_ECHO    0       /* 1 = the LED follows the input while tapping.
                               For working on the input path only: it is the
                               optical equivalent of a loud keyboard, and
                               shows a bystander the code. */
#endif

#ifndef HAVE_JUMP
#define HAVE_JUMP 1         /* 0 = no {goto}, and no loops to escape from */
#endif

#ifndef HAVE_AUTOEXEC
#define HAVE_AUTOEXEC 1     /* 0 = never type anything unasked */
#endif

#ifndef HAVE_IR
#define HAVE_IR 1           /* 0 = no IR remote on P2 */
#endif


#ifndef TOUCH_DEBUG
#define TOUCH_DEBUG 0       /* 1 = type the pad readings twice a second, into
                               whatever window has focus. See below. */
#endif
#define DEBUG_MS    500

/* ---- pins and timing ------------------------------------------------ */

#define PIN_BUTTON              0
#define PIN_TOUCH   PIN_BUTTON      /* the same pin; a pad rather than a switch */
#define PIN_LED                 1   /* clones with the LED on P0 must move it */
#define PIN_IR                  2   /* INT0: an IR receiver's output, if any */

#define LED_SET(v)   do { if (v) PORTB |=  (1 << PIN_LED); \
                          else   PORTB &= ~(1 << PIN_LED); } while (0)
#define BTN_PRESSED()  (!(PINB & (1 << PIN_BUTTON)))

#if HAVE_TOUCH
#define INPUT_ACTIVE()  touch_state     /* refreshed by touch_poll() */
#else
#define INPUT_ACTIVE()  BTN_PRESSED()
#endif

static inline uint16_t ticks(void) { return (uint16_t)millis(); }

#define DEBOUNCE_MS             25
#define DOT_MAX_MS             250  /* shorter press is a dot, longer a dash */
#define END_OF_CODE_MS         700  /* silence this long ends the code       */
#define HOLD_MS               4000  /* hold this long to let the app in      */
#define CONFIG_MS            30000  /* ...and it stays in for this long      */

/* How long to let the host attach before typing at it unprompted. Being
   enumerated is not the same as having somewhere to type: the host reads the
   descriptors early and binds a keyboard to the input stack some time later,
   and a macro that starts before that loses however much of itself fits in
   the gap. The first lock-light report is the host speaking to the keyboard,
   which is as good a sign as there is that one now exists; failing that,
   there is a timeout, because nothing guarantees a host sends one. */
#define AUTO_SETTLE_MS         250  /* after the host says hello */
#define AUTO_WAIT_MS          1500  /* ...or after this, if it never does */

/* ==================================================================== */
/* The table                                                            */
/* ==================================================================== */

/* The app rewrites the whole table, so entries need not be a fixed size:
 *
 *      [hdr][keystroke...][0x00]       repeated, in any order
 *      [0xFF]                          end of table
 *
 * hdr is the code: three bits of symbol-count-minus-one and five bits of
 * symbols, a dash being a 1 and the first symbol sitting in the highest bit
 * that count uses. A count above five is one no code can have, so 0xFF ends
 * the table and a virgin chip reads as an empty one, with nothing to
 * initialise. Two bytes of overhead per macro, and neither of DigiMacroCDC's
 * two limits -- ten macros, thirty keystrokes each -- survives: what is left
 * is 480 bytes of table to divide up however you like.
 *
 * A keystroke is a HID usage rather than a character, because the app does
 * the layout mapping and a 95-byte ASCII table in flash was a poor way to
 * spend it:
 *
 *      bit 7     needs Shift
 *      bits 0-6  HID usage
 *
 * Escapes keep the prefix-plus-argument shape that worked before. Usages
 * 0x01 to 0x03 are ErrorRollOver, POSTFail and ErrorUndefined -- states a
 * keyboard reports, never keys anyone presses -- so those three values are
 * free to mean something else, and so are the same three with bit 7 set,
 * Shift+ErrorRollOver being no more real. Hence the test: a byte whose low
 * seven bits are below four is an escape.
 *
 *      0x01 nn   pause nn tenths of a second
 *      0x02 mm   latch modifier mask mm into the next keystroke
 *      0x03 nn   do the previous keystroke nn more times
 *      0x82      mark: the rest repeats as a group, not one keystroke
 *      0x81 hh   carry on with the macro whose header is hh-1
 *      0x83 nn   stop unless the host has proved itself lately: quietly
 *                if nn is 1, and if nn is 2 exactly as if the code were
 *                not in the table at all -- see "the host" below
 *      0x80 a c x   where the remote's button a/c starts this macro -- see
 *                "the remote" below. Playing past it does nothing
 *      0x81 FE   the rest of the macro is secret: a nonce, and the rest
 *      0x81 FF   encrypted -- see "secrets" below. FE stops quietly without
 *                the key, FF as if the code were not in the table
 *
 * The jump's argument is the header plus one, because a zero byte ends a
 * macro and the header of "." is zero. No valid header is 0xFC or above, so
 * adding one can never produce a zero either.
 *
 * It is a jump and not a call: nothing after it runs and nothing comes back,
 * so a chain of any length needs no stack, and a macro that jumps to itself
 * runs for ever -- which is the point of having it. A jump to a code that is
 * not in the table simply ends the macro.
 *
 * 0x82 is what makes the repeat useful once a keystroke stops being a
 * character. On a dead-key keyboard "c-cedilla" is two presses, the accent
 * and then the letter, so repeating "the previous keystroke" repeats the
 * letter alone and types c-cedilla-c-c-c. A mark says where the thing being
 * repeated began, and the repeat goes back to it. It takes no argument, since
 * its position IS its argument, and marks do not nest: the last one wins.
 *
 * mm is a HID modifier mask -- 0x01 LeftCtrl, 0x02 LeftShift, 0x04 LeftAlt,
 * 0x08 LeftGUI, and the four right-hand ones above them -- so the app builds
 * a combination directly rather than latching one modifier key at a time the
 * way \kE0 did.
 *
 * 0x03 is run-length coding: "left arrow eleven times" costs three bytes
 * rather than eleven. Repeating whatever went before, rather than naming a
 * usage of its own, is both shorter and more general, since it repeats the
 * latched modifiers with it.
 *
 * No argument may be zero, because a zero byte ends the macro. The app must
 * not emit one and the firmware stops if it finds one, which is what \p00
 * taught us the hard way.
 *
 * 0x04 is not free, though it might look it: it is the usage for "a". The
 * escapes have to live where no key does.
 *
 * 0x80 -- Shift and usage zero, which is no key at all -- is the last value
 * the test lets through, and the remote has it. A remote's button is two
 * bytes, an address and a command, and either may be zero, which a macro
 * cannot hold; so they take three, each with bit 7 set: a and c with it
 * forced on, and x = 0x80 plus the two bits that pushed out, a's in bit 0
 * and c's in bit 1. Walking a macro has to know it takes three, not one.
 */

#define TABLE_BASE      ((uint8_t *)0x000)
#define TABLE_TOP       ((uint8_t *)0x1E0)  /* one past the last table byte:
                                               480, leaving 32 above it */
#define MACRO_MAX_SYM   5

#define HDR_END(h)      (((h) & 0x07) >= MACRO_MAX_SYM)
#define HDR_MAKE(n, c)  ((uint8_t)(((c) << 3) | (((n) - 1) & 0x07)))

#define ESC_PAUSE       0x01
#define ESC_MOD         0x02
#define ESC_REPEAT      0x03
#define ESC_JUMP        0x81
#define ESC_MARK        0x82
#define ESC_HOST        0x83
#define ESC_IR          0x80        /* the last escape there is: see below */
#define HOST_SILENT     1           /* its argument, and what macro_play() */
#define HOST_LOUD       2           /* ...returns when the check refuses */
#define SECRET_QUIET    0xFE        /* the jump's argument for {secret}: a  */
#define SECRET_LOUD     0xFF        /* code no macro has, plus one; {secret!} */


/* Touching the pad stops a macro, so a loop that types or pauses can always
   be got out of. What cannot is a loop that does neither -- macros that only
   jump to each other -- because it never reaches a point where the pad is
   read. Counting the jumps between keystrokes bounds that one, and a hundred
   is more than any chain anyone means. */
#define JUMP_MAX        100
#define IS_ESC(c)       (((c) & 0x7F) < 0x04)

#define KEY_SHIFT       0x80        /* in a stored keystroke */
#define MOD_SHIFT       0x02        /* in a HID report       */

/* The last 32 bytes, kept out of the table's reach. All of them are spoken
 * for; anything more will have to come out of the table.
 *
 *   0x1E0   the board's serial number, four random bytes made once
 *           (DM_SERIAL_ADDR -- the core reads it as the serial number,
 *           and hidBegin() makes it on a blank chip)
 *   0x1E4   the secrets' check value, eight bytes (DM_CHECK_ADDR): v2's
 *           host key was here
 *   0x1EC   lockout: wrong codes before a soft lock, 0 or 0xFF for none
 *   0x1ED   ...soft locks in a row before a hard one, 0 or 0xFF for never
 *   0x1EE   ...how long a soft lock lasts, in tens of seconds
 *   0x1EF   ...wrong codes so far              (these three counters are
 *   0x1F0   ...soft locks in a row so far       stored inverted, so that
 *   0x1F1   ...hard locks lifted, ever          an erased byte is a zero)
 *   0x1F2   how long the secrets' key is held, in tens of seconds
 *   0x1F3   the code to type at power-on, if any
 *   0x1F4   password, twelve bytes zero-padded (DM_PASS_ADDR)
 */

/* ---- the password -------------------------------------------------
 *
 * Optional, and absent by default: an erased chip has none, and a board that
 * types eleven right-arrows at a television should not ask anybody for
 * anything. What it adds, when set, is a second gate on the table -- the
 * window says somebody is at the board, which a web page cannot arrange; the
 * password says they are the right somebody, which a thief holding the board
 * cannot.
 *
 * It does not gate *playing* a macro. A device that wants a password before
 * it will type is a device nobody would carry, and the answer to a stolen
 * board typing for strangers is the lockout, not this.
 *
 * In v2 the core checks it and the app library sets it (hidusb.c); what is
 * left here is what it means for the lockout, below.
 */

/* ==================================================================== */
/* Flags                                                                */
/* ==================================================================== */

/* In GPIOR1, which is a register the compiler reaches with SBI and CBI
   rather than a load-modify-store through a pointer. GPIOR0 is the core's
   flags and GPIOR2 its update window (core/layout.h); GPIOR1 is the app's. */
#define FLAG_CODE_OPEN  1       /* symbols tapped, waiting for the silence */
#define FLAG_CODE_OVER  2       /* more symbols than any code can have     */
#define FLAG_CONSUMED   4       /* this press opened the window, not a symbol */
#define FLAG_AUTODONE   8       /* the power-on macro has had its turn */
#define FLAG_AUTOWAIT  16       /* ...and is waiting for the host to settle */
#define FLAG_HELD      32       /* the hold has already fired for this press */
#define FLAG_IR        64       /* the remote has sent a code (ir_addr/ir_cmd) */
#define FLAG_LEARN    128       /* ...type the next one rather than play it */
#define FLAGS           GPIOR1

/* ==================================================================== */
/* The input                                                            */
/* ==================================================================== */

uint8_t  in_down;               /* the input, once we believe it */
uint8_t  in_seen;               /* ...and what it has been reading lately */
uint16_t in_since;              /* since when, which is how long we wait */
uint16_t press_start;

/* ==================================================================== */
/* Waiting                                                              */
/* ==================================================================== */

/* Busy-wait that keeps USB alive. Nothing here is timing critical enough to
   need a state machine, but dropping usbPoll() for 100 ms loses the host. */
void wait_poll(uint16_t ms)
{
    uint16_t t = ticks();
    while ((uint16_t)(ticks() - t) < ms) hidPoll();
}

void blink(uint8_t n, uint16_t ms)
{
    while (n--) {
        LED_SET(1); wait_poll(ms);
        LED_SET(0); wait_poll(ms);
    }
}

/* Whether there is a password, and whether it has been given this session,
   are the core's to know: it checks the password, because the password
   guards updates even when there is no app (core.h). hidPassSet() and
   hidUnlocked() are its. */

/* ---- the lockout ------------------------------------------------------
 *
 * Two stages, so that the owner can fumble a code without needing the app.
 * Too many wrong codes and the board ignores every tap, right or wrong, for a
 * while: a soft lock. Too many soft locks in a row and it ignores them until
 * the password is given: a hard lock. A macro that plays clears both
 * counts, so somebody guessing gets at most (soft x hard) codes before the
 * board stops listening -- and three, at 1 and 3, is a good deal less than
 * the 62 there are.
 *
 * It needs a password, and is off without one whatever the settings say,
 * because the password is the only thing that lifts a hard lock. An erased
 * chip is therefore an open one, as before.
 *
 * Everything lives in EEPROM so that unplugging the board gets nobody
 * anywhere. Whether it is locked is never stored; it is worked out from the
 * counts each time, so it cannot disagree with them. The board has no clock
 * to say how long it spent unplugged, so a soft lock found at power-up starts
 * its whole wait again.
 *
 * A wrong password counts as a wrong code. The password itself is never
 * refused for being locked, though: it is what lifts a hard lock, and a limit
 * on it is the one lock that can shut the owner out too. Its defence is its
 * own length.
 */
#define LOCK_SOFT       ((uint8_t *)0x1EC)  /* wrong codes before a soft lock */
#define LOCK_HARD       ((uint8_t *)0x1ED)  /* soft locks in a row, then hard */
#define LOCK_WAIT       ((uint8_t *)0x1EE)  /* a soft lock, in tens of seconds */
#define LOCK_MISSES     ((uint8_t *)0x1EF)  /* the counters, stored inverted */
#define LOCK_SOFTS      ((uint8_t *)0x1F0)
#define LOCK_CLEARED    ((uint8_t *)0x1F1)

#define LOCK_WAIT_MAX   240                 /* forty minutes */
#define LOCK_WAIT_DEF   6                   /* a minute, for anything else */
#define LOCK_TICK_MS    10000

#define THR_SET(t)      ((uint8_t)((t) - 1) < 0xFE)     /* 1 to 254 */

static uint8_t count_of(uint8_t *a)           { return ~eeprom_read_byte(a); }
static void    count_put(uint8_t *a, uint8_t v) { eeprom_update_byte(a, ~v); }

static void count_up(uint8_t *a)
{
    uint8_t n = count_of(a);
    if (n != 0xFF) count_put(a, n + 1);
}

/* The soft threshold, or 0 if there is no lockout. */
static uint8_t lock_soft(void)
{
    uint8_t t = eeprom_read_byte(LOCK_SOFT);
    return hidPassSet() && THR_SET(t) ? t : 0;
}

uint8_t hidLockState(void)
{
    uint8_t s = lock_soft(), h;

    if (!s) return 0;
    h = eeprom_read_byte(LOCK_HARD);
    if (THR_SET(h) && count_of(LOCK_SOFTS) >= h) return DM_ST_HARDLOCK;
    if (count_of(LOCK_MISSES) >= s)              return DM_ST_SOFTLOCK;
    return 0;
}

/* A wrong code, or a wrong password.
 *
 * Callers count it BEFORE anything shows the result. Otherwise a guesser
 * watches for the three blinks and pulls the plug before the count is
 * written, and guesses for ever. Nothing a person can see differs between a
 * hit and a miss until this has returned.
 *
 * The soft lock is counted before the miss that causes it, for the same
 * reason: unplugged between the two, the board has counted the lock and not
 * imposed it, which costs the guesser a step towards the hard lock and gains
 * them nothing, since they never saw the result. The other way round would
 * hand them a soft lock that did not count. */
static void lock_miss(void)
{
    uint8_t s = lock_soft(), m;

    if (!s || hidLockState()) return;
    m = count_of(LOCK_MISSES) + 1;
    if (m >= s) count_up(LOCK_SOFTS);
    count_put(LOCK_MISSES, m);
}

static uint8_t  lock_left;      /* tens of seconds of soft lock still to wait */
static uint16_t lock_tick;      /* when the last ten began */

/* The right code, or the right password. eeprom_update_byte() writes only
   what has changed, so the common case -- nothing to clear -- costs no wear. */
static void lock_clear(void)
{
    count_put(LOCK_MISSES, 0);
    count_put(LOCK_SOFTS, 0);
    lock_left = 0;
}

/* Runs the soft lock's clock. RAM only, and that is deliberate: a board
   plugged back in finds itself soft-locked with nothing running, and starts
   the whole wait again. */
static void lock_poll(void)
{
    if (hidLockState() != DM_ST_SOFTLOCK) {
        lock_left = 0;
        return;
    }
    if (!lock_left) {
        uint8_t w = eeprom_read_byte(LOCK_WAIT);
        lock_left = (uint8_t)(w - 1) < LOCK_WAIT_MAX ? w : LOCK_WAIT_DEF;
        lock_tick = ticks();
    }
    /* A while and not an if, so that time spent away from loop() -- the
       power-on macro, say, which a soft lock does not stop -- is caught up
       rather than added on. */
    while ((uint16_t)(ticks() - lock_tick) >= LOCK_TICK_MS) {
        lock_tick += LOCK_TICK_MS;
        if (!--lock_left) {
            count_put(LOCK_MISSES, 0);                  /* served */
            break;
        }
    }
}

#define PASS_PAUSE_MS   1000    /* a wrong key costs a second */

/* What a password means for the lockout. Checking it -- comparing in full,
   pausing a second after a wrong one -- is the core's; these are what the
   app library calls with its verdict (hidusb.h).

   A wrong password counts as a wrong code. */
void hidPassWrong(void)
{
    lock_miss();
}

/* The right one clears the lockout, and a hard lock it clears is counted, for
   the app to show. With no password there is nothing to clear. */
void hidPassRight(void)
{
    if (hidLockState() == DM_ST_HARDLOCK) count_up(LOCK_CLEARED);
    lock_clear();
}

/* A new password starts with a clean slate, rather than with counts left over
   from whenever the lockout last ran. */
void hidPassChanged(void)
{
    lock_clear();
}

/* ---- secrets ----------------------------------------------------------
 *
 * A USB device has no idea which computer it is plugged into, so a board
 * that types a password types it for whoever holds it -- and the password
 * sits in the EEPROM for anybody who can read it, with a programmer, past
 * the lock bits, or with the chip open. {secret} answers both: the rest of
 * the macro is stored encrypted, and the key is nowhere on the board. A
 * small client on the host sends it (KEYAUTH); it is held in RAM for the
 * configured wait (0x1F2, a minute by default) and then forgotten -- and at
 * any restart, and any WRITE. Plugged into any other computer, the secret
 * stays shut:
 *
 *      {secret}    stops the macro quietly
 *      {secret!}   stops it exactly as if the code were not in the table:
 *                  three blinks, and a wrong code for the lockout. A guesser
 *                  on the wrong computer cannot tell it from an empty code
 *                  -- provided nothing is typed before it.
 *
 * As the table holds it:
 *
 *      0x81 hh  n0 n1 n2 n3  c0 ... ck  0x00
 *
 * hh is FE or FF: a jump to a code no macro has, so firmware from before
 * secrets types nothing. The page seals it (web/table.js, which says
 * more): n is a nonce, c the rest of the macro and a zero of its own, at
 * most 256 bytes, each the plain byte XOR the first byte of XTEA_K(n, j) --
 * j the byte's place, the nonce and the counter as two little-endian words.
 * Both are free of anything that would make a walk lose its place, so
 * find_header() reads past a secret as it reads past anything else. Playing
 * one, every byte read goes through secret_byte(), and the player is
 * otherwise the same: repeats and pauses work inside a secret, a {goto}
 * leaves it.
 *
 * Which key is the right one the board learns from the check value, x =
 * XTEA_K(0, 0), where v2's host key was (DM_CHECK_ADDR). A wrong key costs
 * a second and never counts towards the lockout: if it did, any program on
 * any computer could lock the board out without anybody touching it. KEYAUTH
 * needs no gate, being the whole point -- so it does nothing else. PROVE
 * gives a client XTEA under x|x of eight bytes of its choosing, so that it
 * can tell this board from one that merely copied its serial number before
 * it sends the key.
 *
 * v2's {host} is gone: a {secret} at the start of a macro is all it was,
 * and more. One left in a table is refused, quietly or loudly as it asked
 * -- a guarded macro never typed by firmware that cannot check it.
 */
#define CHECK_AT        ((uint8_t *)DM_CHECK_ADDR)
#define KEY_WAIT        ((uint8_t *)0x1F2)

static struct {
    uint8_t key[DM_KEY_LEN];
    uint8_t nonce[4];           /* the playing secret's */
    uint8_t ks[8];              /* nonce and counter, then keystream */
} sec;
static uint8_t  key_left;       /* tens of seconds the key is held for */
static uint16_t key_tick;
static uint16_t key_failed;     /* when the last wrong key came... */
static uint8_t  key_pausing;    /* ...and whether that still holds */
static uint8_t *sec_at;         /* where the playing secret's c0 is, or 0 */

void hidKeyForget(void)
{
    key_left = 0;
    memset(&sec, 0, sizeof sec);
}

/* Runs the key's clock, catching up however long it was left, and lets the
   key go the moment its time is up. */
static uint8_t key_ok(void)
{
    while (key_left && (uint16_t)(ticks() - key_tick) >= LOCK_TICK_MS) {
        key_tick += LOCK_TICK_MS;
        if (!--key_left) hidKeyForget();
    }
    return key_left;
}

uint8_t hidKeyState(void)
{
    return key_ok() ? DM_ST_KEY : 0;
}

/* Its check value compared in full, as the password is. */
uint8_t hidKeyAuth(const uint8_t *p, uint8_t n)
{
    uint8_t b[8], diff = 0, w;

    if (n != DM_KEY_LEN) return 0;
    if (key_pausing && (uint16_t)(ticks() - key_failed) < PASS_PAUSE_MS)
        return 0;
    key_pausing = 0;

    memset(b, 0, sizeof b);
    xtea(b, p);
    for (uint8_t i = 0; i < 8; i++)
        diff |= eeprom_read_byte(CHECK_AT + i) ^ b[i];
    if (diff) {
        key_failed = ticks();
        key_pausing = 1;
        return 0;
    }
    memcpy(sec.key, p, DM_KEY_LEN);
    w = eeprom_read_byte(KEY_WAIT);
    key_left = (uint8_t)(w - 1) < LOCK_WAIT_MAX ? w : LOCK_WAIT_DEF;
    key_tick = ticks();
    return 1;
}

uint8_t hidProve(uint8_t *p, uint8_t n)
{
    uint8_t k[DM_KEY_LEN];

    if (n != 8) return 0;
    for (uint8_t i = 0; i < DM_KEY_LEN; i++)
        k[i] = eeprom_read_byte(CHECK_AT + (i & 7));
    xtea(p, k);
    return 1;
}

/* A byte of the macro being played: as stored, or inside a secret,
   decrypted -- or a zero, which ends the macro, if the key has gone since
   it started. */
static uint8_t secret_byte(uint8_t *a)
{
    uint8_t c = eeprom_read_byte(a);

    if (!sec_at) return c;
    if (!key_left) return 0;    /* clocks_poll() runs its clock between bytes */
    memcpy(sec.ks, sec.nonce, 4);
    sec.ks[4] = a - sec_at;
    sec.ks[5] = sec.ks[6] = sec.ks[7] = 0;
    xtea(sec.ks, sec.key);
    return c ^ sec.ks[0];
}

/* The clocks that must keep running while a macro plays, since loop() does
   not: they count in sixteen bits of milliseconds, which wrap in a minute,
   so anything compared against them has to be looked at more often than
   that. The window most of all: left unasked through a long macro, an
   expired one would read as open again, unlock and all. */
static void clocks_poll(void)
{
    lock_poll();
    hidAllowed();               /* the session ends with the window: asking
                                   is what closes it (hidusb.c) */
    key_ok();                   /* forgets the key the moment its time is up */
    if (key_pausing && (uint16_t)(ticks() - key_failed) >= PASS_PAUSE_MS)
        key_pausing = 0;
}

/* The macro to type when the board is plugged in, as a header byte rather
 * than a position in the table: the same lookup playback already does, where
 * a position would need a second walk that counts. A code that is not in the
 * table matches nothing and types nothing, which is the safe way for it to be
 * wrong -- and the table can be rewritten without this having to follow it.
 *
 * Anything impossible as a header means nothing is set, so an erased byte
 * needs no initialising. It types once, when the host has attached a
 * keyboard to type into -- see AUTO_SETTLE_MS above. A macro that needs the
 * host settled further starts with its own pause.
 */
#define AUTO_ADDR       ((uint8_t *)0x1F3)

/* ==================================================================== */
/* Capacitive touch                                                     */
/* ==================================================================== */

#if HAVE_TOUCH

/* Two ways to read the pad.
 *
 * TOUCH_PULLUP 1 empties the pad by driving it low, then lets the chip's own
 * pull-up fill it and counts until it crosses the input threshold. No
 * external parts at all -- but the pull-up is only 20-50k (about 35k
 * typical), so with ~12 pF of pad the whole rise takes around 400 ns while
 * the counting loop can only sample every ~240 ns. That is two counts,
 * against maybe six with a finger on it: the right answer, at a resolution
 * too coarse to threshold against.
 *
 * So a reading is the SUM of TOUCH_REPS charge cycles rather than one. The
 * sum is not just the same number multiplied: the input threshold has real
 * noise around it, so individual cycles land either side of a quantum and
 * the total settles between them. Sixteen against forty-eight is something
 * a threshold can be put through; two against six is not.
 *
 * TOUCH_PULLUP 0 is the other direction -- charge the pad, let it bleed down
 * through a resistor of your own (~1M) -- which gives a slow, wide signal
 * from a single cycle but needs the part fitted.
 */
#ifndef TOUCH_PULLUP
#define TOUCH_PULLUP    1       /* 0 = bleed resistor instead of the pull-up */
#endif

#define TOUCH_GROUPS    4       /* readings taken; the highest wins */
#if TOUCH_PULLUP
#define TOUCH_REPS      8       /* charge cycles summed into one reading */
#define TOUCH_CAP      31       /* ceiling per cycle */
#else
#define TOUCH_REPS      1
#define TOUCH_CAP     255
#endif
#define TOUCH_SAT       (TOUCH_REPS * TOUCH_CAP)    /* a reading that means
                                                       the pad never moved */

#define TOUCH_STUCK_MS  10000   /* give up on a "touch" held this long */
#define TOUCH_BASE_MS     100   /* how often the average may move: see below */

uint8_t  touch_raw;             /* last reading            */
uint8_t  touch_state;           /* finger present          */
uint16_t touch_base;            /* running average, x16    */
uint16_t touch_since;           /* when touch_state went true */
uint16_t touch_moved;           /* when the average last moved */

/* One reading: TOUCH_REPS charge cycles, summed. A finger adds capacitance,
   so each cycle takes longer and the sum goes up. */
uint8_t touch_once(void)
{
    uint8_t sum = 0;

    for (uint8_t r = TOUCH_REPS; r; r--) {
        uint8_t n = 0;
#if TOUCH_PULLUP
        PORTB &= ~(1 << PIN_TOUCH);     /* clear the bit before driving, or
                                           we would drive high for a moment */
        DDRB  |=  (1 << PIN_TOUCH);     /* output low: empty the pad */
        _delay_us(3);
        DDRB  &= ~(1 << PIN_TOUCH);     /* back to an input... */
        PORTB |=  (1 << PIN_TOUCH);     /* ...and let the pull-up fill it */
        while (!(PINB & (1 << PIN_TOUCH)) && n < TOUCH_CAP) n++;
        PORTB &= ~(1 << PIN_TOUCH);     /* pull-up off between readings, so
                                           nothing sits pushing current
                                           through the pad */
#else
        DDRB  |=  (1 << PIN_TOUCH);     /* drive high: charge the pad */
        PORTB |=  (1 << PIN_TOUCH);
        _delay_us(5);
        PORTB &= ~(1 << PIN_TOUCH);     /* pull-up off BEFORE releasing --
                                           it would hold the pad up */
        DDRB  &= ~(1 << PIN_TOUCH);     /* float: it now bleeds down */
        while ((PINB & (1 << PIN_TOUCH)) && n < TOUCH_CAP) n++;
#endif
        sum += n;
    }
    return sum;
}

/* Interrupts stay on and the *highest* of several readings wins.
 *
 * That is the right way round, and worth being sure of: if the USB interrupt
 * fires mid-count the pad keeps charging while we are not counting, so an
 * interrupted reading comes out too LOW, never too high. The maximum is
 * therefore the sample that ran undisturbed, and nothing except real
 * capacitance can push a reading up. Masking interrupts instead would put
 * the USB connection at risk for no gain at all.
 */
uint8_t touch_read(void)
{
    uint8_t m = 0;

    for (uint8_t i = TOUCH_GROUPS; i; i--) {
        uint8_t v = touch_once();
        if (v > m) m = v;
    }
    return m;
}

void touch_poll(void)
{
    uint8_t  base, thr;
    uint16_t now = ticks();

    touch_raw = touch_read();
    base = touch_base >> 4;

    /* A quarter above the running average is a finger; once it is a finger,
       an eighth above is enough to still be one. Relative rather than an
       absolute count, so it survives a change of bleed resistor or pad
       without needing a new threshold; the +2 keeps a tiny baseline from
       triggering on its own noise.
       
       The gap between the two thresholds is hysteresis. Without it a reading
       sitting on the threshold chatters, and a single chattering sample ends
       a press -- which costs a four-second hold everything and a tap almost
       nothing. */
    thr = touch_state ? base + (base >> 3) + 1
                      : base + (base >> 2) + 2;
    touch_state = touch_raw > thr;

    /* The average only follows the pad while it is untouched, so a finger
       left resting on it never quietly becomes the new normal.
       
       It also follows at a rate fixed in TIME rather than per poll, which is
       the whole difference between a pad that works and one that does not.
       The loop reads the pad thousands of times a second and the average
       closes a sixteenth of the gap each time, so per poll it caught up
       within milliseconds -- and a finger does not arrive in milliseconds.
       Skin flattens onto a pad over a few hundred of them, which is why the
       reading was seen to climb slowly on touch and drop instantly on
       release. A quick tap outran the average and registered; a press made
       deliberately, in order to hold it for four seconds, was tracked all
       the way up and never registered at all.

       So the average moves once every TOUCH_BASE_MS, sixteen moves to close
       a gap: a time constant of about 1.6 seconds. Slower than any finger by
       a wide margin, and still four orders of magnitude faster than the
       thing it exists to track, which is a room warming up. Nothing here
       wants to be near a finger's timescale. */
    if (!touch_state) {
        if ((uint16_t)(now - touch_moved) >= TOUCH_BASE_MS) {
            touch_moved = now;
            touch_base += (int16_t)touch_raw - (int16_t)base;
        }
        touch_since = now;
        return;
    }

    /* But freezing the average has a failure of its own: if the pad drifts
       upward on its own -- damp air, a warmer room -- the reading crosses
       the threshold, the average stops following, and the sensor is stuck
       reporting a finger that is not there, with no way back. So a touch
       that outlasts any real one is taken as drift, and the average is
       moved to meet it. */
    if ((uint16_t)(now - touch_since) > TOUCH_STUCK_MS)
        touch_base = (uint16_t)touch_raw << 4;
}

#endif /* HAVE_TOUCH */

/* ==================================================================== */
/* Typing the readings out                                              */
/* ==================================================================== */

#if TOUCH_DEBUG || HAVE_IR
/* Straight to usages, lower case, rather than through hidTypeChar(): it
   would bring the whole keymap with it for sixteen characters. */
static void type_nib(uint8_t n)
{
    n &= 0x0F;
    hidKey(0, !n ? 0x27 : n < 10 ? 0x1D + n : n - 6);  /* 0, 1-9, a-f */
}

__attribute__((noinline)) static void type_hex(uint8_t v)
{
    type_nib(v >> 4);
    type_nib(v);
}
#endif

#if TOUCH_DEBUG
uint16_t touch_polls;           /* polls since the last reading was typed */

static void type_reading(void)
{
    extern uint8_t in_down;

    type_hex(touch_raw);
    hidTypeChar(' ', 0);
    type_hex((uint8_t)(touch_base >> 4));
    hidTypeChar(' ', 0);
    hidTypeChar(touch_state ? 'T' : '.', 0);
    hidTypeChar(' ', 0);
    hidTypeChar(in_down ? '#' : '-', 0);
    hidTypeChar(' ', 0);
    type_hex(touch_polls >> 8);         /* how many times the pad was read */
    type_hex(touch_polls);              /* since the last line: the rate the
                                           average has to be slower than */
    touch_polls = 0;
    hidTypeChar('\n', 0);
}
#endif

/* ==================================================================== */
/* Tapping                                                              */
/* ==================================================================== */

uint16_t last_release;

uint8_t  code_bits;
uint8_t  code_len;

void code_reset(void)
{
    code_bits = code_len = 0;
    FLAGS &= ~(FLAG_CODE_OPEN | FLAG_CODE_OVER);
}

void handle_input(void)
{
    uint16_t now = ticks();
#if HAVE_TOUCH
    uint8_t  raw = touch_state;         /* updated by touch_poll() in loop() */
#else
    uint8_t  raw = BTN_PRESSED();
#endif

    /* Debouncing that filters rather than locks out. The old rule was "ignore
       changes within DEBOUNCE_MS of the last accepted edge", which is right
       for a switch, whose bouncing happens just after an edge. A pad is not a
       switch: it can drop a sample at any moment, and after a press has been
       open a while the next contrary sample was believed at once. One tap
       became two symbols -- ".." where "." was meant, which types the wrong
       macro rather than none, and costs a four-second hold everything.

       So a new level has to hold for DEBOUNCE_MS before it is accepted. Both
       edges are delayed equally, so a press is timed the same as before. */
    if (raw != in_seen) {
        in_seen = raw;
        in_since = now;
    }

    if (raw != in_down && (uint16_t)(now - in_since) >= DEBOUNCE_MS) {
        in_down = raw;

        if (raw) {
            press_start = now;
            FLAGS &= ~(FLAG_CONSUMED | FLAG_HELD);
        } else if (!(FLAGS & FLAG_CONSUMED)) {
            /* A completed press is one symbol. */
            uint16_t held = now - press_start;
            if (code_len >= MACRO_MAX_SYM) {
                FLAGS |= FLAG_CODE_OVER;
            } else {
                code_bits = (code_bits << 1) | (held >= DOT_MAX_MS ? 1 : 0);
                code_len++;
            }
            last_release = now;
            FLAGS |= FLAG_CODE_OPEN;
        }
        return;
    }

    /* Held long enough to mean "let the app in". Fires while the pad is still
       touched so it is obvious it registered, and marks the press consumed so
       the release does not also become a dash.
       
       It used to change what the device was, because that was the only way a
       page could reach it: a keyboard is not something WebUSB will claim. Now
       that the app talks to a vendor collection on the keyboard itself, the
       hold has nothing to switch -- but it still has something to say, which
       is that somebody is here. Reaching the table needs that, and a web page
       cannot forge it. */
    if (in_down && !(FLAGS & FLAG_HELD) &&
        (uint16_t)(now - press_start) >= HOLD_MS) {
        FLAGS |= FLAG_HELD | FLAG_CONSUMED;
        code_reset();
        hidAllow(CONFIG_MS);
    }
}

/* ==================================================================== */
/* Playback                                                             */
/* ==================================================================== */

/* Where this code's keystrokes start, or 0 if no entry has it. Zero can
   never be a real answer, because every entry has a header byte in front of
   it. Whole header bytes are compared rather than count and symbols
   separately, which is one instruction; and since a query's count is never
   above five, a query can never match the byte that ends the table.
   
   Taking the header rather than the count and symbols is what lets the
   power-on macro be stored as one byte and found with the same walk.

   IR_WANT, which no header is, asks instead for the place just after the
   {IR} of the remote's button in ir_key -- see "the remote" below. That
   needs the walk to go escape by escape, since 0x80 is also a perfectly
   good pause, modifier or count, and only a walk knows which it is
   reading. */
#define IR_WANT 0xFF
#if HAVE_IR
static uint8_t ir_key[3];       /* the button, as its {IR} holds it */
#endif

uint8_t *find_header(uint8_t want)
{
    uint8_t *a = TABLE_BASE;

    while (a < TABLE_TOP) {
        uint8_t h = eeprom_read_byte(a++);
        if (HDR_END(h)) break;
        if (h == want) return a;
        /* Step over the keystrokes and their terminator. Bounded by the
           table, so a table written with no terminator stops here rather
           than walking on into the reserved bytes above it. */
        uint8_t c;
        while (a < TABLE_TOP && (c = eeprom_read_byte(a++))) {
            if (c == ESC_IR) {
                uint8_t i = 0;
#if HAVE_IR
                if (want == IR_WANT)
                    while (i < 3 && eeprom_read_byte(a + i) == ir_key[i]) i++;
#endif
                a += 3;
                if (i == 3) return a;
            } else if (IS_ESC(c) && c != ESC_MARK) {
                a++;            /* its argument, which may be anything */
            }
        }
    }
    return 0;
}

/* ---- stopping a macro ----------------------------------------------
 *
 * Touching the pad stops whatever is playing. That is worth more than it
 * sounds: a macro that jumps to itself runs for ever, which is the point of
 * having jumps, and the only question that ever mattered was how to stop one.
 * Reading the input between keystrokes answers it for nothing -- a touch
 * reading is a quarter of a millisecond against a keystroke's twenty -- and
 * it means no loop has to be written carefully to remain escapable.
 *
 * The press that stops a macro is marked consumed, so it does not also become
 * a symbol and fire the next macro along. Holding it still opens the
 * configuration window, which is how a runaway gets repaired: stop it, keep
 * holding, and the app can reach the table.
 */
static void stop_for_touch(void)
{
    uint16_t now = ticks();

    /* Tell handle_input() this press has been seen and dealt with, since it
       was not looking while the macro played. */
    in_down = in_seen = 1;
    in_since = press_start = now;
    FLAGS |= FLAG_CONSUMED;
    FLAGS &= ~FLAG_HELD;        /* ...but it may still become a hold */
}

/* Wait, while watching the pad. Nothing else here is timing critical enough
   to need a state machine, but dropping hidPoll() for 100 ms loses the host. */
uint8_t pause_poll(uint16_t ms)
{
    uint16_t t = ticks();

    while ((uint16_t)(ticks() - t) < ms) {
        hidPoll();
        clocks_poll();
#if HAVE_TOUCH
        touch_poll();
#endif
        if (INPUT_ACTIVE()) return 1;
    }
    return 0;
}

/* Type one macro, given where its keystrokes start. Returns 0 if it played,
   or how a {host} check refused it.

   The LED comes on with the first thing that shows -- a keystroke or a
   pause -- and not when the macro is found. A {host!} check that refuses
   before anything shows must look exactly like a code that is not there,
   and a flash of the LED on the way would give it away. */
uint8_t macro_play(uint8_t *a)
{
    uint8_t  refused = 0;
#if HAVE_JUMP
    uint8_t  jumps = 0;         /* since the last keystroke; see JUMP_MAX */
#endif
    uint8_t  mod = 0;           /* latched by ESC_MOD for the next keystroke */
    uint8_t  lastkey = 0;       /* what a bare ESC_REPEAT repeats... */
    uint8_t  lastmod = 0;       /* ...modifiers and all */
    uint8_t *mark = 0;          /* ...and where a marked group began */
    uint8_t  reps = 0;          /* times round that group still to go */

    sec_at = 0;

    while (a < TABLE_TOP) {
        uint8_t c;

        /* Between one thing and the next, is anybody touching it? */
        clocks_poll();
#if HAVE_TOUCH
        touch_poll();
#endif
        if (INPUT_ACTIVE()) { stop_for_touch(); break; }

        c = secret_byte(a++);
        if (!c) break;

        if (IS_ESC(c)) {
            if (c == ESC_MARK) {        /* no argument: where it sits is the
                                           argument */
                mark = a;
                continue;
            }
            if (c == ESC_IR) {          /* a place the remote starts from;
                                           played through, it is nothing --
                                           even in a build with no remote.
                                           Its three bytes all have bit 7
                                           set, and one that does not is a
                                           table to stop in, not to skip
                                           through: past its end, perhaps */
                uint8_t i = 3;
                while (i && (secret_byte(a) & 0x80)) { a++; i--; }
                if (i) break;
                continue;
            }
            if (a >= TABLE_TOP) break;  /* an escape in the last table byte
                                           would take its argument from the
                                           serial number above it, and a
                                           repeat count read from there
                                           would say what it was */
            uint8_t arg = secret_byte(a++);
            if (!arg) break;            /* no argument may be zero; a table
                                           that says otherwise is corrupt */
            if (c == ESC_PAUSE) {
                LED_SET(1);
                if (pause_poll((uint16_t)arg * 100)) {
                    stop_for_touch();
                    break;
                }
            } else if (c == ESC_MOD) {
                mod = arg;
            } else if (c == ESC_REPEAT) {
                /* Without a mark this repeats one keystroke, which is the
                   cheap and common case. With one it goes back to the mark,
                   so whatever the group was -- an accent and its letter, a
                   word, a pause -- repeats whole. */
                if (!mark) {
                    /* Up to 255 keystrokes, each a chance to be stopped,
                       like any other. */
                    LED_SET(1);
                    while (arg--) {
                        clocks_poll();
#if HAVE_TOUCH
                        touch_poll();
#endif
                        if (INPUT_ACTIVE()) break;
                        hidKey(lastmod, lastkey);
                    }
                    if (INPUT_ACTIVE()) { stop_for_touch(); break; }
                } else if (!reps) {
                    reps = arg;         /* the group has played once already */
                    a = mark;
                } else if (--reps) {
                    a = mark;
                } else {
                    mark = 0;           /* done; carry on past the repeat */
                }
            } else if (c == ESC_JUMP && arg >= SECRET_QUIET) {
                /* {secret}: refused without the key, quietly or as if the
                   code were not there */
                if (!key_ok()) {
                    refused = arg == SECRET_LOUD ? HOST_LOUD : HOST_SILENT;
                    break;
                }
                for (uint8_t i = 0; i < 4; i++)
                    sec.nonce[i] = eeprom_read_byte(a++);
                sec_at = a;
#if HAVE_JUMP
            } else if (c == ESC_JUMP) {
                uint8_t *nx = find_header(arg - 1);
                if (!nx || ++jumps > JUMP_MAX) break;
                a = nx;                 /* carry on there, and never come back */
                sec_at = 0;             /* ...in the clear, until it says not */
                mod = 0;
                mark = 0;
                reps = 0;
#endif
            } else if (c == ESC_HOST) {
                /* v2's {host}, which no key satisfies now: see "secrets" */
                refused = arg == HOST_LOUD ? HOST_LOUD : HOST_SILENT;
                break;
            } else {
                break;                  /* an escape this build does not have:
                                           stop rather than guess */
            }
            continue;
        }

        lastmod = mod | ((c & KEY_SHIFT) ? MOD_SHIFT : 0);
        lastkey = c & 0x7F;
        mod = 0;
#if HAVE_JUMP
        jumps = 0;              /* a keystroke is a chance to stop it */
#endif
        LED_SET(1);
        hidKey(lastmod, lastkey);
    }

    LED_SET(0);
    return refused;
}

/* Look the finished code up, and play it or say we could not. Locked, a code
   means nothing at all, right or wrong -- telling the two apart would be the
   very answer the lock is withholding. */
void code_finish(void)
{
    uint8_t *a, r;

    if (hidLockState()) {
        code_reset();
        return;
    }
    a = ((FLAGS & FLAG_CODE_OVER) || !code_len)
        ? 0 : find_header(HDR_MAKE(code_len, code_bits));
    code_reset();

    /* A code that is not there, and one whose {host!} check refused, are
       the same thing from outside, so they are the same thing here. One that
       played clears the lockout's counts -- afterwards, not on being found:
       otherwise tapping a guarded code on the wrong computer would wipe a
       guesser's misses for them. A quiet refusal neither clears nor counts. */
    r = a ? macro_play(a) : HOST_LOUD;
    if (r == HOST_LOUD) {
        lock_miss();            /* before the blinks: see lock_miss() */
        blink(3, 70);
    } else if (!r) {
        lock_clear();
    }
}

#if HAVE_AUTOEXEC
uint16_t auto_since;            /* when the host finished enumerating */

/* Type the nominated macro, once the host is listening. */
void autoexec_run(void)
{
    uint8_t  h = eeprom_read_byte(AUTO_ADDR);
    uint8_t *a;

    if (HDR_END(h)) return;             /* 0xFF, or never set */
    if (hidLockState() == DM_ST_HARDLOCK) return;   /* or a thief replugs it */
    a = find_header(h);
    if (a) macro_play(a);
}
#endif

#if HAVE_IR
/* ==================================================================== */
/* The remote                                                           */
/* ==================================================================== */

/* An IR receiver -- a TSOP4838 or any of its kind, whose output idles high
 * and goes low while it sees the carrier -- on P2, which is INT0. A remote
 * speaking NEC, which most cheap ones do, sends a frame per press:
 *
 *      9 ms of carrier, 4.5 ms of none, then 32 bits, each 0.56 ms of
 *      carrier and 0.56 ms (a 0) or 1.69 ms (a 1) of none, low bit first:
 *      address, its complement, command, its complement
 *
 * and, while the button stays down, a repeat every 108 ms that carries no
 * code. Only the falling edges matter: from one to the next is 13.5 ms
 * before the first bit, and 1.125 or 2.25 ms for a bit. The first is taken
 * from 10 ms: an LG TV's remote arrives at 11.1, the receiver's gain control
 * having cut 2 ms off its 9 ms burst (its bursts all come out short, and
 * the gaps after them long by as much). A repeat, 11.25, gets in as well,
 * and no further: no bits follow it. Timer0 runs free at
 * CK/1024, 62 us a tick, and the interrupt reads and clears it, so each
 * interval arrives as one byte; a gap of more than 16 ms wraps it, and the
 * wrap's flag says so.
 *
 * Only the command's complement is checked. The address's is not, since
 * "extended NEC" remotes use that byte as more address; such a remote is
 * known here by its first address byte alone. Repeats are ignored: holding
 * a button plays its macro once.
 *
 * A button found in the table starts typing just after its {IR a c} --
 * wherever in a macro that is -- and on to the end of that macro, exactly
 * as if it had been played from there. A button that is not there does
 * nothing, and counts for nothing: rooms are full of other people's
 * remotes, and a lockout that counted them would be a lockout anyone could
 * set off with a television. Locked, the remote is ignored, as a tapped
 * code is. A {host} check still applies.
 */
#define IR_T(us)    ((uint8_t)((us) * (F_CPU / 1024) / 1000000UL))

static uint8_t  ir_left;        /* bits of this frame still to come; 0 is
                                   between frames, so a stray edge is no bit */
static uint16_t ir_bits;        /* the last sixteen: at half time the
                                   address and its complement, at the end
                                   the command and its */
static volatile uint8_t ir_addr, ir_cmd;    /* the last good one: FLAG_IR */

/* Not blocking, like the clock's: V-USB's interrupt has to start within a
   few cycles, and this one comes first in the chip's order. A frame that
   arrives while USB holds the CPU arrives a few tens of us late, against
   half a millisecond between a 0 and a 1. */
ISR(INT0_vect, ISR_NOBLOCK)
{
    uint8_t t = TCNT0;

    TCNT0 = 0;
    if (TIFR & (1 << TOV0)) {   /* longer than anything inside a frame */
        TIFR = 1 << TOV0;
        t = 0xFF;
    }
    if (t >= IR_T(10000) && t <= IR_T(14500)) {
        ir_left = 32;           /* the start of a frame */
    } else if (ir_left && t >= IR_T(800) && t <= IR_T(2800)) {
        ir_bits >>= 1;
        if (t > IR_T(1690)) ir_bits |= 0x8000;
        if (--ir_left == 16) {
            ir_addr = ir_bits;
        } else if (!ir_left) {
            uint8_t c = ir_bits;
            if (c == (uint8_t)~(ir_bits >> 8)) {
                ir_cmd = c;
                FLAGS |= FLAG_IR;
            }
        }
    } else {
        ir_left = 0;            /* a repeat, a gap, noise: not a frame */
    }
}

static void ir_begin(void)
{
    PORTB |= (1 << PIN_IR);     /* the pull-up: with no receiver there, a
                                   floating pin would be a remote of noise */
    TCCR0A = 0;
    TCCR0B = (1 << CS02) | (1 << CS00);         /* CK/1024 */
    MCUCR |= (1 << ISC01);                      /* INT0 on a falling edge */
    GIMSK |= (1 << INT0);
}

/* LEARN, from the app: the next button is typed as "a c", in hex, for the
   {IR } the app has just put under the cursor. Ungated, so it is good for
   one button and ten seconds: a LEARN nobody followed up is forgotten, and
   does not type hex into whatever has focus hours later. */
#define LEARN_QS        40      /* ten seconds, in quarters */

static uint8_t learn_at;        /* when, in quarter seconds (of 256 ms) */

void hidLearn(void)
{
    learn_at = millis() >> 8;
    FLAGS |= FLAG_LEARN;
}

static void ir_poll(void)
{
    uint8_t a = ir_addr, c = ir_cmd, x = 0x80;

    FLAGS &= ~FLAG_IR;
    if (FLAGS & FLAG_LEARN) {
        FLAGS &= ~FLAG_LEARN;
        if ((uint8_t)((millis() >> 8) - learn_at) < LEARN_QS) {
            type_hex(a);
            hidKey(0, 0x2C);    /* a space */
            type_hex(c);
            return;
        }                       /* too late: an ordinary press */
    }
    if (hidLockState()) return;
    if (a & 0x80) x |= 1;       /* as the table holds it: see the escapes */
    if (c & 0x80) x |= 2;
    ir_key[0] = a | 0x80;
    ir_key[1] = c | 0x80;
    ir_key[2] = x;
    uint8_t *p = find_header(IR_WANT);
    if (p) macro_play(p);
}
#else
void hidLearn(void) { }         /* no remote to learn from */
#endif

/* ==================================================================== */

void update_led(void)
{
    uint8_t lock = hidLockState();

    /* The window comes first: holding the pad on a locked board is how the
       owner gets the app in to lift the lock, and they want to see it took. */
    if (hidAllowed())
        LED_SET((ticks() & 0x3FF) < 512);       /* a steady blink: configurable */
    else if (lock == DM_ST_HARDLOCK)
        LED_SET(ticks() & 0x40);                /* fast, eight times a second */
    else if (lock == DM_ST_SOFTLOCK)
        LED_SET((ticks() & 0x7FF) < 60);        /* a blip every two seconds */
    else
#if LED_ECHO
        LED_SET(in_down);
#else
        LED_SET(0);             /* dark while a code is tapped, so the LED
                                   does not spell it out for a bystander.
                                   What feedback there is says nothing about
                                   the code: solid while a macro types, three
                                   blinks when nothing matched. */
#endif
}

/* Called by the core once, before it connects to USB (core.h). */
void app_setup(void)
{
    FLAGS = 0;                  /* take nothing on trust: whatever ran before
                                   leaves things in these registers */
    DDRB  |=  (1 << PIN_LED);
#if HAVE_TOUCH
    DDRB  &= ~(1 << PIN_TOUCH); /* input, pull-up off: it would swamp the
                                   pad's capacitance */
    PORTB &= ~(1 << PIN_TOUCH);
    touch_base = (uint16_t)touch_read() << 4;   /* seed before the loop, or
                                       every reading looks like a touch */
#else
    DDRB  &= ~(1 << PIN_BUTTON);        /* input... */
    PORTB |=  (1 << PIN_BUTTON);        /* ...with the pull-up on */
#endif
    hidBegin();
#if HAVE_IR
    ir_begin();                 /* after hidBegin(), which borrows Timer0 */
#endif
}

/* ...and this, over and over. */
void app_loop(void)
{
    hidPoll();
#if HAVE_TOUCH
    touch_poll();                       /* refreshes touch_state */
#if TOUCH_DEBUG
    touch_polls++;
#endif
#endif
    handle_input();
    clocks_poll();
    update_led();
#if HAVE_IR
    if (FLAGS & FLAG_IR) ir_poll();
#endif

#if HAVE_AUTOEXEC
    /* The startup macro waits here rather than holding up setup(), and waits
       for somewhere to type rather than merely for enumeration -- see
       AUTO_SETTLE_MS above for why those are not the same thing. */
    if (!(FLAGS & FLAG_AUTODONE) && hidReady()) {
        if (!(FLAGS & FLAG_AUTOWAIT)) {
            FLAGS |= FLAG_AUTOWAIT;
            auto_since = ticks();
        } else if ((uint16_t)(ticks() - auto_since) >=
                   (hidAwake() ? AUTO_SETTLE_MS : AUTO_WAIT_MS)) {
            FLAGS |= FLAG_AUTODONE;
            autoexec_run();
        }
    }
#endif

#if TOUCH_DEBUG
    {
        static uint16_t last;
        if (hidAwake() && (uint16_t)(ticks() - last) >= DEBUG_MS) {
            last = ticks();
            type_reading();
        }
    }
#endif

    /* Silence long enough after the last symbol means the code is done. */
    if ((FLAGS & FLAG_CODE_OPEN) && !in_down &&
        (uint16_t)(ticks() - last_release) >= END_OF_CODE_MS)
        code_finish();
}
