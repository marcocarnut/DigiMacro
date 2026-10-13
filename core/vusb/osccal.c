/* Name: osccal.c
 * Author: Christian Starkjohann
 * Creation Date: 2008-04-10
 * Tabsize: 4
 * Copyright: (c) 2008 by OBJECTIVE DEVELOPMENT Software GmbH
 * License: GNU GPL v2 (see License.txt), GNU GPL v3 or proprietary (CommercialLicense.txt)
 * This Revision: $Id: osccal.c 762 2009-08-12 17:10:30Z cs $
 */

#include <avr/io.h>
#include "usbdrv.h" // for usbMeasureFrameLength()

#ifndef uchar
#define uchar   unsigned char
#endif

/* ------------------------------------------------------------------------- */
/* ------------------------ Oscillator Calibration ------------------------- */
/* ------------------------------------------------------------------------- */

/* Calibrate the RC oscillator. Our timing reference is the Start Of Frame
 * signal (a single SE0 bit) repeating every millisecond immediately after
 * a USB RESET. We first do a binary search for the OSCCAL value and then
 * optimize this value with a neighboorhod search.
 */
void    calibrateOscillator(void)
{
uchar       step = 128;
uchar       trialValue = 0;
int         x, targetValue = (unsigned)(1499 * (double)F_CPU / 10.5e6 + 0.5);

    /* do a binary search: */
    do{
        OSCCAL = trialValue + step;
        x = usbMeasureFrameLength();    /* proportional to current real frequency */
        if(x < targetValue)             /* frequency still too low */
            trialValue += step;
        step >>= 1;
    }while(step > 0);
    /* The search ends next to the target: the last value that measured
       below it, so the nearest is that one or the next one up. A
       measurement can be off by a count or two, which can put it on the
       wrong side; less than half a step (about 9 counts), though, so the
       next one down is never nearer, and V-USB's look at it is spared
       (core/test/osccal_test.c). The measurement is unsigned: the
       subtractions are not. */
    OSCCAL = trialValue + 1;
    x = usbMeasureFrameLength() - targetValue;
    OSCCAL = trialValue;
    if(targetValue - (int)usbMeasureFrameLength() > x)
        OSCCAL = trialValue + 1;
}
/*
Note: This calibration algorithm may try OSCCAL values of up to 192 even if
the optimum value is far below 192. It may therefore exceed the allowed clock
frequency of the CPU in low voltage designs!
You may replace this search algorithm with any other algorithm you like if
you have additional constraints such as a maximum CPU clock.
For version 5.x RC oscillators (those with a split range of 2x128 steps, e.g.
ATTiny25, ATTiny45, ATTiny85), it may be useful to search for the optimum in
both regions.
*/
