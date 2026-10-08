# DigiMacro

A morse-activated macro keyboard on an ATtiny85 Digispark, configured from a
web page.

Touch a pad in dots and dashes; if it matches an entry in the EEPROM table, the
macro is typed as keystrokes. Hold it for four seconds and a web page may read
and write the table over WebHID — no terminal, no driver to install, and
nothing on the device itself beyond reading and writing its own EEPROM.

Sibling of [DigiMacroCDC](../DigiMacroCDC), which keeps an on-device CDC command
line instead. That one is finished and in use.

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

## Status: working

Tap a code, the board types the macro; touch the pad again to stop it. One
macro can be nominated to type itself when the board is plugged in, and a
macro can carry on with another, including itself. Hold the pad four seconds and
`web/app.html` may read and write the table for the next half minute, behind
an optional password, and wrong codes can lock the board out. The same page
updates the firmware.

**5500 bytes of the 6650** the stock bootloader leaves, and 170 of RAM.

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

### Every board has a serial number

Four random bytes, made once on a chip that has never had one and kept in
EEPROM, rendered as eight hex digits. Chrome remembers a WebHID permission
only for a device that has a serial number, so without one every replug means
picking the board out of the chooser again — and making it per board rather
than per model means two boards get their own grants instead of sharing one.

The randomness comes from the watchdog oscillator's jitter against the CPU
clock: two independent RC oscillators, so the low bits of a timer sampled at
each watchdog timeout are unpredictable. It takes about a second, once in a
board's life. The same trick, for the same reason, as
[DigiISP](../DigiISP).

### The password, which most boards will not have

Optional, and absent by default. An erased chip has none, and a board that
types eleven right-arrows at a television should not ask anybody for anything.

What it adds, when set, is a *second* gate on the table. The window says
somebody is at the board, which a web page cannot arrange; the password says
they are the right somebody, which a thief holding the board cannot. Both are
needed to read or write, and neither is needed if no password is set.

It does not gate *playing* a macro. A device that wants a password before it
will type is a device nobody would carry, and the answer to a stolen board
typing for strangers is the lockout, below.

A wrong answer costs a second, and with the lockout on it counts as a wrong
code. That is all: the password is never refused for being locked, because it
is what lifts a hard lock, and a limit on it is the one lock that can shut the
owner out too. Its defence against guessing is its own length — twelve
characters is plenty even at thousands of guesses a second. The comparison
runs to the end whatever happens, so its timing says nothing about how much of
the answer was right. The unlock lasts as long as
the window and lives in RAM, so unplugging the board locks it again.

**The hold survives, with a different job.** It used to change what the device
was; now it opens a window during which the table may be read and written. That
is the part a web page cannot do for itself, and on a board with no password
it is the only thing between a hostile page and the macros.

### The lockout

Also optional, and it needs the password, since giving the password is the
only way to lift a hard lock. Two stages, so that the owner can fumble a code
without needing the app:

- **Soft lock.** After a set number of wrong codes, the board ignores every
  tap, right or wrong, for a set time: a minute by default, up to forty.
- **Hard lock.** After a set number of soft locks in a row, it ignores taps
  until the password is given in the app. The power-on macro stops too, or a
  thief would only have to plug the board in again.

A code that works clears both counts. So someone guessing gets at most *wrong
codes × soft locks* tries before the board stops listening, which at 1 and 3
is three, out of 62 codes. The app works out those odds for the settings you
choose.

Everything is kept in EEPROM, so unplugging the board gets nobody anywhere.
The board has no clock to tell it how long it was unplugged, so a soft lock
found at power-up starts its whole wait again. A wrong code is counted
*before* the three blinks that report it, so pulling the plug as soon as the
blinks start saves nothing. A code that matches writes nothing unless there
is a count to clear, so ordinary use causes no EEPROM wear. The hold still
opens the window while the board is locked, because that is how the app gets
in to lift the lock.

What it does not stop is someone with a laptop. Micronucleus listens on every
plug-in and can flash a sketch that reads the EEPROM out, lockout or not; the
lockout is against someone *tapping*. Closing the laptop route needs the lock
bits and no bootloader, or the self-wiping bootloader `PLAN.md` describes.

| EEPROM | |
|---|---|
| `0x1EC` | wrong codes before a soft lock; 0 or `0xFF` for no lockout |
| `0x1ED` | soft locks in a row before a hard lock; 0 or `0xFF` for never |
| `0x1EE` | a soft lock's length in tens of seconds, 1–240; anything else is 6 |
| `0x1EF` | wrong codes so far |
| `0x1F0` | soft locks in a row so far |
| `0x1F1` | hard locks lifted with the password, ever |

The three counters are stored inverted, so an erased byte reads as zero.

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
password.

A refused macro does not count as a code that worked. Only a macro that
actually plays clears the lockout's counts. Otherwise, tapping a guarded code
on the wrong computer would wipe a guesser's misses for them.

The key is eight random bytes, made by the app, which has a real random
source. It is written to the board like the table, and downloaded as a file
named after the board's serial number. The client is `tools/hostauth`,
for Linux:

```
cc -O2 -Wall -o digimacro-host tools/hostauth/digimacro-host.c
mkdir -p ~/.config/digimacro
cat digimacro-*.key >> ~/.config/digimacro/host.key
chmod 600 ~/.config/digimacro/host.key
digimacro-host            # once
digimacro-host -e 30      # every 30 seconds, for as long as it runs
```

Run with `-e` from your session and stopped when the screen locks, the trust
becomes "while I am logged in". The client finds every board plugged in by
the serial number the kernel reports for it, so one key file serves several
boards.

Sending the key is the one request, beside INFO, that needs neither the hold
nor the password. That is the point: nobody is at the board. So it can do
nothing else. It opens no window, unlocks nothing and reads nothing, and a
wrong key does **not** count against the lockout. If it did, any program on
any computer could hard-lock the board without anyone touching it. A wrong
key costs a second instead, on a clock of its own.

What it cannot do is protect against someone who reads the EEPROM directly.
That is what [hardened mode](HARDENED_MODE.md) is for. Host authentication
stops the casual case, someone plugging the board in somewhere else and
tapping; hardened mode stops the determined one.

| EEPROM | |
|---|---|
| `0x1E4` | the host key, eight bytes; all `0xFF` for none |
| `0x1F2` | how long the host stays trusted, in tens of seconds, 1–240; anything else is 6 |

That fills the reserved block. Anything more will have to come out of the
table.

## What the LED says

| LED | means |
|---|---|
| off | idle, and also while you tap a code, so it does not spell the code out for anyone watching |
| on, steady | a macro is typing; touch the pad to stop it |
| three quick blinks | the code matched nothing |
| slow blink, once a second | the window is open: the app may read and write the table for half a minute |
| a short blip every two seconds | soft lock: too many wrong codes, so every tap is ignored until the wait is over |
| fast flicker, eight times a second | hard lock: every tap is ignored until the password is given in the app |

The window's blink wins over both locks, so you can see the hold took while
the board is locked.

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
| `0x83 nn` | stop unless the host sent its key lately: quietly if `nn` is 1, as an empty code if 2 |

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
| `0x40` INFO | magic `DM`, protocol, firmware, EEPROM size, and a state byte: window open, password set, unlocked, soft lock, hard lock |
| `0x41` REBOOT | into the bootloader, for a firmware update |
| `0x60` READ | address and count in, data back |
| `0x61` WRITE | address, count and data in |
| `0x62` UNLOCK | the password, if the board has one |
| `0x63` SETPASS | a new password, or none at all |
| `0x64` HOSTAUTH | the host key: needs neither the window nor the password, and does nothing else |
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

For a board that will hold secrets, `./build.sh isp` builds the firmware with
no bootloader, as [HARDENED_MODE.md](HARDENED_MODE.md) describes.

Options: `button` for a pushbutton instead of a pad, `rc` for a pad read
through a bleed resistor, `ledecho` to make the LED follow the input while
tapping — which is for working on the input path only, since it shows a
bystander the code — `touchdebug` to make the board type its own pad readings,
the only diagnostic channel a device with no serial port has, and `isp` for a
chip with no bootloader.

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
 {host}  {host!}              stop unless this computer sent the host key lately
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
has it, and the byte budget — 480 of them — is always on screen. Nothing is
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

## Updating the firmware

The app does it. The status line at the top shows the board's firmware
version, followed by **(latest)**, or by **(update…)** when the page carries
something newer; that opens a dialog with the update in it. The board restarts
into its bootloader, Micronucleus, and the browser asks you to pick it. That is a second picker because the bootloader is a different kind of USB
device, plain vendor class rather than HID, reached over WebUSB. The macros,
the password, the lockout and the serial number all live in EEPROM, which
Micronucleus leaves alone, so they survive the update.

- **Pick it within a few seconds.** The bootloader waits about six and then
  goes back to the old firmware, so the click that starts an update has to
  find the window already open. If it is not, that click asks for the hold,
  and the next click does the update.
- **An old Micronucleus is replaced first.** Digisparks often ship with 1.x.
  With the box ticked, which is the default, the app installs the upstream
  2.6 upgrader, has you pick the new bootloader again (it waits for you this
  time), and then writes the firmware.
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
itself, so serving it needs no AVR toolchain.

The uploader is DigiISP's, ported to plain JavaScript in `web/micronucleus.js`.
It speaks Micronucleus 1.x and 2.x, and lays the firmware out the way the
upstream tool does. DigiISP has run the 1.x → 2.6 upgrade on hardware.

## Testing

```
test/run.sh
```

compiles the real `DigiMacro.ino` for the host against stub hardware and
exercises the table walk, every escape, the bounds that keep a malformed table
from reaching the password, the morse decoding from the input pin, both ways of
reading the pad, and the sample table the loader flashes; then the app's macro
language on its own, the two languages against each other, and — across the
language boundary — a table encoded by `web/table.js` played through the
firmware. Then the firmware the app ships, laid out for the bootloader, with
both jumps decoded and followed to where they land. Eight suites, about a
second.

## Running the app

```
cd web && python3 -m http.server 8000
```

Then `http://localhost:8000/app.html` in Chrome or Edge — WebHID needs a secure
context, and `localhost` counts. `web/README.md` has the Linux udev rule, the
raw protocol page and the notes on reading the pad.

## Layout

| | |
|---|---|
| `DigiMacro.ino` | the firmware: touch, morse, the table walk, playback |
| `DigiHidUSB/` | the V-USB stack: a keyboard, and a vendor collection the app talks to |
| `test/` | host build of the real sketch against stub hardware |
| `tools/loadtable/` | writes a sample table into EEPROM, without a browser |
| `tools/hostauth/` | the Linux client that sends the host key |
| `spikehid/` | the sketch that derisked the vendor collection |
| `web/` | `app.html` is the editor, `hid.html` the raw protocol tool, `table.js` and `i18n.js` what they share, `micronucleus.js` the uploader |
| `web/firmware/` | what the app installs: the released firmware, and the Micronucleus 2.6 upgrader |
| `HARDENED_MODE.md` | how to make a board fit to hold secrets, and what that still does not protect |
| `PLAN.md` | design, decisions and what comes next |

[DigiISP](../DigiISP) solved the BOS and MS OS 2.0 work that the WebUSB
personality needed. The HID transport needs none of it, which is most of why
it won.

## Licence

GPLv2, following V-USB.
