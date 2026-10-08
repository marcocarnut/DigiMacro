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
#define PIN_LED_STUB 1          /* PIN_LED, before the sketch defines it */
/* Read between every step of a macro, which makes it the place to notice
   whether the LED was ever lit while one played. */
static int led_seen = 0;
uint8_t fake_pinb(void){
    if (PORTB & (1 << PIN_LED_STUB)) led_seen = 1;
    return btn_held ? 0x00 : 0xFF;
}
void    fake_charge(void){}

/* captured device behaviour */
static char    typed[4096];
static int     typedn;
void hidBegin(void){}
void hidPoll(void){}
uint8_t hidReady(void){ return 1; }
static uint8_t host_awake = 1;  /* the host has said hello, unless a test says not */
uint8_t hidAwake(void){ return host_awake; }
/* The configuration window the four-second hold opens. */
static uint16_t allow_until; static uint8_t allow_open;
void hidAllow(uint16_t ms){ allow_until = (uint16_t)fake_ms + ms; allow_open = 1; }
uint8_t hidAllowed(void){
    if (allow_open && (int16_t)((uint16_t)fake_ms - allow_until) >= 0) allow_open = 0;
    return allow_open;
}
void hidTypeChar(char, uint8_t){}
void hidType_P(const char*){}

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

    printf("9. an escape this build does not have stops the macro\n");
    /* 0x80 is the one escape value nothing uses yet. A table that holds it
       came from something newer, and guessing at it would be worse than
       stopping. The argument is not zero, so this is not test 8 again. */
    table_reset();
    add(".", { K('a'), 0x80, 0x05, K('b') });
    CHECK(!strcmp(play("."), "<00/04>"),
          "an unknown escape must stop, not guess: \"%s\"", typed);

    printf("10. the walk cannot leave the table\n");
    /* A table with no terminator would otherwise run straight into the
       reserved bytes above it. Every entry here is a "-", so nothing matches, and the walk
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
       EEPROM is the serial number, the host key, the lockout and the
       password; typing those out would be the worst bug this firmware could
       have. */
    memset(EE, 0xFF, sizeof EE);
    EE[0] = HDR_MAKE(1, 0);
    for (uint16_t a = 1; a < (uint16_t)(size_t)TABLE_TOP; a++) EE[a] = K('z');
    for (uint16_t a = (uint16_t)(size_t)TABLE_TOP; a < 512; a++) EE[a] = K('q');
    EE[(size_t)LOCK_SOFT] = 0xFF;   /* or the lockout, reading all those q's
                                       as a password and a pile of wrong
                                       codes, would rightly refuse to play */
    play(".");
    CHECK(strstr(typed, "<00/14>") == NULL,          /* K('q'), the byte above */
          "nothing above the table may be typed: ...\"%s\"", typed + typedn - 21);
    CHECK(typedn == 7 * ((int)(size_t)TABLE_TOP - 1),
          "should have typed the whole table and stopped, %d bytes", typedn);

    printf("11b. an escape in the last table byte takes no argument from above\n");
    /* Its argument would be the serial number's first byte, and a repeat
       count read from there would tell a bystander what that byte is. */
    memset(EE, 0xFF, sizeof EE);
    EE[0] = HDR_MAKE(1, 0);
    EE[1] = K('a');
    for (uint16_t a = 2; a < (uint16_t)(size_t)TABLE_TOP - 1; a++) EE[a] = K('a');
    EE[(size_t)TABLE_TOP - 1] = ESC_REPEAT;     /* the very last table byte */
    EE[(size_t)TABLE_TOP] = 5;                  /* the serial's first byte */
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

    printf("19. the password, which is optional and absent by default\n");
    /* An erased chip has none, and a board that types eleven right-arrows at
       a television should not ask anybody for anything. */
    memset(EE, 0xFF, sizeof EE);
    unlocked = 0;
    CHECK(!hidPassSet(), "an erased chip has no password");
    CHECK(hidUnlocked(), "and is therefore open");

    printf("19b. setting one, and what it then takes to get in\n");
    hidSetPass((const uint8_t *)"abc", 3);
    CHECK(hidPassSet(), "there should be one now");
    CHECK(EE[DM_PASS_ADDR + 3] == 0, "and it should be zero-padded");
    unlocked = 0;
    CHECK(!hidUnlocked(), "which locks it");
    CHECK(!hidUnlock((const uint8_t *)"abd", 3), "a wrong one is refused");
    fake_ms += PASS_PAUSE_MS;
    CHECK(!hidUnlocked(), "and leaves it locked");
    CHECK(!hidUnlock((const uint8_t *)"ab", 2), "a prefix is not enough");
    fake_ms += PASS_PAUSE_MS;
    CHECK(!hidUnlock((const uint8_t *)"abcd", 4), "nor is something longer");
    fake_ms += PASS_PAUSE_MS;
    CHECK(hidUnlock((const uint8_t *)"abc", 3), "the right one gets in");
    CHECK(hidUnlocked(), "and stays in, for the session");

    printf("19b2. a wrong answer costs a second, without stopping the board\n");
    /* It used to wait right there, inside the USB callback, which polled USB
       from inside USB and crashed the board. Now the next answer is turned
       away unseen until the second is up. */
    unlocked = 0;
    CHECK(!hidUnlock((const uint8_t *)"abd", 3), "a wrong one");
    CHECK(!hidUnlock((const uint8_t *)"abc", 3),
          "then even the right one is turned away for a second");
    CHECK(!hidUnlocked(), "and lets nobody in");
    fake_ms += PASS_PAUSE_MS / 2;
    CHECK(!hidUnlock((const uint8_t *)"abc", 3), "half a second is not enough");
    fake_ms += PASS_PAUSE_MS;
    CHECK(hidUnlock((const uint8_t *)"abc", 3), "after a second it is heard");

    printf("19c. ...and how it goes away again\n");
    hidSetPass(0, 0);
    CHECK(!hidPassSet(), "no bytes means no password");
    CHECK(EE[DM_PASS_ADDR] == 0xFF, "and the stored one is really gone");
    unlocked = 0;
    CHECK(hidUnlocked(), "so the board is open again");

    printf("19d. a password does not stop the board being a keyboard\n");
    /* A device that wants a password before it will type is a device nobody
       would carry. The answer to a stolen board is the lockout, not this. */
    hidSetPass((const uint8_t *)"secret", 6);
    unlocked = 0;
    table_reset();
    add(".", { K('a') });
    CHECK(!strcmp(play("."), "<00/04>"),
          "a locked board must still play its macros: \"%s\"", typed);
    hidSetPass(0, 0);
    unlocked = 0;

    /* ---- the lockout ---- */

    /* A wrong code, a right one, and the soft lock's clock run through. */
    auto miss  = [&]() { return play("-----"); };   /* never in these tables */
    auto serve = [&]() {
        for (int i = 0; i < 300; i++) { fake_ms += 1000; loop(); }
    };
    auto lockout = [&](uint8_t soft, uint8_t hard, uint8_t wait) {
        EE[(size_t)LOCK_SOFT] = soft;
        EE[(size_t)LOCK_HARD] = hard;
        EE[(size_t)LOCK_WAIT] = wait;
    };

    printf("21. no lockout on an erased chip, nor without a password\n");
    /* The password is the only thing that lifts a hard lock, so without one
       the lockout must not run whatever the settings say. */
    table_reset();
    add(".", { K('a') });
    unlocked = 0;
    for (int i = 0; i < 10; i++) miss();
    CHECK(!hidLockState(), "an erased chip must never lock");
    CHECK(EE[(size_t)LOCK_MISSES] == 0xFF, "nor even count");
    lockout(1, 1, 1);
    for (int i = 0; i < 10; i++) miss();
    CHECK(!hidLockState(), "no password, no lockout, whatever the settings say");

    printf("21b. too many wrong codes and every tap is ignored, right or wrong\n");
    hidSetPass((const uint8_t *)"secret", 6);
    unlocked = 0;
    lockout(2, 3, 1);                           /* ten seconds */
    miss();
    CHECK(!hidLockState(), "one wrong code of two is allowed");
    CHECK(count_of(LOCK_MISSES) == 1, "and counted, %d", count_of(LOCK_MISSES));
    miss();
    CHECK(hidLockState() == DM_ST_SOFTLOCK, "the second locks it");
    CHECK(count_of(LOCK_SOFTS) == 1, "and counts as a soft lock");
    CHECK(play(".")[0] == 0, "a right code must type nothing now: \"%s\"", typed);

    printf("21c. ...until the wait is served\n");
    loop();                                     /* the clock starts */
    fake_ms += 9000; loop();
    CHECK(hidLockState() == DM_ST_SOFTLOCK, "not before ten seconds");
    fake_ms += 1100; loop();
    CHECK(!hidLockState(), "and over after them");
    CHECK(!strcmp(play("."), "<00/04>"), "taps work again: \"%s\"", typed);
    CHECK(count_of(LOCK_SOFTS) == 0, "and a right code clears the run of locks");

    printf("21d. unplugging does not shorten the wait\n");
    /* No clock runs while it is unplugged, so a soft lock found at power-up
       has to start its whole wait again. */
    miss(); miss();
    loop();
    fake_ms += 8000; loop();
    lock_left = 0;                              /* RAM gone: a replug */
    loop();
    fake_ms += 8000; loop();
    CHECK(hidLockState() == DM_ST_SOFTLOCK, "the wait must start again");
    fake_ms += 2100; loop();
    CHECK(!hidLockState(), "and end ten seconds after the replug");

    printf("21e. soft locks in a row become a hard one\n");
    lock_clear();
    for (int n = 0; n < 2; n++) { miss(); miss(); serve(); }
    CHECK(!hidLockState(), "two soft locks of three is still soft");
    miss(); miss();
    CHECK(hidLockState() == DM_ST_HARDLOCK, "the third is hard");
    serve();
    CHECK(hidLockState() == DM_ST_HARDLOCK, "and no amount of waiting lifts it");
    CHECK(play(".")[0] == 0, "taps still mean nothing: \"%s\"", typed);

    printf("21f. a hard lock stops the power-on macro too\n");
    /* Or a thief would only have to plug it in again. */
    EE[(size_t)AUTO_ADDR] = HDR_MAKE(1, 0);
    CHECK(play_at_power_on()[0] == 0, "nothing may type itself: \"%s\"", typed);

    printf("21g. the password lifts it, and is never refused for the lock\n");
    CHECK(!hidUnlock((const uint8_t *)"wrong", 5), "a wrong one is still wrong");
    fake_ms += PASS_PAUSE_MS;
    CHECK(hidLockState() == DM_ST_HARDLOCK, "and lifts nothing");
    CHECK(hidUnlock((const uint8_t *)"secret", 6), "the right one gets in");
    CHECK(!hidLockState(), "and lifts the lock");
    CHECK(count_of(LOCK_CLEARED) == 1, "which is counted, %d",
          count_of(LOCK_CLEARED));
    CHECK(!strcmp(play_at_power_on(), "<00/04>"), "the power-on macro is back");
    EE[(size_t)AUTO_ADDR] = 0xFF;

    printf("21h. a wrong password counts as a wrong code\n");
    unlocked = 0;
    lock_clear();
    hidUnlock((const uint8_t *)"guess", 5);
    CHECK(count_of(LOCK_MISSES) == 1, "one wrong answer, one miss");
    hidUnlock((const uint8_t *)"guess", 5);
    CHECK(count_of(LOCK_MISSES) == 1,
          "one turned away within the second is not even counted");
    fake_ms += PASS_PAUSE_MS;
    hidUnlock((const uint8_t *)"guess", 5);
    CHECK(hidLockState() == DM_ST_SOFTLOCK, "and two of them lock the taps");
    fake_ms += PASS_PAUSE_MS;
    CHECK(hidUnlock((const uint8_t *)"secret", 6),
          "but the password itself is never shut out");
    CHECK(!hidLockState(), "and giving it ends the soft lock");
    CHECK(count_of(LOCK_CLEARED) == 1,
          "only a hard lock lifted is counted as one");

    printf("21i. the wait is tens of seconds, and nonsense means a minute\n");
    for (uint8_t w : { (uint8_t)0, (uint8_t)241, (uint8_t)0xFF }) {
        lock_clear();
        lockout(1, 0xFF, w);
        miss(); loop();
        fake_ms += 59000; loop();
        CHECK(hidLockState() == DM_ST_SOFTLOCK, "%d: still waiting at 59 s", w);
        fake_ms += 1100; loop();
        CHECK(!hidLockState(), "%d: done after a minute", w);
    }
    lock_clear();
    lockout(1, 0xFF, 240);
    miss(); loop();
    /* Stepped a second at a time, as the board would see it: its clock is
       sixteen bits of milliseconds, so one forty-minute jump would wrap.
       The stub's millis() also ticks on every call, which over this many
       passes adds a few seconds -- hence the margin either side. */
    for (int i = 0; i < 2370; i++) { fake_ms += 1000; loop(); }
    CHECK(hidLockState() == DM_ST_SOFTLOCK, "240 is forty minutes");
    for (int i = 0; i < 30; i++) { fake_ms += 1000; loop(); }
    CHECK(!hidLockState(), "and not much more");

    printf("21j. with no hard threshold, soft locks never become hard\n");
    lockout(1, 0xFF, 1);
    lock_clear();
    for (int n = 0; n < 5; n++) { miss(); serve(); }
    CHECK(!hidLockState(), "0xFF means never");
    CHECK(count_of(LOCK_SOFTS) == 5, "though they are still counted");

    printf("21k. a new password starts the counts again\n");
    miss();
    hidSetPass((const uint8_t *)"other", 5);
    CHECK(count_of(LOCK_MISSES) == 0 && count_of(LOCK_SOFTS) == 0,
          "nothing left over from before");
    CHECK(count_of(LOCK_CLEARED) == 1, "except the record of hard locks lifted");
    hidSetPass(0, 0);
    lockout(0xFF, 0xFF, 0xFF);
    memset(EE + (size_t)LOCK_MISSES, 0xFF, 3);
    unlocked = 0;

    /* ---- the host ---- */

    static const uint8_t KEY[8] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x88 };
    static const uint8_t BAD[8] = { 0x11, 0x22, 0x33, 0x44, 0x55, 0x66, 0x77, 0x89 };
    auto host_forget = [&]() { host_left = 0; host_pausing = 0; };

    printf("22. no host key, no trust: a guarded macro never plays\n");
    table_reset();
    add(".",  { K('a'), ESC_HOST, HOST_SILENT, K('b') });
    add("-",  { ESC_HOST, HOST_LOUD, K('x') });
    add("..", { K('y') });
    host_forget();
    CHECK(!hidHostAuth(KEY, 8), "with no key stored, no key is right");
    CHECK(!strcmp(play("."), "<00/04>"),
          "{host} stops the macro where it stands: \"%s\"", typed);

    printf("22b. a key, and the right host\n");
    memcpy(EE + DM_HOST_ADDR, KEY, 8);
    CHECK(hidHostState() == DM_ST_HOSTKEY, "a key, not yet proved");
    CHECK(hidHostAuth(KEY, 8), "the right key is accepted");
    CHECK(hidHostState() == (DM_ST_HOST | DM_ST_HOSTKEY), "and says so");
    CHECK(!strcmp(play("."), "<00/04><00/05>"), "now it plays: \"%s\"", typed);
    CHECK(!strcmp(play("-"), "<00/1B>"), "and so does {host!}: \"%s\"", typed);

    printf("22c. ...for a minute, by default\n");
    for (int i = 0; i < 55; i++) { fake_ms += 1000; loop(); }
    CHECK(hidHostState() & DM_ST_HOST, "still trusted at 55 s");
    for (int i = 0; i < 10; i++) { fake_ms += 1000; loop(); }
    CHECK(!(hidHostState() & DM_ST_HOST), "not after a minute");
    CHECK(!strcmp(play("."), "<00/04>"), "and the guard holds again: \"%s\"", typed);
    EE[0x1F2] = 1;                              /* ten seconds */
    CHECK(hidHostAuth(KEY, 8), "proved again");
    for (int i = 0; i < 11; i++) { fake_ms += 1000; loop(); }
    CHECK(!(hidHostState() & DM_ST_HOST), "the wait is the configured one");
    EE[0x1F2] = 0xFF;

    printf("22d. a wrong key costs a second, and nothing else\n");
    /* Nothing else meaning the lockout: anything on any computer may send a
       key, and if wrong ones counted it could lock the board out remotely. */
    hidSetPass((const uint8_t *)"secret", 6);
    lockout(2, 3, 1);
    lock_clear();
    CHECK(!hidHostAuth(BAD, 8), "a wrong key is refused");
    CHECK(!hidHostAuth(KEY, 8), "and even the right one, for a second");
    CHECK(count_of(LOCK_MISSES) == 0, "a wrong key is not a wrong code");
    fake_ms += PASS_PAUSE_MS;
    CHECK(!hidHostAuth(KEY, 7), "a short key is no key");
    CHECK(hidHostAuth(KEY, 8), "the right one, once the second is up");
    host_forget();

    printf("22e. {host!} refused is a wrong code, exactly\n");
    lock_clear();
    CHECK(play("-")[0] == 0, "nothing typed: \"%s\"", typed);
    CHECK(count_of(LOCK_MISSES) == 1, "and one miss, as for an empty code");
    play(".");
    CHECK(count_of(LOCK_MISSES) == 1, "a quiet {host} neither counts...");
    play("-");
    CHECK(hidLockState() == DM_ST_SOFTLOCK, "two loud refusals lock it, as two misses would");

    printf("22e2. ...and the LED does not give it away\n");
    /* Found and refused must look like not found, and a flash of the LED on
       the way in would be the difference. */
    lock_clear();
    led_seen = 0;
    PORTB = 0;
    macro_play(find_header(HDR_MAKE(1, 1)));
    CHECK(!led_seen, "the LED must stay dark until something shows");
    macro_play(find_header(HDR_MAKE(2, 0)));
    CHECK(led_seen, "and come on for a macro that types");

    printf("22f. a guarded code does not wipe a guesser's misses\n");
    /* Found is not played. If finding a code cleared the counts, tapping a
       guarded one on the wrong computer would reset them for the guesser,
       and the hard lock would never come. */
    lock_clear();
    miss();
    CHECK(count_of(LOCK_MISSES) == 1, "one miss");
    play(".");                                  /* quiet refusal */
    CHECK(count_of(LOCK_MISSES) == 1, "a quiet refusal leaves it be");
    lock_clear();
    for (int n = 0; n < 2; n++) { miss(); miss(); serve(); }
    play(".");
    CHECK(count_of(LOCK_SOFTS) == 2, "and the run of soft locks stands, %d",
          count_of(LOCK_SOFTS));
    play("..");
    CHECK(count_of(LOCK_SOFTS) == 0, "until a code really plays");

    printf("22f2. ...and a guarded code that plays is a code that played\n");
    lock_clear();
    miss();
    CHECK(hidHostAuth(KEY, 8), "the right host");
    CHECK(!strcmp(play("-"), "<00/1B>"), "{host!} passes and types: \"%s\"", typed);
    CHECK(count_of(LOCK_MISSES) == 0, "and clears the misses, as any code that works");
    host_forget();

    printf("22g. a jump into a guarded macro is checked there\n");
    table_reset();
    add(".", { K('a'), ESC_JUMP, (uint8_t)(HDR_MAKE(1, 1) + 1) });
    add("-", { ESC_HOST, HOST_SILENT, K('x') });
    host_forget();
    CHECK(!strcmp(play("."), "<00/04>"), "the guard holds after a jump: \"%s\"", typed);

    hidSetPass(0, 0);
    lockout(0xFF, 0xFF, 0xFF);
    memset(EE + (size_t)LOCK_MISSES, 0xFF, 3);
    memset(EE + DM_HOST_ADDR, 0xFF, 8);
    host_forget();
    unlocked = 0;

    printf("23. the reserved bytes are where the map says\n");
    /* The table stops 32 bytes below the end; the app is told the same
       number, so if this changes both must. */
    CHECK((size_t)TABLE_TOP == 0x1E0, "TABLE_TOP is %03X", (int)(size_t)TABLE_TOP);
    CHECK((size_t)TABLE_TOP - (size_t)TABLE_BASE == 480, "480 bytes of table");
    CHECK(512 - (size_t)TABLE_TOP == 32, "and 32 bytes kept back");
    CHECK((size_t)AUTO_ADDR >= (size_t)TABLE_TOP,
          "the power-on byte must be above the table, not in it");
    CHECK(DM_PASS_ADDR >= (size_t)TABLE_TOP,
          "and so must the password");
    CHECK(DM_PASS_ADDR + DM_PASS_LEN == 512, "which ends where the EEPROM does");
    CHECK(DM_SERIAL_ADDR >= (size_t)TABLE_TOP,
          "and the serial number, which the table must never reach");
    CHECK((size_t)LOCK_SOFT >= DM_SERIAL_ADDR + 4 &&
          (size_t)LOCK_CLEARED < (size_t)AUTO_ADDR,
          "the lockout sits between the serial and the power-on byte");
    CHECK(DM_HOST_ADDR == DM_SERIAL_ADDR + 4 &&
          DM_HOST_ADDR + DM_HOST_LEN == (size_t)LOCK_SOFT,
          "the host key fills the gap between the serial and the lockout");
    CHECK(0x1F2 > (size_t)LOCK_CLEARED && 0x1F2 < (size_t)AUTO_ADDR,
          "and its wait the byte between the lockout and the power-on byte");

    printf("\n%s\n", fails ? "SOME CHECKS FAILED" : "all checks passed");
    return fails != 0;
}
