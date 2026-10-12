/* digimacro-host's exchange with a board, against a fake one: the client's
 * own code, compiled with ioctl() redirected here. The fake answers PROVE
 * and KEYAUTH as firmware 16 does (DigiMacro.ino, "secrets"), or as a copy
 * of the board that has its serial number and not its check value, or as
 * firmware 15, which knows neither.
 *
 *   cc -I../tools/hostauth -o hostclient hostclient.c && ./hostclient
 */
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>

static int fake_ioctl(int fd, unsigned long req, ...);
#define ioctl fake_ioctl
#define main client_main
#include "../tools/hostauth/digimacro-host.c"
#undef main
#undef ioctl

#include <stdarg.h>

static enum { REAL, COPY, OLD } kind;
static unsigned char check[8], board_key[16], last[1 + VND_LEN];
static int keyauths;                    /* KEYAUTHs the fake was sent */

static int fake_ioctl(int fd, unsigned long req, ...)
{
    va_list ap;
    va_start(ap, req);
    unsigned char *buf = va_arg(ap, unsigned char *);
    va_end(ap);
    (void)fd;
    if (req == HIDIOCSFEATURE(1 + VND_LEN)) {
        memcpy(last, buf, sizeof last);
        if (last[1] == KEYAUTH) keyauths++;
        return 1 + VND_LEN;
    }
    if (req != HIDIOCGFEATURE(1 + VND_LEN)) return -1;
    unsigned char *a = buf + 1;        /* answers come back with the ID, as
                                           Linux gives them */
    memcpy(buf, last, sizeof last);
    if (kind == OLD || (last[1] != PROVE && last[1] != KEYAUTH)) {
        a[0] = 0xEE;
    } else if (last[1] == PROVE) {
        unsigned char k[16];
        for (int i = 0; i < 16; i++) k[i] = kind == REAL ? check[i & 7] : 0x5A;
        xtea(a + 4, k);
    } else {
        unsigned char b[8] = { 0 };
        xtea(b, a + 4);
        if (memcmp(b, check, 8)) a[0] = 0xEE;
        memset(a + 4, 0, 32);           /* wiped */
    }
    return 1 + VND_LEN;
}

static int fails;
static void ok(int c, const char *what) { if (!c) { printf("  FAIL: %s\n", what); fails++; } }

int main(void)
{
    port p = { 3 };
    unsigned char wrong[16];

    for (int i = 0; i < 16; i++) board_key[i] = 0xA0 + i;
    memcpy(wrong, board_key, 16);
    wrong[0] ^= 1;
    memset(check, 0, 8);
    xtea(check, board_key);

    printf("1. the real board proves itself, and is given the key\n");
    kind = REAL; keyauths = 0;
    ok(give_key(&p, board_key) == TRUSTED, "it holds the key");
    ok(keyauths == 1, "KEYAUTH sent once, after PROVE");

    printf("2. a copy with the serial number is not\n");
    kind = COPY; keyauths = 0;
    ok(give_key(&p, board_key) == IMPOSTOR, "PROVE answered wrongly");
    ok(keyauths == 0, "and the key never left");

    printf("3. firmware 15 refuses PROVE, and is not sent the key\n");
    kind = OLD; keyauths = 0;
    ok(give_key(&p, board_key) == TOO_OLD, "too old");
    ok(keyauths == 0, "and the key never left");

    printf("4. a key that is not the board's: refused at PROVE already\n");
    kind = REAL; keyauths = 0;
    ok(give_key(&p, wrong) == IMPOSTOR && keyauths == 0,
       "its check value is another, so the board's answer cannot match");

    printf("5. the key file\n");
    FILE *f = fopen("/tmp/digimacro-hostclient.key", "w");
    fprintf(f, "# a comment\n1234ABCD 0011223344556677\n"
               "7DD5DC7E a0a1a2a3a4a5a6a7a8a9aaabacadaeaf\nbad line\n");
    fclose(f);
    chmod("/tmp/digimacro-hostclient.key", 0600);
    ok(load_keys("/tmp/digimacro-hostclient.key") == 1, "one 32-digit key, the v2 one skipped");
    ok(key_for("7dd5dc7e") && !memcmp(key_for("7dd5dc7e")->key, board_key, 16),
       "found by serial, any case");
    ok(!key_for("1234ABCD"), "no v2 host key loaded");
    remove("/tmp/digimacro-hostclient.key");

    printf(fails ? "\nSOME CHECKS FAILED\n" : "\nall checks passed\n");
    return fails != 0;
}
