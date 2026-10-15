/*
 * The core updater -- an app for a core with the boundary at 0x1000 (API
 * 1), that carries this one, with the boundary at NEW_START, and puts it in
 * place of the old in one step (make coreupdate).
 *
 * It runs from setup, before USB, with interrupts off and nothing of the
 * old core used again: pages 0 to NCORE-1 from the image, the new core's
 * table into the top half of the page below NEW_START, every other page
 * below 0x1000 erased -- the old core's tail and table included, which are
 * the new app's first pages. Then its own header page, so that after the
 * reset the new core finds no app, the old boundary's or its own, and the
 * page puts DigiMacro back. The EEPROM is not touched.
 *
 * In assembler because it has to be: the image is NCORE pages and the
 * table, and the app region holds 64 pages less a header and a CRC. In C
 * the same loop was 108 bytes more.
 *
 * Power lost in the half second of it leaves a board for a programmer, as
 * with any Micronucleus upload.
 */
#include <avr/io.h>
#include "layout.h"
#include "coreimg.h"                    /* NCORE: the build works it out */

#define LAST    (NEW_START - PAGE)      /* the page that ends in the new table */

    .text
    .global app_setup, app_loop, app_request
app_setup:
    cli
    ldi     r28, lo8(newCore)           ; Y: the image
    ldi     r29, hi8(newCore)
    clr     r26                         ; X: where it goes
    clr     r27
    clr     r18                         ; the page's number
pages:
    wdr                                 ; the core's: 8 s for all of setup
    movw    r30, r26
    ldi     r16, (1 << PGERS) | (1 << SPMEN)
    rcall   doSpm
    ldi     r17, PAGE / 2               ; a whole page from the image...
    cpi     r18, NCORE
    brlo    fill
    cpi     r18, LAST / PAGE
    brne    next
    ldi     r17, CORE_SLOTS             ; ...or the table, at the top of 63
    adiw    r26, PAGE - 2 * CORE_SLOTS
fill:
    movw    r30, r28
    lpm     r0, Z+
    lpm     r1, Z+
    movw    r28, r30
    movw    r30, r26
    ldi     r16, (1 << SPMEN)
    rcall   doSpm
    adiw    r26, 2
    dec     r17
    brne    fill
    sbiw    r26, 2                      ; still in the page just filled
    movw    r30, r26
    ldi     r16, (1 << PGWRT) | (1 << SPMEN)
    rcall   doSpm
    andi    r26, 0xC0                   ; its start again
next:
    inc     r18
    adiw    r26, 32
    adiw    r26, 32
    cpi     r27, hi8(APP_START)             ; the old boundary: 0x1000
    brne    pages
    movw    r30, r26                    ; the header: no app after the reset
    ldi     r16, (1 << PGERS) | (1 << SPMEN)
    rcall   doSpm
    ldi     r16, (1 << WDCE) | (1 << WDE)
    out     _SFR_IO_ADDR(WDTCR), r16
    ldi     r16, (1 << WDE)             ; 16 ms, and a real reset
    out     _SFR_IO_ADDR(WDTCR), r16
    rjmp    .-2

doSpm:
    out     _SFR_IO_ADDR(SPMCSR), r16
    spm
1:  in      r16, _SFR_IO_ADDR(SPMCSR)
    sbrc    r16, SPMEN
    rjmp    1b
    ret

app_request:
    clr     r24
app_loop:
    ret

/* NCORE pages of the core, then its table's 32 bytes */
newCore:
    .incbin "coreimg.bin"
