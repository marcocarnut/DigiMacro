/*
 * Spike: the core updater -- an app that carries a core and puts it in
 * place of the one below it, in one step (make coreupdate).
 *
 * It runs from setup, before USB, with interrupts off and nothing of the
 * old core used again: pages 0 to NCORE-1 from the image, the core's table
 * into the top half of page 63, every other page below the boundary erased.
 * Then its own header page, so that after the reset the new core finds no
 * app, does not run this again, and the page puts DigiMacro back. The
 * EEPROM is not touched.
 *
 * In assembler because it has to be: the image is 61 pages and the table,
 * and the app region holds 64 pages less a header and a CRC. In C the same
 * loop is 108 bytes over. So a core of more than 61 pages cannot be carried
 * this way at all.
 *
 * Power lost in the half second of it leaves a board for a programmer, as
 * with any Micronucleus upload.
 */
#include <avr/io.h>
#include "layout.h"
#include "coreimg.h"                    /* NCORE: the build works it out */

#define LAST    (APP_START - PAGE)      /* the page that ends in the table */

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
    cpi     r27, hi8(APP_START)
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
