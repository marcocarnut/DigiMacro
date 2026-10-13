/*
 * A hung app, on a simulated board: the watchdog, which only a chip has.
 *
 *   hang core.elf hangapp.hex
 *
 *   1. the app runs a second and more without a reset: the watchdog is the
 *      2 s the core sets, not anything shorter;
 *   2. it hangs, and the watchdog resets the chip, once;
 *   3. the core comes up alone, says the app is halted, and stays up.
 */
#include <stdio.h>

#include "board.h"
#include "dm.h"

#define ADDR            7
#define CORE_STATE      12              /* INFO's [12] */
#define CORE_HALTED     0x04

int main(int argc, char **argv)
{
	if (argc < 3) {
		fprintf(stderr, "usage: %s core.elf hangapp.hex\n", argv[0]);
		return 2;
	}
	int fail = 0;
	uint8_t v[64];

	board_load(argv[1], NULL);
	board_load_hex(argv[2]);
	board_wdt_osc(-8000, 2000, 1);
	usb.verbose = 0;

	/* 1, 2 */
	board_run(NULL, 1000);
	fail |= check(board_resets == 0, "no reset in the first second");
	board_run(NULL, 2500);
	fail |= check(board_resets == 1, "the hung app reset by the watchdog, once");

	/* 3 */
	board_resets = 0;
	fail |= board_bring_up();
	if (!fail)
		fail |= board_enumerate(ADDR);
	fail |= check(dm_info(ADDR, v) == DM_VND_LEN && (v[CORE_STATE] & 3) == 0
	              && (v[CORE_STATE] & CORE_HALTED), "the core alone, the app halted");
	board_run(NULL, 3000);
	fail |= check(!board_resets, "and no reset after");

	board_close();
	printf("%s\n", fail ? "FAIL" : "PASS");
	return fail;
}
