# DigiMacro v2 — plan

The firmware updates itself, and Micronucleus goes.

Marco's idea, written down while v1 was being finished so that none of it
was lost. The spike is built and proven on a board — see *The spike, as
built*, which supersedes the guesses before it — and DigiMacro has not yet
moved onto it. v1 — `PLAN.md`, tagged **v1.0** at firmware 9 — is the working
version, and stays the shipping one until v2 has earned its place.

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
     - The core offers the app hidKey, hidPoll, hidAllow, hidAllowed,
       hidReady, hidAwake and the rest.
     - The app offers the core what are now weak hooks: hidPassSet,
       hidUnlocked, hidUnlock, hidSetPass, hidLockState, hidHostAuth,
       hidHostState, hidHostForget.
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
| guess | **2.5–3 KB**, so the boundary lands nearer `0xC00` than the `0x800` first imagined — see *What v1 taught* |
| the app | today's firmware less its USB half, plus the jump tables |
| gain | Micronucleus's 1.5 KB, less jump tables, padding to a page boundary and lost link-time optimisation: perhaps **1–1.3 KB net** |

Measure, don't trust this table: the spike will say. *It did: a 3812-byte
core, the boundary at `0x1000`, and about 1–1.3 KB free — see* The spike, as
built.

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

## What v1 taught, by v1.0

Written after the plan above, from what v1 became. Start v2 from here.

- **The core is bigger than first guessed.** Measured on firmware 9's ELF
  (link-time optimisation blurs it, so this is rough): `usbPoll` 874 bytes
  with the request handling and calibration inlined, most of `vndAnswer`'s
  802, V-USB's assembly interrupt handler (no size symbol; roughly 500–600),
  the descriptors around 300, `initSerial` 244, `sendReport` 152, the timer
  interrupt 142, `hidPoll` 116. Hence 2.5–3 KB and a boundary nearer `0xC00`.
  v1 is 5966 of 6650 bytes; without Micronucleus the chip has 8192.
- **The core does more than USB now:** the serial number (made from watchdog
  jitter when the EEPROM has none), the fuse and lock read at boot (with
  interrupts off: the core's millisecond timer is already running), the
  window (`hidAllow`/`hidAllowed`), the RESTART verb, the INFO answer, and
  wiping the report buffer after anything that carried a secret. All of it
  belongs in the core, which makes the size above believable.
- **The millisecond timer is Timer1 on the Digistump core,** and both halves
  read it. 16-bit millisecond comparisons wrap in 65 seconds, so anything
  compared against them must be asked more often than every 32 — v1's
  `clocks_poll()` does it between every step of a macro. The core's window
  and the app's clocks both depend on that.
- **Every build calibrates its own clock** at each bus reset (`osccal.h`,
  included by `usbconfig.h`), so a board without Micronucleus needs nothing
  more for USB. v1's hardened build proves it on hardware.
- **Never poll USB from inside a V-USB callback.** Note in the callback, act
  in the main loop, as REBOOT and RESTART do. The page writer must follow it:
  a 4.5 ms SPM inside a callback would be the same mistake.
- **The hardening check changes meaning.** v1's app says "(hardened)" for no
  bootloader, lock byte `0xFC`, EESAVE unprogrammed and debugWIRE off, keyed
  on INFO's no-bootloader bit `0x80`. In v2 every board has no bootloader but
  can write its own flash, so that bit and the checklist need rethinking:
  probably SELFPRGEN programmed becomes required, and the wipe-before-update
  option, if built, joins the list.
- **The app has the patterns to reuse:** `run()` with its hold-and-password
  access, `busy` and device affinity; the note-then-act RESTART after a
  restore; the firmware dialog; backups, which make "erase and come back as
  itself" routine. `web/micronucleus.js` stays for v1 boards until they move.
- **Testing the page without hardware worked well:** a fake board in
  JavaScript speaking the feature-report protocol, loaded ahead of the app in
  headless Chrome, scenarios that wait on results rather than clock times
  (scrypt is real CPU time under a virtual clock). It lived in a scratch
  directory and is gone; for v2 it belongs in `test/`, with the page writer's
  protocol in the fake.
- **Host tests do not reach the USB library.** The firmware review found the
  gate matrix, READ/WRITE clamping, report-ID stripping and `restart()` all
  untested. A core that can never be fixed in the field must not repeat that:
  give it a host build against a stub `usbdrv.h` from the start.
- **Hardware rhythm that worked:** every firmware change confirmed on a board
  before committing; Marco has a stock Digispark, a hardened one, an XGecu
  driven by minipro for high-voltage programming, and Windows 10 to test on.

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
- the measured core size, which sets the boundary;
- a host-side test of the core's request handling, before it is ever
  frozen.

If the spike works, porting the app is mostly mechanical. The installer for
boards without a programmer comes last.

## The spike, as built

Built on branch `v2`, in `spike/` -- since split into `core/` (the core,
its V-USB, its host test) and `app/` (what every app links, the spike's
apps, `spikectl.py` and the spike page), built by the top-level `Makefile`
into `build/v2/` -- and tried on a stock Digispark beside a
Linux PC, October 2026. Every item on the list above held on the board.

### What it is

- **A core at `0x0000`** and **an app at the boundary**, linked separately
  by a plain avr-gcc `Makefile`. `layout.h` is the only thing they share:
  where each lives, the two jump tables, the app's header, the RAM split and
  INFO's layout. Once a core is on a board that file is frozen for it: add at
  the end, never move or renumber.
- **The app's header**, at the boundary:
  - a table of rjmps the core calls through: setup, loop, request,
    may-update, then the chip's fourteen interrupt vectors;
  - then the magic `DM`, the image's length, the app's version, and the core
    API it was built for. A CRC-16 of the image follows the image.
  - The app has no C runtime of its own. Its first slot runs the `.init`
    sections, which copy `.data` and clear `.bss`, then calls its setup.
- **The core's table**, sixteen rjmps just below the boundary: poll, ready,
  awake, send a report, restart, the report buffer, and the four password
  calls. An app calls a slot, never a function.
- **The update:**
  - PAGE takes half a page per report. The second half asks for the write,
    and the main loop does it once the host has seen its answer: never from
    a callback.
  - Every page but the header's erases the header first, so the header goes
    last.
  - COMMIT runs the same check the core runs at boot (magic, API, length,
    CRC), then restarts into the new app without a reset. The spike's boards
    still have Micronucleus, which owns the reset vector.
- **Each erase and each write waits for its moment.** It hands the host an
  empty keyboard report, and halts the instant it has been taken: the next
  poll of the interrupt endpoint is then a whole interval away, room for one
  4.5 ms halt. Without that, the halts would meet the host's polls, and
  Linux's usbhid resets a HID device whose errors last a second. There were
  no USB errors in the kernel log through dozens of updates.
- **Tools:**
  - `spikectl.py` drives the protocol over hidraw: info, flash, read,
    unlock, setpass, and slow or stopped updates for testing interruptions.
  - `test/` compiles the real `core.c` and `proto.c` for the host and asks
    them everything: 1307 checks. Each deliberate breakage tried was caught.

### What was decided, and why

- **The core keeps only what nothing else can do.** Marco's line:
  - enumerate and identify (V-USB, the descriptors, the serial read from
    EEPROM);
  - read flash and fuses;
  - write the app's pages, and decide whether there is an app to run;
  - a minimal INFO;
  - check the password.

  Everything else moved into the app, in `proto.c`, linked into every app:
  - the clock, the window, typing with its timeouts;
  - RESTART, READ, WRITE, SETPASS and HOSTAUTH;
  - INFO's other state bits;
  - making the serial, in the app's setup, which runs before USB connects so
    that a second with interrupts off costs nothing.

  It took about 950 bytes out of the core. More to the point, v1's gate
  matrix and its READ/WRITE clamping, where v1's last review found bugs, are
  now code an update can fix.
- **The app owns time, so the core forwards interrupts.** Every vector but
  V-USB's jumps to a slot in the app's header. Before taking a page, the core
  switches off every source an app may have enabled, the watchdog included,
  since the code they would jump to is about to be rewritten. The core's own
  waits are counted turns of poll and 100 µs.
- **The core checks the password; the app sets it.** Again Marco's call:
  verification is the core's and the app calls it as a subroutine. The reason
  is the board left with no app by an update cut short: its EEPROM still
  holds every secret, and a core that took pages from anyone then would let
  someone flash an app that reads them out.
  - The core compares the guess in full, so timing gives nothing away.
  - A wrong guess costs a second, taken in the loop and never in a callback;
    guesses in that second are turned away unread.
  - The core keeps "unlocked" for the session.
  - PAGE needs it whenever a password is set, app or no app.
  - With no app the core answers UNLOCK itself; there is no hold, since
    touch is the app's. A board with no password is as open as v1 was to
    Micronucleus.
  - The lockout stays the app's: a wrong password is a wrong code, and the
    right one clears it.
- **The app has no say in updates.** At first the core asked the app,
  through `app_may_update()`, whether somebody was at the board. Marco
  dropped it: an app that can refuse an update can keep itself on the board
  for good, buggy or not, and the password already guards a board that
  wants guarding.
- **An update window, as Micronucleus has.** Marco's idea:
  - GPIOR2 counts tenths of a second. The core zeroes it at boot, and the
    app's clock counts it, stopping at 255.
  - While an app runs, the first page is taken only within six seconds:
    after a replug, or after the app opens the window again with verb `0x68`,
    which is behind the hold and the password.
  - With no app there is no window, only the password.
  - An app that never counts leaves the window open: it fails open, never
    shut. The core's whole cost is one compare against a register.
- **The watchdog catches an app that stops polling.**
  - The core arms it, two seconds, after the app's setup, and `hidPoll()`
    feeds it.
  - An app that hangs is reset by it, and the core then does not start it
    again. It comes up core only, `DM_CORE_HALTED` in INFO, and takes an
    update behind the password.
  - A replug is a power-on, and runs the app again.
  - The reset reason is read from MCUSR, or from GPIOR0, where a
    Micronucleus built with `SAVE_MCUSR` leaves it. The spike's boards have
    such a build.
  - This was the one gap no gate could close: an app that never polls USB
    hears nothing, password or not.
- **The core's flags are bits of GPIOR0,** reached with SBI, CBI and SBIC
  instead of LDS and STS, which saved 62 bytes. GPIOR1 stays the app's: v1
  keeps its flags there. GPIOR2 is the update window.
  This costs about 300 bytes.
- **FREAD is ungated.** Flash is published firmware; the secrets are in
  EEPROM.
- **Protocol 3.**
  - INFO keeps v1's first twelve bytes where they were, and adds the core's
    state, the app's version, the API, the app region and the page size.
  - The core fills its own bytes, plus the password and unlocked bits.
  - The app fills the EEPROM size and the rest of the state byte.
- **The core carries its own copy of V-USB** (`core/vusb`). It is pinned
  forever anyway, and its `usbconfig.h` lets V-USB build the device and
  configuration descriptors itself.

### What it measured

| | |
|---|---|
| the core | **3812 bytes**: V-USB about 1.7 KB of it (C 930, assembler about 790), the descriptors, the request plumbing, the page writer, the app check, the password, the window and the watchdog |
| boundary | **`0x1000`**, Marco's call: 4 KB each side, with the core's table at `0xFE0` and 252 bytes to spare under it for fixes the port may need. It is final once a board ships with this core |
| for the app | 4096 bytes below `0x2000` |
| v1's app half plus `proto.c` | roughly 2.7–3 KB, so **about 1–1.3 KB free**, against v1's 684 |
| an update | 18–22 pages in under a second |

The first estimate, 2.5–3 KB, was before the core had a password, a page
writer and an app check. The plain port of v1's library measured 4552
bytes. Moving the protocol into the app is what made v2 worth its space.

For the same features, v2's code will be larger than v1's, not smaller.
v1 was 5966 bytes; v2 will be roughly 6.5–6.8 KB. The extra pays for the
page writer, the app check, the password in the core, the two tables and the
vector forwarding, `proto.c`'s own clock, two start-up codes, and the
inlining lost at the boundary. What v2 wins is room: the whole chip rather
than what is left under Micronucleus. Trimming further was tried and gave
almost nothing (options 4 bytes; the rest unrolled back); the core is
mostly V-USB.

### What it taught

- **Two clients share one report.** Twice an update stalled. The v1 app in
  a Chrome tab had the board open, and its INFO landed between the update
  tool's question and its read of the answer. The board has one report
  buffer for everybody. The core was right throughout: nothing was written
  wrong, and the cut-short update left no app to boot. The host has to cope:
  trust only an answer that echoes its own command and address, and check
  each page by reading the flash back rather than by its acknowledgement.
  The v2 page needs the same from the start.
- **Chrome stretches timers in a tab that is not in front** to a second or
  more: the page's first update took 17 s instead of 0.7. The update loop
  now waits on HID requests, which are not slowed, by reading each page back
  until the flash holds it. About 200 of those requests meet a halted board
  in an update and fail within a millisecond each, harmlessly.
- **Linux writes a keyboard's lock lights only when one is on.** An app
  waiting for `hidAwake()` alone never types with every light off; v1's
  fallback to a longer wait is needed, as v1 already knew.
- **An rjmp to a plain number is assembled as an offset.** Jumps to fixed
  addresses go through symbols the linker places (`--defsym`).
- **make's built-in rules will build `apphex.py` from `apphex.py.o`,** and
  from `app.c` at that, overwriting the script with an ELF. The Makefile
  says it is a source.

## Open questions

Answered by the spike:
- **The boundary:** `0x1000`.
- **The recovery-mode gate:** the password alone, checked by the core. A
  board with no password takes an update from anyone, as with Micronucleus.
- **Touch in the core:** no. The core is minimal; recovery needs no hold.
- **The update verb's shape:** half a page per report, the second half
  writing it, and the header page last.
- **How an app proves it is valid:** both. The header erased before
  anything else is written, and a CRC-16 over the image checked at boot and
  at commit.
- **Protocol 2 or 3:** 3.
- **Windows:** the spike page (`app/spike.html`, `make page`), in Chrome on
  Windows 10 under VMware, put on apps A, B and C, and survived a cable
  pulled in the middle of a slow update. It reconnected by itself and showed
  "no app". VMware complained for a while each time the board reappeared --
  after a commit, and on these boards through Micronucleus's own device after
  a replug -- until it had handed the new device to the guest; the page then
  worked. A board without Micronucleus appears once per replug.

Still open:

- **Windows on bare hardware.** Tried so far only in a VMware guest; see
  below.
- **The wipe-before-update option** for hardened boards: default off?
- **The hardening check in v2.** Every board writes its own flash, so what
  "(hardened)" means has to be rethought: SELFPRGEN programmed becomes
  required, and the wipe option, if built, joins the list.
- **The installer** for boards without a programmer, last, on a spare board.
