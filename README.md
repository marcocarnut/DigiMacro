# DigiMacro

A morse-activated macro keyboard on an ATtiny85 Digispark, configured from a
web page.

Touch a pad in dots and dashes; if it matches an entry in the EEPROM table, the
macro is typed as keystrokes. Hold for four seconds and the board re-enumerates
as a vendor-class USB device that a WebUSB page configures — no terminal, no
driver to install, and nothing on the device itself beyond reading and writing
its own EEPROM.

Sibling of [DigiMacroCDC](../DigiMacroCDC), which keeps an on-device CDC command
line instead. That one is finished and in use; this one is early.

## Status: working, less the password

The firmware plays macros out of a packed EEPROM table; `READ` and `WRITE`
vendor requests put one there; and `web/app.html` is an editor for it that
reads a board's table, checks what you type, and writes it back verified.
What is left is the password and lockout that should gate all of this, and
`\j`.

**4058 bytes of the 6650** the stock bootloader leaves, and 106 of RAM — so it
installs over USB like any sketch, with about 2.5 KB still to spend on the
password, the lockout and `\j`.

`spike/` answered the one question everything rested on — whether a keyboard can
become something WebUSB will talk to — and **it does**:

```
picked digimacro.postcogito.org / DigiMacro
  16c0:05dc  USB 2.1
open() ok
configuration 1
claimInterface(0) ok -- the interface is not a protected class
INFO -> 44 4d 01 01
```

| | |
|---|---|
| Linux, Chrome | works |
| Windows 10, Chrome and Edge | works, nothing to install |

`PLAN.md` holds the design and the order of work.

## The table

The app rewrites the whole table, so entries are packed rather than slotted:

```
 [hdr][keystroke...][0x00]      repeated, in any order
 [0xFF]                         end of table
```

`hdr` is the code: three bits of symbol-count-minus-one, five bits of symbols,
a dash being a 1 and the first symbol in the highest bit that count uses. No
count above five exists, so `0xFF` ends the table and **a virgin chip reads as
an empty one** — nothing to initialise. Two bytes of overhead per macro, and
neither of DigiMacroCDC's limits — ten macros, thirty keystrokes each —
survives: what is left is 480 bytes to divide up however you like, the last
32 being kept back for the password, the lockout and whatever this turns out
to want next.

A keystroke is a HID usage rather than a character, because the app does the
layout mapping:

```
 bit 7     needs Shift
 bits 0-6  HID usage
```

Usages `0x01`–`0x03` are states a keyboard reports, never keys anyone presses,
so those three values are free — and so are the same three with bit 7 set.
Hence the escapes, and the test for one: a byte whose low seven bits are below
four.

| | |
|---|---|
| `0x01 nn` | pause `nn` tenths of a second |
| `0x02 mm` | latch HID modifier mask `mm` into the next keystroke |
| `0x03 nn` | do the previous keystroke `nn` more times |
| `0x82` | mark: what follows repeats as a group, not as one keystroke |
| `0x81 hh` | carry on with the macro whose header is `hh` — not implemented yet |

No argument may be zero, since a zero byte ends the macro. `0x03` is
run-length coding: "left arrow eleven times" costs three bytes rather than
eleven, and repeating *whatever went before* carries the latched modifiers with
it.

`0x82` is what keeps that working once a keystroke stops being a character. On
a dead-key keyboard `ç` is two presses, the accent and then the letter, so
repeating "the previous keystroke" repeats the letter and types `çcccc`. A mark
says where the repeated thing began. It takes no argument — its position is its
argument — and marks do not nest.

This is the interface between the firmware and the app, so neither end is
trusted on its own: `tools/loadtable/sample_table.h` is one original shared by
the loader and a test, and `web/table.js` is one original shared by the web
page and a test that plays what it encodes against the real firmware.

## Reading and writing it over USB

Two vendor requests, answered **in vendor mode only** — which is what makes the
four-second hold a physical-presence check rather than a convenience. In
keyboard mode the device answers nothing but HID class requests, so a program on
the host cannot read the table out of a board nobody is touching.

| | |
|---|---|
| `0x60` READ | IN, `wValue` = address, `wLength` = count |
| `0x61` WRITE | OUT, `wValue` = address, payload = the bytes |
| `0x40` INFO | IN, 6 bytes: `DM`, protocol, firmware, EEPROM size |

Writes are chunked, 32 bytes at a time: an EEPROM byte takes 3.4 ms and the
device can answer nothing else while it writes, so the whole table in one
transfer would be nearly two seconds of silence. `eeprom_update_byte`, so a
table saved unchanged costs no wear. A read past the last byte comes back
short and a write that would cross it is refused, because what sits above the
table is the password.

## Why a mode switch rather than one device

A HID keyboard cannot also be a WebUSB device. Chrome refuses to claim
interfaces of protected classes, HID among them, and Windows binds a HID device
to HIDClass rather than to WinUSB. So the board is one or the other, and a
four-second hold changes it over.

Which is the better arrangement anyway: reaching the macro table needs someone
holding the pad, so **no web page can reconfigure the device on its own**, and
while it is configurable it is not a keyboard, so it cannot be made to type.

Entering vendor mode makes Chrome offer to go to `digimacro.postcogito.org`,
the URL in the WebUSB descriptor. That is expected, and eventually it is the
whole install story: plug in, hold the pad, click the notification, configure.
The address does not serve anything yet.

## Building it

```
./build.sh loadtable upload     # a sample table into EEPROM, once
./build.sh upload               # the firmware
```

Then tap into a text editor. `.` types a banner, `-` pauses in the middle, `..`
demonstrates the repeat escape, `.-` sends Ctrl+Left, and `---` is longer than
any DigiMacroCDC entry could be; anything else gets three quick blinks.
`tools/loadtable/loadtable.ino` lists them, and micronucleus never erases
EEPROM, so the table survives the second upload.

Options: `button` for a pushbutton instead of a pad, `rc` for a pad read
through a bleed resistor, `ledecho` to make the LED follow the input while
tapping — which is for working on the input path only, since it shows a
bystander the code — and `isp` for a chip with no bootloader.

## Writing macros

`web/app.html` edits the table: read what a board holds, change it, write it
back. A macro is text, with braces for what text cannot say:

```
 hello{Enter}                 anything is typed as itself
 \n  \t  \\  {{               newline, tab, backslash, a literal {
 {Ctrl+c}  {Alt+Tab}  {F5}    modifiers, and keys that are not characters
 {pause 1.5}                  wait, in seconds
 x{11}                        eleven of them, as in a regular expression
 (ab){3}                      ...of a whole group rather than one thing
```

A key palette inserts any of it at the cursor — arrows, function keys, the
editing keys, modifiers that stay held for the next key, and brackets that wrap
whatever is selected — so nothing has to be spelled from memory. `{11}` means
eleven in all, as a quantifier does in a regular expression, and a count counts
whatever precedes it: one keystroke, one accented letter, or a bracketed group.
A parenthesis is only a parenthesis unless a count follows the closing one.

The editor encodes as you type, so a mistake is reported beside the macro that
has it, and the byte budget — 497 of them — is always on screen. Nothing is
written until every macro is valid.

Accented characters are the host's business, not the board's: it sends key
presses, so what appears depends on the keyboard layout of the machine it is
plugged into. Say which at the top of the page and `coração` is accepted and
expanded into the accent and then the letter; leave it at plain US, where
there is no way to type one, and the editor says so rather than typing
something else.

The app speaks English and Brazilian Portuguese, picked from the browser and
changed at the top right. `web/i18n.js` holds both, the encoder's error
messages included, and a test checks that neither language has a string the
other lacks.

## Testing

```
test/run.sh
```

compiles the real `DigiMacro.ino` for the host against stub hardware and
exercises the table walk, every escape, the bounds that keep a malformed table
from reaching the password, the morse decoding from the input pin, the sample
table the loader flashes, and — across the language boundary — a table encoded
by `web/table.js` played through the firmware. Five suites, about a second.

## Building the spike

```
./build.sh spike upload
cd web && python3 -m http.server 8000
```

Then `http://localhost:8000/spike.html` in Chrome or Edge — WebUSB needs a
secure context, and `localhost` counts. Hold the pad four seconds first, so the
board is in vendor mode. `web/README.md` has the Linux udev rule and the notes
on reading the pad.

Short touch types a line; `TOUCH_DEBUG` in `spike/spike.ino` makes the board
type its own pad readings instead, which is the only diagnostic channel a
device with no serial port has.

## Layout

| | |
|---|---|
| `DigiMacro.ino` | the firmware: touch, morse, the table walk, playback |
| `DigiModeUSB/` | one V-USB stack with two personalities: HID keyboard, or vendor class for WebUSB |
| `test/` | host build of the real sketch against stub hardware |
| `tools/loadtable/` | writes a sample table into EEPROM, until the app can |
| `spike/` | the derisking sketch |
| `web/` | `app.html` is the editor, `eeprom.html` the raw-byte tool, `table.js` and `i18n.js` what they share |
| `PLAN.md` | design, decisions and what comes next |

Descriptors for the WebUSB side are lifted from [DigiISP](../DigiISP), which
had already solved the BOS and MS OS 2.0 work.

## Licence

GPLv2, following V-USB.
