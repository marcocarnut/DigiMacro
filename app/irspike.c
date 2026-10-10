/*
 * The IR spike: what a remote sends, as the board sees it. An app of its
 * own on the core, so the firmware stays as it is until there is something
 * to change in it.
 *
 * Every edge of the receiver's output on P2 (INT0) is timed with Timer0 at
 * CK/1024, 62 us a tick -- the same clock the firmware's decoder reads, so
 * what is typed is what the decoder had to go on. A frame is everything
 * between two gaps of 16 ms or more; once one ends, the board types it as
 * one line, in microseconds, alternating carrier and silence:
 *
 *      67: 9000 4500 560 560 560 1690 ... 560
 *
 * the count first, and a '>' at the end if there was more than fits. Edges
 * that come while a line is being typed are dropped, so press one button,
 * once, and wait for its line. Nothing typed at all means the decoder never
 * had a frame to read either: the receiver sees no carrier it accepts.
 *
 * NEC, for comparison: 9000 4500, then 32 times 560 and 560 or 1690, then
 * 560 -- 67 numbers. A repeat, while a button is held: 9000 2250 560.
 *
 * The board's window is always open (no pad here), so the page can
 * put the firmware back without a hold. Not for a board in daily use.
 */
#include <stdint.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include "core.h"
#include "hidusb.h"

#define LED         PB1
#define IR          PB2                 /* INT0 */
#define MAXEDGES    80                  /* NEC is 67; RAM is short */

static volatile uint8_t dur[MAXEDGES];  /* ticks, each under 16 ms */
static volatile uint8_t n;              /* how many dur[] holds */
static volatile uint8_t on;             /* a frame is coming in */
static volatile uint8_t done;           /* it has ended: edges are dropped
                                           until its line is typed */

/* Not blocking, as the firmware's: V-USB's interrupt must start at once. */
ISR(INT0_vect, ISR_NOBLOCK)
{
    uint8_t t = TCNT0;

    TCNT0 = 0;
    if (TIFR & (1 << TOV0)) {           /* wrapped: a gap, not a pulse */
        TIFR = 1 << TOV0;
        t = 0xFF;
    }
    if (done) return;
    if (!on) {
        /* A frame starts as the output falls, carrier after a gap. */
        if (t == 0xFF && !(PINB & (1 << IR))) on = 1, n = 0;
        return;
    }
    if (t == 0xFF) { done = 1; return; }   /* the gap after it */
    if (n < MAXEDGES) dur[n++] = t;
}

static void type_dec(uint16_t v)
{
    char s[6];
    uint8_t i = 0;

    do { s[i++] = '0' + v % 10; v /= 10; } while (v);
    while (i) hidTypeChar(s[--i], 0);
}

void app_setup(void)
{
    hidBegin();                         /* borrows Timer0: set it up after */
    DDRB |= 1 << LED;
    DDRB &= ~(1 << IR);
    PORTB |= 1 << IR;                   /* the pull-up, as the firmware */
    TCCR0A = 0;
    TCCR0B = (1 << CS02) | (1 << CS00); /* CK/1024 */
    MCUCR = (MCUCR & ~(1 << ISC01)) | (1 << ISC00);     /* INT0 on any edge */
    GIMSK |= 1 << INT0;
}

void app_loop(void)
{
    hidPoll();
    hidAllow(10000);

    /* A frame has ended at the next gap: an edge after one, or the timer
       wrapping with none. */
    uint8_t sreg = SREG;
    cli();
    if (on && (TIFR & (1 << TOV0))) done = 1;
    uint8_t m = n, end = done;
    SREG = sreg;
    if (on) PORTB |= 1 << LED; else PORTB &= ~(1 << LED);
    if (!end) return;

    type_dec(m);
    hidTypeChar(':', 0);
    for (uint8_t i = 0; i < m; i++) {
        hidTypeChar(' ', 0);
        type_dec((uint16_t)((uint32_t)dur[i] * 2048 / 33));    /* ticks to
                                                       us: 1024 / 16.5 */
    }
    if (m == MAXEDGES) hidTypeChar('>', 0);         /* there was more */
    hidTypeChar('\n', 0);

    cli();
    on = 0;
    done = 0;
    SREG = sreg;
}

/* No lockout, no host key, no remote of the firmware's. The password is the
   core's, and still holds. */
void    hidPassWrong(void)                       { }
void    hidPassRight(void)                       { }
void    hidPassChanged(void)                     { }
void    hidLearn(void)                           { }
uint8_t hidLockState(void)                       { return 0; }
uint8_t hidHostState(void)                       { return 0; }
void    hidHostForget(void)                      { }
uint8_t hidHostAuth(const uint8_t *p, uint8_t n) { (void)p; (void)n; return 0; }
