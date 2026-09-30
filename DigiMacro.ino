/*
 * DigiMacro -- a morse-tapped macro keyboard on an ATtiny85 Digispark.
 *
 * One input, a touch pad on P0. Tap a code in dots and dashes; if the table
 * in EEPROM has an entry for that code, the board types it as keystrokes.
 * Hold the pad four seconds and the device re-enumerates as a vendor-class
 * device that a WebUSB page can talk to, which is where the table gets
 * edited; hold it again for the keyboard.
 *
 * Sibling of DigiMacroCDC, which does the same job with an on-device command
 * line over a CDC serial port. Everything that edited the table there has
 * moved to the app, so the firmware here only ever *reads* it. That is what
 * pays for the WebUSB descriptors, and it makes the table format the
 * interface between the two halves of the project -- see below, and keep the
 * app in step with it.
 *
 *   short tap         one symbol; a press under 250 ms is a dot, longer a dash
 *   700 ms of silence the code is finished: look it up and type it
 *   4 s hold          change personality, either way
 *
 *   LED  off          keyboard mode, idle -- deliberately dark while a code
 *                     is tapped, so it does not spell the code out for anyone
 *                     watching
 *        on           a macro is typing
 *        3 quick      the code matched nothing
 *        blinking     vendor mode, ready for the app
 *
 * The pad wants no external parts. When it misbehaves, build with
 * TOUCH_DEBUG=1 (./build.sh touchdebug) and the board types its own readings
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

#define VERSION "0.1"

#include <modeusb.h>
#include <avr/eeprom.h>
#include <avr/pgmspace.h>
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

#ifndef TOUCH_DEBUG
#define TOUCH_DEBUG 0       /* 1 = type the pad readings twice a second, into
                               whatever window has focus. See below. */
#endif
#define DEBUG_MS    500

/* ---- pins and timing ------------------------------------------------ */

#define PIN_BUTTON              0
#define PIN_TOUCH   PIN_BUTTON      /* the same pin; a pad rather than a switch */
#define PIN_LED                 1   /* clones with the LED on P0 must move it */

#define LED_SET(v)   do { if (v) PORTB |=  (1 << PIN_LED); \
                          else   PORTB &= ~(1 << PIN_LED); } while (0)
#define BTN_PRESSED()  (!(PINB & (1 << PIN_BUTTON)))

static inline uint16_t ticks(void) { return (uint16_t)millis(); }

#define DEBOUNCE_MS             25
#define DOT_MAX_MS             250  /* shorter press is a dot, longer a dash */
#define END_OF_CODE_MS         700  /* silence this long ends the code       */
#define HOLD_MS               4000  /* hold this long to change personality  */

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
 * is 497 bytes of table to divide up however you like.
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
 *      0x03 nn   press the previous keystroke nn more times
 *      0x81 hh   carry on with the macro whose header is hh    (not yet)
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
 * NOTE for the app: this is not quite what PLAN.md first sketched. Repeat was
 * to be 0x04, which collides with the usage for "a" -- the escape values had
 * to move once the table started holding usages instead of characters.
 */

#define TABLE_BASE      ((uint8_t *)0x000)
#define TABLE_TOP       ((uint8_t *)0x1F1)  /* one past the last table byte;
                                               the config lives above it */
#define MACRO_MAX_SYM   5

#define HDR_END(h)      (((h) & 0x07) >= MACRO_MAX_SYM)
#define HDR_MAKE(n, c)  ((uint8_t)(((c) << 3) | (((n) - 1) & 0x07)))

#define ESC_PAUSE       0x01
#define ESC_MOD         0x02
#define ESC_REPEAT      0x03
#define ESC_JUMP        0x81
#define IS_ESC(c)       (((c) & 0x7F) < 0x04)

#define KEY_SHIFT       0x80        /* in a stored keystroke */
#define MOD_SHIFT       0x02        /* in a HID report       */

/* The rest of the EEPROM, reserved here so that a table can never grow into
 * it. All of it belongs to steps still to come:
 *
 *   0x1F1   lockout allowance
 *   0x1F2   tries left of it
 *   0x1F3   the code to type at power-on, if any
 *   0x1F4   password, twelve bytes zero-padded
 */

/* ==================================================================== */
/* Flags                                                                */
/* ==================================================================== */

/* In GPIOR1, which is a register the compiler reaches with SBI and CBI
   rather than a load-modify-store through a pointer. Not GPIOR0: micronucleus
   leaves the reset reason there, and a flag that comes up set by itself after
   a power cycle is a bug that takes a long evening to find. */
#define FLAG_CODE_OPEN  1       /* symbols tapped, waiting for the silence */
#define FLAG_CODE_OVER  2       /* more symbols than any code can have     */
#define FLAG_CONSUMED   4       /* this press was a mode switch, not a symbol */
#define FLAGS           GPIOR1

/* ==================================================================== */
/* Waiting                                                              */
/* ==================================================================== */

/* Busy-wait that keeps USB alive. Nothing here is timing critical enough to
   need a state machine, but dropping usbPoll() for 100 ms loses the host. */
void wait_poll(uint16_t ms)
{
    uint16_t t = ticks();
    while ((uint16_t)(ticks() - t) < ms) dusbPoll();
}

void blink(uint8_t n, uint16_t ms)
{
    while (n--) {
        LED_SET(1); wait_poll(ms);
        LED_SET(0); wait_poll(ms);
    }
}

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

#if TOUCH_DEBUG
static void type_nib(uint8_t n)
{
    n &= 0x0F;
    dusbTypeChar(n < 10 ? '0' + n : 'A' + n - 10, 0);
}

static void type_hex(uint8_t v)
{
    type_nib(v >> 4);
    type_nib(v);
}

uint16_t touch_polls;           /* polls since the last reading was typed */

static void type_reading(void)
{
    extern uint8_t in_down;

    type_hex(touch_raw);
    dusbTypeChar(' ', 0);
    type_hex((uint8_t)(touch_base >> 4));
    dusbTypeChar(' ', 0);
    dusbTypeChar(touch_state ? 'T' : '.', 0);
    dusbTypeChar(' ', 0);
    dusbTypeChar(in_down ? '#' : '-', 0);
    dusbTypeChar(' ', 0);
    type_hex(touch_polls >> 8);         /* how many times the pad was read */
    type_hex(touch_polls);              /* since the last line: the rate the
                                           average has to be slower than */
    touch_polls = 0;
    dusbTypeChar('\n', 0);
}
#endif

/* ==================================================================== */
/* Tapping                                                              */
/* ==================================================================== */

uint8_t  in_down;
uint16_t in_changed;            /* last accepted edge, for debouncing */
uint16_t press_start;
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

    if (raw != in_down) {
        if ((uint16_t)(now - in_changed) < DEBOUNCE_MS) return;
        in_down = raw;
        in_changed = now;

        if (raw) {
            press_start = now;
            FLAGS &= ~FLAG_CONSUMED;
        } else if (!(FLAGS & FLAG_CONSUMED) && dusbMode() == DUSB_HID) {
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
       the release does not also become a dash. */
    if (in_down && !(FLAGS & FLAG_CONSUMED) &&
        (uint16_t)(now - press_start) >= HOLD_MS) {
        FLAGS |= FLAG_CONSUMED;
        code_reset();
        LED_SET(0);
        dusbSetMode(dusbMode() == DUSB_HID ? DUSB_VENDOR : DUSB_HID);
    }
}

/* ==================================================================== */
/* Playback                                                             */
/* ==================================================================== */

/* Where this code's keystrokes start, or 0 if no entry has it. Zero can
   never be a real answer, because every entry has a header byte in front of
   it. Whole header bytes are compared rather than count and symbols
   separately, which is one instruction; and since a query's count is never
   above five, a query can never match the byte that ends the table. */
uint8_t *find_code(uint8_t nsym, uint8_t bits)
{
    uint8_t *a = TABLE_BASE;
    uint8_t  want = HDR_MAKE(nsym, bits);

    while (a < TABLE_TOP) {
        uint8_t h = eeprom_read_byte(a++);
        if (HDR_END(h)) break;
        if (h == want) return a;
        /* Step over the keystrokes and their terminator. Bounded by the
           table, so a table written with no terminator stops here rather
           than walking on into the password. */
        while (a < TABLE_TOP && eeprom_read_byte(a)) a++;
        a++;
    }
    return 0;
}

/* Type one macro, given where its keystrokes start. */
void macro_play(uint8_t *a)
{
    uint8_t mod = 0;            /* latched by ESC_MOD for the next keystroke */
    uint8_t lastkey = 0;        /* what ESC_REPEAT repeats... */
    uint8_t lastmod = 0;        /* ...modifiers and all */

    LED_SET(1);

    while (a < TABLE_TOP) {
        uint8_t c = eeprom_read_byte(a++);
        if (!c) break;

        if (IS_ESC(c)) {
            if (a >= TABLE_TOP) break;  /* an escape in the last table byte
                                           would take its argument from the
                                           lockout state, and a repeat count
                                           read from there would say what it
                                           was */
            uint8_t arg = eeprom_read_byte(a++);
            if (!arg) break;            /* no argument may be zero; a table
                                           that says otherwise is corrupt */
            if (c == ESC_PAUSE) {
                wait_poll((uint16_t)arg * 100);
            } else if (c == ESC_MOD) {
                mod = arg;
            } else if (c == ESC_REPEAT) {
                while (arg--) dusbKey(lastmod, lastkey);
            } else {
                break;                  /* ESC_JUMP, which this build does
                                           not have: stop rather than guess */
            }
            continue;
        }

        lastmod = mod | ((c & KEY_SHIFT) ? MOD_SHIFT : 0);
        lastkey = c & 0x7F;
        mod = 0;
        dusbKey(lastmod, lastkey);
    }

    LED_SET(0);
}

/* Look the finished code up, and play it or say we could not. */
void code_finish(void)
{
    uint8_t *a = ((FLAGS & FLAG_CODE_OVER) || !code_len)
                 ? 0 : find_code(code_len, code_bits);

    code_reset();
    if (a) macro_play(a);
    else   blink(3, 70);
}

/* ==================================================================== */

void update_led(void)
{
    if (dusbMode() != DUSB_HID)
        LED_SET((ticks() & 0x3FF) < 512);       /* blinking: configurable */
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

void setup(void)
{
    FLAGS = 0;                  /* take nothing on trust: the bootloader
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
    dusbBegin(DUSB_HID);
}

void loop(void)
{
    dusbPoll();
#if HAVE_TOUCH
    touch_poll();                       /* refreshes touch_state */
#if TOUCH_DEBUG
    touch_polls++;
#endif
#endif
    handle_input();
    update_led();

    if (dusbMode() != DUSB_HID) return; /* the app's turn; nothing to type */

#if TOUCH_DEBUG
    {
        static uint16_t last;
        if (dusbReady() && (uint16_t)(ticks() - last) >= DEBUG_MS) {
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
