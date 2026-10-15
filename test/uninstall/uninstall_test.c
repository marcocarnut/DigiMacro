/* The uninstaller, compiled for the host against a fake chip, and the image
 * make uninstall built for the real one.
 *
 *   uninstall_test uninstall.hex t85_default.hex
 *
 * The image: what the core checks before it runs the app -- the header's
 * length, the CRC after it -- and what Micronucleus needs, which is itself at
 * MN_START and nothing where it looks for a program to run. The code: the
 * EEPROM wiped before page 0 is touched, interrupts off for all of it, and
 * page 0 left jumping to Micronucleus and nothing else changed.
 *
 * The fake EEPROM is busy after a write until something waits for it, as the
 * chip's is for 3.4 ms, and the fake flash ignores an erase or a write while
 * it is: the ATtiny85 does, and a board with a password -- its last bytes at
 * the top of the EEPROM, the wipe's last write -- once kept its core that way.
 */
#include <setjmp.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "layout.h"

uint8_t FL[8192], EE[512];
int ints;

static int fails, page0, ee_after, spm_ints, wdrs, ee_busy, spm_lost;
static uint16_t temp[32];
static jmp_buf reset;

#define check(ok, ...) do { if (!(ok)) { printf("  FAIL: " __VA_ARGS__); \
                                          printf("\n"); fails++; } } while (0)

void eeprom_update_byte(uint8_t *a, uint8_t v)
{
    ee_busy = 0;                        /* avr-libc's waits before it reads */
    if (page0) ee_after++;
    if (EE[(size_t)a] != v) ee_busy = 1;
    EE[(size_t)a] = v;
}
void eeprom_busy_wait(void) { ee_busy = 0; }
void boot_page_erase(uint16_t a)
{
    if (ints) spm_ints++;
    if (ee_busy) { spm_lost++; return; }
    page0 = 1;
    memset(FL + (a & ~63), 0xFF, 64);
}
void boot_page_fill(uint16_t a, uint16_t w) { temp[(a & 63) / 2] &= w; }
void boot_page_write(uint16_t a)
{
    if (ints) spm_ints++;
    if (ee_busy) { spm_lost++; memset(temp, 0xFF, sizeof temp); return; }
    for (int i = 0; i < 32; i++) {     /* flash only ever clears bits */
        FL[(a & ~63) + 2 * i]     &= temp[i];
        FL[(a & ~63) + 2 * i + 1] &= temp[i] >> 8;
    }
    memset(temp, 0xFF, sizeof temp);
}
void wdt_reset(void) { wdrs++; }
void wdt_enable(int t) { (void)t; longjmp(reset, 1); }

#include "../../app/uninstall.c"

static void load(const char *path, uint8_t *mem, uint8_t *seen)
{
    FILE *f = fopen(path, "r");
    char l[600];
    if (!f) { perror(path); exit(2); }
    while (fgets(l, sizeof l, f)) {
        unsigned n, a, t, b;
        if (sscanf(l, ":%2x%4x%2x", &n, &a, &t) != 3 || t != 0) continue;
        for (unsigned i = 0; i < n; i++) {
            sscanf(l + 9 + 2 * i, "%2x", &b);
            mem[a + i] = b;
            if (seen) seen[a + i] = 1;
        }
    }
    fclose(f);
}

static uint16_t crc16(const uint8_t *p, unsigned n)
{
    uint16_t crc = 0xFFFF;
    while (n--) {
        crc ^= *p++;
        for (int i = 0; i < 8; i++) crc = crc & 1 ? (crc >> 1) ^ 0xA001 : crc >> 1;
    }
    return crc;
}

/* A board as DigiMacro leaves it: a core below, a table and a password in
   the EEPROM, and this image written over the app region. */
static uint8_t image[8192], core[APP_START];
static void board(int password)
{
    memset(FL, 0xFF, sizeof FL);
    for (int i = 0; i < APP_START; i++) FL[i] = core[i] = (uint8_t)(i * 7 + 1);
    memcpy(FL + APP_START, image + APP_START, sizeof FL - APP_START);
    /* the table, and the reserved top; a password fills its last bytes */
    for (int i = 0; i < 512; i++) EE[i] = i < 0x1E4 ? (uint8_t)(i ^ 0x5A) : 0xFF;
    if (password) for (int i = 0x1F4; i < 512; i++) EE[i] = 0x30 + (i & 0x0F);
    memset(temp, 0xFF, sizeof temp);
    ints = 1; page0 = ee_after = spm_ints = wdrs = ee_busy = spm_lost = 0;
}

int main(int argc, char **argv)
{
    static uint8_t mn[8192], mnSeen[8192];
    if (argc != 3) { fprintf(stderr, "usage: %s uninstall.hex t85_default.hex\n", argv[0]); return 2; }
    memset(image, 0xFF, sizeof image);
    load(argv[1], image, NULL);
    load(argv[2], mn, mnSeen);

    printf("the image, as the core and Micronucleus see it\n");
    uint16_t len = image[APP_LENGTH] | image[APP_LENGTH + 1] << 8;
    unsigned mnEnd = 0;
    for (unsigned a = 0; a < sizeof mn; a++) if (mnSeen[a]) mnEnd = a + 1;
    check(len == mnEnd - APP_START, "the length is 0x%X, not to Micronucleus's end 0x%X", len, mnEnd - APP_START);
    check(APP_START + len + 2 <= APP_END, "no room for the CRC");
    uint16_t crc = image[APP_START + len] | image[APP_START + len + 1] << 8;
    check(crc == crc16(image + APP_START, len), "the CRC the core checks is wrong");
    int same = 1;
    for (unsigned a = 0; a < sizeof mn; a++) if (mnSeen[a] && image[a] != mn[a]) same = 0;
    check(same, "Micronucleus is not in the image byte for byte");
    check(mnSeen[MN_START] && !mnSeen[MN_START - 1], "Micronucleus does not start at MN_START");
    int blank = 1;
    for (unsigned a = MN_START - 6; a < MN_START; a++) if (image[a] != 0xFF) blank = 0;
    check(blank, "the bytes below Micronucleus, where it looks for a program, are not blank");

    for (int password = 0; password < 2; password++) {
    printf("the uninstaller, %s: the EEPROM, then page 0, then a reset\n",
           password ? "with a password" : "with no password");
    board(password);
    if (!setjmp(reset)) { app_setup(); check(0, "app_setup returned instead of resetting"); }
    check(!spm_lost, "%d flash operations lost to an EEPROM write still going", spm_lost);
    int wiped = 1;
    for (int i = 0; i < 512; i++) if (EE[i] != 0xFF) wiped = 0;
    check(wiped, "the EEPROM is not all blank");
    check(!ee_after, "%d EEPROM writes after page 0 was touched", ee_after);
    check(page0, "page 0 was never written");
    check(!spm_ints, "flash written with interrupts on");
    check(wdrs >= 512, "the watchdog was fed %d times over the wipe", wdrs);
    uint16_t w0 = FL[0] | FL[1] << 8;
    check((w0 & 0xF000) == 0xC000, "word 0 is 0x%04X, not an rjmp", w0);
    check(((1 + (w0 & 0x0FFF)) & 0x0FFF) * 2 == MN_START, "word 0 jumps to 0x%04X, not to Micronucleus",
          ((1 + (w0 & 0x0FFF)) & 0x0FFF) * 2);
    int rest = 1, others = 1;
    for (int a = 2; a < 64; a++) if (FL[a] != 0xFF) rest = 0;
    for (int a = 64; a < APP_START; a++) if (FL[a] != core[a]) others = 0;
    for (unsigned a = APP_START; a < sizeof FL; a++) if (FL[a] != image[a]) others = 0;
    check(rest, "the rest of page 0 is not blank");
    check(others, "flash outside page 0 changed");
    }

    printf("no Micronucleus in the image: nothing touched\n");
    board(1);
    FL[MN_START] = FL[MN_START + 1] = 0xFF;
    if (!setjmp(reset)) app_setup(); else check(0, "it reset with no Micronucleus to go to");
    int untouched = !page0;
    for (int i = 0; i < 0x1E4; i++) if (EE[i] != (uint8_t)(i ^ 0x5A)) untouched = 0;
    for (int a = 0; a < APP_START; a++) if (FL[a] != core[a]) untouched = 0;
    check(untouched, "the board was changed anyway");

    printf(fails ? "\nSOME CHECKS FAILED\n" : "\nall checks passed\n");
    return !!fails;
}
