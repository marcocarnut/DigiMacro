/*
 * The mouse, on the host: app/hidusb.c's own hidPoint() and what it sends.
 *
 * run.sh cuts the code out of app/hidusb.c -- from `static uint8_t ptr[4]`
 * to hidPoint()'s closing brace -- into the file POINTER_SRC names, so this
 * runs the code the board runs, not a copy. sendRaw() is ours: it adds up
 * where the reports would have put the pointer.
 */
#include <stdio.h>
#include <stdint.h>
#define HID_REPORT_PTR  3
#define PTR_BUTTONS     0x20
#define PTR_CLICK       0x10

static int n, sx, sy, held, bad;

static void sendRaw(const uint8_t *r)
{
    if (r[0] != HID_REPORT_PTR) bad++;
    n++;
    sx += (int8_t)r[2];
    sy += (int8_t)r[3];
    held = r[1];
}

#include POINTER_SRC

static int fails;
#define CHECK(c, ...) do { if (!(c)) { fails++; printf("FAIL: " __VA_ARGS__); printf("\n"); } } while (0)

static void reset(void) { n = sx = sy = 0; }
static void move(uint8_t x, uint8_t y) { hidPoint(0x80 | x); hidPoint(0x40 | y); }

int main(void)
{
    reset(); move(0, 0);
    CHECK(n == 60 && sx == -60*127 && sy == -60*127, "{0,0} slams both ways: %d reports, %d,%d", n, sx, sy);
    reset(); move(51, 57);
    CHECK(sx == 51*8 && sy == 57*8, "{40,90} from there: %d,%d counts", sx, sy);
    CHECK(n == 2*57, "a diagonal, then straight on: %d reports", n);
    reset(); move(61, 50);
    CHECK(sx == 10*8 && sy == -7*8, "from where it was: %d,%d", sx, sy);
    reset(); hidPoint(8); hidPoint(0x10 | 11); move(71, 55);
    CHECK(sx == 10*16 && sy == 5*22, "*2 and *2.75: %d,%d", sx, sy);
    reset(); move(81, 55);
    CHECK(sx == 10*8 && sy == 0, "the scale was that move's only: %d,%d", sx, sy);
    reset(); hidPoint(8); move(0, 30);
    CHECK(sx == -60*127 && sy == -25*8, "an edge on x alone, y unscaled: %d,%d", sx, sy);
    reset(); move(10, 30);
    CHECK(sx == 10*8 && sy == 0, "and on from there: %d,%d", sx, sy);
    reset(); hidPoint(0x31);
    CHECK(n == 4 && held == 0 && sx == 0, "{click}: %d reports, %d held after", n, held);
    reset(); hidPoint(0x22); CHECK(held == 2, "{right-hold}: %d", held);
    reset(); move(20, 30); CHECK(held == 2, "held through a move: %d", held);
    reset(); hidPoint(0x20); CHECK(held == 0, "{release}: %d", held);
    reset(); hidPoint(3); move(30, 30);
    CHECK(sx == 10*2*3 && held == 0, "the spike's old {click}, 3, is a scale and presses nothing: %d", sx);
    CHECK(!bad, "%d reports without the mouse's id", bad);
    printf(fails ? "SOME CHECKS FAILED\n" : "all checks passed\n");
    return fails != 0;
}
