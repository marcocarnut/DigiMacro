# Hardened mode

**English** · [Português](HARDENED_MODE.pt-BR.md)

Read all of this before you put a password, a passphrase or anything else
secret on a DigiMacro board.

## Overview

Without several precautions, a DigiMacro board is **NOT** a safe place for
passwords or any kind of secret material. There are only 62 possible morse
codes, and it only takes a few minutes to try them all; and the board talks
to any computer: simply plugging it in elsewhere and opening the
configuration page reveals all its macros.

From now on, we will use the term "secrets" to mean passwords, user names or
any other private information you store in a macro to be typed later; and
the term "password" is reserved for the one password you can set on the
board itself, in the configuration page, to protect it. These are very
different things: don't confuse them.

If you want to store secrets (like administrator credentials) in a macro,
I'd like to at least try to talk you out of it: think long and hard about
whether you really need to. There is usually a safer, better way to do it --
most systems have their own ways to automate privileged actions. Study and
use them.

But it is also true that there are some occasions where there's no better
alternative and the risk can either be mitigated or is outright acceptable.
It depends on your own circumstances and it's your call. So if you really
want to do it, here is what to do, in this order:

1. **Set a password on the board.** Make it strong: the full 12 characters,
   with a mix of uppercase and lowercase letters, numbers and punctuation,
   in the **Password** section of the configuration page. Once it is set,
   the page starts telling you that your board is not fully hardened.

2. **Lock the chip,** right away, before you put any secret on the board.
   With an In-System Programming (ISP) programmer, set the chip's lock bits.
   This stops anyone from reading the board's memory, where the secrets will
   be kept, with a programmer of their own, which would bypass the password
   entirely. The configuration page then says your board is hardened.
   See *Programming the lock bits*.

   The password and the lock bits need each other: either one
   alone is easy to get around. That's why every board I have ever
   personally delivered to end users went out with the lock bits set --
   so that they become "fully hardened" as soon as the user sets the password
   (you can do it either way: 1 then 2 or 2 then 1, as long as you do both).

3. **Turn on the lockout,** whatever else you do. The board then stops
   responding when someone taps too many wrong codes. On its own it is the
   weakest form of protection, since there are only 62 codes to try, but it
   is better than nothing, and it works together with the key. See *The
   lockout*.

4. **Protect the secrets with a key,** if you'll be plugging the board into
   just a handful of computers you control, they all run Linux or Windows,
   and you'll be needing the secrets at moments you can predict. The secrets
   are then stored in encrypted form, and the board types them only on those
   computers, which makes it much less useful to someone who steals it. See
   *Secrets and the key*.

5. **Save a backup.** See *Backups*.

The following sections go into more detail on each step.

## Programming the lock bits

Do this right after setting the password, before you put any secret on the
board. If the board already holds macros, save a backup first (see
*Backups*), just in case. Locking erases nothing, and everything else -- the
macros included -- can still be set up and changed through the configuration
page afterwards.

If you don't have an ISP programmer but you have a second Digispark, you can
turn it into one with [DigiISP](https://digiisp.postcogito.org). If you
already have another ISP programmer, by all means use it. Whatever the
programmer, check its documentation to learn how to write the chip's fuses
and lock bits.

First, check the fuses. Connect to the board in the configuration page and
click **(not fully hardened…)** in its status line: it shows a checklist of
what the chip itself reports. Everything but the lock bits should be ticked.
If anything else isn't, write these fuse values with the programmer, which
erases nothing:

* low fuse: `0xE1`
* high fuse: `0xDD`
* extended fuse: `0xFE`

Then plug the board back in and check again: only the lock bits should be
missing. Get this right now, because once the chip is locked, the fuses can
no longer be changed.

Then write the lock byte: `0xFC`. Make sure the programmer does **not** erase
the chip while it does this: some erase by default before writing, and that
would wipe whatever the board holds.

If we don't do this, anyone could use a programmer just like yours to read
the memory where the macros and the password are stored, since a programmer
bypasses the password check entirely. With the lock bits set, they get what
looks like an empty chip, as it comes from the factory: every byte reads
`0xFF`.

The chip can still be reused -- anyone can erase it and install other
programs on it. But a full chip erase also erases the macros, so the
secrets are safe.

When the lock bits are set, the configuration page says your board is
**(hardened)**. The board works exactly as before, firmware updates
included.

## The lockout

When you tap a morse code that has no macro, the board blinks its LED
quickly three times and counts a wrong code. The lockout, in the
configuration page's **Lockout** section, works in two stages:

* **soft lock:** after a number of wrong codes (three by default), the board
  ignores every tap for a while: a minute by default, adjustable up to 40.

* **hard lock:** after a number of soft locks (also three by default; 0
  means never), the board stops responding to taps at all, the power-on
  macro included, until you give the password in the configuration page.

A wrong password also counts as a wrong code. Unplugging the board gets an
attacker nowhere: the counts survive it, and a soft lock starts its wait
again from the beginning when the board is plugged back in.

Only the password clears the counts. A code that works does not, or anyone
who knew a single code could keep guessing forever; but it means that your
own slips add up too. If that bothers you, add `{clear}` to a macro: it
clears the counts as the password does. Bear in mind that it nearly
nullifies the lockout: anyone who learns that code can use it to buy as many
guesses at the others as they like. Give it a long code that nobody sees you
tap, and never put it in the power-on macro. It's security by obscurity at
best.

## Secrets and the key

A USB device has no way to know which computer it is plugged into, but a
small program called `digimacro-host` can tell the board that it is on one
of yours. It comes in two versions, one for Linux and one for Windows, on the
[releases page](https://github.com/marcocarnut/DigiMacro/releases). Copy the
right one to every computer you plan to plug your DigiMacro into.

To use it:

* In the configuration page's **Secrets** section, press **Make a key**. It
  will show up in your Downloads folder as `digimacro-XXXXXXXX.key`, where
  `XXXXXXXX` is your board's serial number. Copy it to every computer you
  trust, as `~/.config/digimacro/host.key` on Linux or
  `%APPDATA%\digimacro\host.key` on Windows, which is where `digimacro-host`
  looks for it. One file can hold the keys of several boards, one line each.

  Keep a copy of the file somewhere safe. The key is stored neither on the
  board nor in the page, only in that file: without it, nobody, you
  included, can read or change the secrets again (you can delete and
  reenter them, though). The next time you open the configuration page,
  the secrets show up scrambled until you give it the file (**Load the key file**).

* Add `{secret!}` at the beginning of every macro you want protected, and
  write the macros. Everything after it is stored encrypted, and typed only
  if the computer has sent the key within the last minute (adjustable up to
  40 minutes in **Secrets**). If it hasn't, the board acts as if the code
  were not in the table at all: three blinks, and a wrong code for the
  lockout. For a macro that types a user name and then a secret, put
  `{secret}` between the two instead: the user name types anywhere, and the
  rest only where the key was sent.

* Any time you want a computer to receive the secrets, run `digimacro-host`
  on it. `digimacro-host -e 30` sends the key again every 30 seconds for as
  long as it runs: start it when you log in, and the secrets play while you
  are logged in. On Linux it needs the same udev rule as the configuration
  page; the [README](README.md#secrets) has the details.

Bear in mind that the key protects you against your board revealing the
secrets to a different computer than intended, but not to different programs
or form fields on the same computer.

## Backups

The configuration page can save your macros to a single file and restore
them, along with the password, the lockout settings and the board's serial
number. If your board has a password, the file is encrypted with it and asks
for it on restore, so the backup is as secure as your password (you did
choose a strong, hard-to-guess password, did you not?). If your board has no
password, the backup is not encrypted at all. Either way, the secrets in it
stay encrypted with the key, which no backup holds.

Because a backup carries the serial number, restoring it to a different
board makes a clone that your computers cannot tell from the original, and
would send the key to. Keep backups where you would keep the secrets
themselves, and apart from the key file.

## Environment

The configuration page has access to everything on the board. The official
published version never uploads anything to the internet, but you can't
really be sure that the copy you loaded is the official one, and taking the
computer offline is not enough on its own: a fake copy could keep what it
reads and send it the moment the computer is back online. The only way to be
sure is to serve the page yourself, from your own copy of the repository, on
a computer that stays offline -- no Wi-Fi, Ethernet or Bluetooth -- the
whole time; see *A counterfeit or tampered-with page* below. Most people
don't bother, though. Still, we do recommend it.

Bear in mind the usual precautions, too: make sure you're not being filmed
or otherwise observed, and that there is no malware, keylogger or screen
recorder running on your computer.

## Technicalities

The material below is a more technical, in-depth look at what has already
been covered above, for nerds and cybersecurity people.

### What each layer stops

Every layer below answers a different question, and none of them answers the
others'. A secret is only as safe as the weakest one you rely on.

| layer | stops | does not stop |
|---|---|---|
| **the hold** — four seconds on the pad opens a half-minute window | a web page or program on any computer reading or writing the table | anyone holding the board |
| **the password** | someone holding the board reading the table through the app, or putting firmware on it that reads it out | reading the chip with a programmer |
| **the lockout** | someone tapping their way through the 62 possible codes | anything that does not involve tapping |
| **secrets** — `{secret}` and `{secret!}` | a secret typing on a computer that has not sent the board's key; anyone who reads the EEPROM — through the app, with a programmer, or in a laboratory — reading the secret | the board, or a copy made from its EEPROM, being given the key by a computer that sends it |
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

### What hardened mode does not stop

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
  and a reason. The secrets are the exception: they are not on the board in
  any form that can be read without the key. But the check value is, and
  with it a laboratory can make a copy of the board that your computers would
  send the key to. Treat a board you lose as a key you lose: see *Changing
  things afterwards*.
- **The computer it types into.** A macro types its secret as keystrokes, so
  anything on that computer that sees keystrokes sees the secret: a keylogger,
  a malicious program, a screen recorder, whatever field had focus. The key
  decides *where* the board types a secret, not what that computer does with
  it.
- **Your key file.** Anyone who can plug the board into a computer where the
  client runs can make its secrets play. Anyone who can read
  `~/.config/digimacro/host.key` (`%APPDATA%\digimacro\host.key` on Windows)
  can do that anywhere, and with the board's EEPROM, or a backup they can
  open, read the secrets outright. Root on that computer can also read the
  key off the USB bus.
- **Someone watching you tap.** A code seen is a code known. The lockout limits
  guessing, not knowing — though since firmware 17 playing a known code no
  longer clears it, so one code buys no more guesses at the rest. Secrets
  help here: a code seen is no use on another computer, if the macro has
  one.
- **A weak password.** The board allows one wrong answer per second, and the
  app also wants the hold, but a short password still falls to someone
  patient with the board in hand — and with no firmware on the board, after
  an update cut off, the core asks for the password alone. Use the full
  twelve characters.
- **Your own backups.** The app's backup is encrypted with the board's
  password, so it is as strong as that password — but a copied file can be
  guessed at offline, with no hold and no one-second wait. A board with no
  password backs up in the clear. The secrets in a backup stay encrypted
  with the key, which no backup holds. Keep backups where you would keep the
  secrets themselves, and apart from the key file.
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

### What it costs

- **An ISP programmer, once,** for the fuses and the lock bits.
  [DigiISP](https://digiisp.postcogito.org) turns a second Digispark into one,
  programmed from the browser with nothing to install. It reads and writes
  flash and EEPROM and sets fuses and lock bits, all from its web page, and
  draws the wiring for a Digispark target. Any other AVR programmer works
  too; the avrdude lines are below.
- **Starting over erases the board's identity,** unless you restore a backup.
  The serial number lives in EEPROM. A new one means Chrome asks you to pick
  the board again, and the secrets need a new key, which every computer then
  needs in its key file; a restored backup brings both back, so the old key
  file works again.

### Setting it up

The order matters. The password and the lock bits go on first, before any
secret does, so that no secret ever sits on a chip that can be read: a
programmer gets around the password, and without a password anyone can put
firmware on the board that reads it out. Locking erases nothing and changes
nothing the page can do, so everything else is set up afterwards, through
the page, as usual.

If the board already holds macros, save a backup before you start
(**Backup**, **Save a backup**): a slip with the programmer can erase the
chip, and the erase takes the EEPROM with it.

#### 1. Install the firmware

From the page — **Set up a new board** for a Digispark as it comes
with its Micronucleus bootloader. It will first install the core, then
the main application. Connect, and get in with the hold.

#### 2. Set a password

**Password**, twelve characters. Without one, nothing below is worth doing:
the board takes firmware from anyone for six seconds after every replug.

#### 3. Check the fuses

The status line now says **(not fully hardened…)**, in red: the board has a
password and is not locked. Click it for the checklist, which shows what the
chip itself reads:

- a password set;
- the lock bits — unticked until step 4;
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

#### 4. Lock it

Write the lock byte **`0xFC`**: LB1 and LB2 both programmed, mode 3. The flash
and EEPROM can no longer be read or written by a programmer, and the fuses can
no longer be changed. **Do not erase the chip in this step.**

```
avrdude -c usbasp-clone -p t85 -U lock:w:0xfc:m
```

Then read the EEPROM back with the programmer. It should read as all
`0xFF`.

The status line should now say **(hardened)**. The board runs exactly as
before: the lock bits restrict the programmer, not the program. The app keeps
working, because it talks to the board, and the board still reads its own
EEPROM and writes its own flash.

#### 5. Set up the rest

In the app:

- **the lockout**;
- **a key for secrets** (**Secrets**, **Make a key**), with `{secret!}`
  before the secret part of every macro that has one — at its very start,
  unless it types a user name first — and the key file installed on each
  computer you trust. Keep a copy of the file somewhere safe: without it,
  nobody can read or change the secrets again.

Or restore a backup that has them: **Backup**, choose the file, type the
password it was saved with, and press **Restore** twice — the first press
asks you to confirm. The board takes the backup's password along with
everything else, and restarts itself so that its restored serial number
takes effect; press Connect and pick it again.

Either way, try every secret on a trusted computer and on one that is not.

#### 6. Save a backup

**Backup**, **Save a backup**, and keep it where you would keep the secrets
themselves. The lock bits stop a programmer reading the EEPROM, not the
board, so the app backs up a locked board like any other: do it again
whenever the macros change.

### Changing things afterwards

- **Macros, password, lockout, key:** the app, as always. Hardening
  changes none of that.
- **A board lost or stolen:** delete its line from the key file on every
  computer. Its secrets stay unreadable, and no copy of it is ever sent the
  key again.
- **The firmware:** from the page — **(update…)** in the
  status line, the hold and the password. The lock bits stay; nothing is
  erased.
- **Starting over:** save a backup, erase the chip with the programmer — which
  clears the lock bits and the EEPROM together — and go through the steps
  again from 1. **Without a backup, your macros do not come back.** The erase
  takes the only copy, by design.
- **Going back to Micronucleus:** **Back to a plain Digispark**, at the
  bottom of the page, does it with no programmer: it erases the EEPROM and
  puts Micronucleus back. The lock bits stay, which leaves a Digispark a
  programmer cannot read; to clear them as well, erase the chip and write
  the upstream Micronucleus 2.6 bootloader (`t85_default.hex`, which
  DigiISP's repository carries in `firmware/bootloader/`) with the
  programmer, keeping the extended fuse at `0xFE`. The board is then a
  Digispark as it came.

### Checklist

- [ ] core and app from the page or the programmer
- [ ] a twelve-character password
- [ ] fuses `0xE1` `0xDD` `0xFE`: the checklist shows only the lock bits
      missing
- [ ] lock byte `0xFC` written, without an erase
- [ ] EEPROM read back with the programmer: all `0xFF`
- [ ] the app's status line says **(hardened)**
- [ ] the lockout, a key installed and its file kept safe, and `{secret!}`
      before every secret — or a backup restored that has them
- [ ] secrets tried on a trusted computer and an untrusted one
- [ ] a backup saved, and kept with the secrets
