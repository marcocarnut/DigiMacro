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
and *both* limits go: no fixed number of macros and no 30-byte ceiling.
**480 bytes of table**, so twenty ten-key macros or four long ones, as you
like; the last 32 are kept back for the password, the lockout and whatever
comes after, since it is far cheaper to leave room now than to move a board's
table later.

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
4. ~~Repeat count range~~ — settled: one byte of extra presses, 256 in all,
   and the app writes it as a quantifier.

5. ~~**Store the password, or encrypt the table with it?**~~ **Settled: store
   it, and protect the chip with the lock bits.** Marco's reasoning, and it is
   the right way round: a macro has to play with nobody's help, so the key has
   to be present whenever the device is plugged in. Holding it in RAM means
   typing a password before the device does anything, every single time, and
   keeping it in EEPROM is theatre. Per-entry protection softens that but does
   not remove it, and the answer to "my macro pad wants a password before it
   will work" is that nobody would use it.

   **But the lock bits only protect a board with no bootloader, and that is
   not a detail.** The ATtiny85 lock byte has LB1 and LB2 and nothing else:
   there are no boot lock bits, because there is no boot section. SPM is
   therefore allowed whatever the lock bits say, so micronucleus can still
   rewrite flash on a locked chip — and an attacker with the board in hand
   uploads a sketch that dumps the EEPROM, lock bits or no lock bits. A
   protected deployment is the whole set or none of it:

   - flashed with a programmer, **no bootloader** (`./build.sh isp`)
   - lock bits set, which DigiISP can do
   - EESAVE left unprogrammed, so a chip erase takes the table with it

   Which also shrinks what the password is for. It is not protection against
   someone who has the chip; that is the lock bits' job. It is protection
   against someone who has the *board*, briefly, and can tap 62 codes.

   The case for encrypting is stronger than it first looks, and it is not
   about the cipher. Today the password is protected by the lock bits, and
   **the lock bits need a programmer and exclude the bootloader**. So a board
   installed the way we tell people to install it — over USB, micronucleus
   intact — has no protection at all: anyone can upload a sketch that dumps
   the EEPROM. Encrypting the table is the only thing that protects the
   mainstream case, and it costs no hardware.

   What decides the design is **when the key is available**, not which cipher:

   - The device must type a macro with nobody's help, so if the key lives in
     EEPROM the encryption is decoration. It has to be derived at unlock and
     kept in RAM, which means **a protected macro does not work until the
     board has been unlocked since it was last plugged in**.
   - That is intolerable for everyday macros and exactly right for the ones
     holding secrets, so protection should be **per entry**: a bit in the
     header, clear macros playing always, protected ones only after an unlock.
   - A wrong key must never result in typing. Decrypting with the wrong
     password and playing the result sends random keystrokes into whatever has
     focus — often a password field. A verifier over the plaintext, checked
     before the first keystroke, is not optional.
   - The work factor lives in the **key derivation**, not the cipher. An
     attacker with a dumped EEPROM attacks offline at billions of guesses a
     second; a device that can afford two seconds of stretching per unlock
     buys far more than any choice between Speck, XTEA and ChaCha.
   - **Nonce reuse is the trap.** The app rewrites the whole table on every
     save, so a stream cipher at a fixed counter would encrypt different
     plaintexts under the same keystream, and the XOR of two dumps would give
     away the difference. The nonce has to change per save; the app has a real
     random source and the device does not, so the app should supply it.
   - On Speck specifically: it fits, and it is fine engineering, but ISO
     rejected it and its provenance is a live argument. For a project that may
     be published, XTEA or ChaCha20 avoid the conversation entirely at a
     similar or slightly larger size.

6. **Host authentication, and what falls out of it.** Also Marco's. A USB
   device has no idea which host it is plugged into, so a board that types
   passwords will type them for whoever holds it, guarded only by 62 possible
   codes. The lockout blunts a brute-force but not a shoulder-surfed code.

   A second password that unlocks the *input* — the host proving itself before
   the pad means anything — fixes that for anyone who controls the host
   software, which is the case worth fixing. It also composes with the point
   above: the host is a natural place for the key to come from, and then the
   device never stores it at all.

   That needs the host to talk to the device **while it is a keyboard**, which
   is option A from the top of this file: a second top-level collection on a
   vendor usage page (0xFF00). Chrome's WebHID blocks protected usage pages,
   not vendor ones — it is how Vial talks to a keyboard that stays a keyboard
   — and a native helper reaches the same reports through hidraw without them
   reaching the focused window. The mode switch could then be asked for rather
   than held for, which is the thing Marco wanted from the LED knock and
   cannot get, since no web API sets Caps Lock.

   Two obstacles to price before committing, neither fatal and neither free:

   - **Report IDs against boot protocol.** Two collections on one interface
     are told apart by report ID, and a boot keyboard's report must have none.
     The usual answers are to keep the boot interface clean and put the vendor
     collection on a second interface, or to drop boot support, which costs
     the BIOS and little else.
   - **The endpoint budget.** A second HID interface wants an IN endpoint, and
     V-USB has EP3 available if turned back on. That is the thing the original
     survey said was exhausted, and it is not, quite.

7. **Then does the mode switch survive at all?** Marco's follow-up, and the
   answer is probably not. If a vendor collection can carry bytes, it can
   carry these bytes: a feature report is a control transfer, so `READ` and
   `WRITE` move across nearly verbatim — the chunking, the bounds, the latched
   stall and the app's whole encoder are about EEPROM, not about which
   request carried it.

   What that deletes is substantial: the second device and configuration
   descriptors, the BOS, the MS OS 2.0 set, the re-enumeration, and the
   disconnect handling the app needed because of it. Several hundred bytes
   back, and the Windows caveat about a cached `DeviceInterfaceGUID` goes with
   them, since HIDClass needs no binding and no driver.

   What it costs:

   - **Physical presence**, which is the whole security property today.
   - **The landing page.** Chrome offers one for a WebUSB device and not for a
     HID device, so "plug in, click the notification, configure" disappears.
     That is a real loss and the only one I would argue about.

   **The eight-byte trap.** Two collections on one interface need report IDs,
   and a report ID is a byte in front of the report: a boot keyboard's eight
   becomes nine, and `usbSetInterrupt()` cannot send more than eight. So the
   key array has to shrink — ID, modifier, reserved and three keys is six, and
   we only ever send one key at a time — or the vendor collection goes on a
   second interface with EP3, which keeps boot protocol at the price of an
   endpoint. Worth knowing before this looks free.

   **And physical presence need not die with the mode switch.** It was only
   ever bound to re-enumeration by accident. A write could instead require the
   pad to have been touched in the last few seconds: the app says "touch the
   pad", the person does, the write goes through. Same property, no
   re-enumeration, and a better gesture than a four-second hold — which
   suggests the mode switch was the wrong shape for the requirement all along,
   and the requirement is the part worth keeping.

   Sequencing, since this is a rewrite of the transport and the transport
   currently works on two platforms: build the vendor collection as a spike
   beside what exists, the way the first one was built, and keep WebUSB until
   the HID channel is proven on Linux and Windows both. Nothing here is urgent
   enough to break a working device for.

   ### The spike passed on Linux

   `spikehid/` with `DigiHidUSB`: **3350 bytes**, against the first spike's
   3550 for a device that had no EEPROM protocol in it at all.

   ```
   INFO -> magic "DM", protocol 2, firmware 1, 512 bytes of EEPROM
   40 44 4d 02 01 00 02 00 ...
   ```

   Proven: the keyboard still types with a report ID and three key slots; the
   device is offered to WebHID and the vendor collection comes through; a
   feature report reaches the firmware and comes back **while the device is
   still a keyboard**; and `receiveFeatureReport` returns the payload *without*
   the report ID, which is the thing the specification leaves to taste. The
   write path tolerates it either way, and safely, because no command byte is
   2.

   **The udev rule is not a new cost.** WebUSB needs one on Linux as well
   — Chrome needs write access to `/dev/bus/usb/...` there and to
   `/dev/hidraw*` here, and a stock Linux grants neither. The honest
   comparison is: the same rule on Linux, and *nothing at all* on Windows,
   where WebUSB wanted an MS OS 2.0 descriptor, a WinUSB binding and a cached
   `DeviceInterfaceGUID` to worry about. What this trade really costs is the
   landing page, not the installation.

   ### And on Windows, first try, with nothing installed

   Which settles it. The trade is the same udev rule on Linux and *nothing* on
   Windows, against an MS OS 2.0 descriptor, a WinUSB binding and a cached
   `DeviceInterfaceGUID` to worry about. **Migrated.**

   The firmware went from 4244 bytes to 3790 and the app lost its reconnect
   dance, since nothing re-enumerates any more. `DigiModeUSB`, `spike/`,
   `web/spike.html` and `web/eeprom.html` are gone from the tree and live in
   the history, where they can be read if any of this has to be revisited.

   **The hold survives with a different job.** It used to change what the
   device was; now it opens a thirty-second window during which the table may
   be read and written, and `INFO` says whether one is open so the app can ask
   rather than fail. Physical presence was never really about re-enumeration,
   which is the thing the last few days taught: the requirement outlived the
   mechanism, as requirements do.

   Still open, and now the only thing standing between a hostile page and the
   macro table: the password. Step 4.

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
3. ~~**The app.**~~ **Done.** `web/app.html`: read a board's table, edit it,
   write it back verified, with the byte budget on screen and every mistake
   reported beside the macro that has it.

   The work was not the UI but the macro language, since a macro is mostly
   text and occasionally something text cannot say:

   ```
    \n \t \\ {{        newline, tab, backslash, a literal {
    {Ctrl+c} {F5}     modifiers, and keys that are not characters
    {pause 1.5}       wait, in seconds
    x{11}             eleven in all, as a quantifier reads
    (ab){3}           ...of a group rather than one thing
   ```

   `{11}` rather than `{x11}` was Marco's: it borrows the look of a regular
   expression's quantifier. Which then settled what it means — `ç{5}` was
   expected to type five, so the count is the total and the firmware's
   "n more" is reconciled in the encoder, not in the notation.

   That expectation also found a real hole. `ç` is two keystrokes on a
   dead-key keyboard, so a repeat of "the previous keystroke" typed `çcccc`.
   The fix is a **mark** in the firmware, `0x82`, saying where the repeated
   thing began: 42 bytes, no argument, and no nesting. The encoder puts one in
   whenever the count is counting more than one byte, so `ç{5}` works without
   anyone writing a group — and `(ab){3}` is there for when the repeated thing
   really is several characters. A count *inside* a group is refused, because
   there is one mark and the inner count would take the outer one over.

   A parenthesis stays a parenthesis unless a count follows the closing one,
   which keeps `f(x)` meaning what it looks like; `\(` says so explicitly. A
   key palette inserts all of it at the cursor, brackets wrapping whatever is
   selected.

   It decodes as well as encodes, so a table is edited rather than retyped,
   and `test/webtable.js` checks that encoding what was decoded gives the
   same bytes back — not that the text is identical, because it is not:
   `{Enter}` reads back as `\n`, and `{Ctrl++}` as `{Ctrl+Shift+=}`, since
   `+` is shift and equals. What must never drift is the bytes.

   Three parser bugs came out of writing those tests rather than out of use:
   `{Del}` was not a spelling of Delete, `{Ctrl+}` was read as Ctrl-and-plus
   instead of a missing key, and an out-of-range `{x300}` blamed whatever
   preceded it. A fourth came out of driving the page: the editor's model was
   called `rows` while the table body had `id="rows"`, and the DOM wins that
   argument.

   The loop check this step was meant to include has nothing to check yet, as
   `\j` does not exist. It belongs with step 5.

   Three things were added on top after using it:

   **The app can hand the keyboard back** (`DM_FUNC_LEAVE`, 0x62). Holding the
   pad to leave vendor mode took the device off the bus without the page being
   told, so it sat holding a dead handle and reconnecting failed. The page now
   listens for connect and disconnect — a returning board is a new object,
   not the old one — and the asymmetry turned out to be unnecessary: physical
   presence guards getting *in*, because that is what makes the table
   reachable, while asking to stop being reachable can only take capability
   away.

   **Accents are a statement about the host**, not about the text. The board
   sends scan codes, so an accented character is two presses on a dead-key
   layout and impossible on a plain US one. The app says which keyboard the
   board will meet, and refuses what that keyboard cannot type rather than
   typing something else. A bare accent mark is the key and a space; a mark
   with nothing joined to it is `{'}`; and pasted text is composed to NFC
   first, since a combining mark looks identical and is not.

   **English and Brazilian Portuguese**, in `web/i18n.js`, the encoder's error
   messages included — `table.js` keeps English as its default so the tests
   can read it, and takes a translator from the app. `test/i18n.js` checks the
   dictionaries hold the same keys, because a missing translation is invisible
   in use: it falls back to English and reads as if nobody noticed.
4. **`autoexec` and `\j` first**, being the small ones, and because `\j`
   needs something the mode switch used to provide and no longer does.

   A macro that jumps to itself runs for ever, deliberately, and the only way
   out used to be the four-second hold taking the device out of keyboard mode.
   There is no mode to leave now — but the hold still exists, and giving it a
   second meaning during playback costs nothing: a pause is the one
   interruptible moment, so a hold during a pause stops the macro *and* opens
   the configuration window. One gesture, and it leaves the board in exactly
   the state from which the app can fix what is wrong. Which is better than
   what it replaced: the old escape left you in a mode, not in a position to
   repair anything.

   `autoexec` is the brick hazard's other half, so the app's loop check lands
   with it, and both come before the password rather than after.

5. **`STATUS` and `UNLOCK`**, the password and the lockout — including
   counting failed password attempts, which the CDC version does not do and
   which matters far more over USB. `autoexec` belongs here too, beside the
   lockout that has to gate it; its byte is already reserved at 0x1F3.
6. **Reboot into the bootloader**, a command Marco wants for upgrades and
   DigiISP already has as `0x41`. Twenty bytes: enable the watchdog and spin.

   **It must sit behind the same window as READ and WRITE**, and for a sharper
   reason than they do. A page that can reboot a board into micronucleus can
   have anything flashed onto it, including a sketch that reads the table out
   — so an ungated reboot would hand away more than an ungated read. It is
   also the one command that is pointless on a protected board, which has no
   bootloader to reboot into.

   Upgrading *from the browser* is a separate question and a harder one:
   micronucleus is a plain vendor-class device with no BOS and no MS OS 2.0
   set, so Windows wants a driver for it, which is the thing this whole
   transport change was about avoiding.

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
