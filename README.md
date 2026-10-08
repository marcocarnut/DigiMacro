# DigiMacro

A morse-activated macro keyboard on an ATtiny85 Digispark, configured from a
web page.

Touch a pad in dots and dashes; if the code matches an entry in the board's
table, the macro is typed as keystrokes. Hold the pad for four seconds and a
page in Chrome or Edge may read and write the table over WebHID. There is no
terminal, and the board never stops being an ordinary keyboard: Windows needs
nothing installed, and Linux needs one udev rule.

> ### ⚠️ Storing passwords or secrets? Read [HARDENED_MODE.md](HARDENED_MODE.md) first — all of it.
>
> **A board as it comes protects nothing from anyone who holds it.** Its
> bootloader accepts a new sketch every time it is plugged in, and a sketch
> that types out the EEPROM takes a minute to upload: every macro, the
> password and the host key along with it. The password, the lockout and the
> host check are all part of the firmware that sketch replaces, and nothing in
> the app can change that.
>
> Hardened mode removes the bootloader, sets the chip's lock bits and leaves
> EESAVE unprogrammed. It needs an ISP programmer — a second Digispark running
> [DigiISP](https://digiisp.postcogito.org) will do — and it gives up
> firmware updates from the page. Even then it stops a determined person, not
> a laboratory, and it does nothing about the computer the board types into.
> The document explains each of those trade-offs. Do not skip it.

**5576 bytes of the 6650** the stock bootloader leaves, and 200 of RAM.

## What you need

- **A Digispark** (ATtiny85, Micronucleus bootloader) or a clone. New ones
  usually come with Micronucleus 1.x, which is fine; the app can upgrade it.
- **A touch pad on P0.** Any small piece of metal will do — a bare wire, a
  coin, a square of copper tape — and no other parts are needed. A pushbutton
  from P0 to ground works too (`./build.sh button`), as does a pad read
  through a bleed resistor of about 1 MΩ (`./build.sh rc`).
- **The board's LED on P1**, which is where the Digispark has it. A clone with
  its LED on P0 needs `PIN_LED` changed in `DigiMacro.ino`, since P0 is the
  pad.
- **Chrome or Edge**, on a computer: WebHID, and WebUSB for firmware updates,
  are not in Firefox or Safari.
- **To build the firmware:** `arduino-cli` and the Digistump AVR core.
  `./build.sh` says how to install both if either is missing. The tests also
  need `g++` and `node`.

## Getting started

1. **Build and flash the firmware.** Run `./build.sh upload`, then plug the
   board in when Micronucleus asks for it.
2. **On Linux, let the browser reach the board.** Every hidraw device is
   root-only until a udev rule says otherwise:
   ```
   sudo cp web/60-digimacro.rules /etc/udev/rules.d/
   sudo udevadm control --reload
   ```
   Then unplug the board and plug it back in.
3. **Open the app.** WebHID needs a secure context, and `localhost` counts:
   ```
   cd web && python3 -m http.server 8000
   ```
   Then open `http://localhost:8000/app.html`. Any https host serving `web/`
   works the same way.
4. **Connect.** Pick the board, then **hold the pad for four seconds**. The
   page reads everything the board holds by itself; if the board has a
   password, type it and press Unlock.
5. **Write some macros**, press **Write to the board**, and tap a code into a
   text editor.

## Tapping

A press shorter than a quarter of a second is a dot, and a longer one is a
dash. 700 ms of silence ends the code. A code is one to five symbols, so 62
codes exist. Holding the pad for four seconds taps nothing; it opens the
window during which the app may read and write the table, for half a minute.

Touching the pad stops whatever is playing. The pad is read between
keystrokes — a touch reading is a quarter of a millisecond against a
keystroke's twenty — and during pauses, so even a macro that jumps to itself,
which runs for ever by design, stops when touched. That press is not also
counted as a symbol, and holding it on opens the window, so a runaway is
stopped and repaired with one gesture. The one loop that touch cannot stop is
one that never types or pauses at all. The firmware bounds that by counting
jumps, and the app refuses to write one.

### What the LED says

| LED | means |
|---|---|
| off | idle, and also while you tap a code, so it does not spell the code out for anyone watching |
| on, steady | a macro is typing or pausing; touch the pad to stop it |
| three quick blinks | the code matched nothing |
| slow blink, once a second | the window is open: the app may read and write the table for half a minute |
| a short blip every two seconds | soft lock: too many wrong codes, so every tap is ignored until the wait is over |
| fast flicker, eight times a second | hard lock: every tap is ignored until the password is given in the app |

The window's blink wins over both locks, so you can see the hold took while
the board is locked.

## The app

`web/app.html` shows one thing at a time:

- **Nothing connected:** only Connect.
- **Connected:** the hold, and the password if the board has one, typed before
  or after the hold, as you like.
- **In:** everything the board holds, already read.

**Write to the board**, at the right of the status line, is enabled only when
what is on screen differs from what the board holds, compared as the bytes
that would be written. Every other action reports on a line under its own
button. The window lasts half a minute and editing takes longer, so when it
has lapsed an action asks for the hold again, waits for it, and gives the
password again by itself. The page keeps the password only while the board
stays plugged in.

The sections are **Macros**, with the keyboard layout picker beside the
heading; **Keys**, a palette that inserts anything below at the cursor;
**Startup**; **Host**; **Password**; **Lockout**; and a reference. The app
speaks English and Brazilian Portuguese, picked from the browser and changed
at the top right.

### Writing macros

A macro is text, with braces for what text cannot say:

```
 hello{Enter}                 anything is typed as itself
 \n  \t  \\  {{               newline, tab, backslash, a literal {
 {Ctrl+c}  {Alt+Tab}  {F5}    modifiers, and keys that are not characters
 {pause 1.5}                  wait, in seconds
 x{11}                        eleven of them, as in a regular expression
 (ab){3}                      ...of a whole group rather than one thing
 {goto .-}                    carry on with another macro, and never come back
 {host}  {host!}              stop unless this computer sent the host key lately
```

`{11}` means eleven in all, as a quantifier does in a regular expression, and a
count counts whatever precedes it: one keystroke, one accented letter, or a
bracketed group. A parenthesis is only a parenthesis unless a count follows
the closing one.

The editor encodes as you type, so a mistake is reported beside the macro
that has it, and the byte budget — 480 in all — is always on screen. Nothing
is written until every macro is valid.

**Accented characters are the host's business, not the board's.** The board
sends key presses, so what appears depends on the keyboard layout of the
computer it is plugged into. Pick that layout beside the Macros heading and
`coração` is accepted and expanded into the accent and then the letter. Leave
it at plain US, where there is no way to type one, and the editor says so
rather than typing something else.

### Startup

One macro can type itself when the board is plugged in. It is stored as the
code rather than a position in the table, so reordering the macros cannot
point it somewhere else, and a code that is no longer there types nothing.

It waits for the host to have somewhere to put the keystrokes, which is not
the same as being plugged in. The host reads the descriptors early and
attaches a keyboard to its input stack some time later, and a macro that
started in between would lose however much of itself fitted in the gap. So
the board waits for the host to send it a lock-light report — the host
talking *to* the keyboard, which it only does once there is one — and gives
up waiting after a second and a half in case a host never does.

## Security

What follows is layered, optional, and absent on an erased chip: a board that
types eleven right-arrows at a television should not ask anybody for
anything. None of it protects the EEPROM from someone with the board and a
laptop; [HARDENED_MODE.md](HARDENED_MODE.md) is for that, and sets out what
each layer stops and what it does not.

### The hold

Reaching the table needs the four-second hold, which no web page or program
can do for itself. On a board with no password, it is the only thing between
a hostile page and the macros.

### The password

A *second* gate on the table. The window says somebody is at the board; the
password says they are the right somebody, which a thief holding the board is
not. It does not gate *playing* a macro: a device that wants a password before
it will type is a device nobody would carry.

A wrong answer costs a second, and with the lockout on it counts as a wrong
code. That is all: the password is never refused for being locked, because it
is what lifts a hard lock, and a limit on it is the one lock that can shut the
owner out too. Its defence against guessing is its own length, up to twelve
characters. The comparison runs to the end whatever happens, so its timing
says nothing about how much of the answer was right. The unlock lasts as long
as the window and lives in RAM, so unplugging the board locks it again.

### The lockout

It needs the password, since giving the password is the only way to lift a
hard lock. Two stages, so that the owner can fumble a code without needing
the app:

- **Soft lock.** After a set number of wrong codes, the board ignores every
  tap, right or wrong, for a set time: a minute by default, up to forty.
- **Hard lock.** After a set number of soft locks in a row, it ignores taps
  until the password is given in the app. The power-on macro stops too, or a
  thief would only have to plug the board in again.

A macro that actually plays clears both counts. So someone guessing gets at
most *wrong codes × soft locks* tries before the board stops listening, which
at 1 and 3 is three, out of 62 codes. The app works out those odds for the
settings you choose.

Everything is kept in EEPROM, so unplugging the board gets nobody anywhere.
The board has no clock to tell it how long it was unplugged, so a soft lock
found at power-up starts its whole wait again. A wrong code is counted
*before* the three blinks that report it, so pulling the plug as soon as the
blinks start saves nothing. A code that plays writes nothing unless there is
a count to clear, so ordinary use causes no EEPROM wear.

### Trusting the host

A USB device has no idea which computer it is plugged into, so a board that
types a password types it for whoever holds it. For a computer whose software
you control, there is an answer: a small client there sends the board a key,
and for a while afterwards — a minute by default, up to forty — macros
guarded by a host check play. Plugged into any other computer, they stop.

```
 {host}      stop here, quietly, unless this computer sent the key lately
 {host!}     stop as if the code were not in the table at all
```

`{host!}` is the one that matters against a guesser. Refused, it looks exactly
like a code that is not there — three blinks, and a wrong code for the
lockout — so on the wrong computer a guarded code cannot be told from an
empty one. That holds only if nothing shows before the check, which the app
warns about. The LED stays dark until a macro types or pauses, for the same
reason: a flash on the way in would give the code away. `{host}` may go
anywhere, for a macro that types a user name and then stops short of the
password. A refused macro does not count as a code that worked, so tapping a
guarded code on the wrong computer does not clear a guesser's misses.

The key is eight random bytes, made in the app's Host section, written to the
board, and downloaded as a file named after the board's serial number. The
client is `tools/hostauth`, one C file with no library, for Linux and
Windows. On Linux it needs the same udev rule as the app:

```
make -C tools/hostauth install            # into ~/.local/bin
mkdir -p ~/.config/digimacro
cat ~/Downloads/digimacro-*.key >> ~/.config/digimacro/host.key
chmod 600 ~/.config/digimacro/host.key
digimacro-host                            # once
digimacro-host -e 30                      # every 30 seconds, for as long as it runs
```

For Windows, `make -C tools/hostauth windows` cross-compiles
`digimacro-host.exe` with MinGW-w64 (`gcc-mingw-w64-x86-64` on Debian and
Ubuntu). It is a single file needing nothing installed beside it, and it reads
its keys from `%APPDATA%\digimacro\host.key`.

Run with `-e` from your session, and stopped when the screen locks, the trust
becomes "while I am logged in". The client finds every board plugged in by
its serial number, so one key file serves several boards. `-k` names another
key file; on Linux the default honours `$XDG_CONFIG_HOME`.

Sending the key is the one request, beside INFO, that needs neither the hold
nor the password, because nobody is at the board. So it can do nothing else:
it opens no window, unlocks nothing and reads nothing, and a wrong key does
**not** count against the lockout. If it did, any program on any computer
could hard-lock the board without anyone touching it. A wrong key costs a
second instead, on a clock of its own.

### Every board has a serial number

Four random bytes, made once on a chip that has never had one and kept in
EEPROM, rendered as eight hex digits after `postcogito.org:`. The prefix is
obdev's rule for `16c0:27db`, the shared ID for keyboards told apart by serial
number: the serial must begin with a domain the author owns, so that devices
from different people on the same ID cannot be confused. Chrome remembers a WebHID permission
only for a device that has a serial number, so without one every replug would
mean picking the board out of the chooser again. Making it per board rather
than per model means two boards get their own grants, and their own host keys.

The randomness comes from the watchdog oscillator's jitter against the CPU
clock: two independent RC oscillators, so the low bits of a timer sampled at
each watchdog timeout are unpredictable. It takes about a second, once in a
board's life.

## Updating the firmware

The status line shows the board's firmware version, followed by **(latest)**,
or by **(update…)** when the page carries something newer; that opens a
dialog with the update in it. The board restarts into its bootloader,
Micronucleus, and the browser asks you to pick it — a second picker, because
the bootloader is a different kind of USB device, plain vendor class reached
over WebUSB. The macros, the password, the lockout, the host key and the
serial number all live in EEPROM, which Micronucleus leaves alone, so they
survive.

- **Pick it within a few seconds.** The bootloader waits about six and then
  goes back to the old firmware, so the click that starts an update has to
  find the window already open. If it is not, that click asks for the hold,
  and the next click does the update.
- **An old Micronucleus is replaced first.** With the box ticked, which is the
  default, the app installs the upstream 2.6 upgrader, has you pick the new
  bootloader again (it waits for you this time), and then writes the
  firmware.
- **Firmware older than 3** cannot restart itself into the bootloader, so it
  needs one update with `./build.sh upload`. The page says so.
- **A hardened board** has no bootloader, and says *(hardened: no bootloader)*
  instead. See [HARDENED_MODE.md](HARDENED_MODE.md).
- **Linux** needs the Micronucleus line in `web/60-digimacro.rules`.
- **Windows** needs a driver for the bootloader once: run
  [Zadig](https://zadig.akeo.ie/) while the board is in Micronucleus, and
  install WinUSB for it. The keyboard itself needs nothing.

The REBOOT command sits behind the window and the password like everything
else, and for a sharper reason than the table does: a board in its bootloader
can be flashed with anything, including a sketch that reads the table out.

A new firmware reaches the app through `./build.sh release`, which copies the
build and its version number into `web/firmware/`. Commit that, and the page
offers it to every board running something older. The page carries the files
itself, so serving it needs no AVR toolchain. The uploader is
[DigiISP](https://digiisp.postcogito.org)'s, ported to plain JavaScript in
`web/micronucleus.js`.

## How it works

### It never stops being a keyboard

A HID keyboard is not something a browser will talk to: Chrome refuses
protected HID usage pages, keyboards among them. So the board carries a
second top-level collection on a **vendor usage page**, `0xFF00`, which WebHID
does hand over, while the keyboard half carries on being a keyboard. HID
needs no driver anywhere, so Windows needs nothing installed.

It cost two things. The keyboard gave up boot protocol, since two collections
on one interface need report IDs and a boot report may not have one, which
also caps it at three key slots rather than six — nothing that presses one key
at a time notices. And there is no landing page: Chrome offers one for a
WebUSB device, not a HID one. An earlier version re-enumerated as a WebUSB
device after the hold, and back again afterwards; `PLAN.md` tells that story.

### The protocol

One 36-byte feature report on the vendor collection, both ways. Set it to ask,
and get it to see the answer:

```
 [0] command   [1] address low   [2] address high   [3] count   [4..35] data
```

| | |
|---|---|
| `0x40` INFO | always answers: `[1..2]` "DM", `[3]` protocol (2), `[4]` firmware, `[5..6]` EEPROM size, `[7]` state |
| `0x41` REBOOT | into the bootloader, for a firmware update. Absent from the hardened build |
| `0x60` READ | address and count in, up to 32 bytes back |
| `0x61` WRITE | address, count and up to 32 bytes in |
| `0x62` UNLOCK | the password, if the board has one |
| `0x63` SETPASS | a new password, or none at all with a count of 0 |
| `0x64` HOSTAUTH | the host key. Needs neither the window nor the password, and does nothing else |
| `0xEE` | comes back in place of the command when a request is refused: the window is shut, the password has not been given, the password or host key is wrong or came within a second of a wrong one, or the command is not one this build has |

Everything but INFO and HOSTAUTH needs the window, and the password if one is
set. INFO's state byte:

| bit | |
|---|---|
| `0x01` | the window is open |
| `0x02` | a password is set |
| `0x04` | ...and has been given, or there is none |
| `0x08` | soft lock |
| `0x10` | hard lock |
| `0x20` | the host sent its key lately |
| `0x40` | there is a host key |
| `0x80` | built with no bootloader |

READ and WRITE reach the whole EEPROM, reserved bytes included — that is how
the app writes the settings, and why they sit behind both gates. A read past
the end returns `0xFF`, and a write past it stops. Writes go 32 bytes at a
time because an EEPROM byte takes 3.4 ms and the device can answer nothing
else while it writes. They use `eeprom_update_byte`, so a table saved
unchanged costs no wear.

Whether the report ID travels in the payload as well as in the request is the
one thing the HID specification leaves to taste. Chrome does not send one
back; the app looks rather than assuming, and the firmware accepts either,
which is safe because no command byte is 2.

### The table

The app rewrites the whole table, so entries are packed rather than slotted:

```
 [hdr][keystroke...][0x00]      repeated, in any order
 [0xFF]                         end of table
```

`hdr` is the code: three bits of symbol-count-minus-one, five bits of symbols,
a dash being a 1 and the first symbol in the highest bit that count uses. No
count above five exists, so `0xFF` ends the table and **a virgin chip reads as
an empty one** — nothing to initialise. Two bytes of overhead per macro, and
480 bytes to divide up however you like.

A keystroke is a HID usage rather than a character, because the app does the
layout mapping:

```
 bit 7     needs Shift
 bits 0-6  HID usage
```

Usages `0x01`–`0x03` are states a keyboard reports, never keys anyone
presses, so those three values are free to be escapes, and so are the same
three with bit 7 set. Hence the test for an escape: a byte whose low seven
bits are below four. All six are taken:

| | |
|---|---|
| `0x01 nn` | pause `nn` tenths of a second |
| `0x02 mm` | latch HID modifier mask `mm` into the next keystroke |
| `0x03 nn` | do the previous keystroke `nn` more times |
| `0x81 hh` | carry on with the macro whose header is `hh-1` |
| `0x82` | mark: what follows repeats as a group, not as one keystroke |
| `0x83 nn` | stop unless the host sent its key lately: quietly if `nn` is 1, as an empty code if 2 |

No argument may be zero, since a zero byte ends the macro. `0x03` is
run-length coding: "left arrow eleven times" costs three bytes rather than
eleven, and repeating *whatever went before* carries the latched modifiers with
it. `0x82` is what keeps that working once a keystroke stops being a
character: on a dead-key keyboard `ç` is two presses, the accent and then the
letter, so repeating "the previous keystroke" alone would type `çcccc`. A mark
says where the repeated thing began. It takes no argument — its position is
its argument — and marks do not nest.

This is the interface between the firmware and the app, so neither end is
trusted on its own: `web/table.js` is one original, shared by the app and a
test that plays what it encodes through the real firmware, and another test
reads the escape values out of `DigiMacro.ino` and checks the two agree.

### The EEPROM

| | |
|---|---|
| `0x000`–`0x1DF` | the macro table |
| `0x1E0`–`0x1E3` | the serial number |
| `0x1E4`–`0x1EB` | the host key; all `0xFF` for none |
| `0x1EC` | lockout: wrong codes before a soft lock; 0 or `0xFF` for no lockout |
| `0x1ED` | ...soft locks in a row before a hard lock; 0 or `0xFF` for never |
| `0x1EE` | ...a soft lock's length in tens of seconds, 1–240; anything else is 6 |
| `0x1EF` | ...wrong codes so far |
| `0x1F0` | ...soft locks in a row so far |
| `0x1F1` | ...hard locks lifted with the password, ever |
| `0x1F2` | how long the host stays trusted, in tens of seconds, 1–240; anything else is 6 |
| `0x1F3` | the power-on macro's header; anything impossible as one means none |
| `0x1F4`–`0x1FF` | the password, zero-padded; `0xFF` in the first byte for none |

The three lockout counters are stored inverted, so an erased byte reads as
zero. The reserved block is full: anything more will have to come out of the
table.

## Building

```
./build.sh upload               # the firmware, through Micronucleus
./build.sh                      # just build it
```

Options, which combine:

- `button`: a pushbutton from P0 to ground instead of a pad.
- `rc`: a pad read through a bleed resistor rather than the chip's pull-up.
- `ledecho`: the LED follows the input while tapping. For working on the input
  path only, since it shows a bystander the code.
- `touchdebug`: the board types its own pad readings twice a second, the only
  diagnostic channel a device with no serial port has. `web/README.md` says
  how to read them.
- `isp`: no bootloader, for [hardened mode](HARDENED_MODE.md). This one is
  flashed with a programmer, not uploaded.
- `release`: the stock firmware, copied into `web/firmware/` for the app to
  offer.

## Testing

```
test/run.sh
```

compiles the real `DigiMacro.ino` for the host against stub hardware and
exercises the table walk, every escape, the bounds that keep a malformed table
from reaching the reserved bytes, the morse decoding from the input pin, the
password, the lockout and the host check, and both ways of reading the pad.
Then the app's macro language on its own,
the two languages against each other, and — across the language boundary — a
table encoded by `web/table.js` played through the firmware. Then the firmware
the app ships, laid out for the bootloader, with both jumps decoded and
followed to where they land. Seven suites, about a second. Without `node`, the
four that need it are skipped.

## Layout

| | |
|---|---|
| `DigiMacro.ino` | the firmware: touch, morse, the table walk, playback, the password, the lockout, the host check |
| `DigiHidUSB/` | the V-USB stack: a keyboard, and the vendor collection the app talks to |
| `build.sh` | builds and flashes everything here |
| `test/` | host build of the real sketch against stub hardware, and the web side's tests |
| `tools/hostauth/` | the Linux client that sends the host key |
| `web/` | `app.html` is the app, `hid.html` a raw protocol page for developers, `table.js` and `i18n.js` what they share, `micronucleus.js` the uploader, `60-digimacro.rules` the udev rule |
| `web/firmware/` | what the app installs: the released firmware, and the Micronucleus 2.6 upgrader |
| `HARDENED_MODE.md` | how to make a board fit to hold secrets, and what that still does not protect |
| `PLAN.md` | design, decisions and how it got here |

## Related

- [DigiISP](https://digiisp.postcogito.org) turns a Digispark into an AVR
  programmer from the browser. It is what hardened mode is set up with, and
  where the firmware uploader and the serial-number trick came from.
- [V-USB](https://www.obdev.at/products/vusb/) is the USB stack, and
  [Micronucleus](https://github.com/micronucleus/micronucleus) the bootloader.

## Licence

Copyright © 2026 Marco Carnut. GPLv2, following V-USB; see `LICENSE`.
