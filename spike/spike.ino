/*
 * spike -- does the mode switch reach WebUSB, and is the pad being read?
 *
 * Two questions at once, because the first cannot be answered until the
 * second is: the input has to work before a four-second hold on it can mean
 * anything.
 *
 *   short touch   types a line, proving the keyboard half
 *   4 s hold      switches personality, either way
 *
 *   LED  off        keyboard mode, idle
 *        on         keyboard mode, pad touched
 *        blinking   vendor mode, ready for WebUSB
 *
 * With TOUCH_DEBUG the board types its own pad readings instead of the
 * banner, twice a second, into whatever window has focus:
 *
 *      1A 18 .       raw, running average, and . or T
 *      30 18 T
 *
 * which is the same numbers DigiMacroCDC's `touch` command printed, by the
 * only channel a device with no serial port has. Read them like this:
 *
 *   raw sits at F8    the pad never crosses: nothing is charging it. Check the
 *                     pad is on P0, and raise TOUCH_REPS
 *   raw sits at 00    it crosses within one loop pass: lower TOUCH_REPS, or
 *                     fit the bleed resistor and set TOUCH_PULLUP 0
 *   raw == base, flat a pad that is connected but not moving
 *   raw rises on touch working; a quarter above base trips it
 *
 * Wiring: a touch pad on P0. P1 is the onboard LED on most boards.
 */

#include <modeusb.h>
#include <util/delay.h>

#define PIN_BUTTON  0
#define PIN_TOUCH   PIN_BUTTON      /* the same pin; a pad rather than a switch */
#define PIN_LED     1

#define TOUCH_DEBUG 1           /* 0 types the banner instead */
#define DEBUG_MS    500         /* a line takes ~160 ms to type */

#define HOLD_MS     4000
#define DEBOUNCE_MS   25

#define LED_SET(v)  do { if (v) PORTB |=  (1 << PIN_LED); \
                         else   PORTB &= ~(1 << PIN_LED); } while (0)

static inline uint16_t ticks(void) { return (uint16_t)millis(); }

static const char BANNER[] PROGMEM = "DigiMacro spike: keyboard mode\n";

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

uint8_t  touch_raw;             /* last reading            */
uint8_t  touch_state;           /* finger present          */
uint16_t touch_base;            /* running average, x16    */
uint16_t touch_since;           /* when touch_state went true */

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
 * fires mid-count the pad keeps discharging while we are not counting, so an
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
    uint8_t base;

    touch_raw = touch_read();
    base = touch_base >> 4;

    /* A quarter above the running average is a finger. Relative rather than
       an absolute count, so it survives a change of bleed resistor or pad
       without needing a new threshold; the +2 keeps a tiny baseline from
       triggering on its own noise. */
    touch_state = touch_raw > base + (base >> 2) + 2;

    /* The average only follows the pad while it is untouched, so a finger
       left resting on it never quietly becomes the new normal. */
    if (!touch_state) {
        touch_base += (int16_t)touch_raw - (int16_t)base;
        touch_since = ticks();
        return;
    }

    /* But freezing the average has a failure of its own: if the pad drifts
       upward on its own -- damp air, a warmer room -- the reading crosses
       the threshold, the average stops following, and the sensor is stuck
       reporting a finger that is not there, with no way back. So a touch
       that outlasts any real one is taken as drift, and the average is
       moved to meet it. */
    if ((uint16_t)(ticks() - touch_since) > TOUCH_STUCK_MS)
        touch_base = (uint16_t)touch_raw << 4;
}

/* ==================================================================== */
/* Typing the readings out                                              */
/* ==================================================================== */

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

static void type_reading(void)
{
    type_hex(touch_raw);
    dusbTypeChar(' ', 0);
    type_hex((uint8_t)(touch_base >> 4));
    dusbTypeChar(' ', 0);
    dusbTypeChar(touch_state ? 'T' : '.', 0);
    dusbTypeChar('\n', 0);
}

/* ==================================================================== */
/* Input                                                                */
/* ==================================================================== */

static uint8_t  in_down, in_consumed;
static uint16_t in_changed, press_start;

static void handle_input(void)
{
    uint16_t now = ticks();
    uint8_t  raw = touch_state;         /* refreshed by touch_poll() */

    if (raw != in_down) {
        if ((uint16_t)(now - in_changed) < DEBOUNCE_MS) return;
        in_down = raw;
        in_changed = now;
        if (raw) {
            press_start = now;
            in_consumed = 0;
        }
#if !TOUCH_DEBUG
        else if (!in_consumed && dusbMode() == DUSB_HID && dusbReady()) {
            dusbType_P(BANNER);         /* a completed short touch */
        }
#endif
        return;
    }

    /* Held long enough to mean "change personality". Fires while the pad is
       still touched so it is obvious it registered, and marks the press
       consumed so the release does not also type. */
    if (in_down && !in_consumed && (uint16_t)(now - press_start) >= HOLD_MS) {
        in_consumed = 1;
        LED_SET(0);
        dusbSetMode(dusbMode() == DUSB_HID ? DUSB_VENDOR : DUSB_HID);
    }
}

/* ==================================================================== */

void setup(void)
{
    DDRB  &= ~(1 << PIN_TOUCH);         /* input, pull-up off: it would swamp
                                           the pad's capacitance */
    PORTB &= ~(1 << PIN_TOUCH);
    DDRB  |=  (1 << PIN_LED);

    touch_base = (uint16_t)touch_read() << 4;   /* seed before the loop, or
                                       every reading looks like a touch */
    dusbBegin(DUSB_HID);
}

void loop(void)
{
    static uint16_t last;

    dusbPoll();
    touch_poll();
    handle_input();

    if (dusbMode() == DUSB_VENDOR) {
        LED_SET((ticks() & 0x3FF) < 512);       /* blink: configurable */
        return;
    }
    LED_SET(in_down);

#if TOUCH_DEBUG
    if (dusbReady() && (uint16_t)(ticks() - last) >= DEBUG_MS) {
        last = ticks();
        type_reading();
    }
#endif
}
