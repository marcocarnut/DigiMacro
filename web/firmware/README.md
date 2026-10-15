# What the app installs

For v2 boards, written by `make release`:

- `digimacro-v2.hex`: the firmware above the core, for updating a v2 board
  over WebHID.
- `digimacro-v2-core.hex`: the core alone, for setting a board up through
  Micronucleus; the board then asks for the firmware above.
- `digimacro-v2-full.hex`: both in one image, for a programmer.
- `digimacro-v2.json`: the firmware's version, the core's, and the core API
  the firmware was built for. The app offers the firmware only to a board
  whose core speaks that API.
- `digimacro-uninstall.hex`: the uninstaller, for **Back to a plain
  Digispark**: it wipes the EEPROM and puts Micronucleus 2.6 back.
- `digimacro-coreupdate.hex`: the core updater, for a board on core 3 or 4
  (API 1, the boundary at `0x1000`): it puts the core above in place of
  theirs, and the board then asks for the firmware.
- `digimacro-uninstall-api1.hex`: the uninstaller for such a board, byte for
  byte v3.0's.

v1 boards get nothing of their own: the page moves them to v2, writing
`digimacro-v2-core.hex` through their Micronucleus as it does for a new
board. v1.0's firmware 9 is at tag `v1.0`.

The core goes on through whatever Micronucleus the board has, 1.x or 2.x
(`web/micronucleus.js` speaks both), and the firmware then overwrites it,
after the core has taken the reset vector back. Nothing here is
Micronucleus's own: the page ships none of it.
