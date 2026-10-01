/* Plays every code in tools/loadtable's sample table against the real
 * firmware, before it is written to a board.
 *
 * The table is the interface between the firmware and whatever writes it --
 * the loader now, the app later -- and it is written out by hand in hex. This
 * is where a wrong byte gets caught: the same header the loader flashes is
 * read here by the same walk the board runs.
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
uint8_t fake_pinb(void){ return 0xFF; }         /* nothing pressed */
void    fake_charge(void){}

static char typed[4096];
static int  typedn;
void hidBegin(void){}
void hidPoll(void){}
uint8_t hidReady(void){ return 1; }
static uint8_t host_awake = 1;  /* the host has said hello, unless a test says not */
uint8_t hidAwake(void){ return host_awake; }
uint8_t hidLeds(void){ return 0; }
/* The configuration window the four-second hold opens. */
static uint16_t allow_until; static uint8_t allow_open;
void hidAllow(uint16_t ms){ allow_until = (uint16_t)fake_ms + ms; allow_open = 1; }
uint8_t hidAllowed(void){
    if (allow_open && (int16_t)((uint16_t)fake_ms - allow_until) >= 0) allow_open = 0;
    return allow_open;
}
void hidKey(uint8_t mod, uint8_t key)
{
    if (typedn < (int)sizeof typed - 16)
        typedn += sprintf(typed + typedn, "<%02X/%02X>", mod, key);
}
void hidTypeChar(char, uint8_t){}
void hidType(const char*){} void hidType_P(const char*){}

#include "DigiMacro.ino"
#include "../tools/loadtable/sample_table.h"

static int fails;
#define CHECK(c,...) do{ if(!(c)){ printf("  FAIL: "); printf(__VA_ARGS__); \
                                   printf("\n"); fails++; } }while(0)

static const char *play(const char *code)
{
    uint8_t bits = 0, n = 0;
    for (const char *s = code; *s; s++) { bits = (bits << 1) | (*s == '-'); n++; }
    typedn = 0; typed[0] = 0;
    code_reset();
    code_bits = bits; code_len = n;
    code_finish();
    return typed;
}

int main(void)
{
    /* What the loader does, byte for byte */
    memset(EE, 0xFF, sizeof EE);
    for (uint16_t i = 0; i < sizeof table; i++) EE[i] = table[i];

    printf("1. the table fits below the reserved bytes, and is terminated\n");
    CHECK(sizeof table <= (size_t)TABLE_TOP, "%zu bytes of table", sizeof table);
    CHECK(table[sizeof table - 1] == 0xFF, "the last byte must end the table");

    printf("2. \".\" types its banner, Shift and digits included\n");
    CHECK(!strcmp(play("."),
          "<02/07><00/0C><00/0A><00/0C><02/10><00/04><00/06><00/15><00/12>"
          "<00/2C><00/1E><00/37><00/27><00/28>"),
          "got \"%s\"", typed);

    printf("3. \"-\" repeats the dot three times over, then pauses\n");
    unsigned long t0 = fake_ms;
    CHECK(!strcmp(play("-"),
          "<00/1A><00/04><00/0C><00/17><00/37><00/37><00/37><00/12><00/0E>"),
          "got \"%s\"", typed);
    CHECK(fake_ms - t0 >= 1000, "the pause should be a second, was %lu ms",
          fake_ms - t0);

    printf("4. \"..\" writes one b and repeats it five times\n");
    CHECK(!strcmp(play(".."),
          "<00/04><00/05><00/05><00/05><00/05><00/05>"), "got \"%s\"", typed);

    printf("5. \".-\" sends Ctrl with a key that is not a character\n");
    CHECK(!strcmp(play(".-"), "<01/50><00/1D>"), "got \"%s\"", typed);

    printf("6. \"---\" is longer than any DigiMacroCDC entry could be\n");
    play("---");
    CHECK(typedn / 7 == 37, "37 keystrokes, got %d", typedn / 7);
    CHECK(typedn / 7 > 30, "the old per-slot limit is gone");
    CHECK(!strncmp(typed, "<00/17><00/0B><00/0C><00/16>", 28),
          "should start \"this\": \"%.28s\"", typed);

    printf("7. nothing else in the table answers\n");
    CHECK(play("--")[0] == 0, "\"--\" is not in the table: \"%s\"", typed);
    CHECK(play("....")[0] == 0, "nor \"....\": \"%s\"", typed);
    CHECK(play("-.-")[0] == 0, "nor \"-.-\": \"%s\"", typed);

    printf("\n%s\n", fails ? "SOME CHECKS FAILED" : "all checks passed");
    return fails != 0;
}
