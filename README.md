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

Tap a code, the board types the macro; touch the pad again to stop it. One
macro can be nominated to type itself when the board is plugged in, and a
macro can carry on with another, including itself. Hold the pad four seconds and
`web/app.html` may read and write the table for the next half minute. What is
left is `\j`, and the password and lockout that should gate all of this.

**4118 bytes of the 6650** the stock bootloader leaves, and 133 of RAM.

### It never stops being a keyboard

It used to. A HID keyboard is not something a browser will talk to — Chrome
refuses to claim interfaces of protected classes — so the board re-enumerated as
a vendor-class device to be configured, and back again afterwards. That worked,
and it is gone.

The replacement is a second top-level collection on a **vendor usage page**,
`0xFF00`. WebHID blocks protected usage pages, not vendor ones, so a page can
reach that collection while the keyboard half carries on being a keyboard. A
feature report is a control transfer, so the EEPROM protocol moved across
unchanged.

| | before | after |
|---|---|---|
| Linux | a udev rule | a udev rule |
| Windows | MS OS 2.0 descriptor, WinUSB binding | **nothing** |
| flash | 4244 bytes | 3790 |
| configuring | re-enumerate, reconnect, re-enumerate | nothing moves |

What it cost: the WebUSB landing page, where Chrome offered to open the app
when the board appeared. And the keyboard gave up boot protocol, since two
collections on one interface need report IDs and a boot report may not have
one. That also caps it at three key slots rather than six, which matters to
nothing that only ever presses one key at a time.

### Stopping a macro

Touching the pad stops whatever is playing. The pad is read between keystrokes
— a touch reading is a quarter of a millisecond against a keystroke's twenty
— and during pauses, so a macro that jumps to itself, which runs for ever by
design, is got out of by touching it. That press is not also counted as a
symbol, and holding it on opens the configuration window, so a runaway is
stopped and repaired with one gesture.

The one loop that cannot be stopped that way is one that never types or pauses
at all, since the pad is never reached. The firmware bounds that by counting
jumps, and the app refuses to write it, because nobody meant it.

**The hold survives, with a different job.** It used to change what the device
was; now it opens a window during which the table may be read and written. That
is the part a web page cannot do for itself, and until there is a password it
is the only thing between a hostile page and the macros.
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
| `0x81 hh` | carry on with the macro whose header is `hh-1` |
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

One feature report on the vendor collection, both ways:

```
 [0] command   [1] address low   [2] address high   [3] count   [4..35] data
```

| | |
|---|---|
| `0x40` INFO | magic `DM`, protocol, firmware, EEPROM size, window open? |
| `0x60` READ | address and count in, data back |
| `0x61` WRITE | address, count and data in |
| `0xEE` | comes back in place of the command when the window is shut |

Set the report to ask, get it to see the answer. INFO always answers — it says
what this is and whether it may be asked, which gives nothing away. READ and
WRITE need the window, which the four-second hold opens for half a minute.

Writes are chunked at 32 bytes because an EEPROM byte takes 3.4 ms and the
device can answer nothing else while it writes. `eeprom_update_byte`, so a
table saved unchanged costs no wear. A read past the last byte returns 0xFF and
a write past it stops, because what sits above the table is the password.

Whether the report ID travels in the payload as well as in the request is the
one thing the HID specification leaves to taste. Chrome does not send one back;
the app looks rather than assuming, and the firmware accepts either, which is
safe because no command byte is 2.

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
 {goto .-}                    carry on with another macro, and never come back
```

A key palette inserts any of it at the cursor — arrows, function keys, the
editing keys, modifiers that stay held for the next key, and brackets that wrap
whatever is selected — so nothing has to be spelled from memory. `{11}` means
eleven in all, as a quantifier does in a regular expression, and a count counts
whatever precedes it: one keystroke, one accented letter, or a bracketed group.
A parenthesis is only a parenthesis unless a count follows the closing one.

**When plugged in** nominates one macro to type itself at power-on, stored as
the code rather than a position in the table — so reordering the macros cannot
point it somewhere else, and a code that is no longer there types nothing
rather than something.

It waits for the host to have somewhere to put the keystrokes, which is not
the same as being plugged in: the host reads the descriptors early and attaches
a keyboard to the input stack some time later, and a macro that starts in
between loses however much of itself fits in the gap. The board waits for the
host to send it a lock-light report — the host talking *to* the keyboard, which
it only does once there is one — and gives up waiting after a second and a half
in case a host never does.

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
| `DigiHidUSB/` | the V-USB stack: a keyboard, and a vendor collection the app talks to |
| `test/` | host build of the real sketch against stub hardware |
| `tools/loadtable/` | writes a sample table into EEPROM, until the app can |
| `spikehid/` | the sketch that derisked the vendor collection |
| `web/` | `app.html` is the editor, `eeprom.html` the raw-byte tool, `table.js` and `i18n.js` what they share |
| `PLAN.md` | design, decisions and what comes next |

[DigiISP](../DigiISP) solved the BOS and MS OS 2.0 work that the WebUSB
personality needed. The HID transport needs none of it, which is most of why
it won.

## Licence

GPLv2, following V-USB.
