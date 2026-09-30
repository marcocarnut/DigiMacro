# DigiMacro — plan

A morse-activated macro keyboard on an ATtiny85 Digispark, configured from a
WebUSB app rather than from an on-device command line.

Sibling of **DigiMacroCDC**, which keeps the CDC terminal. That one is
finished and working; this one starts from it and removes more than it adds.

## What goes, and what that buys

| removed | estimated flash |
|---|---|
| `cmd_*`, `run_command`, `handle_cmdline`, and their strings | ~2150 |
| the CDC descriptors, state machine and rings | ~450 |
| `keymapUS` and the ASCII mapping in `typeChar` | ~155 |
| **total** | **~2750** |

| added | estimated flash |
|---|---|
| vendor requests: EEPROM read/write, unlock, status | ~250 |
| BOS + MS OS 2.0 descriptors (lifted from DigiISP) | **289, measured** |

`isp default lockout` is 6994 today, so the guess is **~4.6 KB** — which would
fit the stock micronucleus with room to spare, not just a bootloaderless chip.
That changes who can install this, so it mattered to check early. The spike
says the guess is about right: both personalities, HID typing, the keymap and
every WebUSB descriptor come to **3372 bytes of 6650**, with 93 of RAM. What
is left to add is the table walk, morse decoding, playback and the EEPROM
requests.

## Architecture: decided — mode switch

Keyboard mode is HID as now; a four-second hold re-enumerates as pure vendor
class, the DigiISP device. The reasoning is below, kept because it is the
constraint that shapes everything else.

**A HID keyboard cannot simply also be a WebUSB device.** Chrome's WebUSB
refuses to claim interfaces of protected classes, HID among them, and on
Windows a HID device binds to HIDClass rather than WinUSB, so it is not a
WebUSB device at all. DigiISP sidesteps this by being pure vendor class
(`16c0:05dc`, device and interface class `0xFF`) — which a macro keyboard
cannot be, because it has to type.

Three ways out:

**A. WebHID.** Stay a HID keyboard; add a second top-level collection on a
vendor usage page (`0xFF00`) and carry the protocol in feature reports over
EP0. Chrome's WebHID blocks *protected usage pages*, not vendor ones, so this
works — it is what QMK's Vial does. No new endpoints needed. But it throws
away DigiISP's WebUSB descriptors and web code.

**B. Mode switch — chosen.** Reasons:

- DigiISP's descriptors, BOS, MS OS 2.0 set and much of its TypeScript
  transfer nearly unchanged.
- The re-enumeration machinery already exists in DigiMacroCDC and is tested.
- **It makes the hold a physical-presence check.** No web page can reach the
  macro table unless someone is holding the pad. That is a better security
  property than anything the CDC version had, and it comes free.
- While configuring, the device is not a keyboard, so a hostile page cannot
  make it type.

**C. Composite: HID interface 0 plus a vendor interface 1 with no endpoints.**
An interface descriptor with zero endpoints is legal, so V-USB's endpoint
budget is not a problem. MS OS 2.0 can bind WinUSB to interface 1 alone via a
function subset. Elegant — you could edit the table while it is still a
keyboard — but it is the most new descriptor work and the most to get wrong on
Windows.

Options A and C stay recorded in case the spike finds trouble.

## The table is packed, not slotted

The app rewrites the whole table, so entries need not be a fixed size:

```
 [hdr][keystrokes...][0x00]     repeated
 [0xFF]                          end of table
```

`hdr` keeps its present shape — three bits of symbol-count-minus-one, five
bits of code — and a length field above 4 is not one the app can write, so
`0xFF` ends the table and **a virgin chip reads as an empty one**. The same
trick that made "vacant" free.

Overhead drops from "32 bytes however few you used" to two bytes per macro,
and *both* limits go: no fixed number of macros and no 30-byte ceiling. About
496 bytes of table, so twenty ten-key macros or four long ones, as you like.

Five things follow from it:

- **The firmware's table code becomes read-only.** No `HDR_MAKE`, no
  `find_vacant`, no `set`, no `del` — the app writes raw bytes and the
  firmware only ever walks the table to play a macro. A further saving on top
  of the estimate above.
- **Slots survive as ordinals in the UI; the firmware stores a code.** The two
  are not in conflict. Numbering entries by table order is what the app shows,
  but storing a code in `autoexec` and `\j` reuses the lookup that playback
  already performs, where an ordinal would need a second walk that counts. A
  dangling code simply matches nothing, which is the safe failure.
- **The walk must be bounded to the table region.** A table written without a
  terminator would otherwise run straight into the password. Settled at
  0x000-0x1F0, 497 bytes, with the config bytes above it; both the lookup and
  playback stop at that boundary, and two of the tests are about nothing else.
- **`\j` survives only for loops**, its original purpose — getting past 30
  characters — having disappeared. Worth keeping (~150 bytes is cheap now) but
  **implement it last**: it is the one feature that can brick a board, and it
  needs the pause-escape to be survivable. The hazard also largely moves to the
  app, which can walk the jump graph before writing and refuse an `autoexec`
  that loops with no pause long enough to escape — a far better place for that
  check than the firmware.
- **More macros makes guessing easier.** Thirty live codes out of 62 means
  nearly every guess lands. The lockout matters more than it did, and `lock 1`
  becomes the sensible default rather than a strict option.

## Storage format

The app translates ASCII to HID usages, so the table stops holding text. One
byte per keystroke, in exactly the form `keymapUS` used to produce:

```
 bit 7     needs Shift
 bits 0-6  HID usage
```

which keeps the density of the current format — 30 keystrokes per slot — while
deleting the table and the mapping code. Escapes keep the prefix-plus-argument
shape that already works:

```
 0x01 nn   pause nn tenths of a second
 0x02 mm   latch modifier bits mm into the next keystroke
 0x03 nn   repeat the previous keystroke nn more times
 0x81 hh   carry on with the macro whose header is hh   (jump; loops)
```

**Corrected while implementing: 0x04 was going to be the repeat, and 0x04 is
the usage for "a".** Once the table holds usages rather than characters, the low
control bytes are no longer free just because a command line could not type
them. What *is* free: usages 0x01 to 0x03 are ErrorRollOver, POSTFail and
ErrorUndefined, states a keyboard reports rather than keys anyone presses — and
so are the same three with bit 7 set, Shift+ErrorRollOver being no more real.
Six values where four were wanted, and the test for one is a byte whose low
seven bits are below four.

`0x02` replaces `\kE0`-style latching with a direct modifier mask, since the
app can compose it. `0x03` is the run-length idea: "left arrow eleven times"
becomes 3 bytes rather than 11. Repeating *the previous keystroke* rather than
taking a usage of its own is both shorter and more general — it repeats
whatever modifiers were latched. The jump takes a header byte rather than an
ordinal, for the reason given above: playback already looks headers up.

No argument may be zero: a zero byte terminates the string. The app must
enforce that, and the firmware must reject it, as `\p00` taught us.

## Vendor requests

Numbered above DigiISP's range so a shared tool can tell the devices apart.

| | |
|---|---|
| `INFO` | magic, protocol version, firmware version, slot count, EEPROM size |
| `READ` | 0x60. wValue = address, wLength = count. To be refused while locked |
| `WRITE` | 0x61. wValue = address, data in the payload. To be refused while locked |
| `STATUS` | password set? lockout armed? tries left? unlocked this session? |
| `UNLOCK` | password in the payload; on success clears the lockout count |

Only `INFO` and `STATUS` answer while locked, and `STATUS` must say nothing
about *what* the password is.

**Writes must be chunked.** 496 bytes at 3.4 ms each is nearly two seconds
with USB unpolled, because `eeprom_update_byte` blocks. Take 8–32 bytes per
control transfer so each one completes promptly; DigiISP already writes its
target's EEPROM this way. Use `update` rather than `write` so unchanged bytes
cost no wear when the app saves a table it mostly did not touch. **Settled at
32 bytes, chosen by the app**, the firmware imposing no chunk size of its own
beyond V-USB's 254-byte control payload.

A checksum over the table would let the firmware notice a write interrupted
half way, which is otherwise silent. Cheap, optional, worth pricing — though
the app reading the table back and comparing, which `eeprom.html` already does,
covers the same ground for nothing.

## Security, including one new hole

The password and lockout carry over, but the threat model shifts:

- **Password attempts must count against the lockout.** Over a CDC terminal a
  brute-force was slow and clumsy; over vendor requests it is thousands per
  second. Without this the password is decoration. DigiMacroCDC does not do
  this, and should probably learn to.
- **Reads must be gated, not just writes.** The macro table *is* the secret.
- **Recovery changes.** There is no terminal fallback: a locked-out board
  needs the app. Worth stating plainly in the README.
- `autoexec` remains unfixable by design, as before.

## Open decisions

1. ~~Product strings must differ from DigiMacroCDC's~~ — moot: DigiMacroCDC
   stays private, DigiMacro is the one that ships. To be fixed on the CDC side
   only if that changes.
2. Which PID for vendor mode — DigiISP's `16c0:05dc` is for "vendor class with
   libusb" and fits, but sharing it with DigiISP means the strings must
   distinguish them.
3. **Read-back is needed but is the thing being protected.** The app cannot
   edit one macro without reading the table first, so a locked board can only
   be overwritten wholesale. Resolution: no password means read freely, a
   password means unlock first — which is what the lockout is for.
4. Repeat count range: one byte gives 1-255 more presses. Enough?

## What to reuse

| from | what |
|---|---|
| `DigiMacroCDC` | V-USB vendoring, dynamic descriptors, re-enumeration, morse decoding, the macro table, `\p`/`\j` playback, touch sensing, the host test harness |
| `DigiISP` | `usbconfig.h` for vendor class, BOS and MS OS 2.0 descriptors, `protocol.h` conventions, the web app's WebUSB plumbing and build setup |

## Next, in this order

The USB layer is done and lives in `DigiModeUSB`. Each step is separately
testable, which is why they are in this order rather than firmware-then-app.

1. ~~**Playback from a packed table.**~~ **Done: 3804 bytes of 6650, 102 RAM.**
   The morse decoding, the table walk and playback came over from DigiMacroCDC,
   repacked and read-only, with `keymapUS` dropped. The linker takes the keymap
   and `dusbTypeChar` out of the binary by itself now that nothing calls them,
   so the library keeps both for the spike at no cost here. The whole of
   playback — table walk, three escapes, morse — cost 254 bytes over the
   spike's 3550, less than the keymap and ASCII mapping it replaced.

   Two things came with it that were not planned. **`tools/loadtable`**, which
   writes a sample table into EEPROM over USB: micronucleus cannot write EEPROM,
   but it does not erase it either, so a one-off sketch that writes the table
   and then sits blinking is enough, and the firmware upload that follows leaves
   it alone. And **a fourth test suite** that plays that same table against the
   real firmware, because the table is the interface between the two halves of
   the project, `tools/loadtable/sample_table.h` is the one original both ends
   read, and hand-written hex is exactly where a wrong byte hides.

   Left undone deliberately: no `autoexec` yet, which wants the password work
   beside it; and a pause is not interruptible, which is `\j`'s problem to
   solve in step 5, since without jumps a macro cannot run for ever.

2. ~~**`READ` and `WRITE`** vendor requests, chunked.~~ **Done: 4058 bytes of
   6650, 106 RAM.** 0x60 and 0x61, in their own block above everything DigiISP
   uses, answered in vendor mode only — which is what turns the four-second
   hold into a physical-presence check rather than a convenience, since in
   keyboard mode the device answers nothing but HID class requests. INFO grew
   two bytes to report the EEPROM size.

   `usbFunctionWrite` writes straight to EEPROM rather than buffering, so the
   RAM cost is four bytes of state: a byte takes 3.4 ms and holds the driver up
   for 27 ms a packet, but the USB interrupt keeps running and the host retries
   whatever arrives while we are busy. A HID LED report reaches the same
   callback, so a flag says which of the two is being served. A read past the
   last byte comes back short; a write that would cross it stalls, and the
   stall has to be latched, because the driver keeps calling after one.

   `web/eeprom.html` dumps, writes and verifies a board's table, with a button
   for the bounds. Its encoder is `web/table.js`, and `test/run.sh` now loads
   that same module in node and plays what it produces through the real
   firmware: five suites. That check earned its keep at once — put the repeat
   escape back to 0x04 and the diff shows "ab" followed by two literal a's,
   which is the collision that moved it.

   `tools/loadtable` stays useful as a way to get a board going without a
   browser.
3. **The app.** Table editor, ASCII to usages, and the loop check before it
   writes.
4. **`STATUS` and `UNLOCK`**, the password and the lockout — including
   counting failed password attempts, which the CDC version does not do and
   which matters far more over USB. `autoexec` belongs here too, beside the
   lockout that has to gate it; its byte is already reserved at 0x1F3.
5. **`\j`** last: it is the one feature that can brick a board, and by then
   the app can refuse a looping `autoexec`.

## Spike: passed on hardware

`spike/spike.ino` with the `DigiModeUSB` library: HID keyboard, a four-second
hold to vendor class and back, and one vendor request. **3372 bytes (50%),
93 RAM.**

Mechanisms confirmed by reading DigiISP and reproducing them:

- `USB_CFG_DESCR_PROPS_UNKNOWN = USB_PROP_IS_DYNAMIC` is what gets
  `usbFunctionDescriptor()` called for descriptor type `0x0F`, the BOS. Nothing
  else would serve it.
- `bcdUSB` must read 2.10 in the vendor personality or the host never asks for
  the BOS at all — and then Windows will not bind WinUSB and Chrome will not
  see a WebUSB device. The HID personality stays at 1.10.
- A pointer returned from `usbFunctionSetup()` is read out of **RAM** unless
  `usbMsgFlags = USB_FLG_MSGPTR_IS_ROM` is set explicitly, because
  `usbProcessRx()` zeroes the flags first. The opposite of
  `usbFunctionDescriptor()`, where ROM is the default and the driver
  overwrites whatever you set.
- A vendor interface needs no endpoints, so V-USB's exhausted endpoint budget
  never comes into it.

Decoded out of the built ELF and checked: `bcdUSB 2.10`, a 64-byte BOS with
three capabilities and both platform GUIDs recognised, the 162-byte MS OS 2.0
set with a `WINUSB` compatible ID, an 18-byte configuration with one
vendor-class interface and zero endpoints, and the landing-page URL.

### Two placeholders to confirm

- The manufacturer string is now `digimacro.postcogito.org`, inferred from
  DigiISP's `digiisp.postcogito.org`. obdev's terms for `16c0:05dc` require a
  domain or e-mail the author owns, so this needs to be right.
- The `DeviceInterfaceGUID` in the MS OS 2.0 set is DigiISP's. Fine for a
  spike; a released DigiMacro wants its own, or Windows will file the two
  devices under one interface class.

### The first version of the spike had no touch sensing at all

Written for a pushbutton — `!(PINB & bit)` with the internal pull-up on — so a
pad on the same pin did nothing, since the pull-up holds the pin high and
nothing ever charges it. The tested touch code was carried over from
DigiMacroCDC verbatim rather than rewritten.

It also now **types its own pad readings**, twice a second, which is the same
diagnostic DigiMacroCDC's `touch` command gave over the terminal — by the only
channel a device with no serial port has. `raw base .` or `raw base T`, in hex.
3606 bytes.

### It works

```
picked digimacro.postcogito.org / DigiMacro
  16c0:05dc  USB 2.1
open() ok
configuration 1
claimInterface(0) ok -- the interface is not a protected class
INFO -> 44 4d 01 01
  magic "DM", protocol 1, firmware 1
```

A keyboard became a WebUSB device and answered a vendor request. The line that
matters is `claimInterface(0)`: that is what a HID interface refuses, and why
the mode switch exists rather than a composite device.

Proven: the two personalities, the re-enumeration, `bcdUSB 2.10` bringing on a
BOS request, the BOS itself, a vendor interface with no endpoints, and control
transfers in both directions through `usbFunctionSetup`.

Tested on:

| | |
|---|---|
| Linux, Chrome | works |
| Windows 10, Chrome and Edge | works, with nothing to install |

So the MS OS 2.0 set does its job and WinUSB binds itself, which was the whole
of the no-driver-install claim.

**One caveat to close before release.** The spike shares `16c0:05dc` *and*
DigiISP's `DeviceInterfaceGUID`, so a machine that has already met DigiISP may
have had WinUSB bound and cached for that pair — in which case the Windows pass
was inherited rather than earned. Giving DigiMacro its own GUID is on the list
anyway, and doing so invalidates any cached binding, so the re-test comes free
at that point. Note that Windows caches a *failed* MS OS 2.0 read too, under
`HKLM\SYSTEM\CurrentControlSet\Control\usbflags\`, and will not retry until
the entry is deleted.

**The landing page works too.** Holding the pad four seconds now makes Chrome
offer "go to digimacro.postcogito.org", which is the URL in the WebUSB
capability being served and honoured. The prompt appears every time the device
enters vendor mode, which is the point of it: once that address serves the app,
the install story is plug in, hold the pad, click the notification, configure.
Nothing to download and nothing to find.

That makes the domain a real dependency rather than a placeholder. `postcogito.org`
is the author's, as DigiISP's `digiisp.postcogito.org` shows, but the subdomain
has to exist and serve the app over https or the notification leads nowhere.
obdev's terms for `16c0:05dc` want the vendor string to contain a domain the
author owns, so the same name does both jobs.

Everything the spike left downstream — the packed table, the EEPROM requests —
has since been built and run on hardware. What is left is the app itself.
