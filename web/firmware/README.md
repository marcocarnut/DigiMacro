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

For v1 boards, frozen:

- `digimacro.hex` and `digimacro.json`: v1.0's firmware 9, which the app
  offers to v1 boards on anything older -- except firmware older than 3,
  which cannot restart itself into the bootloader, and a hardened v1 board,
  which has none.
- `micronucleus-2.6-t85_default-upgrade.hex`: the unmodified upstream
  Micronucleus upgrader (`firmware/upgrades/upgrade-t85_default.hex`,
  upstream tag v2.6, commit 6dca90b, 2021-11-04). It is an application that,
  uploaded through whatever Micronucleus a board has (1.x included), rewrites
  the bootloader area with Micronucleus 2.6 and reboots into it. Copied from
  DigiISP, which uses it the same way.

Micronucleus 2.6 for the ATtiny85 (`t85_default`): bootloader at 0x1A00,
entry on every reset, 6 s timeout, OSCCAL saved at 0x19FA. A v2 board set up
through it overwrites it with the firmware, after the core has taken the
reset vector back.

Micronucleus is free software under the GNU GPL v2, like DigiMacro. Its
corresponding source is the upstream repository at tag v2.6:
https://github.com/micronucleus/micronucleus/tree/v2.6
