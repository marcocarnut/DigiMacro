/*
 * spikehid -- can a keyboard that never stops being a keyboard carry the
 * EEPROM protocol?
 *
 * The question the first spike did not ask. That one proved a keyboard can
 * become a WebUSB device; this one asks whether it needs to. A second
 * top-level collection on a vendor usage page is something WebHID will hand
 * to a page, and a feature report is a control transfer, so the EEPROM
 * protocol fits in it unchanged.
 *
 * What has to be proven, in this order:
 *
 *   1. the keyboard still types, now that its reports carry a report ID and
 *      three key slots rather than six
 *   2. Chrome offers the device at all, and gives up the vendor collection
 *      while refusing the keyboard one
 *   3. a feature report reaches the firmware, and comes back
 *   4. whether the report ID is in the payload, in wValue, or in both -- the
 *      one thing the specification leaves to taste
 *   5. and all of it on Windows as well, where HIDClass binds without being
 *      asked and no MS OS 2.0 descriptor is needed
 *
 *   short touch   types a line, which answers 1
 *   LED           on while touched
 *
 * Wiring: a touch pad on P0, the onboard LED on P1.
 */

#include <hidusb.h>
#include <avr/eeprom.h>
#include <util/delay.h>

#define PIN_TOUCH   0
#define PIN_LED     1

#define LED_SET(v)  do { if (v) PORTB |=  (1 << PIN_LED); \
                         else   PORTB &= ~(1 << PIN_LED); } while (0)

static inline uint16_t ticks(void) { return (uint16_t)millis(); }

static const char BANNER[] PROGMEM = "DigiMacro: still a keyboard\n";

#define DEBOUNCE_MS     25
#define TOUCH_GROUPS     4
#define TOUCH_REPS       8
#define TOUCH_CAP       31
#define TOUCH_BASE_MS  100

uint8_t  touch_raw, touch_state;
uint16_t touch_base, touch_moved;

/* The tested reading from DigiMacro.ino, with its time-based average: the
   pad is emptied, the internal pull-up fills it, and the count is summed
   over several cycles because one is only worth about two. */
static uint8_t touch_once(void)
{
    uint8_t sum = 0;
    for (uint8_t r = TOUCH_REPS; r; r--) {
        uint8_t n = 0;
        PORTB &= ~(1 << PIN_TOUCH);
        DDRB  |=  (1 << PIN_TOUCH);
        _delay_us(3);
        DDRB  &= ~(1 << PIN_TOUCH);
        PORTB |=  (1 << PIN_TOUCH);
        while (!(PINB & (1 << PIN_TOUCH)) && n < TOUCH_CAP) n++;
        PORTB &= ~(1 << PIN_TOUCH);
        sum += n;
    }
    return sum;
}

static uint8_t touch_read(void)
{
    uint8_t m = 0;
    for (uint8_t i = TOUCH_GROUPS; i; i--) {
        uint8_t v = touch_once();
        if (v > m) m = v;
    }
    return m;
}

static void touch_poll(void)
{
    uint16_t now = ticks();
    uint8_t  base, thr;

    touch_raw = touch_read();
    base = touch_base >> 4;
    thr = touch_state ? base + (base >> 3) + 1 : base + (base >> 2) + 2;
    touch_state = touch_raw > thr;

    if (!touch_state && (uint16_t)(now - touch_moved) >= TOUCH_BASE_MS) {
        touch_moved = now;
        touch_base += (int16_t)touch_raw - (int16_t)base;
    }
}

static uint8_t  in_down, in_seen;
static uint16_t in_since;

void setup(void)
{
    DDRB  |=  (1 << PIN_LED);
    DDRB  &= ~(1 << PIN_TOUCH);
    PORTB &= ~(1 << PIN_TOUCH);
    touch_base = (uint16_t)touch_read() << 4;
    hidBegin();
}

void loop(void)
{
    hidPoll();
    touch_poll();

    uint16_t now = ticks();
    if (touch_state != in_seen) { in_seen = touch_state; in_since = now; }
    if (touch_state != in_down && (uint16_t)(now - in_since) >= DEBOUNCE_MS) {
        in_down = touch_state;
        if (!in_down && hidReady()) hidType_P(BANNER);
    }
    LED_SET(in_down);
}
