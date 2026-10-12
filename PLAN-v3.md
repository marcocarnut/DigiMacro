# DigiMacro v3: secret macros

Branch `v3`, from v2.3 (bf5c596).

**Status:**
- Design agreed with Marco on Oct 11 2026, then revised the same day when
  the firmware would not fit (see "What changed, and why").
- Done. Marco confirmed it all on real hardware on Oct 11, the Windows
  client included, and the docs in both languages say how it works.

## What it is

The part of a macro after `{secret}` is stored encrypted. The board can type
it only while it holds the key in RAM. A host's client sends the key, and the
board forgets it when the timeout runs out, at a restart, or on any WRITE.
The key is never stored on the board. A stolen board, even an unhardened or
delidded one, holds only ciphertext.

    user{Tab}{secret}hunter2{Enter}

The user name is typed by anyone's tap. The rest needs the key.

- `{secret}`: without the key, stop quietly.
- `{secret!}`: without the key, act as if the code were not in the table:
  three blinks, and it counts as a wrong code for the lockout.

**`{host}` and `{host!}` are gone.** Marco decided this on Oct 11: nothing is
lost, since `{secret}` at the start of a macro is everything `{host}` was,
plus encryption.
- **Firmware 16** refuses every `{host}` left in a table, quietly or loudly
  as it asked: it fails closed.
- **The page** still reads `{host}` back, so that it can be changed, but it
  refuses to write one, pointing at `{secret}`.
- **The host key, HOSTAUTH 0x64 and the old `digimacro-host` mode** go too.
  HOSTAUTH is refused like any unknown request.
- **Kept only if `{secret}` proved not viable** for some other reason.

## What changed, and why

The first version of the firmware was 960 bytes over the 582 free. These
changes brought it to 3,998 of 4,096 bytes, leaving 98 free:

| Change | Saved, bytes |
|---|---|
| `{host}` dropped (Marco's decision) | about 200 |
| Check value at a fixed address instead of an entry after the last macro: no table walk to find it | about 110 |
| One cipher block per byte instead of per 8: no keystream cache | about 40 |
| `xtea.S` saving its registers through the register file in a loop: 252 → 214 bytes | 38 |
| `-mcall-prologues`, for the firmware only; the core is built as before and is byte-for-byte unchanged | 34 |
| Shared code, a static keystream buffer, and the key state in one struct | the rest |

**Measured alternatives** (the full record is in the chat on Oct 11):
- **Secrets and `{host}`:** 4,266 bytes, which does not fit.
- **Secrets without IR:** 3,892 bytes. The fallback, had `{host}` stayed, was
  two firmware flavours.

## The table

### A secret

    0x81 hh  n0 n1 n2 n3  c0 c1 ... ck  0x00

- **`0x81 hh`** is a jump to a code no macro can have: hh − 1 is an end
  header, its symbol count 5–7.
  - `hh = 0xFE` is `{secret}`; `hh = 0xFF` is `{secret!}`.
  - Old firmware treats it as a jump to a missing code and ends the macro, so
    it types nothing.
- **`n0..n3`** is the nonce: 4 random bytes, new each time the page seals the
  secret. It is never 0, which is the check value's block. The walk rules
  below already forbid any zero byte.
- **`c0..ck`** is the rest of the macro plus a 0x00 of its own, encrypted. It
  is at most 256 bytes, so a secret holds at most 255 keystrokes, because the
  firmware counts its place in one byte. Decrypted, the 0x00 ends the macro,
  as in plain text.
- **The final 0x00** is the ordinary terminator that walkers see.

**No length field.** The page tries nonces until all three hold for the nonce
and the ciphertext, walked with the existing rules:
1. no 0x00 anywhere;
2. no 0x80 where a walk would read it as a byte, so a remote's button can
   never be found inside a secret;
3. the walk ends exactly on the terminator.

Then the firmware's walk and the old page's (by zero bytes) keep their place
in a table with secrets. How often a nonce works, measured:

| Secret length | Nonces that work |
|---|---|
| 40 bytes | 69% |
| 160 bytes | 27% |
| 255 bytes | about 13% |

A try takes microseconds.

**The cipher** is XTEA, 32 cycles, in counter mode, so the board only ever
encrypts.
- Byte j of a secret is c_j XOR the first byte of XTEA_K(v0 = nonce, v1 = j).
- Both words are little-endian, and the block's bytes are v0 then v1.
- It costs one block per byte, about 0.5 ms, next to 20 ms for a keystroke.

### The check value

x = XTEA_K(0, 0), 8 bytes at **0x1E4**, `DM_CHECK_ADDR`.
- It sits where v2's host key was, in the reserved bytes, so the table keeps
  all 480 bytes.
- The page writes it with the table.
- It is the only way the board can tell the right key from a wrong one.

## The requests

Both need neither the hold nor the password, so they must do nothing else.

- **KEYAUTH 0x6A: count 16, the key.**
  - The board computes XTEA_K(0, 0) and compares it with x in full.
  - A wrong key costs a second and never counts towards the lockout.
  - When the key is right, it is kept in RAM for the wait at 0x1F2 (a minute
    by default).
  - The request is wiped from the report either way.
- **PROVE 0x6B: count 8, a challenge.** The answer, in the report, is
  XTEA under x‖x of the challenge.
- **INFO state byte bit 0x80: the key is held.** Bits 0x20 and 0x40 (v2's
  host trust and host key) are no longer set.

The key, with its last keystream block, is wiped from RAM at the timeout, at
a restart, and on any WRITE. A key that runs out part way through a macro
stops it there: nothing after is typed.

## The cloned board

A clone can copy the serial number, but it cannot know x unless the real
board's EEPROM was read. Hardened mode and a password stop that.

So the client:
1. sends 8 random bytes with PROVE;
2. checks the answer against x, which it computes from the key it holds;
3. sends KEYAUTH only if the answer is right.

## The page (milestone 4, done)

- **Key setup:**
  - Makes a random 128-bit key and offers it as a file.
  - Writes x to 0x1E4.
  - Offers to turn each `{host}…` of an old table into `{secret}…`.
- **Writing a table:**
  - The page asks for the key file only when a secret's plain text has to be
    sealed.
  - Secrets it cannot open stay sealed, as `{secret <hex>}`, and can be moved
    to another code or position.
- **Reading a table** without the key shows each secret as
  `{secret <hex>}`, with a 🔒 badge saying how many keystrokes it holds.
- **Old firmware:** the page refuses to write secrets to, or restore a backup
  with secrets onto, firmware below 16.
- **Host-key UI:** replaced by the Secrets section, which reads the key
  state bit and the check value.

## The host client (milestone 5, done)

- `digimacro-host` does PROVE, then KEYAUTH, from the key file.
- The key file keeps v2's format, `serial key` per line, with 32 hex digits
  for the key; v2's 16-digit lines are skipped with a word.

## Firmware: version 16 (milestone 2, done)

- **`app/xtea.S`** (214 bytes), checked 8/8 against the vectors on the
  simulated chip. The PC build uses the C reference in `test/xtea.h`.
- **`secret_byte(a)`** gives every byte the player reads. Mark and repeat,
  modifiers, pauses and jumps work inside a secret, and a `{goto}` leaves it
  in the clear.
- **`find_header`** is unchanged.
- **Size:** 3,998 of 4,096 bytes, 98 free. RAM: 28 bytes for the key, nonce
  and keystream block, and 6 for the clock and pause.

## Tests

- **Firmware on the PC** (`test/test.cpp`, sections 22–22k):
  - no key; the right key; the timeout, with the key gone from RAM; a wrong
    key's second, never a wrong code;
  - `{secret!}` as a wrong code; the LED;
  - a guesser's misses; jumps in and out; repeats and pauses inside;
  - the key running out mid-macro;
  - PROVE;
  - an old `{host}`.
- **The core's suite** (`core/test`): KEYAUTH and PROVE without the gates,
  HOSTAUTH refused, any WRITE forgets the key.
- **test/webtable.js** (section 7):
  - XTEA against the spike's vectors;
  - the keystream spelled out from `xtea()`;
  - 300 random secrets sealed and opened; the walk rules; the length limit;
  - text with and without the key; sealed secrets moved without it;
  - the refusals.
- **The cross-check** (test/run.sh): the page seals a table, the firmware
  opens it with KEYAUTH, and each code types exactly what the same macro
  typed in the clear.
- **Mutation checks:**
  - 7 on table.js;
  - 8 on the firmware: the counter, the nonce, the wipe at the timeout, the
    per-byte key check, the pause, loud refused as quiet, a jump that keeps
    decrypting, PROVE's key.
  - All were caught.
- **End to end on the simulated board** (`test/sim/secret.c`,
  `make simtest`, and test/run.sh when `../simavr-softusb` is built). It runs
  the real `xtea.S`, which the PC tests cannot:
  1. the page's sealed EEPROM is loaded;
  2. without the key, a tap stops at the secret;
  3. PROVE matches a client's computation;
  4. a wrong key, then the second's pause;
  5. the right key: INFO bit, wiped report, and the tap types it all;
  6. a minute later the key is gone, from RAM too, and the tap stops again.
  - A wrong constant in `xtea.S` fails 4 of its checks; the PC tests pass
    with it.

## Milestones

1. **Done.** XTEA and counter mode in table.js; the encoder, decoder and
   nonce tries, with tests.
2. **Done (PC-tested).** Firmware: the cipher, `secret_byte` and the player.
3. **Done.** Firmware: KEYAUTH, PROVE and the wipe, with the simulated
   end-to-end test.
4. **Done (tested in headless Chrome against a mock board).** The page:
   - the Secrets section: make, load, remove, and the wait;
   - sealed rows shown with a 🔒 badge;
   - sealing on Write;
   - the `{host}` conversion;
   - version gates on writing and restoring;
   - the power-on warning, the reference table, and the strings that
     mentioned the host key.
5. **Done.** `digimacro-host`:
   - It reads 32-digit keys from host.key, skipping v2's with a word.
   - It sends PROVE with 8 random bytes (getrandom, or rand_s on Windows)
     and checks the answer; only then does it send KEYAUTH.
   - It says which happened: holds the key, refused, failed to prove itself
     (a copy, or a key that is not the board's), or firmware older than 16.
   - `-e` and `-k` are as before.
   - The XTEA reference is `tools/hostauth/xtea.h`, which the PC tests use
     too.
   - test/hostclient.c runs the client's own code against a fake board: the
     real one, a copy with its serial number, and firmware 15. A client
     that skips the check fails 4 of its checks.
6. **Done.** Confirmed on hardware by Marco: the update to 16, the `{host}`
   conversion, secrets with and without the key, the minute running out,
   the key file in a new session, the Linux and Windows clients, and the
   regressions `-mcall-prologues` could have caused (taps, the hold, IR,
   the power-on macro, the password and lockout). The READMEs and
   HARDENED_MODEs in both languages describe secrets in place of `{host}`.
   What remains is the release: the tag, and the host binaries rebuilt.
