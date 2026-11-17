# What the app installs

Written by `make release`:

- `digimacro-v2.hex`: the firmware above the core — the assembly app,
  `app/asm/digimacro.S`, as it ships — for updating a board over WebHID.
- `digimacro-v2-core.hex`: the core alone, for setting a board up through
  Micronucleus; the board then asks for the firmware above.
- `digimacro-v2-full.hex`: both in one image, for a programmer.
- `digimacro-v2.json`: the firmware's version, the core's, and the core API
  the firmware was built for. The app offers the firmware only to a board
  whose core speaks that API.
- `digimacro-uninstall.hex`: the uninstaller, for **Back to a plain
  Digispark**: it wipes the EEPROM and puts Micronucleus 2.6 back.
- `digimacro-coreupdate.hex`: the core updater, for a board on an older core
  (API 1, the boundary at `0x1000`): it puts the core above in place of
  theirs, and the board then asks for the firmware.
- `digimacro-uninstall-api1.hex`: the uninstaller for such a board.

A board that still runs beside its Micronucleus gets nothing of its own: the
page moves it, writing `digimacro-v2-core.hex` through its Micronucleus as it
does for a new board.

The core goes on through whatever Micronucleus the board has, 1.x or 2.x
(`web/micronucleus.js` speaks both), and the firmware then overwrites it,
after the core has taken the reset vector back. Nothing here is
Micronucleus's own: the page ships none of it.
