/* Compiles the real DigiMacro.ino for the host, against stub hardware, and
 * exercises the parts that do not need a USB bus or a timer: the table walk,
 * playback with its escapes, and the morse decoding all the way from the
 * input pin.
 *
 * The point is that this is the *same* code that gets flashed -- not a model
 * of it. It runs in under a second, which beats reflashing a board to find
 * out that an escape argument was read one byte early.
 *
 * Built with the button input, so a press is one line of the test rather
 * than a synthetic pad; touch_test.cpp takes the pad.
 */
#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <initializer_list>

#define HAVE_TOUCH 0
#include "Arduino.h"
#include "hidusb.h"

uint8_t PORTB, DDRB;
uint8_t GPIOR0, GPIOR1, GPIOR2;
unsigned long fake_ms = 0;
uint8_t EE[512];

static int btn_held = 0;        /* the input, as handle_input() sees it */
uint8_t fake_pinb(void){ return btn_held ? 0x00 : 0xFF; }
void    fake_charge(void){}

/* captured device behaviour */
static char    typed[4096];
static int     typedn;
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
void hidTypeChar(char, uint8_t){}
void hidType(const char*){} void hidType_P(const char*){}

/* Every keystroke, modifiers and all, in the order they were sent. Hex
   rather than letters so that a modifier or an odd usage cannot hide. */
void hidKey(uint8_t mod, uint8_t key)
{
    if (typedn < (int)sizeof typed - 16)
        typedn += sprintf(typed + typedn, "<%02X/%02X>", mod, key);
}

#include "DigiMacro.ino"

/* ---- harness ------------------------------------------------------- */

static int fails;
#define CHECK(c,...) do{ if(!(c)){ printf("  FAIL: "); printf(__VA_ARGS__); \
                                   printf("\n"); fails++; } }while(0)

/* US usages, so the expectations read as something */
#define K(c)   ((uint8_t)((c) - 'a' + 0x04))    /* a letter */
#define KS(c)  ((uint8_t)(K(c) | KEY_SHIFT))    /* ...with Shift */
#define K_LEFT 0x50

/* ".-" -> the header byte's code and length */
static uint8_t code_of(const char *s, uint8_t *len)
{
    uint8_t bits = 0, n = 0;
    for (; *s; s++) { bits = (bits << 1) | (*s == '-'); n++; }
    *len = n;
    return bits;
}

/* ---- building a table by hand, the way the app will ---- */

static uint16_t tw;             /* write cursor */

static void table_reset(void)
{
    memset(EE, 0xFF, sizeof EE);
    tw = 0;
}

static void add(const char *code, std::initializer_list<uint8_t> body)
{
    uint8_t len, bits = code_of(code, &len);
    EE[tw++] = HDR_MAKE(len, bits);
    for (uint8_t b : body) EE[tw++] = b;
    EE[tw++] = 0;               /* end of this macro */
    EE[tw] = 0xFF;              /* end of table -- the next add overwrites it */
}

/* ---- playing ---- */

static const char *play(const char *code)
{
    typedn = 0; typed[0] = 0;
    code_reset();
    code_bits = code_of(code, &code_len);
    code_finish();
    return typed;
}

/* ---- tapping, through the pin and loop() ---- */

/* A level is believed only once it has held for DEBOUNCE_MS, so each edge
   takes two passes: one that sees it and one, later, that accepts it. */
static void tap(int ms)
{
    btn_held = 1; loop();
    fake_ms += DEBOUNCE_MS + 1; loop();                 /* press accepted  */
    fake_ms += ms;
    btn_held = 0; loop();
    fake_ms += DEBOUNCE_MS + 1; loop();                 /* release accepted */
}
#define DOT()   tap(DOT_MAX_MS - 100)
#define DASH()  tap(DOT_MAX_MS + 100)

static const char *silence(void)
{
    typedn = 0; typed[0] = 0;
    fake_ms += END_OF_CODE_MS + 10;
    loop();
    return typed;
}

int main(void)
{
    printf("1. a virgin chip is an empty table\n");
    /* 0xFF everywhere means "end of table" at the first byte, so there is
       nothing to initialise and nothing to go wrong. */
    memset(EE, 0xFF, sizeof EE);
    CHECK(find_header(HDR_MAKE(1, 0)) == 0, "a dot must match nothing on a blank chip");
    CHECK(play(".")[0] == 0, "and nothing must be typed: \"%s\"", typed);
    CHECK(play("-----")[0] == 0, "nor for the longest code");

    printf("2. the length is part of the code\n");
    /* "." and ".." both have zero symbol bits; only the count tells them
       apart, which is why the whole header byte is compared. */
    table_reset();
    add(".",   { K('a') });
    add("..",  { K('b') });
    add("-",   { K('c') });
    add("..-", { K('d') });
    CHECK(!strcmp(play("."),   "<00/04>"), "\".\" -> a, got \"%s\"", typed);
    CHECK(!strcmp(play(".."),  "<00/05>"), "\"..\" -> b, got \"%s\"", typed);
    CHECK(!strcmp(play("-"),   "<00/06>"), "\"-\" -> c, got \"%s\"", typed);
    CHECK(!strcmp(play("..-"), "<00/07>"), "\"..-\" -> d, got \"%s\"", typed);
    CHECK(play("-.")[0] == 0, "an absent code types nothing");

    printf("3. entries are found wherever they sit in the table\n");
    /* Packed, so the fourth entry is only reachable by walking the first
       three. A wrong length step would find the wrong macro, not nothing. */
    CHECK(tw == 4 * 3, "four one-key macros should be 12 bytes, got %d", tw);
    CHECK(EE[tw] == 0xFF, "and the table must be terminated");

    printf("4. a keystroke is a usage, and bit 7 is Shift\n");
    table_reset();
    add(".", { K('h'), KS('i'), K('j') });
    CHECK(!strcmp(play("."), "<00/0B><02/0C><00/0D>"),
          "Shift must arrive as modifier 0x02: \"%s\"", typed);

    printf("5. a latched modifier applies to exactly one keystroke\n");
    table_reset();
    add(".", { ESC_MOD, 0x01, K('c'), K('v') });        /* Ctrl+C, then v */
    CHECK(!strcmp(play("."), "<01/06><00/19>"),
          "the modifier must not stick: \"%s\"", typed);

    printf("5b. ...and combines with Shift rather than replacing it\n");
    table_reset();
    add(".", { ESC_MOD, 0x01, KS('t') });               /* Ctrl+Shift+T */
    CHECK(!strcmp(play("."), "<03/17>"),
          "wanted Ctrl|Shift, got \"%s\"", typed);

    printf("6. repeat repeats the previous keystroke, modifiers and all\n");
    table_reset();
    add(".",  { K_LEFT, ESC_REPEAT, 3 });      /* left arrow, four in all */
    add("..", { ESC_MOD, 0x01, KS('a'), ESC_REPEAT, 1 });
    CHECK(!strcmp(play("."), "<00/50><00/50><00/50><00/50>"),
          "one press plus three more: \"%s\"", typed);
    CHECK(!strcmp(play(".."), "<03/04><03/04>"),
          "the repeat must carry the modifiers: \"%s\"", typed);

    printf("7. a pause waits, and keeps typing afterwards\n");
    table_reset();
    add(".", { K('a'), ESC_PAUSE, 5, K('b') });         /* half a second */
    unsigned long t0 = fake_ms;
    CHECK(!strcmp(play("."), "<00/04><00/05>"), "got \"%s\"", typed);
    CHECK(fake_ms - t0 >= 500, "a \\p05 should take 500 ms, took %lu",
          fake_ms - t0);

    printf("8. a zero argument ends the macro\n");
    /* Zero is the terminator, so no escape may take it. The app must not
       write one; if one arrives anyway, stopping is the safe reading -- and
       it is what the byte would have meant on its own. */
    table_reset();
    EE[tw++] = HDR_MAKE(1, 0);
    EE[tw++] = K('a');
    EE[tw++] = ESC_PAUSE; EE[tw++] = 0;         /* \p00 */
    EE[tw++] = K('b');
    EE[tw++] = 0; EE[tw] = 0xFF;
    CHECK(!strcmp(play("."), "<00/04>"),
          "must stop at the bad escape: \"%s\"", typed);

    printf("9. a jump stops the macro in a build without jumps\n");
    table_reset();
    add(".", { K('a'), ESC_JUMP, HDR_MAKE(1, 0), K('b') });
    CHECK(!strcmp(play("."), "<00/04>"),
          "an unimplemented escape must stop, not loop: \"%s\"", typed);

    printf("10. the walk cannot leave the table\n");
    /* A table with no terminator would otherwise run straight into the
       password. Every entry here is a "-", so nothing matches, and the walk
       has to give up at the top of the table rather than read on. */
    memset(EE, 0xFF, sizeof EE);
    for (uint16_t a = 0; a < (uint16_t)(size_t)TABLE_TOP; a += 3) {
        EE[a] = HDR_MAKE(1, 1); EE[a+1] = K('z'); EE[a+2] = 0;
    }
    memcpy(EE + 0x1F4, "hunter2", 8);           /* the thing being protected */
    CHECK(find_header(HDR_MAKE(3, 0)) == 0, "an unterminated table must still end");
    CHECK(play("...")[0] == 0, "and type nothing: \"%s\"", typed);

    printf("11. playback stops at the top of the table too\n");
    /* An entry whose keystrokes are not terminated either. What follows in
       EEPROM is the lockout state and the password; typing those out would
       be the worst bug this firmware could have. */
    memset(EE, 0xFF, sizeof EE);
    EE[0] = HDR_MAKE(1, 0);
    for (uint16_t a = 1; a < (uint16_t)(size_t)TABLE_TOP; a++) EE[a] = K('z');
    for (uint16_t a = (uint16_t)(size_t)TABLE_TOP; a < 512; a++) EE[a] = K('q');
    play(".");
    CHECK(strstr(typed, "<00/14>") == NULL,          /* K('q'), the byte above */
          "nothing above the table may be typed: ...\"%s\"", typed + typedn - 21);
    CHECK(typedn == 7 * ((int)(size_t)TABLE_TOP - 1),
          "should have typed the whole table and stopped, %d bytes", typedn);

    printf("11b. an escape in the last table byte takes no argument from above\n");
    /* Its argument would be the lockout state, and a repeat count read from
       there would tell a bystander what that byte is. */
    memset(EE, 0xFF, sizeof EE);
    EE[0] = HDR_MAKE(1, 0);
    EE[1] = K('a');
    for (uint16_t a = 2; a < (uint16_t)(size_t)TABLE_TOP - 1; a++) EE[a] = K('a');
    EE[(size_t)TABLE_TOP - 1] = ESC_REPEAT;     /* the very last table byte */
    EE[(size_t)TABLE_TOP] = 5;                  /* the lockout byte */
    play(".");
    CHECK(typedn == 7 * ((int)(size_t)TABLE_TOP - 2),
          "the escape must be dropped, not fed: %d of %d bytes",
          typedn, 7 * ((int)(size_t)TABLE_TOP - 2));

    printf("12. too many symbols matches nothing at all\n");
    /* Not even the first five of them: a slip of the finger must not fire a
       macro the tapper did not mean. */
    table_reset();
    add("....", { K('a') });
    typedn = 0; typed[0] = 0;
    code_reset();
    btn_held = 0;
    DOT(); DOT(); DOT(); DOT();
    CHECK(!strcmp(silence(), "<00/04>"), "four dots should fire it: \"%s\"",
          typed);
    code_reset();
    DOT(); DOT(); DOT(); DOT(); DOT(); DOT();
    CHECK(silence()[0] == 0, "six dots must fire nothing: \"%s\"", typed);

    printf("13. tapping, from the pin to the keystroke\n");
    /* The whole input path: debounce, dot against dash by how long the press
       lasted, and the silence that ends the code. */
    table_reset();
    add(".-.", { K('x') });
    code_reset();
    DOT(); DASH(); DOT();
    CHECK(code_len == 3, "three symbols, got %d", code_len);
    CHECK(code_bits == 0x02, "dot-dash-dot is 010, got %02X", code_bits);
    CHECK(!strcmp(silence(), "<00/1B>"), "should type x: \"%s\"", typed);
    CHECK(code_len == 0, "and the code must be cleared afterwards");

    printf("13b. one tap with a dropout in it is still one symbol\n");
    /* What a marginal pad does: the reading dips for a moment in the middle
       of a press. Believed at once, that is a release and a new press -- so a
       single tap arrives as "..", which types the wrong macro rather than
       none, and would cost a four-second hold everything. */
    code_reset();
    btn_held = 1; loop();
    fake_ms += DEBOUNCE_MS + 1; loop();         /* the press is accepted */
    fake_ms += 60;
    btn_held = 0; loop();                       /* one poll of nothing... */
    CHECK(in_down, "a single dropped sample must not end a press -- under the "
                   "old rule it ended it at once, and the press that followed "
                   "became a second symbol");
    fake_ms += 1;
    btn_held = 1; loop();                       /* ...and it is back */
    fake_ms += 60;
    btn_held = 0; loop();
    fake_ms += DEBOUNCE_MS + 1; loop();
    CHECK(code_len == 1, "one tap with a dropout is one symbol, got %d",
          code_len);
    CHECK(!(FLAGS & FLAG_CODE_OVER), "and nothing may be marked overlong");

    printf("14. a four-second hold lets the app in, and taps nothing\n");
    /* It used to change what the device was, because that was the only way a
       page could reach it. Now the device never stops being a keyboard, and
       the hold says the one thing a web page cannot say for itself: that
       somebody is standing here. */
    code_reset();
    CHECK(!hidAllowed(), "nothing is open to begin with");
    btn_held = 1; loop();
    fake_ms += DEBOUNCE_MS + 1; loop();
    fake_ms += HOLD_MS + 10; loop();
    CHECK(hidAllowed(), "the hold should have opened the window");
    btn_held = 0; loop();
    fake_ms += DEBOUNCE_MS + 1; loop();
    CHECK(code_len == 0, "the hold must not leave a dash behind");

    printf("14b. ...and the keyboard goes on working while it is open\n");
    /* The old mode switch stopped the device being a keyboard at all. This
       does not, which is the whole point of the change. */
    table_reset();
    add(".", { K('q') });
    code_reset();
    DOT();
    CHECK(!strcmp(silence(), "<00/14>"),
          "a code must still play with the window open: \"%s\"", typed);
    CHECK(hidAllowed(), "and typing must not close it");

    printf("15. the window closes by itself\n");
    fake_ms += 31000;
    CHECK(!hidAllowed(), "half a minute should be the end of it");

    printf("16. a marked group repeats whole\n");
    /* Once a keystroke stops being a character, "repeat the previous
       keystroke" repeats the wrong thing: an accented letter is the accent
       and then the letter, so a plain repeat repeats the letter alone. The
       mark says where the repeated thing began. */
    table_reset();
    add(".",  { K('a'), K('b'), ESC_REPEAT, 2 });          /* b three times */
    add("..", { ESC_MARK, K('a'), K('b'), ESC_REPEAT, 2 });/* ab three times */
    CHECK(!strcmp(play("."), "<00/04><00/05><00/05><00/05>"),
          "without a mark, only the last keystroke repeats: \"%s\"", typed);
    CHECK(!strcmp(play(".."),
          "<00/04><00/05><00/04><00/05><00/04><00/05>"),
          "with one, the whole group does: \"%s\"", typed);

    printf("16b. ...and the macro carries on after it\n");
    table_reset();
    add(".", { ESC_MARK, K('a'), ESC_REPEAT, 1, K('z') });
    CHECK(!strcmp(play("."), "<00/04><00/04><00/1D>"),
          "a group must not swallow what follows it: \"%s\"", typed);

    printf("16c. a mark with no repeat is harmless\n");
    table_reset();
    add(".", { ESC_MARK, K('a'), K('b') });
    CHECK(!strcmp(play("."), "<00/04><00/05>"),
          "a mark on its own must change nothing: \"%s\"", typed);

    printf("17. the power-on macro\n");
    /* Being enumerated is not the same as having somewhere to type, so it
       waits for the host to speak to the keyboard before it types at it. */
    auto play_at_power_on = [&]() {
        typedn = 0; typed[0] = 0;
        FLAGS &= ~(FLAG_AUTODONE | FLAG_AUTOWAIT);
        loop();                             /* notices the host is there */
        fake_ms += AUTO_SETTLE_MS + 10;
        loop();                             /* and now it may type */
        return (const char *)typed;
    };

    /* Stored as a header byte rather than a position, so the same walk finds
       it and rewriting the table does not have to move it. */
    table_reset();
    add(".",  { K('a') });
    add("-.", { K('b') });
    EE[(size_t)AUTO_ADDR] = HDR_MAKE(2, 0x02);          /* "-.", dash then dot */
    CHECK(!strcmp(play_at_power_on(), "<00/05>"),
          "should have typed b: \"%s\"", typed);

    printf("17a. ...but not before the host has somewhere to put it\n");
    /* The bug this exists for: a macro that started at enumeration lost
       however much of itself fitted in the gap before the host attached a
       keyboard, so "wheee!{pause 1}waaah!" arrived as "waaah!". */
    typedn = 0; typed[0] = 0;
    FLAGS &= ~(FLAG_AUTODONE | FLAG_AUTOWAIT);
    loop();
    CHECK(typedn == 0, "nothing may be typed the instant it is enumerated");
    fake_ms += AUTO_SETTLE_MS - 20; loop();
    CHECK(typedn == 0, "nor before the settling time is up");
    fake_ms += 40; loop();
    CHECK(typedn > 0, "and then it should type");

    printf("17b2. a host that never says hello is waited for, and given up on\n");
    host_awake = 0;
    typedn = 0; typed[0] = 0;
    FLAGS &= ~(FLAG_AUTODONE | FLAG_AUTOWAIT);
    loop();
    fake_ms += AUTO_SETTLE_MS + 10; loop();
    CHECK(typedn == 0, "the short wait is for a host that answered");
    fake_ms += AUTO_WAIT_MS; loop();
    CHECK(typedn > 0, "but it must not wait for ever: \"%s\"", typed);
    host_awake = 1;

    printf("17b. ...once, and only once\n");
    typedn = 0; typed[0] = 0;
    for (int i = 0; i < 5; i++) loop();
    CHECK(typedn == 0, "it must not keep going: \"%s\"", typed);

    printf("17c. nothing set means nothing typed\n");
    EE[(size_t)AUTO_ADDR] = 0xFF;
    CHECK(play_at_power_on()[0] == 0,
          "an erased byte must mean nothing: \"%s\"", typed);

    printf("17d. a code that is no longer there types nothing\n");
    /* The table can be rewritten without this following it, so a dangling
       code has to be safe rather than impossible. */
    EE[(size_t)AUTO_ADDR] = HDR_MAKE(5, 0x1F);          /* "-----", absent */
    CHECK(play_at_power_on()[0] == 0,
          "a dangling code must type nothing: \"%s\"", typed);
    EE[(size_t)AUTO_ADDR] = 0xFF;

    printf("18. a jump carries on somewhere else\n");
    /* A jump, not a call: nothing after it runs and nothing comes back, so a
       chain of any length needs no stack. The argument is the header plus
       one, because a zero byte ends a macro and the header of "." is zero. */
    table_reset();
    add(".",  { K('a'), ESC_JUMP, (uint8_t)(HDR_MAKE(1, 1) + 1), K('z') });
    add("-",  { K('b') });
    CHECK(!strcmp(play("."), "<00/04><00/05>"),
          "a jumps to b, and what follows the jump never runs: \"%s\"", typed);

    printf("18b. a jump to a code that is not there ends the macro\n");
    table_reset();
    add(".", { K('a'), ESC_JUMP, (uint8_t)(HDR_MAKE(5, 0x1F) + 1), K('z') });
    CHECK(!strcmp(play("."), "<00/04>"), "should stop at the jump: \"%s\"", typed);

    printf("18c. a macro may jump to itself, and any touch stops it\n");
    /* The point of having jumps, and the only question that ever mattered:
       how to stop one. The pad is read between keystrokes, so a loop needs no
       pause in it to remain escapable. */
    table_reset();
    add(".", { K('a'), ESC_JUMP, (uint8_t)(HDR_MAKE(1, 0) + 1) });
    btn_held = 0;
    typedn = 0; typed[0] = 0;
    code_reset();
    code_bits = 0; code_len = 1;
    btn_held = 1;                       /* a finger goes down */
    code_finish();
    CHECK(typedn / 7 < 3, "a touch must stop it almost at once, typed %d",
          typedn / 7);

    printf("18d. ...and that press is not also a symbol\n");
    /* Otherwise stopping a macro would tap a dot, and the dot would fire
       whatever macro has that code. */
    btn_held = 0; loop();
    fake_ms += DEBOUNCE_MS + 1; loop();
    CHECK(code_len == 0, "the press that stopped it must not become a symbol, "
          "got %d", code_len);

    printf("18e. ...but holding it still opens the window\n");
    /* Which is how a runaway gets repaired: stop it, keep holding, and the
       app can reach the table. */
    CHECK(!hidAllowed(), "nothing open yet");
    typedn = 0; typed[0] = 0;
    code_reset();
    code_bits = 0; code_len = 1;
    btn_held = 1;
    code_finish();                      /* stopped by the touch again */
    for (int i = 0; i < 3; i++) { fake_ms += HOLD_MS / 2; loop(); }
    CHECK(hidAllowed(), "holding on after the stop must open the window");
    btn_held = 0; loop();
    fake_ms += 40000; loop();           /* let it lapse */

    printf("18f. a loop that does nothing at all is still bounded\n");
    /* Macros that only jump to each other never reach a keystroke, so the pad
       is never read. The jump count is what stops that one. */
    table_reset();
    add(".", { ESC_JUMP, (uint8_t)(HDR_MAKE(1, 1) + 1) });
    add("-", { ESC_JUMP, (uint8_t)(HDR_MAKE(1, 0) + 1) });
    btn_held = 0;
    typedn = 0; typed[0] = 0;
    code_reset();
    code_bits = 0; code_len = 1;
    code_finish();                      /* must return at all, is the point */
    CHECK(typedn == 0, "it types nothing, and must stop doing so");

    printf("19. the reserved bytes are where the map says\n");
    /* The table stops 32 bytes below the end; the app is told the same
       number, so if this changes both must. */
    CHECK((size_t)TABLE_TOP == 0x1E0, "TABLE_TOP is %03X", (int)(size_t)TABLE_TOP);
    CHECK((size_t)TABLE_TOP - (size_t)TABLE_BASE == 480, "480 bytes of table");
    CHECK(512 - (size_t)TABLE_TOP == 32, "and 32 bytes kept back");
    CHECK((size_t)AUTO_ADDR >= (size_t)TABLE_TOP,
          "the power-on byte must be above the table, not in it");

    printf("\n%s\n", fails ? "SOME CHECKS FAILED" : "all checks passed");
    return fails != 0;
}
