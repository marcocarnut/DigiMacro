# What the app installs

- `digimacro.hex` and `digimacro.json`: the released firmware and its version,
  written by `./build.sh release`. The app offers it to any board running an
  older version.
- `micronucleus-2.6-t85_default-upgrade.hex`: the unmodified upstream
  Micronucleus upgrader (`firmware/upgrades/upgrade-t85_default.hex`,
  upstream tag v2.6, commit 6dca90b, 2021-11-04). It is an application that,
  uploaded through whatever Micronucleus a board has (1.x included), rewrites
  the bootloader area with Micronucleus 2.6 and reboots into it. Copied from
  DigiISP, which uses it the same way.

Micronucleus 2.6 for the ATtiny85 (`t85_default`): bootloader at 0x1A00,
entry on every reset, 6 s timeout, OSCCAL saved at 0x19FA.

Micronucleus is free software under the GNU GPL v2, like DigiMacro. Its
corresponding source is the upstream repository at tag v2.6:
https://github.com/micronucleus/micronucleus/tree/v2.6
