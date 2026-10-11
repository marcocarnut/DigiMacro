/*
 * Secrets, end to end: the real firmware -- app/xtea.S and all, which the
 * PC tests cannot run -- on a simulated ATtiny85 with a USB host
 * (../simavr-softusb), driven as a client and a finger would.
 *
 *   secret core.elf digimacro.hex sealed.txt
 *
 * sealed.txt is what `node test/webtable.js --emit-secret` writes: the
 * EEPROM as the page leaves it, a table whose "." is user\t{secret}hunter2
 * {Enter}, the check value at 0x1E4; then key=, the key it was sealed with.
 *
 *   1. without the key, a tap types the user name and stops;
 *   2. PROVE answers what a client computes from the key;
 *   3. a wrong key is refused, and so is the right one for a second after;
 *   4. the right key: INFO says so, and the tap types it all;
 *   5. a minute later the key is gone -- from RAM too -- and the tap stops
 *      where it did at first.
 */
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "dm.h"
#include "touchpad.h"
#include "../xtea.h"

#define ADDR            7
#define KEYAUTH         0x6A
#define PROVE           0x6B
#define REFUSED         0xEE
#define ST_KEY          0x80
#define CHECK_ADDR      0x1E4

static touchpad_t pad;

static int ask(uint8_t cmd, uint8_t count, const uint8_t *data, uint8_t *answer)
{
	uint8_t out[1 + DM_VND_LEN] = { DM_VND_ID, cmd, 0, 0, count };
	if (data)
		memcpy(out + 5, data, count);
	int n = board_ctl(ADDR, 0x21, 0x09, 0x0300 | DM_VND_ID, 0, sizeof out, out);
	if (n != sizeof out)
		return n;
	return board_ctl(ADDR, 0xA1, 0x01, 0x0300 | DM_VND_ID, 0, sizeof out, answer);
}

/* A tap on the pad -- the code "." -- and what it types */
static int tap(const char *want, char *got)
{
	dm_keys_t k;
	dm_keys_init(&k);
	touchpad_touch(&pad, 1);
	dm_keys_read(&k, ADDR, 120, NULL);
	touchpad_touch(&pad, 0);
	int ok = dm_keys_read(&k, ADDR, 3000, want);
	dm_keys_read(&k, ADDR, 300, NULL);      /* and nothing more after it */
	strcpy(got, k.text);
	return ok && !strcmp(k.text, want);
}

static uint8_t info_state(void)
{
	uint8_t v[64];
	return dm_info(ADDR, v) == DM_VND_LEN ? v[7] : 0xFF;
}

int main(int argc, char **argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: %s core.elf digimacro.hex sealed.txt\n", argv[0]);
		return 2;
	}
	int fail = 0;
	char what[300], got[64];
	uint8_t ee[512], key[16], v[64];

	FILE *f = fopen(argv[3], "r");
	if (!f) { perror(argv[3]); return 2; }
	for (int i = 0; i < 512; i++) {
		unsigned b;
		if (fscanf(f, " %x", &b) != 1) { fprintf(stderr, "short EEPROM\n"); return 2; }
		ee[i] = b;
	}
	if (fscanf(f, " key=") != 0) return 2;
	for (int i = 0; i < 16; i++) {
		unsigned b;
		if (fscanf(f, "%2x", &b) != 1) { fprintf(stderr, "no key\n"); return 2; }
		key[i] = b;
	}
	fclose(f);

	board_load(argv[1], NULL);
	board_load_hex(argv[2]);
	board_eeprom(0, ee, sizeof ee);
	board_wdt_osc(-8000, 2000, 1);          /* a serial number that is not 0 */
	touchpad_init(&pad, avr, 'B', 0);

	char app_elf[512];
	snprintf(app_elf, sizeof app_elf, "%.*s.elf", (int)strlen(argv[2]) - 4, argv[2]);
	uint32_t sec_at = board_symbol_in(app_elf, "_ZL3sec", NULL);

	fail |= board_bring_up();
	if (fail)
		goto out;
	usb.verbose = 0;
	fail |= board_enumerate(ADDR);
	if (fail)
		goto out;
	uint8_t leds[2] = { DM_KBD_ID, 0x00 };
	board_ctl(ADDR, 0x21, 0x09, 0x0200 | DM_KBD_ID, 0, 2, leds);
	dm_keys_t idle;
	dm_keys_init(&idle);
	dm_keys_read(&idle, ADDR, 1500, NULL);  /* the pad's baseline settles */

	/* 1 */
	fail |= check(!(info_state() & ST_KEY), "no key held at first");
	int ok = tap("user\t", got);
	snprintf(what, sizeof what, "without the key the tap stops at the secret (\"%s\")", got);
	fail |= check(ok, what);

	/* 2 */
	uint8_t challenge[8] = { 0x10, 0x32, 0x54, 0x76, 0x98, 0xBA, 0xDC, 0xFE }, want[8], k2[16];
	memcpy(want, challenge, 8);
	memset(v, 0, sizeof v);
	memset(k2, 0, 8);
	xtea(k2, key);                          /* the check value, twice */
	memcpy(k2 + 8, k2, 8);
	xtea(want, k2);
	fail |= check(ask(PROVE, 8, challenge, v) == DM_VND_LEN && v[0] == PROVE &&
	              !memcmp(v + 4, want, 8),
	              "PROVE answers XTEA under the check value, as a client computes it");

	/* 3 */
	uint8_t bad[16];
	memcpy(bad, key, 16);
	bad[15] ^= 1;
	fail |= check(ask(KEYAUTH, 16, bad, v) == DM_VND_LEN && v[0] == REFUSED,
	              "a wrong key is refused");
	fail |= check(ask(KEYAUTH, 16, key, v) == DM_VND_LEN && v[0] == REFUSED,
	              "...and the right one, within the second after");
	fail |= check(!(info_state() & ST_KEY), "no key held");
	board_run(NULL, 1100);

	/* 4 */
	fail |= check(ask(KEYAUTH, 16, key, v) == DM_VND_LEN && v[0] == KEYAUTH,
	              "the right key is taken");
	fail |= check(v[4] == 0 && v[19] == 0, "...and wiped from the report");
	fail |= check(info_state() & ST_KEY, "INFO says the key is held");
	ok = tap("user\thunter2\n", got);
	snprintf(what, sizeof what, "with it the tap types the secret too (\"%s\")", got);
	fail |= check(ok, what);

	/* 5 */
	board_run(NULL, 61000);
	fail |= check(!(info_state() & ST_KEY), "a minute later INFO says no key");
	int zero = 1;
	for (int i = 0; i < 16; i++)
		zero &= !avr->data[sec_at + i];
	fail |= check(zero, "and the key is gone from RAM");
	ok = tap("user\t", got);
	snprintf(what, sizeof what, "the tap stops at the secret again (\"%s\")", got);
	fail |= check(ok, what);

out:
	fail |= check(!usb.contention, "no contention on the bus");
	fail |= check(!board_resets, "no chip reset");
	board_close();
	printf("%s\n", fail ? "FAIL" : "PASS");
	return fail;
}
