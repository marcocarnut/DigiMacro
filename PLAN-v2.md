# DigiMacro v2 — plan

The firmware updates itself, and Micronucleus goes.

Marco's idea, written down while v1 is being finished so that none of it is
lost. Nothing here is built yet. v1 — `PLAN.md` — is the working version,
and stays the shipping one until v2 has earned its place.

## The idea

A board today carries **two USB stacks**: V-USB in DigiMacro, and another
copy inside Micronucleus, which exists only to rewrite the first. v2 keeps
one.

- The flash splits at a fixed boundary into a **core**, low, and the **app**,
  high. The core holds the USB stack, the HID descriptors, the feature-report
  handling and a flash writer. The app is everything that makes it DigiMacro:
  touch, morse, the table, playback, the password, the lockout, the host check.
- A new verb on the vendor HID channel writes flash pages, **refusing any
  address below the boundary.** The core can never rewrite itself; the app can
  be replaced whole.
- The linker places the core below the boundary and the app above it.
- Micronucleus is gone. Its 1.5 KB come back.

What it buys:

- **One device, one API, one picker.** No reboot into a different kind of USB
  device, no WebUSB beside WebHID, no second Chrome permission, no Zadig on
  Windows, no udev line for Micronucleus.
- **Updates on locked boards.** Lock bits restrict outside programmers, not
  the chip writing its own flash, so a hardened board takes updates from the
  page too.
- **About 1–1.3 KB more for features** (below).

## Why it can work

- **The ATtiny85 has no boot section.** Code anywhere in flash may execute
  SPM, so nothing in the chip stands in the way, provided SELFPRGEN is
  programmed.
- **The lock bits do not stop SPM.** LB1/LB2 restrict serial and parallel
  programming, not self-programming.
- **A page write halts the CPU for about 4.5 ms,** which V-USB cannot survive
  in the middle of a transaction. Micronucleus already solves this, and the
  same answer fits here: the page arrives in RAM over feature reports, the
  device replies, and the erase and write run *after* the reply, while the
  host waits before its next request. It is the pattern REBOOT uses already:
  note it in the callback, do it in the main loop. **Never poll USB from a
  V-USB callback** — see `PLAN.md`.
- **The interrupt vectors live in page 0, inside the core,** so an update
  cannot change them. That is fine as long as the app needs no interrupt of
  its own. Today it needs none: V-USB's and the millisecond timer's both
  belong in the core, and the entropy routine polls the watchdog flag rather
  than taking its interrupt.

## The hard parts

In rough order of difficulty.

1. **Two programs that must agree forever.**
   - The core and the app are linked separately, and call each other through
     fixed jump tables.
     - The core offers the app hidKey, hidPoll, hidAllow and the rest.
     - The app offers the core what are now weak hooks: hidPassSet,
       hidUnlock, hidLockState, hidHostAuth, hidHostState.
   - RAM must be split so the app's `.data` and `.bss` never land on the
     core's: V-USB's buffers, the feature report, the serial descriptor. The
     stack is shared.
   - arduino-cli cannot do any of this, so the build becomes a plain avr-gcc
     Makefile with two links, the second against the first's symbols.
   - Link-time optimisation can no longer cross the boundary, which costs
     some bytes.
2. **The core can never be fixed in the field.** A bug in it is permanent,
   short of a programmer. It has to be small, boring, and tested hard before
   anything ships on it.
3. **An update interrupted half way.** If power fails mid-write, the next boot
   must see that the app is incomplete — a checksum over the app region, or a
   "valid" mark written last — and stay in a core-only update mode. Which gate
   applies then is a real decision:
   - the password lives in EEPROM, and the core can check it;
   - but the hold needs the touch code, which lives in the app. Moving touch
     sensing into the core costs space.

   Accepting the password alone in recovery mode is a reasonable answer. What
   to do with a board that has no password in that state needs deciding too.
4. **Getting existing boards onto v2.**
   - **With a programmer** it is simple: erase, write core and app. Marco has
     one.
   - **Without one,** an installer uploaded through Micronucleus has to
     overwrite page 0, the page it is itself running in. It would have to copy
     itself up into Micronucleus's area at 0x1A00 and finish from there — the
     step that can brick a board. Try it on a spare board first, and do it
     last.
5. **The core has to own the update gate.** The app is what is being
   replaced, so the decision to accept a new one cannot live in it. The core
   checks the window and the password before entering update mode, and once
   it has, nothing in the app is called until the new one is complete and
   valid.

## Size

| | |
|---|---|
| the core | V-USB (assembly and C), the descriptors, the feature-report dispatch, the page writer, clock calibration, the timer |
| guess | **2–2.5 KB**, so the boundary lands nearer `0xA00` than the `0x800` first imagined |
| the app | today's firmware less its USB half, plus the jump tables |
| gain | Micronucleus's 1.5 KB, less jump tables, padding to a page boundary and lost link-time optimisation: perhaps **1–1.3 KB net** |

Measure, don't trust this table: the spike will say.

## Security

**Better than today, provided the core gates correctly.**

- **Flashing gives nothing new.** Anyone with the password and the hold can
  already read every secret through the app, so writing flash adds nothing.
  Micronucleus, by contrast, accepts anyone who plugs the board in, with no
  authentication at all.
- **A bad USB firmware.** Someone who has the password and holds the pad
  could flash firmware that types something malicious later. That is equally
  possible today through Micronucleus, with no password at all.

What changes for hardened mode:

- **SELFPRGEN must stay programmed:** extended fuse `0xFE`. v1's hardened mode
  first asked for `0xFF`, and dropped it: v1 contains no SPM instruction, so
  the fuse was of no consequence there, and asking for `0xFE` from the start
  means nobody has to change it for v2.
- **Protection becomes the password** — guessed at one per second, with the
  pad held — instead of "only a full chip erase gets in". With a long
  password that is as strong. `HARDENED_MODE.md` needs rewriting to say so.
- **An owner's option to wipe the EEPROM before accepting an update** would
  keep hardened mode's v1 guarantee for anyone who wants it, at the cost of
  restoring a backup after every update.

## A spike first

As the vendor collection was proven, before anything of DigiMacro moves:

- **a tiny core:** V-USB, the HID collection, the page writer, linked at
  `0x0000`;
- **a trivial app**, linked above the boundary, that types one line;
- **the page** uses the core to replace that app with a second one that types
  a different line.

What it must prove, on hardware:

- the two-part link, and the jump tables both ways;
- the shared interrupts and timer;
- page writes without dropping USB, through a full app replacement;
- recovery from an update pulled out half way;
- the measured core size, which sets the boundary.

If the spike works, porting the app is mostly mechanical. The installer for
boards without a programmer comes last.

## Open questions

- The boundary address, once the spike measures the core.
- The recovery-mode gate: password only, and what a board with no password
  does then.
- Whether touch sensing belongs in the core after all.
- The update verb's shape: one page per pair of 32-byte reports and a commit,
  or a page buffer filled at offsets.
- How an app proves it is valid: a CRC over the region, or a mark written
  last — the way Micronucleus writes its last page last.
- The wipe-before-update option for hardened boards: default off?
- Whether v2 keeps protocol version 2 with new verbs, or becomes protocol 3.
