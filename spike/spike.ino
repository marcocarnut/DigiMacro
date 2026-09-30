/*
 * spike -- does the mode switch reach WebUSB?
 *
 * The one thing DigiMacro rests on and nothing else can tell us: that a board
 * can be an HID keyboard, become a vendor-class device a WebUSB page can talk
 * to, and become a keyboard again. No macros, no morse, no EEPROM -- just the
 * two personalities and one vendor request, so that a failure here is a
 * failure of the idea rather than of something built on top of it.
 *
 *   short press   types a line, proving the keyboard half
 *   4 s hold      switches personality, either way
 *
 *   LED  off        keyboard mode, idle
 *        on         keyboard mode, input held
 *        blinking   vendor mode
 *
 * Wiring: a button from P0 to GND. P1 is the onboard LED on most boards.
 */

#include <modeusb.h>

#define PIN_BUTTON  0
#define PIN_LED     1

#define HOLD_MS     4000
#define DEBOUNCE_MS   25

#define LED_SET(v)  do { if (v) PORTB |=  (1 << PIN_LED); \
                         else   PORTB &= ~(1 << PIN_LED); } while (0)
#define BTN_PRESSED() (!(PINB & (1 << PIN_BUTTON)))

static inline uint16_t ticks(void) { return (uint16_t)millis(); }

static const char BANNER[] PROGMEM = "DigiMacro spike: keyboard mode\n";

static uint8_t  in_down, in_consumed;
static uint16_t in_changed, press_start;

static void handle_input(void)
{
    uint16_t now = ticks();
    uint8_t  raw = BTN_PRESSED();

    if (raw != in_down) {
        if ((uint16_t)(now - in_changed) < DEBOUNCE_MS) return;
        in_down = raw;
        in_changed = now;
        if (raw) {
            press_start = now;
            in_consumed = 0;
        } else if (!in_consumed && dusbMode() == DUSB_HID && dusbReady()) {
            dusbType_P(BANNER);         /* a completed short press */
        }
        return;
    }

    /* Held long enough to mean "change personality". Fires while the input is
       still down so it is obvious it registered, and marks the press consumed
       so the release does not also type. */
    if (in_down && !in_consumed && (uint16_t)(now - press_start) >= HOLD_MS) {
        in_consumed = 1;
        LED_SET(0);
        dusbSetMode(dusbMode() == DUSB_HID ? DUSB_VENDOR : DUSB_HID);
    }
}

void setup(void)
{
    DDRB  &= ~(1 << PIN_BUTTON);        /* input with the pull-up on */
    PORTB |=  (1 << PIN_BUTTON);
    DDRB  |=  (1 << PIN_LED);
    dusbBegin(DUSB_HID);
}

void loop(void)
{
    dusbPoll();
    handle_input();

    if (dusbMode() == DUSB_VENDOR)
        LED_SET((ticks() & 0x3FF) < 512);   /* blink: configurable */
    else
        LED_SET(in_down);
}
