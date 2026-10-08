/* Loads a table given as hex on standard input, then plays the codes named on
 * the command line with the real firmware and prints what each one typed.
 *
 * It exists so that the app's encoder and the firmware's decoder can be
 * checked against each other rather than each being trusted on its own: the
 * table is the interface between the two halves of this project, and a wrong
 * header byte looks exactly like a firmware bug from the outside.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

#define HAVE_TOUCH 0
#include "Arduino.h"
#include "hidusb.h"

uint8_t PORTB, DDRB;
uint8_t GPIOR0, GPIOR1, GPIOR2;
unsigned long fake_ms = 0;
uint8_t EE[512];
uint8_t fake_pinb(void){ return 0xFF; }
void    fake_charge(void){}

static char typed[4096];
static int  typedn;

void hidBegin(void){}
void hidPoll(void){}
uint8_t hidReady(void){ return 1; } uint8_t hidAwake(void){ return 1; }
void hidAllow(uint16_t){}
uint8_t hidAllowed(void){ return 1; }
void hidKey(uint8_t mod, uint8_t key)
{
    if (typedn < (int)sizeof typed - 16)
        typedn += sprintf(typed + typedn, "<%02X/%02X>", mod, key);
}
void hidTypeChar(char, uint8_t){}

#include "DigiMacro.ino"

int main(int argc, char **argv)
{
    unsigned v;
    uint16_t n = 0;

    memset(EE, 0xFF, sizeof EE);
    while (n < sizeof EE && scanf(" %x", &v) == 1) EE[n++] = (uint8_t)v;
    fprintf(stderr, "loaded %u bytes\n", n);

    for (int i = 1; i < argc; i++) {
        uint8_t bits = 0, len = 0;
        for (const char *s = argv[i]; *s; s++) {
            bits = (bits << 1) | (*s == '-');
            len++;
        }
        typedn = 0; typed[0] = 0;
        code_reset();
        code_bits = bits; code_len = len;
        code_finish();
        printf("%s %s\n", argv[i], typed);
    }
    return 0;
}
