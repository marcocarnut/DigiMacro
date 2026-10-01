/*
 * DigiMacro -- a morse-tapped macro keyboard on an ATtiny85 Digispark.
 *
 * One input, a touch pad on P0. Tap a code in dots and dashes; if the table
 * in EEPROM has an entry for that code, the board types it as keystrokes.
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
 *   4 s hold          change personality, either way
 *
 *   LED  off          idle -- deliberately dark while a code is tapped, so it
 *                     does not spell the code out for anyone watching
 *        on           a macro is typing
 *        3 quick      the code matched nothing
 *        blinking     the app may configure it, for half a minute
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

#include <hidusb.h>
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

#ifndef HAVE_JUMP
#define HAVE_JUMP 1         /* 0 = no {goto}, and no loops to escape from */
#endif

#ifndef HAVE_AUTOEXEC
#define HAVE_AUTOEXEC 1     /* 0 = never type anything unasked */
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
 *      0x03 nn   do the previous keystroke nn more times
 *      0x82      mark: the rest repeats as a group, not one keystroke
 *      0x81 hh   carry on with the macro whose header is hh-1
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
 * NOTE for the app: this is not quite what PLAN.md first sketched. Repeat was
 * to be 0x04, which collides with the usage for "a" -- the escape values had
 * to move once the table started holding usages instead of characters.
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

/* Touching the pad stops a macro, so a loop that types or pauses can always
   be got out of. What cannot is a loop that does neither -- macros that only
   jump to each other -- because it never reaches a point where the pad is
   read. Counting the jumps between keystrokes bounds that one, and a hundred
   is more than any chain anyone means. */
#define JUMP_MAX        100
#define IS_ESC(c)       (((c) & 0x7F) < 0x04)

#define KEY_SHIFT       0x80        /* in a stored keystroke */
#define MOD_SHIFT       0x02        /* in a HID report       */

/* The last 32 bytes, reserved here so that a table can never grow into them.
 * All of it belongs to steps still to come, and the gap is deliberate: it is
 * cheaper to leave room now than to move a board's table later.
 *
 *   0x1E0   free, for whatever this turns out to want
 *   0x1F1   lockout allowance
 *   0x1F2   tries left of it
 *   0x1F3   the code to type at power-on, if any
 *   0x1F4   password, twelve bytes zero-padded
 */

/* The macro to type when the board is plugged in, as a header byte rather
 * than a position in the table: the same lookup playback already does, where
 * a position would need a second walk that counts. A code that is not in the
 * table matches nothing and types nothing, which is the safe way for it to be
 * wrong -- and the table can be rewritten without this having to follow it.
 *
 * Anything impossible as a header means nothing is set, so an erased byte
 * needs no initialising. It types once, when the host has finished
 * enumerating; keystrokes sent before that go nowhere. A macro that needs the
 * host settled further starts with its own pause.
 */
#define AUTO_ADDR       ((uint8_t *)0x1F3)

/* ==================================================================== */
/* Flags                                                                */
/* ==================================================================== */

/* In GPIOR1, which is a register the compiler reaches with SBI and CBI
   rather than a load-modify-store through a pointer. Not GPIOR0: micronucleus
   leaves the reset reason there, and a flag that comes up set by itself after
   a power cycle is a bug that takes a long evening to find. */
#define FLAG_CODE_OPEN  1       /* symbols tapped, waiting for the silence */
#define FLAG_CODE_OVER  2       /* more symbols than any code can have     */
#define FLAG_CONSUMED   4       /* this press opened the window, not a symbol */
#define FLAG_AUTODONE   8       /* the power-on macro has had its turn */
#define FLAG_AUTOWAIT  16       /* ...and is waiting for the host to settle */
#define FLAG_HELD      32       /* the hold has already fired for this press */
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
   power-on macro be stored as one byte and found with the same walk. */
uint8_t *find_header(uint8_t want)
{
    uint8_t *a = TABLE_BASE;

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
#if HAVE_TOUCH
        touch_poll();
#endif
        if (INPUT_ACTIVE()) return 1;
    }
    return 0;
}

/* Type one macro, given where its keystrokes start. */
void macro_play(uint8_t *a)
{
#if HAVE_JUMP
    uint8_t  jumps = 0;         /* since the last pause; see JUMP_MAX */
#endif
    uint8_t  mod = 0;           /* latched by ESC_MOD for the next keystroke */
    uint8_t  lastkey = 0;       /* what a bare ESC_REPEAT repeats... */
    uint8_t  lastmod = 0;       /* ...modifiers and all */
    uint8_t *mark = 0;          /* ...and where a marked group began */
    uint8_t  reps = 0;          /* times round that group still to go */

    LED_SET(1);

    while (a < TABLE_TOP) {
        uint8_t c;

        /* Between one thing and the next, is anybody touching it? */
#if HAVE_TOUCH
        touch_poll();
#endif
        if (INPUT_ACTIVE()) { stop_for_touch(); break; }

        c = eeprom_read_byte(a++);
        if (!c) break;

        if (IS_ESC(c)) {
            if (c == ESC_MARK) {        /* no argument: where it sits is the
                                           argument */
                mark = a;
                continue;
            }
            if (a >= TABLE_TOP) break;  /* an escape in the last table byte
                                           would take its argument from the
                                           lockout state, and a repeat count
                                           read from there would say what it
                                           was */
            uint8_t arg = eeprom_read_byte(a++);
            if (!arg) break;            /* no argument may be zero; a table
                                           that says otherwise is corrupt */
            if (c == ESC_PAUSE) {
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
                    while (arg--) hidKey(lastmod, lastkey);
                } else if (!reps) {
                    reps = arg;         /* the group has played once already */
                    a = mark;
                } else if (--reps) {
                    a = mark;
                } else {
                    mark = 0;           /* done; carry on past the repeat */
                }
#if HAVE_JUMP
            } else if (c == ESC_JUMP) {
                uint8_t *nx = find_header(arg - 1);
                if (!nx || ++jumps > JUMP_MAX) break;
                a = nx;                 /* carry on there, and never come back */
                mod = 0;
                mark = 0;
                reps = 0;
#endif
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
        hidKey(lastmod, lastkey);
    }

    LED_SET(0);
}

/* Look the finished code up, and play it or say we could not. */
void code_finish(void)
{
    uint8_t *a = ((FLAGS & FLAG_CODE_OVER) || !code_len)
                 ? 0 : find_header(HDR_MAKE(code_len, code_bits));

    code_reset();
    if (a) macro_play(a);
    else   blink(3, 70);
}

#if HAVE_AUTOEXEC
uint16_t auto_since;            /* when the host finished enumerating */

/* Type the nominated macro, once the host is listening. */
void autoexec_run(void)
{
    uint8_t  h = eeprom_read_byte(AUTO_ADDR);
    uint8_t *a;

    if (HDR_END(h)) return;             /* 0xFF, or never set */
    a = find_header(h);
    if (a) macro_play(a);
}
#endif

/* ==================================================================== */

void update_led(void)
{
    if (hidAllowed())
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
    hidBegin();
}

void loop(void)
{
    hidPoll();
#if HAVE_TOUCH
    touch_poll();                       /* refreshes touch_state */
#if TOUCH_DEBUG
    touch_polls++;
#endif
#endif
    handle_input();
    update_led();

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
