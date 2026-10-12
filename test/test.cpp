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
void hidLock(void);
uint8_t hidAllowed(void){
    if (allow_open && (int16_t)((uint16_t)fake_ms - allow_until) >= 0) {
        allow_open = 0;
        hidLock();              /* as app/hidusb.c does: the session ends */
    }
    return allow_open;
}
void hidTypeChar(char, uint8_t){}

/* Every keystroke, modifiers and all, in the order they were sent. Hex
   rather than letters so that a modifier or an odd usage cannot hide. */
/* A finger that arrives after so many keystrokes, for the tests that need
   one part way through a macro: -1 for never. */
static int stop_after = -1;

void hidKey(uint8_t mod, uint8_t key)
{
    if (typedn < (int)sizeof typed - 16)
        typedn += sprintf(typed + typedn, "<%02X/%02X>", mod, key);
    if (stop_after > 0 && --stop_after == 0) btn_held = 1;
}

#include "DigiMacro.ino"

/* ---- the core's password, and the app library's verbs --------------

   Stand-ins with the same behaviour as core/core.c and app/hidusb.c, which
   core/test runs for real: compared in full, a wrong one turning the next
   away unread for a second, the session ended by the window. The core
   counts its second in turns of its loop; here it is the clock. */
static uint8_t unlocked;
static uint8_t pass_waiting;
static unsigned long pass_wrong_at;
#define PASS_PAUSE_MS 1000

uint8_t hidPassSet(void) { return EE[DM_PASS_ADDR] != 0xFF; }
uint8_t hidUnlocked(void) { return !hidPassSet() || unlocked; }
void    hidLock(void) { unlocked = 0; }
uint8_t hidUnlock(const uint8_t *p, uint8_t n)
{
    uint8_t diff = 0;
    if (!hidPassSet()) return DM_PASS_RIGHT;
    if (pass_waiting && fake_ms - pass_wrong_at < PASS_PAUSE_MS)
        return DM_PASS_UNCHECKED;
    pass_waiting = 0;
    if (n > DM_PASS_LEN) return DM_PASS_UNCHECKED;
    for (uint8_t i = 0; i < DM_PASS_LEN; i++)
        diff |= EE[DM_PASS_ADDR + i] ^ (i < n ? p[i] : 0);
    if (diff) { pass_waiting = 1; pass_wrong_at = fake_ms; return DM_PASS_WRONG; }
    unlocked = 1;
    return DM_PASS_RIGHT;
}

/* UNLOCK and SETPASS as app/hidusb.c answers them, gates aside: what the
   sketch hears of a password is through these. */
static uint8_t unlock(const uint8_t *p, uint8_t n)
{
    uint8_t r = hidUnlock(p, n);
    if (r == DM_PASS_RIGHT) { if (hidPassSet()) hidPassRight(); return 1; }
    if (r == DM_PASS_WRONG) hidPassWrong();
    return 0;
}

static void setpass(const uint8_t *p, uint8_t n)
{
    for (uint8_t i = 0; i < DM_PASS_LEN; i++)
        EE[DM_PASS_ADDR + i] = n == 0 ? 0xFF : (i < n ? p[i] : 0);
    hidPassChanged();
    hidUnlock(p, n);
}

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

/* ---- secrets, sealed as the page seals them (web/table.js), from the
   test's own XTEA: the nonce drawn again until a walk keeps its place ---- */

static int walk_safe(const uint8_t *b, int n)
{
    int i = 0;
    for (int k = 0; k < n; k++) if (!b[k]) return 0;
    while (i < n) {
        if (b[i] == ESC_IR) return 0;
        i += IS_ESC(b[i]) && b[i] != ESC_MARK ? 2 : 1;
    }
    return i == n;
}

static void add_secret(const char *code, std::initializer_list<uint8_t> before,
                       int loud, std::initializer_list<uint8_t> plain,
                       const uint8_t *key)
{
    uint8_t len, bits = code_of(code, &len), p[260];
    int pn = 0, n;

    for (uint8_t b : plain) p[pn++] = b;
    p[pn++] = 0;
    EE[tw++] = HDR_MAKE(len, bits);
    for (uint8_t b : before) EE[tw++] = b;
    EE[tw++] = ESC_JUMP;
    EE[tw++] = loud ? SECRET_LOUD : SECRET_QUIET;
    for (uint32_t nonce = 0x01010101; ; nonce++) {
        uint8_t *s = EE + tw;
        for (int i = 0; i < 4; i++) s[i] = nonce >> (8 * i);
        for (n = 0; n < pn; n++) {
            uint8_t blk[8] = { s[0], s[1], s[2], s[3], (uint8_t)n, 0, 0, 0 };
            xtea(blk, key);
            s[4 + n] = p[n] ^ blk[0];
        }
        if (walk_safe(s, 4 + pn)) break;
    }
    tw += 4 + pn;
    EE[tw++] = 0;
    EE[tw] = 0xFF;
}

/* The key's check value, where the page writes it */
static void set_check(const uint8_t *key)
{
    uint8_t b[8] = { 0 };
    xtea(b, key);
    memcpy(EE + DM_CHECK_ADDR, b, 8);
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

/* ---- tapping, through the pin and app_loop() ---- */

/* A level is believed only once it has held for DEBOUNCE_MS, so each edge
   takes two passes: one that sees it and one, later, that accepts it. */
static void tap(int ms)
{
    btn_held = 1; app_loop();
    fake_ms += DEBOUNCE_MS + 1; app_loop();                 /* press accepted  */
    fake_ms += ms;
    btn_held = 0; app_loop();
    fake_ms += DEBOUNCE_MS + 1; app_loop();                 /* release accepted */
}
#define DOT()   tap(DOT_MAX_MS - 100)
#define DASH()  tap(DOT_MAX_MS + 100)

static const char *silence(void)
{
    typedn = 0; typed[0] = 0;
    fake_ms += END_OF_CODE_MS + 10;
    app_loop();
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

    printf("9. a remote's place is skipped when played through, and a bad one stops\n");
    /* 0x80 was the last escape value free, and the remote has it: three
       bytes, each with bit 7 set. One without would have the walk step over
       the macro's end, so it stops instead. */
    table_reset();
    add(".", { K('a'), ESC_IR, 0x81, 0xC5, 0x82, K('b') });
    CHECK(!strcmp(play("."), "<00/04><00/05>"),
          "an {IR} types nothing: \"%s\"", typed);
    table_reset();
    add(".", { K('a'), ESC_IR, 0x05, K('b') });
    add("-", { K('c') });
    CHECK(!strcmp(play("."), "<00/04>"),
          "a short {IR} must stop, not run on into \"-\": \"%s\"", typed);

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
       EEPROM is the serial number, the key check, the lockout and the
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
    btn_held = 1; app_loop();
    fake_ms += DEBOUNCE_MS + 1; app_loop();         /* the press is accepted */
    fake_ms += 60;
    btn_held = 0; app_loop();                       /* one poll of nothing... */
    CHECK(in_down, "a single dropped sample must not end a press -- under the "
                   "old rule it ended it at once, and the press that followed "
                   "became a second symbol");
    fake_ms += 1;
    btn_held = 1; app_loop();                       /* ...and it is back */
    fake_ms += 60;
    btn_held = 0; app_loop();
    fake_ms += DEBOUNCE_MS + 1; app_loop();
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
    btn_held = 1; app_loop();
    fake_ms += DEBOUNCE_MS + 1; app_loop();
    fake_ms += HOLD_MS + 10; app_loop();
    CHECK(hidAllowed(), "the hold should have opened the window");
    btn_held = 0; app_loop();
    fake_ms += DEBOUNCE_MS + 1; app_loop();
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
        app_loop();                             /* notices the host is there */
        fake_ms += AUTO_SETTLE_MS + 10;
        app_loop();                             /* and now it may type */
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
    app_loop();
    CHECK(typedn == 0, "nothing may be typed the instant it is enumerated");
    fake_ms += AUTO_SETTLE_MS - 20; app_loop();
    CHECK(typedn == 0, "nor before the settling time is up");
    fake_ms += 40; app_loop();
    CHECK(typedn > 0, "and then it should type");

    printf("17b2. a host that never says hello is waited for, and given up on\n");
    host_awake = 0;
    typedn = 0; typed[0] = 0;
    FLAGS &= ~(FLAG_AUTODONE | FLAG_AUTOWAIT);
    app_loop();
    fake_ms += AUTO_SETTLE_MS + 10; app_loop();
    CHECK(typedn == 0, "the short wait is for a host that answered");
    fake_ms += AUTO_WAIT_MS; app_loop();
    CHECK(typedn > 0, "but it must not wait for ever: \"%s\"", typed);
    host_awake = 1;

    printf("17b. ...once, and only once\n");
    typedn = 0; typed[0] = 0;
    for (int i = 0; i < 5; i++) app_loop();
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
    btn_held = 0; app_loop();
    fake_ms += DEBOUNCE_MS + 1; app_loop();
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
    for (int i = 0; i < 3; i++) { fake_ms += HOLD_MS / 2; app_loop(); }
    CHECK(hidAllowed(), "holding on after the stop must open the window");
    btn_held = 0; app_loop();
    fake_ms += 40000; app_loop();           /* let it lapse */

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

    printf("18g. a long macro does not reopen the window\n");
    /* The window's expiry is a sixteen-bit comparison of milliseconds, so it
       has to be asked about more often than every 32 seconds, or an expired
       window reads as open again -- unlock and all. Three 25.5-second pauses
       put the first chance to ask 46 seconds past the expiry, squarely in
       the half of the cycle where the old code got it wrong. */
    table_reset();
    add(".", { ESC_PAUSE, 255, ESC_PAUSE, 255, ESC_PAUSE, 255, K('a') });
    btn_held = 0;
    hidAllow(CONFIG_MS);
    unlocked = 1;
    play(".");
    CHECK(!strcmp(typed, "<00/04>"), "the macro plays through: \"%s\"", typed);
    CHECK(!hidAllowed(), "the window must be shut after 76 seconds");
    CHECK(unlocked == 0, "and the unlock gone with it");

    printf("18h. a repeat of hundreds can be stopped like anything else\n");
    /* A finger arrives part way through: the stub's pin reads it from the
       next poll on, so count how many keystrokes went out before that. */
    table_reset();
    add(".", { K('a'), ESC_REPEAT, 200 });
    btn_held = 0;
    typedn = 0; typed[0] = 0;
    code_reset();
    code_bits = 0; code_len = 1;
    stop_after = 20;
    code_finish();
    stop_after = -1;
    CHECK(typedn / 7 < 40, "it must stop near where the touch came, typed %d",
          typedn / 7);
    btn_held = 0; app_loop();
    fake_ms += DEBOUNCE_MS + 1; app_loop();

    printf("19. the password, which is optional and absent by default\n");
    /* An erased chip has none, and a board that types eleven right-arrows at
       a television should not ask anybody for anything. */
    memset(EE, 0xFF, sizeof EE);
    unlocked = 0;
    CHECK(!hidPassSet(), "an erased chip has no password");
    CHECK(hidUnlocked(), "and is therefore open");

    printf("19b. setting one, and what it then takes to get in\n");
    setpass((const uint8_t *)"abc", 3);
    CHECK(hidPassSet(), "there should be one now");
    CHECK(EE[DM_PASS_ADDR + 3] == 0, "and it should be zero-padded");
    unlocked = 0;
    CHECK(!hidUnlocked(), "which locks it");
    CHECK(!unlock((const uint8_t *)"abd", 3), "a wrong one is refused");
    fake_ms += PASS_PAUSE_MS;
    CHECK(!hidUnlocked(), "and leaves it locked");
    CHECK(!unlock((const uint8_t *)"ab", 2), "a prefix is not enough");
    fake_ms += PASS_PAUSE_MS;
    CHECK(!unlock((const uint8_t *)"abcd", 4), "nor is something longer");
    fake_ms += PASS_PAUSE_MS;
    CHECK(unlock((const uint8_t *)"abc", 3), "the right one gets in");
    CHECK(hidUnlocked(), "and stays in, for the session");

    printf("19b2. a wrong answer costs a second, without stopping the board\n");
    /* It used to wait right there, inside the USB callback, which polled USB
       from inside USB and crashed the board. Now the next answer is turned
       away unseen until the second is up. */
    unlocked = 0;
    CHECK(!unlock((const uint8_t *)"abd", 3), "a wrong one");
    CHECK(!unlock((const uint8_t *)"abc", 3),
          "then even the right one is turned away for a second");
    CHECK(!hidUnlocked(), "and lets nobody in");
    fake_ms += PASS_PAUSE_MS / 2;
    CHECK(!unlock((const uint8_t *)"abc", 3), "half a second is not enough");
    fake_ms += PASS_PAUSE_MS;
    CHECK(unlock((const uint8_t *)"abc", 3), "after a second it is heard");

    printf("19c. ...and how it goes away again\n");
    setpass(0, 0);
    CHECK(!hidPassSet(), "no bytes means no password");
    CHECK(EE[DM_PASS_ADDR] == 0xFF, "and the stored one is really gone");
    unlocked = 0;
    CHECK(hidUnlocked(), "so the board is open again");

    printf("19d. a password does not stop the board being a keyboard\n");
    /* A device that wants a password before it will type is a device nobody
       would carry. The answer to a stolen board is the lockout, not this. */
    setpass((const uint8_t *)"secret", 6);
    unlocked = 0;
    table_reset();
    add(".", { K('a') });
    CHECK(!strcmp(play("."), "<00/04>"),
          "a locked board must still play its macros: \"%s\"", typed);
    setpass(0, 0);
    unlocked = 0;

    /* ---- the lockout ---- */

    /* A wrong code, a right one, and the soft lock's clock run through. */
    auto miss  = [&]() { return play("-----"); };   /* never in these tables */
    auto serve = [&]() {
        for (int i = 0; i < 300; i++) { fake_ms += 1000; app_loop(); }
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
    setpass((const uint8_t *)"secret", 6);
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
    app_loop();                                     /* the clock starts */
    fake_ms += 9000; app_loop();
    CHECK(hidLockState() == DM_ST_SOFTLOCK, "not before ten seconds");
    fake_ms += 1100; app_loop();
    CHECK(!hidLockState(), "and over after them");
    CHECK(!strcmp(play("."), "<00/04>"), "taps work again: \"%s\"", typed);
    CHECK(count_of(LOCK_SOFTS) == 0, "and a right code clears the run of locks");

    printf("21d. unplugging does not shorten the wait\n");
    /* No clock runs while it is unplugged, so a soft lock found at power-up
       has to start its whole wait again. */
    miss(); miss();
    app_loop();
    fake_ms += 8000; app_loop();
    lock_left = 0;                              /* RAM gone: a replug */
    app_loop();
    fake_ms += 8000; app_loop();
    CHECK(hidLockState() == DM_ST_SOFTLOCK, "the wait must start again");
    fake_ms += 2100; app_loop();
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
    CHECK(!unlock((const uint8_t *)"wrong", 5), "a wrong one is still wrong");
    fake_ms += PASS_PAUSE_MS;
    CHECK(hidLockState() == DM_ST_HARDLOCK, "and lifts nothing");
    CHECK(unlock((const uint8_t *)"secret", 6), "the right one gets in");
    CHECK(!hidLockState(), "and lifts the lock");
    CHECK(count_of(LOCK_CLEARED) == 1, "which is counted, %d",
          count_of(LOCK_CLEARED));
    CHECK(!strcmp(play_at_power_on(), "<00/04>"), "the power-on macro is back");
    EE[(size_t)AUTO_ADDR] = 0xFF;

    printf("21h. a wrong password counts as a wrong code\n");
    unlocked = 0;
    lock_clear();
    unlock((const uint8_t *)"guess", 5);
    CHECK(count_of(LOCK_MISSES) == 1, "one wrong answer, one miss");
    unlock((const uint8_t *)"guess", 5);
    CHECK(count_of(LOCK_MISSES) == 1,
          "one turned away within the second is not even counted");
    fake_ms += PASS_PAUSE_MS;
    unlock((const uint8_t *)"guess", 5);
    CHECK(hidLockState() == DM_ST_SOFTLOCK, "and two of them lock the taps");
    fake_ms += PASS_PAUSE_MS;
    CHECK(unlock((const uint8_t *)"secret", 6),
          "but the password itself is never shut out");
    CHECK(!hidLockState(), "and giving it ends the soft lock");
    CHECK(count_of(LOCK_CLEARED) == 1,
          "only a hard lock lifted is counted as one");

    printf("21i. the wait is tens of seconds, and nonsense means a minute\n");
    for (uint8_t w : { (uint8_t)0, (uint8_t)241, (uint8_t)0xFF }) {
        lock_clear();
        lockout(1, 0xFF, w);
        miss(); app_loop();
        fake_ms += 59000; app_loop();
        CHECK(hidLockState() == DM_ST_SOFTLOCK, "%d: still waiting at 59 s", w);
        fake_ms += 1100; app_loop();
        CHECK(!hidLockState(), "%d: done after a minute", w);
    }
    lock_clear();
    lockout(1, 0xFF, 240);
    miss(); app_loop();
    /* Stepped a second at a time, as the board would see it: its clock is
       sixteen bits of milliseconds, so one forty-minute jump would wrap.
       The stub's millis() also ticks on every call, which over this many
       passes adds a few seconds -- hence the margin either side. */
    for (int i = 0; i < 2370; i++) { fake_ms += 1000; app_loop(); }
    CHECK(hidLockState() == DM_ST_SOFTLOCK, "240 is forty minutes");
    for (int i = 0; i < 30; i++) { fake_ms += 1000; app_loop(); }
    CHECK(!hidLockState(), "and not much more");

    printf("21j. with no hard threshold, soft locks never become hard\n");
    lockout(1, 0xFF, 1);
    lock_clear();
    for (int n = 0; n < 5; n++) { miss(); serve(); }
    CHECK(!hidLockState(), "0xFF means never");
    CHECK(count_of(LOCK_SOFTS) == 5, "though they are still counted");

    printf("21k. a new password starts the counts again\n");
    miss();
    setpass((const uint8_t *)"other", 5);
    CHECK(count_of(LOCK_MISSES) == 0 && count_of(LOCK_SOFTS) == 0,
          "nothing left over from before");
    CHECK(count_of(LOCK_CLEARED) == 1, "except the record of hard locks lifted");
    setpass(0, 0);
    lockout(0xFF, 0xFF, 0xFF);
    memset(EE + (size_t)LOCK_MISSES, 0xFF, 3);
    unlocked = 0;

    /* ---- secrets ---- */

    static const uint8_t SKEY[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16 };
    static const uint8_t SBAD[16] = { 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 17 };
    auto key_forget = [&]() { hidKeyForget(); key_pausing = 0; };
    auto key_held = [&]() {
        for (uint8_t b : sec.key) if (b) return 1;
        return 0;
    };

    printf("22. no key, no secret: the rest of the macro never plays\n");
    table_reset();
    add_secret(".",  { K('a') }, 0, { K('b'), K('c') }, SKEY);
    add_secret("-",  { }, 1, { K('x') }, SKEY);
    add("..", { K('y') });
    key_forget();
    CHECK(!hidKeyAuth(SKEY, 16), "with no check value stored, no key is right");
    key_forget();
    set_check(SKEY);
    CHECK(!strcmp(play("."), "<00/04>"),
          "{secret} stops the macro where it stands: \"%s\"", typed);
    CHECK(!strcmp(play(".."), "<00/1C>"), "and the walk finds what follows: \"%s\"", typed);

    printf("22b. the right key\n");
    CHECK(hidKeyState() == 0, "no key held");
    CHECK(hidKeyAuth(SKEY, 16), "the right key is accepted");
    CHECK(hidKeyState() == DM_ST_KEY, "and says so");
    CHECK(!strcmp(play("."), "<00/04><00/05><00/06>"), "now it plays: \"%s\"", typed);
    CHECK(!strcmp(play("-"), "<00/1B>"), "and so does {secret!}: \"%s\"", typed);
    for (int n = 0; n < 2; n++) {
        bool plain = false;
        for (int a = 0; a < (int)(size_t)TABLE_TOP - 1; a++)
            if (EE[a] == K('b') && EE[a + 1] == K('c')) plain = true;
        CHECK(!plain, "the secret is not in the EEPROM as it is typed");
    }

    printf("22c. ...for a minute, by default, and then forgotten\n");
    for (int i = 0; i < 55; i++) { fake_ms += 1000; app_loop(); }
    CHECK(hidKeyState() & DM_ST_KEY, "still held at 55 s");
    for (int i = 0; i < 10; i++) { fake_ms += 1000; app_loop(); }
    CHECK(!(hidKeyState() & DM_ST_KEY), "not after a minute");
    CHECK(!key_held(), "and gone from RAM, not just ignored");
    CHECK(!strcmp(play("."), "<00/04>"), "the secret is shut again: \"%s\"", typed);
    EE[0x1F2] = 1;                              /* ten seconds */
    CHECK(hidKeyAuth(SKEY, 16), "given again");
    for (int i = 0; i < 11; i++) { fake_ms += 1000; app_loop(); }
    CHECK(!(hidKeyState() & DM_ST_KEY), "the wait is the configured one");
    EE[0x1F2] = 0xFF;
    CHECK(hidKeyAuth(SKEY, 16), "given again");
    hidKeyForget();
    CHECK(!key_held() && !hidKeyState(), "a restart or a WRITE forgets it");

    printf("22d. a wrong key costs a second, and nothing else\n");
    /* Nothing else meaning the lockout: anything on any computer may send a
       key, and if wrong ones counted it could lock the board out remotely. */
    setpass((const uint8_t *)"secret", 6);
    lockout(2, 3, 1);
    lock_clear();
    CHECK(!hidKeyAuth(SBAD, 16), "a wrong key is refused");
    CHECK(!hidKeyAuth(SKEY, 16), "and even the right one, for a second");
    CHECK(count_of(LOCK_MISSES) == 0, "a wrong key is not a wrong code");
    CHECK(!key_held(), "and nothing of it is kept");
    fake_ms += PASS_PAUSE_MS;
    CHECK(!hidKeyAuth(SKEY, 15), "a short key is no key");
    CHECK(hidKeyAuth(SKEY, 16), "the right one, once the second is up");
    key_forget();

    printf("22e. {secret!} refused is a wrong code, exactly\n");
    lock_clear();
    CHECK(play("-")[0] == 0, "nothing typed: \"%s\"", typed);
    CHECK(count_of(LOCK_MISSES) == 1, "and one miss, as for an empty code");
    play(".");
    CHECK(count_of(LOCK_MISSES) == 1, "a quiet {secret} neither counts...");
    play("-");
    CHECK(hidLockState() == DM_ST_SOFTLOCK, "two loud refusals lock it, as two misses would");

    printf("22e2. ...and the LED does not give it away\n");
    lock_clear();
    led_seen = 0;
    PORTB = 0;
    macro_play(find_header(HDR_MAKE(1, 1)));
    CHECK(!led_seen, "the LED must stay dark until something shows");
    macro_play(find_header(HDR_MAKE(2, 0)));
    CHECK(led_seen, "and come on for a macro that types");

    printf("22f. a secret code does not wipe a guesser's misses\n");
    lock_clear();
    miss();
    play(".");                                  /* quiet refusal, after an 'a' */
    CHECK(count_of(LOCK_MISSES) == 1, "a quiet refusal leaves them be");
    lock_clear();
    for (int n = 0; n < 2; n++) { miss(); miss(); serve(); }
    play("-");
    play(".");
    CHECK(count_of(LOCK_SOFTS) >= 2, "and the run of soft locks stands, %d",
          count_of(LOCK_SOFTS));
    lock_clear();
    miss();
    CHECK(hidKeyAuth(SKEY, 16), "the key");
    CHECK(!strcmp(play("-"), "<00/1B>"), "{secret!} opens and types: \"%s\"", typed);
    CHECK(count_of(LOCK_MISSES) == 0, "and clears the misses, as any code that works");
    key_forget();

    printf("22g. jumps: into a secret it is checked there, out of one in the clear\n");
    table_reset();
    add(".", { K('a'), ESC_JUMP, (uint8_t)(HDR_MAKE(1, 1) + 1) });
    add_secret("-", { }, 0, { K('x'), ESC_JUMP, (uint8_t)(HDR_MAKE(2, 0) + 1) }, SKEY);
    add("..", { K('y') });
    set_check(SKEY);
    CHECK(!strcmp(play("."), "<00/04>"), "the secret holds after a jump: \"%s\"", typed);
    CHECK(hidKeyAuth(SKEY, 16), "the key");
    CHECK(!strcmp(play("."), "<00/04><00/1B><00/1C>"),
          "and opened, jumps out of it into the clear: \"%s\"", typed);

    printf("22h. repeats, groups, modifiers and pauses inside a secret\n");
    table_reset();
    add_secret(".", { K('q') }, 0, { K('a'), ESC_REPEAT, 2, ESC_MARK, K('b'), K('c'),
                                     ESC_REPEAT, 1, ESC_MOD, 0x01, K('d'),
                                     ESC_PAUSE, 1, KS('e') }, SKEY);
    set_check(SKEY);
    CHECK(hidKeyAuth(SKEY, 16), "the key");
    CHECK(!strcmp(play("."), "<00/14><00/04><00/04><00/04><00/05><00/06><00/05><00/06>"
                             "<01/07><02/08>"),
          "as they play in the clear: \"%s\"", typed);

    printf("22i. a key that runs out part way through stops the secret there\n");
    table_reset();
    add_secret(".", { }, 0, { K('a'), ESC_PAUSE, 30, K('b') }, SKEY);
    set_check(SKEY);
    EE[0x1F2] = 1;                              /* ten seconds; the pause, three */
    CHECK(hidKeyAuth(SKEY, 16), "the key");
    fake_ms += 8000;
    CHECK(!strcmp(play("."), "<00/04>"),
          "what comes after the key went is never typed: \"%s\"", typed);
    CHECK(!key_held(), "and the key went");
    EE[0x1F2] = 0xFF;

    printf("22j. PROVE: XTEA under the check value, of the client's bytes\n");
    {
        uint8_t c[8] = { 0xC0, 0xFF, 0xEE, 0, 1, 2, 3, 4 }, want[8], k[16];
        memcpy(want, c, 8);
        for (int i = 0; i < 16; i++) k[i] = EE[DM_CHECK_ADDR + (i & 7)];
        xtea(want, k);
        CHECK(hidProve(c, 8) && !memcmp(c, want, 8), "the answer a client computes");
        CHECK(!hidProve(c, 7), "eight bytes, no fewer");
        set_check(SBAD);
        memcpy(c, want, 8);
        hidProve(c, 8);
        CHECK(memcmp(c, want, 8), "and another key's check value answers otherwise");
    }

    printf("22k. v2's {host} is refused, quietly or loudly, key or no key\n");
    table_reset();
    add(".", { K('a'), ESC_HOST, HOST_SILENT, K('b') });
    add("-", { ESC_HOST, HOST_LOUD, K('x') });
    add_secret("..", { }, 0, { K('y') }, SKEY);
    set_check(SKEY);
    CHECK(hidKeyAuth(SKEY, 16), "the key");
    setpass((const uint8_t *)"secret", 6);  /* table_reset() took both */
    lockout(2, 3, 1);
    lock_clear();
    CHECK(!strcmp(play("."), "<00/04>"), "{host} stops: \"%s\"", typed);
    CHECK(play("-")[0] == 0 && count_of(LOCK_MISSES) == 1, "{host!} is a wrong code");
    key_forget();

    setpass(0, 0);
    lockout(0xFF, 0xFF, 0xFF);
    memset(EE + (size_t)LOCK_MISSES, 0xFF, 3);
    memset(EE + DM_CHECK_ADDR, 0xFF, 8);
    key_forget();
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
    CHECK(DM_CHECK_ADDR == DM_SERIAL_ADDR + 4 &&
          DM_CHECK_ADDR + 8 == (size_t)LOCK_SOFT,
          "the check value fills the gap between the serial and the lockout");
    CHECK(0x1F2 > (size_t)LOCK_CLEARED && 0x1F2 < (size_t)AUTO_ADDR,
          "and its wait the byte between the lockout and the power-on byte");

    printf("24. the remote\n");
    /* Through the interrupt, as the board takes it: each falling edge with
       Timer0's count since the last. */
    {
        auto edge = [](uint8_t t, bool wrapped) {
            TCNT0 = t; TIFR = wrapped ? 1 << TOV0 : 0; isr_int0();
        };
        auto frame = [&](uint8_t a, uint8_t na, uint8_t c, uint8_t nc) {
            uint32_t f = a | (uint32_t)na << 8 | (uint32_t)c << 16 | (uint32_t)nc << 24;
            edge(0, true);                      /* out of silence */
            edge(IR_T(13500), false);           /* 9 + 4.5 ms */
            for (int i = 0; i < 32; i++)
                edge((f >> i) & 1 ? IR_T(2250) : IR_T(1125), false);
        };
        auto press = [&](uint8_t a, uint8_t c) {
            typedn = 0; typed[0] = 0;
            frame(a, (uint8_t)~a, c, (uint8_t)~c);
            app_loop();
            return (const char *)typed;
        };
        /* {IR a c}, as table.js writes it */
        #define IRTAG(a, c) ESC_IR, (uint8_t)((a) | 0x80), (uint8_t)((c) | 0x80), \
            (uint8_t)(0x80 | ((a) >> 7) | (((c) >> 7) << 1))

        table_reset();
        add(".", { K('a'), IRTAG(0x00, 0x45), K('b'), K('c') });
        add("-", { IRTAG(0x80, 0xC5), K('d'), IRTAG(0x00, 0x46), K('e') });
        CHECK(!strcmp(press(0x00, 0x45), "<00/05><00/06>"),
              "a button starts just after its {IR}: \"%s\"", typed);
        CHECK(!strcmp(press(0x00, 0x46), "<00/08>"),
              "...wherever in a macro it is: \"%s\"", typed);
        CHECK(!strcmp(press(0x80, 0xC5), "<00/07><00/08>"),
              "...with the high bits, and on through the next {IR}: \"%s\"", typed);
        CHECK(!strcmp(press(0x00, 0xC5), ""), "a different address is a different button");
        CHECK(!strcmp(press(0x80, 0x45), ""), "and the high bits count: \"%s\"", typed);
        CHECK(!strcmp(press(0x01, 0x45), ""), "nothing for a button not there");
        CHECK(!strcmp(play("."), "<00/04><00/05><00/06>"),
              "tapped, the macro plays whole, {IR} and all: \"%s\"", typed);

        typedn = 0; typed[0] = 0;
        frame(0x00, 0xFF, 0x45, 0x45);  app_loop();
        CHECK(!typedn, "a command that is not its complement's is noise");
        frame(0x00, 0x12, 0x45, 0xBA);  app_loop();
        CHECK(!strcmp(typed, "<00/05><00/06>"),
              "the address's complement is not checked: extended NEC: \"%s\"", typed);

        typedn = 0; typed[0] = 0;
        edge(0, true); edge(IR_T(11250), false); edge(IR_T(1125), false);
        app_loop();
        CHECK(!typedn, "a repeat plays nothing");
        edge(0, true); edge(IR_T(13500), false);
        for (int i = 0; i < 20; i++) edge(IR_T(1125), false);
        edge(0, true);                          /* cut off: a long gap */
        for (int i = 0; i < 12; i++) edge(IR_T(1125), false);
        app_loop();
        CHECK(!typedn, "a frame cut off and stray bits after it are nothing");
        edge(0, true); edge(IR_T(13500), false);
        for (int i = 0; i < 16; i++) edge(IR_T(1125), false);
        edge(IR_T(400), false);                 /* a glitch mid-frame */
        for (int i = 0; i < 16; i++) edge(IR_T(1125), false);
        app_loop();
        CHECK(!typedn, "a glitch ends the frame");
        /* ...even one that would have made a good frame of it: a blip just
           before the last edge, where that bit was a 0 anyway. */
        {
            uint32_t f = 0x80 | 0x7Fu << 8 | 0xC5u << 16 | 0x3Au << 24;
            edge(0, true); edge(IR_T(13500), false);
            for (int i = 0; i < 31; i++)
                edge((f >> i) & 1 ? IR_T(2250) : IR_T(1125), false);
            edge(IR_T(400), false);
            edge(IR_T(725), false);
            app_loop();
            CHECK(!typedn, "a blip is not a bit: \"%s\"", typed);
        }

        /* An LG TV's remote, power, as the IR spike saw it: from one falling
           edge to the next, in us. Its first is 11.1 ms, not 13.5 -- in the
           second frame the burst even broke up, and the edges still fell
           in the same places. */
        {
            static const uint16_t lg[2][33] = {
                { 11170, 1116, 1116, 2171, 1178, 1054, 1054, 1054, 1054, 2171, 2295,
                  1116, 2171, 2233, 2295, 2233, 2233, 1116, 1116, 1054, 2171, 1116,
                  1054, 1054, 1054, 2171, 2233, 2295, 1116, 2171, 2295, 2233, 2233 },
                { 11108, 1178, 1116, 2171, 1116, 1054, 1054, 1054, 1054, 2171, 2295,
                  1116, 2171, 2233, 2295, 2233, 2233, 1116, 1116, 1054, 2171, 1116,
                  1054, 1054, 1054, 2171, 2233, 2295, 1116, 2171, 2295, 2233, 2233 },
            };
            table_reset();
            add(".", { IRTAG(0x04, 0x08), K('l'), K('g') });
            for (int k = 0; k < 2; k++) {
                typedn = 0; typed[0] = 0;
                edge(0, true);
                for (int i = 0; i < 33; i++) edge(IR_T(lg[k][i]), false);
                app_loop();
                CHECK(!strcmp(typed, "<00/0F><00/0A>"),
                      "an LG remote's frame %d is {IR 04 08}: \"%s\"", k + 1, typed);
            }
        }

        /* A {pause 12.8} is 0x01 0x80, and what follows it can look like
           an {IR}'s bytes; the walk knows it is a pause. */
        table_reset();
        add(".", { ESC_PAUSE, 0x80, (uint8_t)(0x05 | 0x80), (uint8_t)(0x06 | 0x80),
                   0x80 | 0x02, K('z'), K('z') });
        add("-", { IRTAG(0x07, 0x08), K('y') });
        CHECK(!strcmp(press(0x05, 0x86), ""),
              "an argument of 0x80 is not an {IR}: \"%s\"", typed);
        CHECK(!strcmp(press(0x07, 0x08), "<00/1C>"),
              "and the walk goes on past it: \"%s\"", typed);

        printf("24b. the remote counts for nothing, and does nothing locked\n");
        setpass((const uint8_t *)"secret", 6);
        lockout(2, 0xFF, 1);
        press(0x01, 0x01); press(0x01, 0x02); press(0x01, 0x03);
        CHECK(!hidLockState(), "buttons not in the table are not wrong codes");
        miss(); miss();
        CHECK(hidLockState() == DM_ST_SOFTLOCK, "two wrong taps lock it");
        CHECK(!strcmp(press(0x07, 0x08), ""), "and locked, the remote plays nothing");
        setpass(0, 0);
        lockout(0xFF, 0xFF, 0xFF);
        memset(EE + (size_t)LOCK_MISSES, 0xFF, 3);
        CHECK(!hidLockState(), "(unlocked again)");

        printf("24c. LEARN types the next button, once, window or not, within ten seconds\n");
        hidAllow(1000);
        hidLearn();
        CHECK(!strcmp(press(0x07, 0xA8), "<00/27><00/24><00/2C><00/04><00/25>"),
              "\"07 a8\", and not the macro: \"%s\"", typed);
        CHECK(!strcmp(press(0x07, 0x08), "<00/1C>"), "once: the next one plays");
        hidLearn();
        fake_ms += 2000;
        CHECK(!hidAllowed(), "(the window has shut)");
        CHECK(!strcmp(press(0x07, 0x08), "<00/27><00/24><00/2C><00/27><00/25>"),
              "learning needs no window: \"%s\"", typed);
        CHECK(!strcmp(press(0x07, 0x08), "<00/1C>"), "and is forgotten");
        hidLearn();
        fake_ms += 9500;
        CHECK(!strcmp(press(0x07, 0x08), "<00/27><00/24><00/2C><00/27><00/25>"),
              "nine and a half seconds on, it still types: \"%s\"", typed);
        hidLearn();
        fake_ms += 10500;
        CHECK(!strcmp(press(0x07, 0x08), "<00/1C>"),
              "after ten, the button plays as if never asked: \"%s\"", typed);
        CHECK(!strcmp(press(0x07, 0x08), "<00/1C>"), "and the next one too");
        typedn = 0; typed[0] = 0;
        type_hex(0x00); type_hex(0x9F); type_hex(0x10);
        CHECK(!strcmp(typed, "<00/27><00/27><00/26><00/09><00/1E><00/27>"),
              "every digit: \"%s\"", typed);
    }

    printf("\n%s\n", fails ? "SOME CHECKS FAILED" : "all checks passed");
    return fails != 0;
}
