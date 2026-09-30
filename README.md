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

## Status: architecture proven, firmware not written

`spike/` answers the one question everything rested on — whether a keyboard can
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

3550 bytes of the 6650 the stock bootloader leaves, so this should install over
USB like any sketch — no programmer needed.

`PLAN.md` holds the design and the order of work.

## Why a mode switch rather than one device

A HID keyboard cannot also be a WebUSB device. Chrome refuses to claim
interfaces of protected classes, HID among them, and Windows binds a HID device
to HIDClass rather than to WinUSB. So the board is one or the other, and a
four-second hold changes it over.

Which is the better arrangement anyway: reaching the macro table needs someone
holding the pad, so **no web page can reconfigure the device on its own**, and
while it is configurable it is not a keyboard, so it cannot be made to type.

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
| `DigiModeUSB/` | one V-USB stack with two personalities: HID keyboard, or vendor class for WebUSB |
| `spike/` | the derisking sketch |
| `web/` | the WebUSB test page; the real app will live here |
| `PLAN.md` | design, decisions and what comes next |

Descriptors for the WebUSB side are lifted from [DigiISP](../DigiISP), which
had already solved the BOS and MS OS 2.0 work.

## Licence

GPLv2, following V-USB.
