/*
 * The uninstaller: DigiMacro out, Micronucleus back, the board a Digispark
 * as it came.
 *
 * Micronucleus does not come back by being written here: it comes with this
 * app, in the same image, the way any app arrives (make uninstall):
 *
 *      0x1000  this app, a few dozen bytes of it
 *              blank -- which is what Micronucleus finds 4 bytes below its
 *              start, where it looks for a program to run: none, so it
 *              stays in the bootloader instead of timing out to one
 *      0x1A00  Micronucleus 2.6, as released (app/micronucleus/)
 *
 * and the core has already checked the CRC over all of it before running
 * any. What is left is two things, both before USB connects:
 *
 *   1. wipe the EEPROM -- the table, the password, the key check -- so that
 *      no program put on the board afterwards finds any of it;
 *   2. point the reset vector, in page 0, at Micronucleus.
 *
 * Page 0 is the core's, and its vectors with it: from the erase on, any
 * interrupt is a jump into nothing, so they stay off, and nothing after it
 * may need the core. A power cut before it lands leaves the core and this
 * app to do it all again; only one during the erase and write of that page
 * leaves a board for a programmer.
 */
#include <stdint.h>
#include <avr/io.h>
#include <avr/interrupt.h>
#include <avr/eeprom.h>
#include <avr/boot.h>
#include <avr/pgmspace.h>
#include <avr/wdt.h>

#ifndef MN_START
#error "MN_START: where Micronucleus starts (the Makefile says)"
#endif

void app_setup(void)
{
    cli();

    /* Not there -- a mistaken image the CRC could not know about: leave the
       board as it is, its core able to take DigiMacro back. */
    if (pgm_read_word(MN_START) == 0xFFFF) return;

    for (uint16_t a = 0; a <= E2END; a++) {
        wdt_reset();                    /* the core's: 8 s for all of setup */
        eeprom_update_byte((uint8_t *)a, 0xFF);
    }

    /* The last byte may still be being written, for 3.4 ms, and the chip
       ignores an erase or a write of flash meanwhile: a password, at the
       top of the EEPROM, made the last write, and page 0 stayed the core's. */
    eeprom_busy_wait();

    /* rjmp to MN_START: relative, from word 1, and wrapping round the 4K
       words of flash -- the way the upload tool writes it for 2.x */
    boot_page_erase(0);
    boot_spm_busy_wait();
    boot_page_fill(0, 0xC000 | ((MN_START / 2 - 1) & 0x0FFF));
    boot_page_write(0);
    boot_spm_busy_wait();

    /* A real reset, so that Micronucleus starts as from power on. */
    wdt_enable(WDTO_15MS);
    for (;;) ;
}

/* Only if Micronucleus was missing: a board that does nothing, and takes
   DigiMacro back from the page. */
void app_loop(void) { }
uint8_t app_request(uint8_t *v) { (void)v; return 0; }
