/* Exercises the capacitive-touch detection logic of the HAVE_TOUCH build.
 *
 * The analog side -- whether a finger actually changes the charge time on
 * your pad -- can only be answered on hardware. What can be answered here is
 * everything downstream of the count: that the sampler rejects interference
 * the right way round, that the running average follows drift but not a
 * finger, and that the detector cannot latch on permanently. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include "Arduino.h"
#include "hidusb.h"

#define HAVE_TOUCH 1

uint8_t PORTB, DDRB;
uint8_t GPIOR0, GPIOR1, GPIOR2;
unsigned long fake_ms = 0;
uint8_t EE[512];

/* Synthetic pad. fake_charge() is hooked to _delay_us(), which touch_once()
   calls exactly once per charge cycle, and arms a countdown of reads during
   which the pin has "not crossed the threshold yet". Which level that is
   depends on the method: the pull-up method waits for the pin to go high,
   the bleed-resistor method waits for it to go low. */
static int pinb_count = 0, q[64], qn = 1, qi = 0, pin_idle_high = 0;
uint8_t fake_pinb(void)
{
    int busy = pinb_count > 0;
    if (busy) pinb_count--;
    /* "busy" = still moving toward the threshold */
    return (busy == pin_idle_high) ? 0x00 : 0xFF;
}
void fake_charge(void){ pinb_count = q[qi++ % qn]; }


void hidBegin(void){} void hidPoll(void){}
uint8_t hidReady(void){ return 1; } uint8_t hidAwake(void){ return 1; }
void hidAllow(uint16_t){} uint8_t hidAllowed(void){ return 0; }
void hidKey(uint8_t,uint8_t){} void hidTypeChar(char,uint8_t){} void hidPoint(uint8_t){}
/* The core's password: none, here */
uint8_t hidPassSet(void){ return 0; } uint8_t hidUnlocked(void){ return 1; }
uint8_t hidUnlock(const uint8_t *, uint8_t){ return DM_PASS_RIGHT; }
void hidLock(void){}

#include "DigiMacro.ino"

/* every charge cycle takes v */
static void cycles(int v){ qn = 1; qi = 0; q[0] = v; }
/* a per-cycle cost that adds up to `want` -- `want` must divide evenly */
static void reading(int want){ cycles(want / TOUCH_REPS); }
/* an exact reading, spread over the cycles of a group -- so a press can be
   ramped one count at a time, the way a finger actually arrives */
static void level(int want)
{
    qn = TOUCH_REPS; qi = 0;
    for (int i = 0; i < TOUCH_REPS; i++)
        q[i] = want / TOUCH_REPS + (i < want % TOUCH_REPS ? 1 : 0);
}

/* per-GROUP cycle costs, so group i sums to g[i] * TOUCH_REPS */
static void groups(int a,int b,int c,int d)
{
    int g[4] = {a,b,c,d};
    qn = 0; qi = 0;
    for (int i = 0; i < TOUCH_GROUPS; i++)
        for (int r = 0; r < TOUCH_REPS; r++) q[qn++] = g[i % 4];
}


static int fails;
#define CHECK(c,...) do{ if(!(c)){ printf("  FAIL: "); printf(__VA_ARGS__); printf("\n"); fails++; } }while(0)

/* a reading is the sum of TOUCH_REPS cycles */
#define G(perCycle) ((perCycle) * TOUCH_REPS)

static void settle(int perCycle){
    cycles(perCycle);
    touch_base = G(perCycle) << 4;
    touch_since = touch_moved = (uint16_t)fake_ms;
    touch_state = 0;
}

/* The average moves once every TOUCH_BASE_MS, and the harness advances the
   clock a millisecond per poll, so settling wants time rather than
   iterations: a poll is a millisecond, and the time constant is 1.6 s. */
static void poll_for(int n){ while (n--) touch_poll(); }
static int base(void){ return touch_base >> 4; }

int main(void)
{
    /* the pull-up method counts while the pin is still LOW */
    pin_idle_high = TOUCH_PULLUP ? 1 : 0;
    printf("(TOUCH_PULLUP=%d, %d cycles summed per reading, %d readings)\n\n",
           TOUCH_PULLUP, TOUCH_REPS, TOUCH_GROUPS);

    printf("1. a reading sums the cycles, and the highest reading wins\n");
    /* An interrupt mid-count leaves the pad charging while we are not
       counting, so a disturbed reading comes out LOW. The max is the clean
       one -- the opposite of the usual "take the minimum to reject noise". */
    groups(1, 5, 2, 3);
    uint8_t r = touch_read();
    CHECK(r == G(5), "got %d, wanted the highest group %d", r, G(5));
    cycles(4); r = touch_read();
    CHECK(r == G(4), "undisturbed groups must agree: got %d, wanted %d", r, G(4));

    printf("2. a reading is wide enough to put a threshold through\n");
    /* Roughly what each method gives on real hardware: the pull-up rise is
       ~2 cycles idle against ~6 touched, which is only thresholdable once
       TOUCH_REPS of them are summed; a 1M bleed resistor stretches a single
       cycle to ~50 against ~200 and needs no summing at all. */
#if TOUCH_PULLUP
    cycles(2); uint8_t idle = touch_read();
    cycles(6); uint8_t down = touch_read();
#else
    cycles(50); uint8_t idle = touch_read();
    cycles(200); uint8_t down = touch_read();
#endif
    CHECK(down - idle >= 16, "only %d counts between idle and touched -- "
          "too coarse to threshold; raise TOUCH_REPS", down - idle);

    printf("3. a pad that never crosses saturates visibly\n");
    cycles(1000); r = touch_read();
    CHECK(r == TOUCH_SAT, "wanted TOUCH_SAT %d, got %d", TOUCH_SAT, r);

    printf("4. the average settles on an untouched pad\n");
    /* Seeded the way app_setup() does. Without that seed every reading looks
       like a touch against a base of zero, the average freezes, and only
       the stuck-touch timeout gets it out -- which is why app_setup() takes a
       reading before the loop starts. */
    cycles(2);
    touch_base = (uint16_t)touch_read() << 4;
    touch_state = 0; touch_since = touch_moved = (uint16_t)fake_ms;
    poll_for(20000);
    CHECK(base() == G(2), "settled at %d, wanted %d", base(), G(2));
    CHECK(!touch_state, "a steady pad must not read as touched");

    printf("5. powering up with a finger already on the pad recovers\n");
    cycles(6);
    touch_base = (uint16_t)touch_read() << 4;   /* seeded high */
    touch_state = 0; touch_since = touch_moved = (uint16_t)fake_ms;
    cycles(2);                                  /* the finger lifts */
    poll_for(20000);
    CHECK(!touch_state, "must not report a touch once the finger is gone");
    CHECK(base() == G(2), "base should have come down to %d, got %d", G(2), base());

    printf("6. the threshold sits a quarter above the average\n");
    /* 24 is reachable either way, and lands exactly on the boundary for a
       base of 18: 18 + 18/4 + 2 = 24. */
    reading(24);
    touch_base = 18 << 4; touch_state = 0;
    touch_since = touch_moved = (uint16_t)fake_ms;
    touch_poll();
    CHECK(touch_raw == 24, "expected a reading of 24, got %d", touch_raw);
    CHECK(!touch_state, "a reading equal to the threshold must not trigger");
    reading(24);
    touch_base = 17 << 4; touch_state = 0;
    touch_since = touch_moved = (uint16_t)fake_ms;
    touch_poll();
    CHECK(touch_state, "one count above the threshold must trigger");

    printf("7. the average does not follow a finger\n");
    settle(2); cycles(6);
    for (int i = 0; i < 50; i++){ fake_ms += 10; touch_poll(); }
    CHECK(touch_state, "%d against a base of %d is a touch", G(6), G(2));
    CHECK(base() == G(2), "base crept to %d while touched", base());

    printf("8. ...but it does follow slow drift\n");
    /* Each step stays under the threshold, so it is drift, not a finger. */
    touch_base = 100 << 4; touch_state = 0;
    touch_since = touch_moved = (uint16_t)fake_ms;
    for (int want = 104; want <= 128; want += 8) {
        reading(want);
        poll_for(20000);
    }
    CHECK(!touch_state, "a slow climb must not read as a touch");
    CHECK(base() >= 120, "base should have followed the drift, got %d", base());

    printf("9. a touch that outlasts any real one is treated as drift\n");
    settle(2); cycles(6); touch_poll();
    CHECK(touch_state, "should be touched to begin with");
    fake_ms += TOUCH_STUCK_MS + 1000;
    touch_poll();
    CHECK(base() == G(6), "should have re-based to %d, got %d", G(6), base());
    touch_poll();
    CHECK(!touch_state, "and then stopped reporting a touch");

    printf("10. release is detected\n");
    settle(2); cycles(6); touch_poll();
    CHECK(touch_state, "touched");
    cycles(2); touch_poll();
    CHECK(!touch_state, "must release when the finger goes");

    printf("11. the stuck-touch timeout outlasts a deliberate hold\n");
    /* The firmware holds the pad for HOLD_MS on purpose, to reach the app.
       If the stuck-touch timeout were shorter, that hold would re-base the
       sensor mid-press and cancel itself. */
    CHECK(TOUCH_STUCK_MS > HOLD_MS,
          "TOUCH_STUCK_MS (%d) must exceed HOLD_MS (%d)",
          TOUCH_STUCK_MS, HOLD_MS);

    printf("12. a press that lands slowly still registers\n");
    /* The bug this pair of changes fixes, and the reason to keep the test.
       The average used to move every poll, and the loop polls the pad
       thousands of times a second, so a finger arriving over tens of
       milliseconds was tracked all the way up and never crossed the
       threshold. A quick tap outran it; a press made deliberately, in order
       to hold it for four seconds, did not. Readings here are the ones the
       board actually reported: 8 at rest, 16 with a finger on it. */
    touch_base = 8 << 4; touch_state = 0;
    touch_since = touch_moved = (uint16_t)fake_ms;
    for (int r = 8; r <= 16; r++) {             /* ~360 ms to land */
        level(r);
        poll_for(40);
    }
    CHECK(touch_state, "a press landing over 360 ms must register: raw %d "
          "against a base of %d", touch_raw, base());
    CHECK(base() <= 9, "the average must barely move while it lands: %d",
          base());

    printf("12b. ...and having registered, it stays registered\n");
    /* Hysteresis: once it is a finger, a smaller margin keeps it one. The
       four-second hold needs an unbroken press, and a single sample dipping
       below the threshold would otherwise end it. */
    poll_for(4000);                             /* the four-second hold, and
                                                   no longer: a press held
                                                   past TOUCH_STUCK_MS is
                                                   meant to be given up on,
                                                   which test 9 covers */
    CHECK(touch_state, "the press must survive being held");
    CHECK(base() == 8, "and the average must not creep: %d", base());
    level(8 + (8 >> 2) + 2);                    /* exactly the press threshold */
    touch_poll();
    CHECK(touch_state, "a dip to the press threshold must not end a press");
    level(8);                                   /* the finger really lifts */
    touch_poll();
    CHECK(!touch_state, "but the finger lifting must");

    printf("13. the pad is left idle between readings\n");
    touch_once();
    CHECK(!(DDRB  & (1 << PIN_TOUCH)), "pad must be left an input");
    CHECK(!(PORTB & (1 << PIN_TOUCH)), "pull-up must be left off");

    printf("\n%s\n", fails ? "SOME CHECKS FAILED" : "all checks passed");
    return fails != 0;
}
