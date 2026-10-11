# DigiMacro v3: secret macros

Branch `v3`, from v2.3 (bf5c596). Status: design agreed with Marco on
Oct 11 2026; nothing is built yet.

## What it is

The part of a macro after `{secret}` is stored encrypted. The board can type
it only while it holds the key in RAM. A host's client sends the key, and the
board forgets it when the trust timeout runs out or the board restarts. The
key is never stored on the board. A stolen board, even an unhardened or
delidded one, holds only ciphertext.

    user{Tab}{secret}hunter2{Enter}

The user name is typed by anyone's tap. The rest needs the key.

- `{secret}`: without the key, stop quietly.
- `{secret!}`: without the key, act as if the code were not in the table:
  three blinks, and it counts as a wrong code for the lockout. This mirrors
  `{host!}`.

It is an add-on to v2:
- `{host}`/`{host!}`, the host key, its request, the UI and every existing
  `digimacro-host` script keep working unchanged.
- Tables without secrets are the same bytes as before.

## The table

### A secret

    0x81 hh  n0 n1 n2 n3  c0 c1 ... ck  0x00

- **`0x81 hh`** is a jump to a code no macro can have: hh − 1 is an end
  header, its symbol count 5–7.
  - `hh = 0xFE` is `{secret}`; `hh = 0xFF` is `{secret!}`.
  - Old firmware treats it as a jump to a missing code and ends the macro, so
    it types nothing. `{secret!}` ends quietly on old firmware.
- **`n0..n3`** is the nonce: 4 random bytes, new each time the page encrypts
  the secret. It must never be 0, which is reserved for the check value.
- **`c0..ck`** is the encryption of the rest of the macro plus a 0x00 of its
  own. On decryption, a 0x00 ends the macro, as it does in plain text.
- **The final 0x00** is the ordinary terminator that walkers see.

**No length field.** The page tries nonces until all three hold for the
nonce and ciphertext, walked with today's rules (an escape skips its
argument, 0x80 skips 3):
1. no 0x00 where a walker would read it as a byte;
2. no 0x80 where a walker would read it as a byte, so a remote's button can
   never be found inside ciphertext;
3. the walk ends exactly on the terminator.

Then `find_header`, the old firmware and the old pages all keep their place
in a table with secrets. About half of nonces succeed for a 160-byte secret,
and about 15% for 480 bytes.

**The cipher** is XTEA, 32 cycles, used in counter mode, so the board only
ever encrypts.
- Byte j of a secret is c_j XOR byte (j & 7) of
  XTEA_K(v0 = nonce, v1 = j >> 3).
- Both words are little-endian u32, and the block's bytes are v0 then v1,
  each little-endian.
- These conventions are settled in the spike (`build/cipher-spike/`) and
  have to be checked against the page's JS.

### The check value

    [macros ...] 0xFE x0..x7 0xFF

- x = XTEA_K(0, 0), the block a nonce of 0 would give.
- 0xFE is an end header (count field 6), so old firmware and old pages stop
  there.
- It costs 9 bytes of the table only when the table has secrets, and it goes
  with backups.
- It is the only way the board can tell the right key from a wrong one.

## The requests

Two new requests, 0x6A and 0x6B. Both need neither the hold nor the password,
like HOSTAUTH 0x64, so they must do nothing else.

- **KEYAUTH 0x6A: count 16, the key.**
  - The board computes XTEA_K(0, 0) and compares it with x.
  - A wrong key costs a second, on the host key's pause clock, and never
    counts towards the lockout.
  - Without a check value it is refused.
  - When the key is right, it is kept in RAM for the trust timeout (0x1F2,
    the same setting as the host's).
- **PROVE 0x6B: count 8, a challenge.** The answer is
  XTEA_{x‖x}(challenge), so a client can make sure this is the real board
  before it sends the key. Without a check value it is refused.
- **State byte bit 0x80: the key is held.** INFO[7] already has 0x01–0x40
  in use.

The key is wiped from RAM when the timeout runs out, on restart, and on any
WRITE, because the table and its check value may have changed.

## The cloned board

A clone can copy the serial number, but it cannot know x unless the real
board's EEPROM was read. Hardened mode and a password stop that.

So the new client:
1. sends 8 random bytes with PROVE;
2. checks the answer against x, which it computes from the key it holds;
3. sends KEYAUTH only if the answer is right.

Old clients and `{host}` are unchanged.

## The page

- **Key setup** makes a random 128-bit key and offers it as a file. The page
  never sends the key anywhere except to the board's KEYAUTH, if at all.
- **Writing** a table with secrets:
  - The page asks for the key file only when a secret's plain text has to
    be encrypted.
  - Secrets it cannot decrypt stay as opaque blobs and can be moved around.
    A nonce belongs to its blob, not to its position in the table.
  - It writes the check value at the table's end.
- **Reading a table** without the key shows each secret as
  `{secret}` + "(encrypted, n bytes)".
- **Old firmware:** the page refuses to write secrets to, or restore a
  backup with secrets onto, firmware below 16.
- **table.js** needs the encoder, the decoder, the nonce tries, XTEA and the
  0xFE entry, with tests in test/webtable.js.

## The host client

- `digimacro-host` gains a mode that takes the key file and does PROVE, then
  KEYAUTH. Its existing modes and their arguments stay as they are.
- The key file format is still to be settled, after reading tools/hostauth.

## Firmware: version 16

- `xtea()` in assembly, from the spike, with a custom calling convention.
- `macro_byte(a)` gives every byte the player reads inside a secret, keeping
  the last block. Mark/repeat, jumps and pauses inside a secret then work
  unchanged. A secret starts at its marker, and a jump into another macro
  starts a fresh plain-text read.
- The player sees 0x81 0xFE/0xFF:
  - without the key, it refuses, quiet or loud;
  - with it, it reads the nonce and switches to decrypting.
- `find_header` is unchanged.
- KEYAUTH, PROVE, the check-value lookup and the RAM key with its timeout.

## Budget

There are 582 bytes of flash free and about 150 bytes of RAM. These numbers
are estimates.

| Part | Flash, bytes |
|---|---|
| XTEA | ~190 |
| macro_byte | ~40 |
| KEYAUTH and check | ~50 |
| PROVE | ~30 |
| Check-value lookup | ~30 |
| Timeout wipe | ~10 |
| **Total** | **~350** |

RAM: key 16 + block 8 + block index and nonce ~6.

## Tests

- **Host tests** (test/test.cpp): the player with secrets, with and without
  the key; old-rule walks over grinded tables; KEYAUTH's pause; PROVE.
- **test/webtable.js:** encoder and decoder round trips, the nonce
  conditions, and an XTEA cross-check with the spike's vectors.
- **End to end on the simulated board** (`../simavr-softusb/harness`): write
  a table with a secret, PROVE, KEYAUTH, tap, read the typed plain text, let
  the timeout lapse, and check that the tap types nothing.
- **Mutation checks** for each: wrong round count, nonce reuse, a missing
  wipe.

## Milestones

1. XTEA and CTR in table.js, matching the spike's vectors; the encoder,
   decoder and nonce tries, with tests.
2. Firmware: the cipher, macro_byte and the player, host-tested.
3. Firmware: KEYAUTH, PROVE and the wipe; the simulated end-to-end test.
4. The page: key setup and file, writing and reading secrets, version gates.
5. The digimacro-host mode.
6. Hardware confirmation, then docs in both languages, then a release.
