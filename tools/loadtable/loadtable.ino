/*
 * loadtable -- write a sample macro table into EEPROM.
 *
 * The app normally does this, over USB. This sketch writes a known table
 * without it -- the same one test/table_test.cpp plays -- for trying the
 * firmware on a board with no browser to hand. Micronucleus cannot write
 * EEPROM, since it only ever touches program memory, so a sketch does it:
 *
 *     ./build.sh loadtable upload     <- this; the LED goes solid when done
 *     ./build.sh upload               <- the firmware
 *
 * and the table survives the second upload, because micronucleus does not
 * erase EEPROM either. Once per board, or once per change to the table below.
 *
 * Tap the codes into a text editor and each one should type what the comment
 * says. Between them they use every escape the format has:
 *
 *      .     DigiMacro 1.0 <newline>      plain keystrokes, and Shift
 *      -     wait... <one second> ok      a pause
 *      ..    abbbbb                       repeat: one b, then five more
 *      .-    <Ctrl+Left> z                a modifier mask on a non-character
 *      ---   a line long enough to show that nothing limits it
 *
 * A code that is not here should get three quick blinks instead.
 */

#include <avr/pgmspace.h>
#include <avr/eeprom.h>

#include "sample_table.h"

#define PIN_LED     1       /* as on the board DigiMacro runs on */

void setup(void)
{
    uint8_t bad = 0;

    DDRB |= (1 << PIN_LED);

    for (uint16_t i = 0; i < sizeof(table); i++) {
        uint8_t v = pgm_read_byte(&table[i]);
        eeprom_update_byte((uint8_t *)i, v);
        if (eeprom_read_byte((uint8_t *)i) != v) bad = 1;
    }

    /* Solid means written and read back. Blinking means it did not take. */
    for (;;) {
        if (bad) PORTB ^= (1 << PIN_LED);
        else     PORTB |= (1 << PIN_LED);
        for (volatile uint16_t d = 0; d < 12000; d++) ;
    }
}

void loop(void) { }
