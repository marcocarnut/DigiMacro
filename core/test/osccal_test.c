/*
 * The oscillator calibration, on the host: ../vusb/osccal.c itself, against
 * a model of the ATtiny85's RC oscillator, since simavr has none -- there
 * every measurement reads the target, and any search passes.
 *
 * OSCCAL has two ranges, 0-127 and 128-255, which overlap: 128 runs slower
 * than 127. Within a range each step is about 0.4%, some 9 counts of a
 * frame. A measurement jitters, so the binary search can end a step to
 * either side of the target. What the calibration must do then is look at
 * the neighbours and keep the nearest: here they measure true, and nothing
 * else will do. The real jitter is a count or two -- the measuring loop
 * takes seven cycles -- and the model's goes to four, under half a step.
 */
#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define F_CPU 16500000L
#define __usbdrv_h_included__           /* V-USB's real header: not here */
unsigned usbMeasureFrameLength(void);
#include "../vusb/osccal.c"

uint8_t OSCCAL;

static int lowBase, highBase, stepLow, stepHigh, noise, measured;

static int truth(uint8_t o)
{
    return o < 128 ? lowBase + o * stepLow : highBase + (o - 128) * stepHigh;
}

unsigned usbMeasureFrameLength(void)
{
    int n = truth(OSCCAL);
    if (measured++ < 8 && noise)        /* the binary search's eight */
        n += rand() % (2 * noise + 1) - noise;
    return n;
}

int main(void)
{
    const int target = (unsigned)(1499 * (double)F_CPU / 10.5e6 + 0.5);
    int fail = 0, runs = 0, wrong = 0;

    srand(1);
    for (int k = 0; k < 20000; k++) {
        stepLow = 8 + rand() % 3;
        stepHigh = 8 + rand() % 3;
        /* the target somewhere inside one range or the other, not at its ends */
        lowBase = target - (16 + rand() % 96) * stepLow;
        highBase = rand() % 2 ? target - (16 + rand() % 96) * stepHigh
                              : lowBase + 64 * stepLow;
        noise = k < 1000 ? 0 : 1 + rand() % 4;  /* under half a step */

        /* the search can only find what is in the range it picks at 128 */
        int range = truth(128) < target ? 128 : 0;
        int bestIn = range;
        for (int o = range; o < range + 128; o++)
            if (abs(truth(o) - target) < abs(truth(bestIn) - target))
                bestIn = o;

        measured = 0;
        calibrateOscillator();
        runs++;
        if (abs(truth(OSCCAL) - target) != abs(truth(bestIn) - target)) {
            if (wrong++ < 5)
                printf("  noise %d: OSCCAL %d measures %d, %d would measure %d (target %d)\n",
                       noise, OSCCAL, truth(OSCCAL), bestIn, truth(bestIn), target);
        }
    }
    printf("  [%s] the nearest value, %d calibrations, jitter up to 4 counts (%d not)\n",
           wrong ? "FAIL" : " ok ", runs, wrong);
    fail |= wrong != 0;
    printf("%s\n", fail ? "FAIL" : "PASS");
    return fail;
}
