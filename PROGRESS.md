# DigiMacro in hand-tuned AVR assembly: progress

Isolated tree; no hardware, no git commits, no network. Phase 3 (`{totp}`) is
only estimated: NOT built, by instruction.

## State

- Phase 1 done: function map (`tools/sizemap.py`), differential harness
  (`test/sim/difftest.c`, `test/diff/`), proven by C against C (clean) and C
  against a build with DOT_MAX_MS changed (caught).
- Phase 2 done: `app/asm/straight.S` (the straightforward translation) and
  `app/asm/digimacro.S` (tuned), both matching the C on the scenarios.
- Phase 3 done: {totp SECRET}, clock, page and digimacro-host (see "Phase 3 result").
- Next: hands-on, by Marco. Nothing here has run on a chip.

## Numbers (measured: build sizes, painted-RAM runs on the simulated board)

| | C fw 17 | straight asm | tuned asm |
|---|---|---|---|
| flash incl. 2-byte CRC | 3,956 | 4,112 | 3,068 |
| saving against C | | -3.9% | 22.4% |
| flash, xtea and header removed from each | 3,696 | 3,852 | 2,794 (24.4%) |
| static RAM | 91 | 92 | 84 (plus r2-r11, four I/O registers) |
| peak stack, deepest scenarios | 106 | 101 | 73 |

Tests: 122 of 122 differential scenarios pass against the C for both asm
builds; `test/run.sh` exits 0 using the tuned image; m3_digimacro, m4_touch and
m6_update pass on the tuned image. Mutations: 92, all caught, see
`test/diff/mutation-results.txt` (8 needed a scenario or the timing check
strengthening first; rerun after).

## How to reproduce

    make build/v2/digimacro-asm.hex build/v2/asm-straight.hex build/v2/sim_diff
    make difftest                       # every scenario, tuned asm against C
    python3 test/diff/run.py build/v2/digimacro.hex build/v2/asm-straight.hex
    python3 test/diff/mutate.py -j 5    # the mutation table (slow)
    python3 tools/sizemap.py avr-nm build/v2/digimacro.elf \
            build/v2/asm-straight.elf build/v2/digimacro-asm.elf
    bash test/run.sh                    # uses the asm image where it can

`test/diff/INVENTORY.md` maps every C-bound test section to its scenario, and
says what is not covered.

## What the tuned asm assumes about the core

Checked by `tools/checkcore.py` at build time: the core must be the published
core 5 (hash pinned), which never touches r2-r11, holds nothing in the
callee-saved registers across APP_SETUP/APP_LOOP, and whose small accessors
(hidPassSet, hidUnlocked, hidReport, hidLock, hidReady, hidAwake) change only
r24, r25 and flags. A new core means rechecking these, or the app breaks
silently: this is the price of the global registers.

## Decisions

- r2:r3 ms, r4/r5 the ISRs' saved SREG, r6 the tenths countdown; r7-r17 the
  macro's state; Y the variables' base pointer (set once).
- The interrupts' state lives in spare I/O registers (OCR0A/OCR0B/OCR1B,
  USIDR) because they cannot use Y. On real silicon these are plain
  read/write in this configuration per the datasheet: NOT tested on hardware.
- Twelve unused interrupt vector slots hold the timer ISR.
- `touch_read` keeps the C's loop cycle for cycle (7 cycles a count): the
  readings depend on it.
- XTEA was re-allocated off r2-r6 and is 18 bytes larger than app/xtea.S.

## Phase 3 result (measured in simulation)

3,952 bytes with CRC (272 free of 4,224); statics 112 (to 0x1C0); stack peak 73
in the deepest app scenario, 71 with {totp} under USB commands.
Bytes: {totp} 714 (692 + 22 in the walk and player), clock 92, gates 44,
readback 34. `make build/v2/digimacro-asm.hex`, `make asmweb`. See test/diff/
INVENTORY.md and mutation-results-phase3.txt (131 of 132 caught; the survivor,
`out F2`, is unobservable because a chip reset clears USIDR).

The first attempt at each of these was wrong and the tests caught it:
one byte of stack not wiped; a dead key check; a redundant page check.

## Phase 3 estimate (written before building it; unmeasured unless marked)

Flash free after Phase 2: 4,224 - 3,068 = 1,156 bytes (measured). Estimated
cost, all unmeasured: TOTP core 372-420 (tinytotp on a mega32u4, measured
there) plus 30-60 porting; `{totp}` in the player 60-90; six digits typed
20; clock upkeep 40-70 (seconds counter; the tick is 1.00073 ms so a 32-bit
count needs a 999/1000 correction or it falls behind about 63 s a day); page SETCLOCK
behind the window 30; key-authenticated clock (time carried inside a KEYAUTH
of n = 20) 25-35; clock-unset path ("000000" and stop) 20. Total 600-800:
350-550 left for bugfixes.

RAM: 188 bytes above the app's statics (0x1A4-0x25F), peak stack 73 measured
in the deepest scenario, 115 free. TOTP's W (64 bytes) can live in the core's
pageBuf (0x62-0xA1), idle whenever the app runs: an update never returns to
the app, so a page arriving mid-computation only spoils the code of a board
being rewritten. It is not 64-aligned (0x62), so tinytotp's `andi 63` wrap
needs an offset. S (20) and PRECOMP's nothing-else fit on the stack (about 26
with the 6 of depth) or in a Y-relative buffer. Raw key: H (20) more, and the
key itself (up to 64 bytes) has to be read twice.

Sealed seed: streamed through secret_byte, not buffered. PRECOMP preferred:
40 bytes of midstates in the macro, read once each in order, nothing buffered,
the page computes them from the base32 (about 30 lines of JS, SHA-1 compress).
Raw key would mean reading the key twice (inner and outer block), so buffering
it (20 bytes for typical seeds) or re-streaming. XTEA per byte is about 0.4 ms
in the main context, as the secret bytes are now; corePoll keeps running
between bytes. The page has to be told a sealed seed uses 40 bytes of table.

Clock set: page 0x6C behind the window and password; digimacro-host by making
KEYAUTH accept n = 20, key then four bytes of time: the time is taken only
when the key is right. Cheaper than a MAC and a rogue program cannot do it
without the key. Replay needs a USB capture, by which time the key is already
out. A "key held" gate alone would let anything set the clock for the minute
after a client ran; not recommended.

Clock unset (owner's decision): `{totp}` types "000000" and stops the macro.

## Phase 3 owner decisions (estimate only, not built)

- Clock settable from the page (behind the window) and from digimacro-host
  (key-authenticated).
- `{secret!}{totp SECRET}` must work: seed sealed in a secret region.
- Clock unset: `{totp}` types "000000" and then stops the rest of the macro.
