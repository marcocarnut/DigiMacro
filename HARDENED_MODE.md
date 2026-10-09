# Hardened mode

Read all of this before you put a password, a passphrase or anything else
secret on a DigiMacro board.

A DigiMacro board with no password is **not** a safe place for a secret. Its
core takes new firmware from anyone in the six seconds after it is plugged in
— that is how the page updates it — and firmware that types out the whole
EEPROM takes seconds to write: every macro and the host key with it. The
hold, the lockout and the host check all live in the firmware, which is
exactly what such firmware replaces.

**A password closes that door.** The board's core checks it, not the
firmware, and an update needs it, whether firmware is running or not — and
a board left half updated ends its own unlock after a few seconds. But a
password does nothing about someone who opens the chip with a programmer:
reading flash and EEPROM directly, password and all. Hardened mode closes
that door too. It needs an ISP programmer once, it is permanent until you
choose to erase everything, and the board goes on updating from the page as
before. This document says what it protects against, what it does not, and
how to set it up.

This is v2's. A v1 board — firmware 9 or older, with Micronucleus — hardens
differently, by losing its bootloader: see *v1 boards* at the end.

## What each layer stops

Every layer below answers a different question, and none of them answers the
others'. A secret is only as safe as the weakest one you rely on.

| layer | stops | does not stop |
|---|---|---|
| **the hold** — four seconds on the pad opens a half-minute window | a web page or program on any computer reading or writing the table | anyone holding the board |
| **the password** | someone holding the board reading the table through the app, or putting firmware on it that reads it out | reading the chip with a programmer |
| **the lockout** | someone tapping their way through the 62 possible codes | anything that does not involve tapping |
| **host trust** — `{host}` and `{host!}` | a guarded macro typing on a computer that has not sent the board's host key | anything that reads the EEPROM directly |
| **lock bits** | reading the flash or the EEPROM with a programmer, and changing the fuses | erasing the chip, which also clears the lock bits |
| **EESAVE left unprogrammed** | that erase being a way in: erasing the chip erases the EEPROM with it | — |
| **brown-out detection** | a sagging supply corrupting the flash the board writes when it updates itself | — |

The password and the next two together are hardened mode; the last keeps a
board that writes its own flash from damaging itself. Each alone is worth
little:

- **The password** stops anything over USB: the page, and new firmware. But a
  programmer reads the chip directly, unless **the lock bits** are set.
- **The lock bits** stop a programmer reading the chip. But they cannot stop
  the chip writing its own flash — there is no protected boot section on an
  ATtiny85 — so they are worth nothing while anyone can put firmware on the
  board over USB. **The password has to be there.**
- **The lock bits can be cleared,** but only by erasing the whole chip. With
  **EESAVE unprogrammed** — the factory setting, and a Digispark's — that
  erase takes the EEPROM with it. An attacker who clears the lock bits finds
  nothing left to read.

## What hardened mode does not stop

Be honest with yourself about these before relying on it.

- **Whoever has the password.** They can read every macro through the app,
  and they can put firmware on the board that does anything at all — types
  the EEPROM out, or writes over the core itself. That adds nothing to what
  the password already gives them, but it is worth knowing that the password
  is the whole of it.
- **A laboratory.** The ATtiny85 is a general-purpose microcontroller, not a
  secure element. Lock bits on chips like it have been defeated by techniques
  like voltage and clock glitching, and by opening the package ("delidding
  the chip", as it is called). Hardened mode stops someone with a programmer
  and an afternoon. It does not stop someone with equipment, money, time, skill
  and a reason.
- **The computer it types into.** A macro types its secret as keystrokes, so
  anything on that computer that sees keystrokes sees the secret: a keylogger,
  a malicious program, a screen recorder, whatever field had focus. Host trust
  decides *where* the board types, not what that computer does with it.
- **Your host key file.** Anyone who can read `~/.config/digimacro/host.key`
  (`%APPDATA%\digimacro\host.key` on Windows), or plug the board into the
  computer where the client runs, can make guarded macros play. Root on that
  computer can also read the key off the USB bus.
- **Someone watching you tap.** A code seen is a code known. The lockout limits
  guessing, not knowing. Host trust helps here: a code seen is no use on
  another computer, if the macro is guarded.
- **A weak password.** The board allows one wrong answer per second, and the
  app also wants the hold, but a short password still falls to someone
  patient with the board in hand — and with no firmware on the board, after
  an update cut off, the core asks for the password alone. Use the full
  twelve characters.
- **Your own backups.** The app's backup is encrypted with the board's
  password, so it is as strong as that password — but a copied file can be
  guessed at offline, with no hold and no one-second wait. A board with no
  password backs up in the clear. Keep backups where you would keep the
  secrets themselves.
- **A counterfeit or tampered-with page.** The page reads every macro and
  holds the password, so if someone can change the code it runs, all bets
  are off. Ours is served over HTTPS and we did our homework, but it is
  hosted by an inexpensive cloud provider that we cannot guarantee will be
  hack-proof forever. Taking the computer offline is not enough on its own:
  a page loaded while online can keep what it reads and send it once the
  computer reconnects. Use a copy served from your own checkout of the
  repository (`cd web && python3 -m http.server 8000`), on a computer that
  is offline — no Wi-Fi, no Bluetooth, no Ethernet — while the page is
  open.

## What it costs

- **An ISP programmer, once,** for the fuses and the lock bits.
  [DigiISP](https://digiisp.postcogito.org) turns a second Digispark into one,
  programmed from the browser with nothing to install. It reads and writes
  flash and EEPROM and sets fuses and lock bits, all from its web page, and
  draws the wiring for a Digispark target. Any other AVR programmer works
  too; the avrdude lines are below.
- **Starting over erases the board's identity,** unless you restore a backup.
  The serial number lives in EEPROM. A new one means Chrome asks you to pick
  the board again, and every computer needs the new host key file; a restored
  backup brings both back.

Updates are not a cost: a hardened board takes them from the page, behind the
hold and the password, like any other v2 board.

## Setting it up

The order matters. Everything is configured while the chip is still open, and
it is locked last, because locking is the step that cannot be undone without
losing everything.

### 1. Put v2 on the board

From the page — **Set up a new board** for a Digispark as it comes, **(move
to v2…)** for a board on v1 — or with the programmer, writing
`web/firmware/digimacro-v2-full.hex` as the README's *With a programmer*
shows. Connect, and get in with the hold.

### 2. Set it up

In the app:

- **a password, twelve characters.** Without one, nothing below is worth
  doing: the board takes firmware from anyone for six seconds after every
  replug;
- **the lockout**;
- **a host key**, with `{host!}` at the very start of every macro that types a
  secret, and the key file installed on each computer you trust.

Or restore a backup that has them: **Backup**, choose the file, type the
password it was saved with, and press **Restore** twice — the first press
asks you to confirm. The board restarts itself so that its restored serial
number takes effect; press Connect and pick it again.

Either way, try every guarded macro on a trusted computer and on one that is
not, while the chip is still open to fixing mistakes.

### 3. Save a backup

**Backup**, **Save a backup**. If something goes wrong in the next steps, the
programmer's erase is the way back, and it takes the EEPROM with it.

### 4. Check the fuses

The status line now says **(not fully hardened…)**, in red: the board has a
password and is not locked. Click it for the checklist, which shows what the
chip itself reads:

- a password set;
- the lock bits — unticked until step 5;
- EESAVE unprogrammed;
- debugWIRE off;
- SELFPRGEN programmed, which the board needs to update itself;
- brown-out detection on.

If anything but the lock bits is unticked, set the fuses with the programmer.
Writing fuses erases nothing:

| fuse | value | why |
|---|---|---|
| low | `0xE1` | as a Digispark: the PLL clock, which the board then tunes to the 16.5 MHz USB needs. `0xF1`, with a longer start-up delay, works too |
| high | `0xDD` | **EESAVE (bit 3) is 1, unprogrammed**, so a chip erase takes the EEPROM with it. Brown-out detection at 2.7 V, so a sagging supply resets the chip rather than letting it write flash badly. Reset stays enabled, so a programmer can still erase the chip if you start over; disabling it would gain nothing, since the lock bits already stop both ISP and high-voltage programming reading the chip — tried and confirmed. debugWIRE stays off |
| extended | `0xFE` | SELFPRGEN programmed: the core writes the firmware into its own flash, and without it the board cannot update itself |

With avrdude and DigiISP, or any USBasp:

```
avrdude -c usbasp-clone -p t85 -U lfuse:w:0xe1:m -U hfuse:w:0xdd:m -U efuse:w:0xfe:m
```

**If the programmer cannot reach the chip at all,** check whether the board
has its reset pin disabled: some boards ship with a high fuse of `0x5D`
instead of `0xDD`, turning P5 into an ordinary pin. A programmer like DigiISP
cannot talk to such a chip, and it needs a high-voltage programmer to turn
reset back on first — an XGecu TL866 or T48, say, which `minipro` drives on
Linux and which does high-voltage serial programming for the ATtiny85. Most
Digisparks keep reset enabled.

Plug the board back in, get in, and check again: only the lock bits should
be unticked.

### 5. Lock it

Write the lock byte **`0xFC`**: LB1 and LB2 both programmed, mode 3. The flash
and EEPROM can no longer be read or written by a programmer, and the fuses can
no longer be changed. **Do not erase the chip in this step.**

```
avrdude -c usbasp-clone -p t85 -U lock:w:0xfc:m
```

Then read the EEPROM back with the programmer. It should no longer show your
data.

The status line should now say **(hardened)**. The board runs exactly as
before: the lock bits restrict the programmer, not the program. The app keeps
working, because it talks to the board, and the board still reads its own
EEPROM and writes its own flash.

### 6. Save a new backup

Save one now, from the board as it will be used, and keep it where you would
keep the secrets themselves. The lock bits stop a programmer reading the
EEPROM, not the board, so the app can back up a locked board too: do it
again whenever the macros change.

## Changing things afterwards

- **Macros, password, lockout, host key:** the app, as always. Hardening
  changes none of that.
- **The firmware:** from the page, as on any v2 board — **(update…)** in the
  status line, the hold and the password. The lock bits stay; nothing is
  erased.
- **Starting over:** save a backup, erase the chip with the programmer — which
  clears the lock bits and the EEPROM together — and go through the steps
  again from 1. **Without a backup, your macros do not come back.** The erase
  takes the only copy, by design.
- **Going back to Micronucleus:** erase the chip and write the upstream
  Micronucleus 2.6 bootloader (`t85_default.hex`, which DigiISP's repository
  carries in `firmware/bootloader/`) with the programmer, keeping the extended
  fuse at `0xFE`. The board is then a Digispark as it came.

## Checklist

- [ ] v2 on the board, from the page or the programmer
- [ ] a twelve-character password, the lockout, a host key installed, and
      `{host!}` first in every secret macro — or a backup restored that has
      them
- [ ] guarded macros tried on a trusted computer and an untrusted one
- [ ] a backup saved
- [ ] fuses `0xE1` `0xDD` `0xFE`: the checklist shows only the lock bits
      missing
- [ ] lock byte `0xFC` written, without an erase
- [ ] EEPROM read back with the programmer, and no longer your data
- [ ] the app's status line says **(hardened)**
- [ ] a new backup saved, and kept with the secrets

## v1 boards

A v1 board — firmware 9 or older — keeps Micronucleus, which takes a new
sketch from anyone for a few seconds after every replug, password or not:
v1's password lives in its firmware, and the sketch replaces it. Hardening a
v1 board therefore means removing the bootloader as well, and giving up
updates from the page; v1.0's own copy of this document (`git show
v1.0:HARDENED_MODE.md`) walks through it, and the page's status line still
says **(hardened)** or **(not fully hardened…)** for such boards. Moving the
board to v2 first is simpler, and keeps the updates.
