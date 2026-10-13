/*
 * Spike: the core updater (app/coreupdate.S) on a simulated board.
 *
 *   coreupdate core.elf old-core.hex coreupdate.hex core-flash.bin
 *
 * The board starts with an older core -- v3.0's, as boards have it -- and
 * the updater above it. core.elf is the new core, for its symbols: the
 * harness watches its calibration by address, and once the updater has
 * run, the new core is what runs. core-flash.bin is its 4 KB as they should
 * end up.
 *
 *   1. the old core runs the updater, which rewrites everything below the
 *      boundary into the new core, byte for byte;
 *   2. erases its own header, so it does not run again;
 *   3. resets, and the new core comes up alone and enumerates.
 */
#include <stdio.h>
#include <string.h>

#include "board.h"
#include "dm.h"

#define ADDR    7

int main(int argc, char **argv)
{
	if (argc < 5) {
		fprintf(stderr, "usage: %s core.elf old-core.hex coreupdate.hex core-flash.bin\n", argv[0]);
		return 2;
	}
	int fail = 0;
	uint8_t want[4096], v[64];
	char what[100];
	FILE *f = fopen(argv[4], "rb");
	if (!f || fread(want, 1, sizeof want, f) != sizeof want) { perror(argv[4]); return 2; }
	fclose(f);

	board_load(argv[1], NULL);
	memset(avr->flash, 0xFF, sizeof want);  /* the old core, and nothing of the new */
	board_load_hex(argv[2]);
	board_load_hex(argv[3]);
	board_wdt_osc(-8000, 2000, 1);
	fail |= check(memcmp(avr->flash, want, sizeof want) != 0, "the board starts with the old core");

	/* 1 */
	board_run(NULL, 2000);
	int diff = 0;
	for (int i = 0; i < (int)sizeof want; i++)
		diff += avr->flash[i] != want[i];
	snprintf(what, sizeof what, "below the boundary, the new core byte for byte (%d differ)", diff);
	fail |= check(!diff, what);

	/* 2 */
	int blank = 1;
	for (int i = 0x1000; i < 0x1040; i++)
		blank &= avr->flash[i] == 0xFF;
	fail |= check(blank, "the updater's header is erased");

	/* 3 */
	fail |= check(board_resets == 1, "one reset, the updater's");
	board_resets = 0;
	usb.verbose = 0;
	fail |= board_bring_up();
	if (!fail)
		fail |= board_enumerate(ADDR);
	fail |= check(dm_info(ADDR, v) == DM_VND_LEN, "the new core answers INFO");

	fail |= check(!usb.contention, "no contention on the bus");
	fail |= check(!board_resets, "no reset after");
	board_close();
	printf("%s\n", fail ? "FAIL" : "PASS");
	return fail;
}
