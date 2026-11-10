/*
 * The differential test's runner: one scenario, one firmware image, on the
 * simulated board, and a trace of everything the board did that a host, a
 * finger or a programmer could see. test/diff/run.py runs it twice -- the C
 * image and the assembly one -- and compares the traces.
 *
 *   difftest core.elf image.hex scenario.txt
 *
 * image.elf, next to the hex, gives symbols for `ram`. Every trace line
 * starts with "@ "; everything else on stdout is the harness talking.
 * A scenario is a list of the commands in run(), one to a line.
 *
 * The pad is touchpad.c's with one change: its rise times can be a programmed
 * sequence, so a test can say what each charge cycle reads (touch_test.cpp's
 * q[]). And there is an IR receiver, driving P2 low for the bursts, and the
 * LED is watched on P1.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stddef.h>
#include <stdarg.h>
#include <fcntl.h>
#include <unistd.h>
#include <gelf.h>

#include "board.h"
#include "dm.h"
#include "avr_ioport.h"
#include "avr_eeprom.h"
#include "sim_cycle_timers.h"

#define ADDR		7
#define EE_SIZE		512
#define RAM_TOP		0x25F
#define PAINT		0xA5

/* ---- the pad ------------------------------------------------------- */

static struct {
	avr_irq_t *irq;
	int	touched, charging, level;
	uint32_t rise_free, rise_touched;
	uint32_t q[64];
	int	qn, qi;
} pad = { .rise_free = 8, .rise_touched = 25 };

static avr_cycle_count_t charged(avr_t *a, avr_cycle_count_t when, void *param)
{
	pad.charging = 0;
	pad.level = 1;
	avr_raise_irq(pad.irq, 1);
	return 0;
}

static avr_cycle_count_t settle(avr_t *a, avr_cycle_count_t when, void *param)
{
	avr_ioport_state_t st;
	if (avr_ioctl(a, AVR_IOCTL_IOPORT_GETSTATE('B'), &st))
		return 0;
	int out = st.ddr & 1, up = st.port & 1;

	if (out) {			/* driven: the pad follows */
		avr_cycle_timer_cancel(a, charged, NULL);
		pad.charging = 0;
		pad.level = up;
	} else if (up) {		/* the pull-up fills it */
		if (!pad.level && !pad.charging) {
			uint32_t rise = pad.touched ? pad.rise_touched : pad.rise_free;
			if (pad.qn)
				rise = pad.q[pad.qi++ % pad.qn];
			pad.charging = 1;
			if (rise)
				avr_cycle_timer_register(a, rise, charged, NULL);
			else
				charged(a, 0, NULL);
		}
	} else {			/* floating: it keeps its charge */
		avr_cycle_timer_cancel(a, charged, NULL);
		pad.charging = 0;
	}
	/* simavr raised the pin for the pull-up already: put the pad back */
	if (!out)
		avr_raise_irq(pad.irq, pad.level);
	return 0;
}

static void pad_changed(struct avr_irq_t *irq, uint32_t value, void *param)
{
	avr_cycle_timer_register(avr, 0, settle, NULL);
}

static void pad_init(void)
{
	pad.irq = avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'), 0);
	avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'),
			IOPORT_IRQ_DIRECTION_ALL), pad_changed, NULL);
	avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'),
			IOPORT_IRQ_REG_PORT), pad_changed, NULL);
}

/* ---- the trace ----------------------------------------------------- */

static char keys[65536];
static int keysn;
static uint8_t answer[64];
static int answern;
static int led = -1, expect_fails;

static void tr(const char *fmt, ...) __attribute__((format(printf, 1, 2)));
static void tr(const char *fmt, ...)
{
	va_list ap;
	va_start(ap, fmt);
	printf("@ %.2f ", board_ms());
	vprintf(fmt, ap);
	printf("\n");
	va_end(ap);
}

static void led_changed(struct avr_irq_t *irq, uint32_t value, void *param)
{
	if ((int)value != led) {
		led = value;
		tr("LED %d", led);
	}
}

/* ---- timed pin events: the remote ------------------------------------- */

static struct { avr_cycle_count_t t; int level; } ev[4096];
static int nev, evi;

static unsigned min_sp = 0xFFFF;
static uint32_t hook_pc_gap, hook_pc_wiped, hook_end, hook_ks;
static int gap_hit, wipe_checks, wipe_bad;
static uint32_t gap_key;

static void step(softusb_t *u)
{
	board_step(u);
	/* the deepest the stack has been: TOTP wipes what it used, so paint
	   alone would see a stack as deep as it wiped */
	unsigned sp = avr->data[0x5D] | avr->data[0x5E] << 8;
	if (sp > 0x60 && sp < min_sp)
		min_sp = sp;
	if (hook_pc_gap && avr->pc == hook_pc_gap) {
		if (gap_key)			/* WRITE or RESTART, forgetting the key */
			avr->data[gap_key] = 0;
		else
			avr->data[0x31] |= 0x20;	/* half a page: GPIOR0's pageHalf */
		hook_pc_gap = 0;
		gap_hit++;
	}
	if (hook_pc_wiped && avr->pc == hook_pc_wiped) {/* the wipe just done */
		uint32_t e = hook_end;
		int bad = 0;
#define WC(lo, hi) for (uint32_t a = (lo); a <= (hi); a++) \
			if (avr->data[a]) { bad++; printf("  residue at %03X: %02X\n", a, avr->data[a]); }
		WC(e, sp - 2)		/* not sp or sp-1: an interrupt that just
					   returned left its return address there */
		WC(hook_ks, hook_ks + 7)		/* the keystream, gone too */
		WC(18, 27)				/* and the registers */
		wipe_checks++;
		wipe_bad += bad;
		if (bad) printf("  stack not wiped: %d bytes between %03X and SP %03X\n", bad, e, sp);
	}
	while (evi < nev && softusb_now(&usb) >= ev[evi].t) {
		avr_ioport_external_t x = { .mask = 1 << 2, .value = ev[evi].level << 2 };
		avr_ioctl(avr, AVR_IOCTL_IOPORT_SET_EXTERNAL('B'), &x);
		avr_raise_irq(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'), 2), ev[evi].level);
		evi++;
	}
}

/* Bursts starting at falling edges `gaps` us apart, the first `lead` us from
   now; each burst 560 us (the leader's first, 9 ms), the pin high between. */
static void ir_edges(double lead, const int *gaps, int n, int leader)
{
	double t = softusb_now(&usb) + lead * F_CPU / 1e6;
	if (evi >= nev)
		evi = nev = 0;
	for (int i = 0; i <= n; i++) {
		double burst = i == leader ? 9000 : 560;
		ev[nev].t = t;
		ev[nev++].level = 0;
		ev[nev].t = t + (i < n && burst > gaps[i] - 100 ? gaps[i] - 100 : burst) * (double)F_CPU / 1e6;
		ev[nev++].level = 1;
		if (i < n)
			t += gaps[i] * (double)F_CPU / 1e6;
	}
	/* keep it time-ordered */
	for (int a = evi + 1; a < nev; a++)
		for (int b = a; b > evi && ev[b].t < ev[b - 1].t; b--) {
			__typeof__(ev[0]) x = ev[b]; ev[b] = ev[b - 1]; ev[b - 1] = x;
		}
}

/* ---- the host's side -------------------------------------------------- */

static uint8_t toggle = PID_DATA0;
static double next_poll;
static int polling = 1;

static void log_report(const uint8_t *v, int n)
{
	if (n == 6 && v[0] == DM_KBD_ID) {
		tr("KBD %02X %02X %02X %02X %02X", v[1], v[2], v[3], v[4], v[5]);
		if (v[1] || v[3])
			keysn += sprintf(keys + keysn, "<%02X/%02X>", v[1], v[3]);
	} else if (n == 4 && v[0] == 3) {
		tr("PTR %02X %02X %02X", v[1], v[2], v[3]);
		keysn += sprintf(keys + keysn, "[%02X %02X %02X]", v[1], v[2], v[3]);
	} else {
		tr("REPORT len %d id %d", n, n > 0 ? v[0] : -1);
	}
}

static void advance(double ms)
{
	uint8_t v[64];
	double end = board_ms() + ms;
	while (board_ms() < end) {
		double to = end;
		if (polling && next_poll < end)
			to = next_poll;
		if (to > board_ms())
			board_run(NULL, to - board_ms());
		if (polling && board_ms() >= next_poll && board_ms() < end) {
			next_poll = board_ms() + 10;
			int n = softusb_interrupt_in(&usb, ADDR, 1, &toggle, v);
			if (n >= 0)
				log_report(v, n);
			else if (n != -SOFTUSB_E_NAK)
				tr("POLLERR %s", softusb_strerror(-n));
		}
	}
}

/* Until `ms` have gone by with no report, at most `max`: a macro's end */
static void quiesce(double ms, double max)
{
	double start = board_ms();
	int last = keysn;
	double since = board_ms();
	while (board_ms() - start < max && board_ms() - since < ms) {
		advance(5);
		if (keysn != last) {
			last = keysn;
			since = board_ms();
		}
	}
}

static int command(uint8_t func, uint16_t addr, uint8_t count, const uint8_t *data, int ndata)
{
	uint8_t out[1 + DM_VND_LEN] = { DM_VND_ID, func, addr & 0xFF, addr >> 8, count };
	if (data && ndata)
		memcpy(out + 5, data, ndata);
	int n = board_ctl(ADDR, 0x21, 0x09, 0x0300 | DM_VND_ID, 0, sizeof out, out);
	if (n != sizeof out) {
		tr("CMD %02X failed: %d", func, n);
		return n;
	}
	answern = board_ctl(ADDR, 0xA1, 0x01, 0x0300 | DM_VND_ID, 0, sizeof out, answer);
	return answern;
}

/* What the board's USB and app did to the stack: the lowest address that is
   no longer the paint, between the app's statics and the top. */
static uint32_t stack_low(uint32_t from)
{
	uint32_t a;
	for (a = from; a <= RAM_TOP; a++)
		if (avr->data[a] != PAINT)
			break;
	return a;
}

/* ---- the script ------------------------------------------------------- */

static char *image_elf;
static uint8_t ee[EE_SIZE];
static int booted;

static int hexval(char c)
{
	return isdigit((unsigned char)c) ? c - '0' : toupper(c) - 'A' + 10;
}

/* Hex bytes, with spaces or not, from *s: count returned, into b */
static int hexbytes(char *s, uint8_t *b, int max)
{
	int n = 0;
	while (*s && n < max) {
		while (*s && isspace((unsigned char)*s))
			s++;
		if (!s[0] || !s[1])
			break;
		b[n++] = hexval(s[0]) << 4 | hexval(s[1]);
		s += 2;
	}
	return n;
}

/* A symbol of the image, or BOARD_NO_SYMBOL: board_symbol_in() exits */
static uint32_t lookup(const char *name)
{
	uint32_t found = BOARD_NO_SYMBOL;
	elf_version(EV_CURRENT);
	int fd = open(image_elf, O_RDONLY);
	Elf *e = fd < 0 ? NULL : elf_begin(fd, ELF_C_READ, NULL);
	Elf_Scn *scn = NULL;
	while (e && (scn = elf_nextscn(e, scn)) && found == BOARD_NO_SYMBOL) {
		GElf_Shdr sh;
		gelf_getshdr(scn, &sh);
		if (sh.sh_type != SHT_SYMTAB)
			continue;
		Elf_Data *d = elf_getdata(scn, NULL);
		for (size_t i = 0; i < sh.sh_size / sh.sh_entsize; i++) {
			GElf_Sym sym;
			gelf_getsym(d, i, &sym);
			if (!strcmp(elf_strptr(e, sh.sh_link, sym.st_name), name)) {
				found = sym.st_value >= 0x800000 ? sym.st_value - 0x800000
								 : sym.st_value;
				break;
			}
		}
	}
	if (e)
		elf_end(e);
	if (fd >= 0)
		close(fd);
	return found;
}

/* The first of a comma-separated list of names that exists */
static uint32_t symbol(char *names)
{
	for (char *n = strtok(names, ","); n; n = strtok(NULL, ",")) {
		uint32_t a = lookup(n);
		if (a != BOARD_NO_SYMBOL)
			return a;
	}
	return BOARD_NO_SYMBOL;
}

/* A whole app image through the core, as the page does it: the hold, UPDATE,
   the pages header last and read back, COMMIT, and a new enumeration. */
static void expect(int ok, const char *what);

static void board_install(char *hexfile)
{
	static uint8_t img[0x2000];
	memset(img, 0xFF, sizeof img);
	uint32_t end = board_read_hex(hexfile, img, sizeof img);
	if (end <= 0xF80) {
		expect(0, "install image read");
		return;
	}
	pad.touched = 1;
	board_run(NULL, 4300);
	pad.touched = 0;
	board_run(NULL, 150);
	expect(command(0x68, 0, 0, NULL, 0) == DM_VND_LEN && answer[0] == 0x68, "UPDATE taken");
	int pages = (end - 0xF80 + 63) / 64;
	int bad = 0;
	for (int k = 0; k < pages && !bad; k++) {
		uint16_t p = 0xF80 + 64 * ((k + 1) % pages);	/* the header last */
		if (command(0x65, p, 32, img + p, 32) != DM_VND_LEN || answer[0] != 0x65) {
			tr("NOTE page %04X refused (%02X)", p, answer[0]);
			bad = 1;
			break;
		}
		uint8_t out[1 + DM_VND_LEN] = { DM_VND_ID, 0x65, p + 32, (p + 32) >> 8, 32 };
		memcpy(out + 5, img + p + 32, 32);
		board_ctl(ADDR, 0x21, 0x09, 0x0300 | DM_VND_ID, 0, sizeof out, out);
		/* read it back until the core has written it */
		int same = 0;
		for (int tries = 0; tries < 500 && !same; tries++) {
			if (command(0x66, p, 32, NULL, 0) != DM_VND_LEN || answer[0] != 0x66)
				continue;
			same = !memcmp(answer + 4, img + p, 32);
			if (same && (command(0x66, p + 32, 32, NULL, 0) != DM_VND_LEN || answer[0] != 0x66 ||
				     memcmp(answer + 4, img + p + 32, 32)))
				same = 0;
		}
		if (!same) {
			tr("NOTE page %04X not read back", p);
			bad = 1;
		}
	}
	expect(!bad, "image written through the core, each page read back");
	if (bad)
		return;
	int resets = board_resets;
	command(0x67, 0, 0, NULL, 0);
	double t0 = board_ms();
	while (board_ms() < t0 + 4000 && board_resets == resets)
		board_run(NULL, 2);
	expect(board_resets > resets, "COMMIT reset the chip");
	while (board_ms() < t0 + 6000 && usb.attached != 1)
		board_run(NULL, 2);
	board_run(NULL, 600);		/* its setup, then USB up */
	nmeasured = 0;
	int fail = board_bus_reset();
	nmeasured = 0;
	fail |= board_enumerate(ADDR);
	toggle = PID_DATA0;
	next_poll = board_ms() + 10;
	tr("REBOOT fail=%d", fail);
}

static void boot(int enumerate, int hello)
{
	booted = 1;
	memset(avr->data + 0x60, PAINT, RAM_TOP + 1 - 0x60);
	board_eeprom(0, ee, EE_SIZE);
	int fail = board_bring_up();
	if (!fail && enumerate) {
		usb.verbose = 0;
		fail |= board_enumerate(ADDR);
		if (!fail && hello) {
			uint8_t leds[2] = { DM_KBD_ID, 0x00 };
			fail |= board_ctl(ADDR, 0x21, 0x09, 0x0200 | DM_KBD_ID, 0, 2, leds) != 2;
		}
	}
	usb.verbose = 0;
	toggle = PID_DATA0;
	next_poll = board_ms() + 10;
	tr("BOOT fail=%d", fail);
}

static void run_code(const char *code)
{
	for (; *code; code++) {
		pad.touched = 1;
		advance(*code == '-' ? 400 : 120);
		pad.touched = 0;
		advance(120);
	}
}

static void expect(int ok, const char *what)
{
	tr("EXPECT %s %s", ok ? "ok" : "FAIL", what);
	if (!ok)
		expect_fails++;
}

static void run(FILE *f)
{
	static char line[16384];
	while (fgets(line, sizeof line, f)) {
		char cmd[32]; static char rest[16000]; rest[0] = 0;
		line[strcspn(line, "\n")] = 0;
		if (!line[0] || line[0] == '#')
			continue;
		if (sscanf(line, "%31s %15999[^\n]", cmd, rest) < 1)
			continue;

		if (!strcmp(cmd, "ee")) {
			unsigned at; int n = 0;
			sscanf(rest, "%x%n", &at, &n);
			uint8_t b[EE_SIZE];
			int c = hexbytes(rest + n, b, EE_SIZE);
			for (int i = 0; i < c && at + i < EE_SIZE; i++)
				ee[at + i] = b[i];
			if (booted)		/* after boot: into the board */
				board_eeprom(at, ee + at, c > EE_SIZE - (int)at ? EE_SIZE - at : c);
		} else if (!strcmp(cmd, "eefill")) {
			unsigned v = 0xFF;
			sscanf(rest, "%x", &v);
			memset(ee, v, EE_SIZE);
			if (booted)
				board_eeprom(0, ee, EE_SIZE);
		} else if (!strcmp(cmd, "boot")) {
			boot(strcmp(rest, "noenum") != 0, !strstr(rest, "nohello"));
		} else if (!strcmp(cmd, "bootwdt")) {
			board_wdt_osc(-8000, 2000, 1);
			boot(1, 1);
		} else if (!strcmp(cmd, "hello")) {
			uint8_t leds[2] = { DM_KBD_ID, 0x00 };
			board_ctl(ADDR, 0x21, 0x09, 0x0200 | DM_KBD_ID, 0, 2, leds);
		} else if (!strcmp(cmd, "poll")) {
			polling = atoi(rest);
			next_poll = board_ms();
		} else if (!strcmp(cmd, "run")) {
			advance(atof(rest));
		} else if (!strcmp(cmd, "quiesce")) {
			double a = 300, b = 60000;
			sscanf(rest, "%lf %lf", &a, &b);
			quiesce(a, b);
		} else if (!strcmp(cmd, "touch")) {
			pad.touched = atoi(rest);
		} else if (!strcmp(cmd, "pad")) {
			sscanf(rest, "%u %u", &pad.rise_free, &pad.rise_touched);
			pad.qn = 0;
		} else if (!strcmp(cmd, "padseq")) {
			pad.qn = pad.qi = 0;
			for (char *t = strtok(rest, " "); t && pad.qn < 64; t = strtok(NULL, " "))
				pad.q[pad.qn++] = strtoul(t, 0, 0);
		} else if (!strcmp(cmd, "code")) {
			run_code(rest);
		} else if (!strcmp(cmd, "keys")) {
			tr("KEYS %s", keys);
			keysn = 0; keys[0] = 0;
		} else if (!strcmp(cmd, "clearkeys")) {
			keysn = 0; keys[0] = 0;
		} else if (!strcmp(cmd, "expect_keys")) {
			char what[1100];
			snprintf(what, sizeof what, "keys [%s]", rest);
			tr("KEYS %s", keys);
			expect(!strcmp(keys, !strcmp(rest, "-") ? "" : rest), what);
			keysn = 0; keys[0] = 0;
		} else if (!strcmp(cmd, "expect_keys_oneof")) {	/* a|b|-; "-" is nothing */
			char copy[16000], what[1100];
			strcpy(copy, rest);
			int ok = 0;
			for (char *t = strtok(copy, "|"); t; t = strtok(NULL, "|"))
				ok |= !strcmp(keys, !strcmp(t, "-") ? "" : t);
			snprintf(what, sizeof what, "keys one of [%.900s]", rest);
			tr("KEYS %s", keys);
			expect(ok, what);
			keysn = 0; keys[0] = 0;
		} else if (!strcmp(cmd, "expect_clean")) {	/* no pattern anywhere in RAM */
			/* hex patterns, space-separated: any found, anywhere in the
			   register file, I/O or RAM, fails */
			int bad = 0, n = 0;
			for (char *t = strtok(rest, " "); t; t = strtok(NULL, " ")) {
				uint8_t pat[32];
				int l = hexbytes(t, pat, sizeof pat);
				n++;
				for (int a = 0; a + l <= RAM_TOP + 1; a++)
					if (!memcmp(avr->data + a, pat, l)) {
						if (bad < 8)
							printf("  residue %s at %03X\n", t, a);
						bad++;
					}
			}
			char what[100];
			snprintf(what, sizeof what, "no residue of %d patterns in RAM (%d found)", n, bad);
			expect(!bad, what);
		} else if (!strcmp(cmd, "expect_zero")) {	/* addr len: RAM all zero */
			unsigned a = 0, n = 1;
			sscanf(rest, "%x %x", &a, &n);
			int z = 1;
			for (unsigned i = 0; i < n; i++)
				z &= !avr->data[a + i];
			char what[80];
			snprintf(what, sizeof what, "RAM %03X+%x zero", a, n);
			expect(z, what);
		} else if (!strcmp(cmd, "expect_eqbytes")) {	/* addr hex: RAM equals */
			unsigned a = 0; int n = 0;
			sscanf(rest, "%x%n", &a, &n);
			uint8_t b[64];
			int c = hexbytes(rest + n, b, sizeof b);
			char what[80];
			snprintf(what, sizeof what, "RAM %03X+%x as expected", a, c);
			expect(!memcmp(avr->data + a, b, c), what);
		} else if (!strcmp(cmd, "checkclock")) {	/* read it; against the last read */
			/* seconds and ticks from the board against simulated time:
			   "checkclock tol-ms": prints, and fails if the clock's
			   advance since the mark is off by more */
			static double t0, c0;
			static int marked;
			unsigned tol = atoi(rest);
			if (command(0x6C, 0, 0, NULL, 0) != DM_VND_LEN || answer[0] != 0x6C) {
				expect(0, "clock read answered");
			} else {
				uint32_t secs = answer[4] | answer[5] << 8 | answer[6] << 16 | (uint32_t)answer[7] << 24;
				unsigned ticks = answer[8] | answer[9] << 8;
				double now = board_ms(), c = secs * 1000.0 + ticks * 1.00072727;
				tr("CLOCK %u + %u ticks, set=%u", secs, ticks, answer[10]);
				if (marked) {
					char what[100];
					double dc = c - c0, dt = now - t0;
					snprintf(what, sizeof what, "clock moved %.1f ms in %.1f ms", dc, dt);
					expect(dc - dt < tol && dt - dc < tol, what);
				}
				t0 = now; c0 = c; marked = 1;
			}
		} else if (!strcmp(cmd, "watch_wipe")) {	/* check the stack at every wipe's end */
			hook_pc_wiped = lookup("totp_wiped");
			hook_end = lookup("_end");
			hook_ks = lookup("sec_ks");
		} else if (!strcmp(cmd, "expect_wiped")) {
			char what[80];
			snprintf(what, sizeof what, "%d wipes, %d bytes left on the stack", wipe_checks, wipe_bad);
			expect(wipe_checks > 0 && !wipe_bad, what);
		} else if (!strcmp(cmd, "gap_page")) {		/* half a page at the gap */
			hook_pc_gap = lookup("totp_gap");
			gap_key = 0;
			gap_hit = 0;
		} else if (!strcmp(cmd, "gap_forget")) {	/* the key forgotten at the gap */
			hook_pc_gap = lookup("totp_gap");
			gap_key = lookup("key_left");
			gap_hit = 0;
		} else if (!strcmp(cmd, "expect_gap_hit")) {
			expect(gap_hit == 1, "the totp reached its gap");
		} else if (!strcmp(cmd, "powercycle")) {	/* unplugged and plugged back */
			avr_reset(avr);
			avr->data[0x54] |= 1;		/* MCUSR: power-on, as after a replug */
			memset(avr->data + 0x60, PAINT, RAM_TOP + 1 - 0x60);
			board_run(NULL, 1500);
			nmeasured = 0;
			int fail = board_bus_reset();
			nmeasured = 0;
			fail |= board_enumerate(ADDR);
			uint8_t leds[2] = { DM_KBD_ID, 0x00 };
			board_ctl(ADDR, 0x21, 0x09, 0x0200 | DM_KBD_ID, 0, 2, leds);
			toggle = PID_DATA0;
			next_poll = board_ms() + 10;
			tr("REBOOT fail=%d", fail);
		} else if (!strcmp(cmd, "markclock")) {
			command(0x6C, 0, 0, NULL, 0);	/* forget the mark: read again */
		} else if (!strcmp(cmd, "page")) {		/* a half page: "page addr hex" */
			unsigned a = 0; int n = 0;
			sscanf(rest, "%x%n", &a, &n);
			uint8_t b[64];
			int c = hexbytes(rest + n, b, sizeof b);
			int r = command(0x65, a, c, b, c);
			printf("@ %.2f ANS %d", board_ms(), r);
			for (int i = 0; i < 12 && i < r; i++)
				printf(" %02X", answer[i]);
			printf("\n");
		} else if (!strcmp(cmd, "reinstall")) {	/* a whole image through the core */
			board_install(rest);
		} else if (!strcmp(cmd, "cmd")) {
			unsigned func, a = 0, c = 0; int n = 0;
			sscanf(rest, "%x %x %x%n", &func, &a, &c, &n);
			uint8_t b[64];
			int nd = hexbytes(rest + n, b, sizeof b);
			int r = command(func, a, c, b, nd);
			printf("@ %.2f ANS %d", board_ms(), r);
			for (int i = 0; i < r && i < 64; i++)
				printf(" %02X", answer[i]);
			printf("\n");
		} else if (!strcmp(cmd, "expect_ans")) {
			unsigned i, v;
			if (sscanf(rest, "%u %x", &i, &v) == 2) {
				char what[80];
				snprintf(what, sizeof what, "answer[%u]==%02X (is %02X)", i, v, answer[i]);
				expect(answer[i] == v, what);
			}
		} else if (!strcmp(cmd, "expect_ansbits")) {
			unsigned i, m, v;
			if (sscanf(rest, "%u %x %x", &i, &m, &v) == 3) {
				char what[80];
				snprintf(what, sizeof what, "answer[%u]&%02X==%02X (is %02X)", i, m, v, answer[i]);
				expect((answer[i] & m) == v, what);
			}
		} else if (!strcmp(cmd, "eedump")) {
			unsigned a = 0, n = EE_SIZE;
			sscanf(rest, "%x %x", &a, &n);
			uint8_t b[EE_SIZE];
			avr_eeprom_desc_t d = { .ee = b, .offset = a, .size = n };
			avr_ioctl(avr, AVR_IOCTL_EEPROM_GET, &d);
			printf("@ %.2f EE %03X", board_ms(), a);
			for (unsigned i = 0; i < n; i++)
				printf(" %02X", d.ee[i]);
			printf("\n");
		} else if (!strcmp(cmd, "expect_ee")) {
			unsigned a = 0; int n = 0;
			sscanf(rest, "%x%n", &a, &n);
			uint8_t want[EE_SIZE], b[EE_SIZE];
			int c = hexbytes(rest + n, want, EE_SIZE);
			avr_eeprom_desc_t d = { .ee = b, .offset = a, .size = c };
			avr_ioctl(avr, AVR_IOCTL_EEPROM_GET, &d);
			char what[80];
			snprintf(what, sizeof what, "eeprom[%03X..] as expected", a);
			expect(!memcmp(d.ee, want, c), what);
		} else if (!strcmp(cmd, "ram")) {
			char names[256]; unsigned n = 1;
			sscanf(rest, "%255s %x", names, &n);
			char label[256];
			strcpy(label, names);
			uint32_t a = symbol(names);
			if (a == BOARD_NO_SYMBOL) {
				tr("RAM %s missing", label);
			} else {
				printf("@ %.2f RAM %s", board_ms(), label);
				for (unsigned i = 0; i < n; i++)
					printf(" %02X", avr->data[a + i]);
				printf("\n");
			}
		} else if (!strcmp(cmd, "peek")) {		/* raw data memory */
			unsigned a = 0, n = 1;
			sscanf(rest, "%x %x", &a, &n);
			printf("@ %.2f PEEK %03X", board_ms(), a);
			for (unsigned i = 0; i < n; i++)
				printf(" %02X", avr->data[a + i]);
			printf("\n");
		} else if (!strcmp(cmd, "ramw")) {		/* a word, shifted */
			char names[256]; unsigned sh = 0;
			sscanf(rest, "%255s %u", names, &sh);
			char label[256];
			strcpy(label, names);
			uint32_t a = symbol(names);
			if (a == BOARD_NO_SYMBOL)
				tr("RAM %s missing", label);
			else
				tr("RAM %s >>%u %u", label, sh,
				   (unsigned)(avr->data[a] | avr->data[a + 1] << 8) >> sh);
		} else if (!strcmp(cmd, "peekm")) {		/* masked: pins the pad's reads move */
			unsigned a = 0, m = 0xFF;
			sscanf(rest, "%x %x", &a, &m);
			tr("PEEK %03X &%02X %02X", a, m, avr->data[a] & m);
		} else if (!strcmp(cmd, "expect_serial")) {	/* made, and not a constant */
			uint8_t b[4];
			avr_eeprom_desc_t d = { .ee = b, .offset = 0x1E0, .size = 4 };
			avr_ioctl(avr, AVR_IOCTL_EEPROM_GET, &d);
			uint32_t v = d.ee[0] | d.ee[1] << 8 | d.ee[2] << 16 | (uint32_t)d.ee[3] << 24;
			static uint32_t first; static int seen;
			int ok = v != 0 && v != 0xFFFFFFFFu && (!seen || v == first);
			first = v; seen = 1;
			expect(ok, "a serial was made, and is kept");
		} else if (!strcmp(cmd, "expect_ram0")) {	/* all zero */
			char names[256]; unsigned n = 1;
			sscanf(rest, "%255s %x", names, &n);
			uint32_t a = symbol(names);
			int z = a != BOARD_NO_SYMBOL;
			if (z)
				for (unsigned i = 0; i < n; i++)
					z &= !avr->data[a + i];
			expect(z, "ram is zero");
		} else if (!strcmp(cmd, "pc_in")) {		/* the app's clock */
			uint32_t a = lookup("ms");
			tr("MSBYTES %02X %02X", avr->data[a], avr->data[a + 1]);
		} else if (!strcmp(cmd, "ir")) {		/* a NEC frame */
			unsigned a, na, c, nc;
			if (sscanf(rest, "%x %x %x %x", &a, &na, &c, &nc) == 4) {
				uint32_t f = a | na << 8 | (uint32_t)c << 16 | (uint32_t)nc << 24;
				int g[33] = { 13500 };
				for (int i = 0; i < 32; i++)
					g[i + 1] = (f >> i) & 1 ? 2250 : 1125;
				ir_edges(100, g, 33, 0);
			}
		} else if (!strcmp(cmd, "irraw")) {		/* gaps in us; leading 0 gap */
			int g[300], n = 0;
			for (char *t = strtok(rest, " "); t && n < 300; t = strtok(NULL, " "))
				g[n++] = atoi(t);
			ir_edges(100, g, n, -1);
		} else if (!strcmp(cmd, "restart")) {		/* after a RESTART */
			/* it has dropped the bus by now, or will: wait for the
			   pull-up to come back and take it through a new
			   enumeration, as Linux would */
			board_run(NULL, 800);
			nmeasured = 0;		/* the harness keeps only 32 */
			int fail = board_bus_reset();
			fail |= board_enumerate(ADDR);
			uint8_t leds[2] = { DM_KBD_ID, 0x00 };
			board_ctl(ADDR, 0x21, 0x09, 0x0200 | DM_KBD_ID, 0, 2, leds);
			usb.verbose = 0;
			toggle = PID_DATA0;
			next_poll = board_ms() + 10;
			tr("REBOOT fail=%d", fail);
		} else if (!strcmp(cmd, "note")) {
			tr("NOTE %s", rest);
		} else if (!strcmp(cmd, "led")) {
			tr("LEDSTATE %d", led);
		} else if (!strcmp(cmd, "stack")) {		/* painted-RAM report */
			uint32_t e = lookup("_end");
			uint32_t low = min_sp + 1;	/* the deepest SP; stack_low() is paint */
			(void)stack_low;
			printf("STACK low %03X end %03X peak %u\n", low, e, RAM_TOP - low + 1);
		} else if (!strcmp(cmd, "verbose")) {
			usb.verbose = atoi(rest);
		} else {
			fprintf(stderr, "unknown command: %s\n", line);
			exit(2);
		}
	}
}

int main(int argc, char **argv)
{
	if (argc < 4) {
		fprintf(stderr, "usage: %s core.elf image.hex scenario.txt\n", argv[0]);
		return 2;
	}
	static char elf[512];
	snprintf(elf, sizeof elf, "%.*s.elf", (int)strlen(argv[2]) - 4, argv[2]);
	image_elf = elf;

	memset(ee, 0xFF, sizeof ee);
	memcpy(ee + 0x1E0, "\xDE\xAD\xBE\xEF", 4);	/* a serial: no second of entropy */
	FILE *f = fopen(argv[3], "r");
	if (!f) {
		perror(argv[3]);
		return 2;
	}
	board_load(argv[1], NULL);
	board_load_hex(argv[2]);
	usb.step = step;
	pad_init();
	/* the IR pin is driven from outside, so the app's pull-up on it and any
	   write to PORTB (the pad's) do not overrule it in simavr */
	avr_ioport_external_t x = { .mask = 1 << 2, .value = 1 << 2 };
	avr_ioctl(avr, AVR_IOCTL_IOPORT_SET_EXTERNAL('B'), &x);
	avr_irq_register_notify(avr_io_getirq(avr, AVR_IOCTL_IOPORT_GETIRQ('B'), 1),
			led_changed, NULL);
	run(f);
	fclose(f);
	printf("@ %.2f END contention=%u resets=%d\n", board_ms(), usb.contention, board_resets);
	board_close();
	return expect_fails ? 1 : 0;
}
