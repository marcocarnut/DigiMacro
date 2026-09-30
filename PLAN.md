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
| BOS + MS OS 2.0 descriptors (lifted from DigiISP) | ~150 |

`isp default lockout` is 6994 today, so the guess is **~4.4 KB** — which would
fit the stock micronucleus with room to spare, not just a bootloaderless
chip. Worth confirming early, because it changes who can install this.

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
  terminator would otherwise run straight into the password.
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
 0x03 'n'  carry on with slot n            (jump; chaining and loops)
 0x04 nn   repeat the previous keystroke nn more times
```

`0x02` replaces `\kE0`-style latching with a direct modifier mask, since the
app can compose it. `0x04` is the run-length idea: "left arrow eleven times"
becomes 3 bytes rather than 11. Repeating *the previous keystroke* rather than
taking a usage of its own is both shorter and more general — it repeats
whatever modifiers were latched.

No argument may be zero: a zero byte terminates the string. The app must
enforce that, and the firmware must reject it, as `\p00` taught us.

## Vendor requests

Numbered above DigiISP's range so a shared tool can tell the devices apart.

| | |
|---|---|
| `INFO` | magic, protocol version, firmware version, slot count, EEPROM size |
| `READ` | wValue = address, wLength = count. Refused while locked |
| `WRITE` | wValue = address, data in the payload. Refused while locked |
| `STATUS` | password set? lockout armed? tries left? unlocked this session? |
| `UNLOCK` | password in the payload; on success clears the lockout count |

Only `INFO` and `STATUS` answer while locked, and `STATUS` must say nothing
about *what* the password is.

**Writes must be chunked.** 496 bytes at 3.4 ms each is nearly two seconds
with USB unpolled, because `eeprom_update_byte` blocks. Take 8–32 bytes per
control transfer so each one completes promptly; DigiISP already writes its
target's EEPROM this way. Use `update` rather than `write` so unchanged bytes
cost no wear when the app saves a table it mostly did not touch.

A checksum over the table would let the firmware notice a write interrupted
half way, which is otherwise silent. Cheap, optional, worth pricing.

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

## First session

1. Copy DigiMacroCDC across; strip the CDC personality, `handle_cmdline`,
   `run_command`, every `cmd_*`, and `keymapUS`. Confirm it still types from a
   table written into EEPROM by hand. Measure — this is where the ~4.4 KB
   guess gets tested.
2. Repack the table and make the walk read-only and bounded.
3. Bring DigiISP's vendor-class personality in as the second mode. Success is
   `lsusb` showing the vendor device after a four-second hold, and the
   keyboard coming back.
4. One request answered — `INFO` — to a WebUSB page. That is the spike.
5. Then `READ`/`WRITE`, then `STATUS`/`UNLOCK`, then the app.
