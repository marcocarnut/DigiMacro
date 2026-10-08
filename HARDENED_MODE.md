# Hardened mode

Read all of this before you put a password, a passphrase or anything else
secret on a DigiMacro board.

A DigiMacro board as it comes is **not** a safe place for a secret. It has a
bootloader, Micronucleus, which listens for a few seconds every time the board
is plugged in and will write whatever it is sent into flash. Anyone who has
the board for a minute and knows how to upload a sketch can upload one that
types out the whole EEPROM: every macro, the password, the host key. The hold,
the password, the lockout and the host check all live in the firmware, and the
firmware is exactly what that sketch replaces. Nothing in the app can change
this.

Hardened mode closes that hole. It costs the convenience of updating the
firmware from the page, it needs an ISP programmer, and it is permanent until
you choose to erase everything. This document says what it protects against,
what it does not, and how to set it up.

## What each layer stops

Every layer below answers a different question, and none of them answers the
others'. A secret is only as safe as the weakest one you rely on.

| layer | stops | does not stop |
|---|---|---|
| **the hold** — four seconds on the pad opens a half-minute window | a web page or program on any computer reading or writing the table | anyone holding the board |
| **the password** | someone holding the board reading the table through the app | anyone uploading a sketch through the bootloader, or reading the chip with a programmer |
| **the lockout** | someone tapping their way through the 62 possible codes | anything that does not involve tapping |
| **host trust** — `{host}` and `{host!}` | a guarded macro typing on a computer that has not sent the board's host key | anything that reads the EEPROM directly |
| **no bootloader** | uploading a sketch over USB, the minute-long attack above | reading the chip with an ISP programmer |
| **lock bits** | reading the flash or the EEPROM with a programmer, and changing the fuses | erasing the chip, which also clears the lock bits |
| **EESAVE left unprogrammed** | that erase being a way in: erasing the chip erases the EEPROM with it | — |

The last three together are hardened mode. Each one alone is worth little:

- **The lock bits** stop a programmer reading the chip. But on an ATtiny85 they
  cannot stop the chip writing its own flash: there is no protected boot
  section. So with a bootloader present, the minute-long attack works with the
  lock bits set. **The bootloader has to go.**
- **Without the bootloader** nothing can be uploaded over USB. But a
  programmer can read the chip directly, unless **the lock bits** are set.
- **The lock bits can be cleared,** but only by erasing the whole chip. With
  **EESAVE unprogrammed** — the factory setting, and a Digispark's — that
  erase takes the EEPROM with it. An attacker who clears the lock bits finds
  nothing left to read.

## What hardened mode does not stop

Be honest with yourself about these before relying on it.

- **A laboratory.** The ATtiny85 is a general-purpose microcontroller, not a
  secure element. Lock bits on chips like it have been defeated by voltage and
  clock glitching, and by opening the package. Hardened mode stops someone
  with a programmer and an afternoon. It does not stop someone with equipment,
  skill and a reason.
- **The computer it types into.** A macro types its secret as keystrokes, so
  anything on that computer that sees keystrokes sees the secret: a keylogger,
  a malicious program, a screen recorder, whatever field had focus. Host trust
  decides *where* the board types, not what that computer does with it.
- **Your host key file.** Anyone who can read `~/.config/digimacro/host.key`,
  or plug the board into the computer where the client runs, can make guarded
  macros play. Root on that computer can also read the key off the USB bus.
- **Someone watching you tap.** A code seen is a code known. The lockout limits
  guessing, not knowing. Host trust helps here: a code seen is no use on
  another computer, if the macro is guarded.
- **A weak password.** The app needs the hold, and allows one wrong answer per
  second, but a short password still falls to someone patient with the board in
  hand. Use the full twelve characters.
- **Your own backups.** The app's backup is encrypted with the board's
  password, so it is as strong as that password — but a copied file can be
  guessed at offline, with no hold and no one-second wait. A board with no
  password backs up in the clear. Keep backups where you would keep the
  secrets themselves.

## What it costs

- **No firmware updates from the page.** There is no bootloader to update
  through. The status line says *(hardened: no bootloader)*. Updating means
  erasing the chip with a programmer, which erases everything, and starting
  again.
- **An ISP programmer.** [DigiISP](https://digiisp.postcogito.org) turns a second
  Digispark into one, programmed from the browser with nothing to install. It
  reads and writes flash and EEPROM and sets fuses and lock bits, all from its
  web page, and draws the wiring for a Digispark target. Any other AVR
  programmer works too; the avrdude lines are below.
- **Starting over erases the board's identity.** The serial number lives in
  EEPROM. A new one means Chrome asks you to pick the board again, and every
  computer needs the new host key file.

## Setting it up

The order matters. The chip is erased first, configured while it is still
open, and locked last, because locking is the step that cannot be undone
without losing everything.

### 1. Save a backup

Erasing the chip in step 3 erases the EEPROM: your macros, password, lockout
settings, host key and serial number. Save them first: in the app, **Backup**,
**Save a backup**. The file holds the whole EEPROM, encrypted with the board's
password if it has one, and restoring it in step 5 brings the board back as
itself — serial number included, so Chrome's permission and every computer's
host key file still fit. Or start fresh, and set everything up again in
step 5.

### 2. Build the firmware without a bootloader

```
./build.sh isp
```

This leaves `build/DigiMacro.ino.hex`. It differs from the ordinary build in
three ways:

- it may use all 8192 bytes of flash;
- it has no REBOOT command, since there is nothing to reboot into;
- **it calibrates its own clock.** Micronucleus normally tunes the chip's RC
  oscillator against the computer's USB timing and leaves it tuned for the
  firmware. Without Micronucleus, the firmware does this itself, at the end of
  every USB reset, using the routine V-USB provides for exactly this.

### 3. Erase, flash and set the fuses

Wire the programmer to the DigiMacro board as the DigiISP page shows for a
Digispark target.

**If the programmer cannot reach the chip at all,** check whether the board
has its reset pin disabled: some boards ship with a high fuse of `0x5D`
instead of `0xDD`, turning P5 into an ordinary pin. A programmer like this one
cannot talk to such a chip, and it needs a high-voltage programmer to turn
reset back on first — an XGecu TL866 or T48, say, which `minipro` drives on
Linux and which does high-voltage serial programming for the ATtiny85. Most
Digisparks keep reset enabled.

Then, in this order:

1. **Erase the chip.** This removes Micronucleus and, with EESAVE
   unprogrammed, the EEPROM.
2. **Write `build/DigiMacro.ino.hex` to flash.**
3. **Write the fuses:**

| fuse | value | why |
|---|---|---|
| low | `0xE1` | as a Digispark: the PLL clock, which is then tuned to the 16.5 MHz USB needs |
| high | `0xDD` | as a Digispark. **EESAVE (bit 3) is 1, unprogrammed**, so a chip erase takes the EEPROM with it. Reset stays enabled, so a programmer can still erase the chip if you start over. Disabling reset would gain nothing, since high-voltage programming obeys the same lock bits and erase rules. debugWIRE stays off. |
| extended | `0xFE` | as a Digispark. It leaves SELFPRGEN programmed, which lets code on the chip write flash -- and that is of no consequence here: with no bootloader nothing else runs on the chip, and DigiMacro contains no SPM instruction at all, so there is nothing that could. `0xFF` is equally fine. |

With avrdude and DigiISP, or any USBasp:

```
avrdude -c usbasp-clone -p t85 -e -U flash:w:build/DigiMacro.ino.hex:i \
        -U lfuse:w:0xe1:m -U hfuse:w:0xdd:m -U efuse:w:0xfe:m
```

### 4. Check that it works

Plug the board in on its own.

- **It must enumerate and type.** Tap a code, or open the app. If it does not,
  the clock calibration has failed on this board. Nothing is locked yet, so
  you can go back to step 3, or return to an ordinary board as described
  under *Changing things afterwards*.
- **The status line must say *(hardened: no bootloader)*.**

### 5. Restore, or set it up

**With a backup:** in the app, **Backup**, choose the file, type the password
it was saved with, and **Restore**. Everything comes back as it was, and the
board restarts itself so that its restored serial number takes effect; press
Connect and pick it again. (Firmware older than 8 cannot restart itself:
unplug the board and plug it in again — the restored serial is only sent when
the board is plugged in.)

**Without one,** set up everything in the app:

- **a password, twelve characters.** Without one, the hold is the only thing
  between a stranger holding the board and the table;
- **the lockout**;
- **a host key**, with `{host!}` at the very start of every macro that types a
  secret, and the key file installed on each computer you trust.

Either way, try every guarded macro on a trusted computer and on one that is
not, while the chip is still open to fixing mistakes.

### 6. Lock it

Write the lock byte **`0xFC`**: LB1 and LB2 both programmed, mode 3. The flash
and EEPROM can no longer be read or written by a programmer, and the fuses can
no longer be changed. **Do not erase the chip in this step.**

```
avrdude -c usbasp-clone -p t85 -U lock:w:0xfc:m
```

Then read the EEPROM back with the programmer. It should no longer show your
data.

The app checks too. A board on firmware 8 or later reports its fuses and lock
bits as the chip reads them, and the status line says **(hardened)** only when
everything above is in place: no bootloader, lock byte `0xFC`, EESAVE
unprogrammed, debugWIRE off. If anything is missing it
says **(not fully hardened…)** instead, in red, and lists what. Before this
step the lock bits are expected to be missing; after it, nothing should be.

The firmware runs exactly as before: the lock bits restrict the programmer,
not the program. The app keeps working, because it talks to the firmware, and
the firmware still reads its own EEPROM.

### 7. Save a new backup

Save one now, from the board as it will be used, and keep it where you would
keep the secrets themselves. The lock bits stop a programmer reading the
EEPROM, not the firmware, so the app can back up a locked board too: do it
again whenever the macros change.

## Changing things afterwards

- **Macros, password, lockout, host key:** the app, as always. Hardening
  changes none of that.
- **The firmware:** save a backup in the app, then erase the chip with the
  programmer, which clears the lock bits and the EEPROM together, and go
  through the steps again from 2: restore, check, lock. The app reads a locked
  board's EEPROM through the firmware, so the backup is always current. A
  high-voltage programmer does all of this too, without the reset pin.

  **Without a backup, your macros do not come back.** The erase takes the only
  copy, by design.
- **Going back to an ordinary board:** erase the chip, write the upstream
  Micronucleus 2.6 bootloader (`t85_default.hex`, which DigiISP's repository
  carries in `firmware/bootloader/`) with the programmer, and make sure the
  extended fuse is `0xFE`, since Micronucleus needs to write flash. The board then
  takes firmware over USB again, from the page or with `./build.sh upload`.

## Checklist

- [ ] backup saved from the app, or a fresh start chosen
- [ ] `./build.sh isp`
- [ ] chip erased, flash written, fuses `0xE1` `0xDD` `0xFE`
- [ ] board enumerates, types, and says *(hardened: no bootloader)*
- [ ] backup restored and the board replugged — or, from scratch: a
      twelve-character password, the lockout, a host key installed, and
      `{host!}` first in every secret macro
- [ ] guarded macros tried on a trusted computer and an untrusted one
- [ ] lock byte `0xFC` written, without an erase
- [ ] EEPROM read back with the programmer, and no longer your data
- [ ] the app's status line says **(hardened)**
- [ ] a new backup saved, and kept with the secrets
