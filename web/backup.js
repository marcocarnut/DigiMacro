/* backup.js -- a board's whole EEPROM, saved to a file and put back.
 *
 * What is saved is the image itself, all 512 bytes: the table, the serial
 * number, the key check, the lockout, the power-on macro and the password,
 * exactly as stored. That is the point of a backup -- after a hardened board
 * has been erased to update it, it comes back as the same board, Chrome's
 * permission and every computer's key file included.
 *
 * A board with a password has its backup encrypted with that password, so
 * the file is no easier a way in than the board itself. Except that it is, in
 * one respect: the board allows one guess a second and only while somebody
 * holds the pad, and a copied file can be guessed at offline as fast as a
 * machine can go. So the key comes from scrypt with BIP38's parameters,
 * which costs a GPU on the order of a millisecond a guess, and the app warns
 * about a password under eight characters. AES-GCM then encrypts it and says
 * so plainly when the password is wrong.
 *
 * Browsers have PBKDF2 and AES-GCM built in but not scrypt, which is PBKDF2
 * wrapped round a memory-hard mix; the mix is written out here, and
 * test/backup.js checks it against RFC 7914's vectors and node's own scrypt.
 */

const BACKUP_FORMAT = 'digimacro-backup';
const BACKUP_VERSION = 1;
const BACKUP_MAP = 1;           /* the EEPROM layout this image follows; a
                                   firmware that moves things must change it */
const SCRYPT = { N: 16384, r: 8, p: 8 };    /* BIP38's */

/* ---- scrypt (RFC 7914) ---------------------------------------------- */

async function pbkdf2(pass, salt, bytes) {
  const key = await crypto.subtle.importKey('raw', pass, 'PBKDF2', false, ['deriveBits']);
  return new Uint8Array(await crypto.subtle.deriveBits(
    { name: 'PBKDF2', salt, iterations: 1, hash: 'SHA-256' }, key, bytes * 8));
}

const rotl = (a, b) => (a << b) | (a >>> (32 - b));

/* Salsa20/8 of x, in place, on sixteen words starting at x[o]. */
function salsa8(x, o, t) {
  for (let i = 0; i < 16; i++) t[i] = x[o + i];
  for (let i = 0; i < 8; i += 2) {
    t[ 4] ^= rotl(t[ 0] + t[12],  7); t[ 8] ^= rotl(t[ 4] + t[ 0],  9);
    t[12] ^= rotl(t[ 8] + t[ 4], 13); t[ 0] ^= rotl(t[12] + t[ 8], 18);
    t[ 9] ^= rotl(t[ 5] + t[ 1],  7); t[13] ^= rotl(t[ 9] + t[ 5],  9);
    t[ 1] ^= rotl(t[13] + t[ 9], 13); t[ 5] ^= rotl(t[ 1] + t[13], 18);
    t[14] ^= rotl(t[10] + t[ 6],  7); t[ 2] ^= rotl(t[14] + t[10],  9);
    t[ 6] ^= rotl(t[ 2] + t[14], 13); t[10] ^= rotl(t[ 6] + t[ 2], 18);
    t[ 3] ^= rotl(t[15] + t[11],  7); t[ 7] ^= rotl(t[ 3] + t[15],  9);
    t[11] ^= rotl(t[ 7] + t[ 3], 13); t[15] ^= rotl(t[11] + t[ 7], 18);
    t[ 1] ^= rotl(t[ 0] + t[ 3],  7); t[ 2] ^= rotl(t[ 1] + t[ 0],  9);
    t[ 3] ^= rotl(t[ 2] + t[ 1], 13); t[ 0] ^= rotl(t[ 3] + t[ 2], 18);
    t[ 6] ^= rotl(t[ 5] + t[ 4],  7); t[ 7] ^= rotl(t[ 6] + t[ 5],  9);
    t[ 4] ^= rotl(t[ 7] + t[ 6], 13); t[ 5] ^= rotl(t[ 4] + t[ 7], 18);
    t[11] ^= rotl(t[10] + t[ 9],  7); t[ 8] ^= rotl(t[11] + t[10],  9);
    t[ 9] ^= rotl(t[ 8] + t[11], 13); t[10] ^= rotl(t[ 9] + t[ 8], 18);
    t[12] ^= rotl(t[15] + t[14],  7); t[13] ^= rotl(t[12] + t[15],  9);
    t[14] ^= rotl(t[13] + t[12], 13); t[15] ^= rotl(t[14] + t[13], 18);
  }
  for (let i = 0; i < 16; i++) x[o + i] = (x[o + i] + t[i]) | 0;
}

/* BlockMix: b holds 2r sixteen-word blocks; the result goes to y, evens
   first and then odds, as the RFC orders them. */
function blockMix(b, y, r, x, t) {
  for (let i = 0; i < 16; i++) x[i] = b[(2 * r - 1) * 16 + i];
  for (let i = 0; i < 2 * r; i++) {
    for (let k = 0; k < 16; k++) x[k] ^= b[i * 16 + k];
    salsa8(x, 0, t);
    const at = ((i & 1) ? r + (i >> 1) : (i >> 1)) * 16;
    for (let k = 0; k < 16; k++) y[at + k] = x[k];
  }
}

function roMix(block, N, r) {
  const words = 32 * r;
  const X = new Int32Array(words), Y = new Int32Array(words);
  const V = new Int32Array(words * N);
  const x = new Int32Array(16), t = new Int32Array(16);

  for (let i = 0; i < words; i++)               /* little-endian words */
    X[i] = block[4 * i] | block[4 * i + 1] << 8 | block[4 * i + 2] << 16 | block[4 * i + 3] << 24;
  for (let i = 0; i < N; i++) {
    V.set(X, i * words);
    blockMix(X, Y, r, x, t);
    X.set(Y);
  }
  for (let i = 0; i < N; i++) {
    const j = (X[(2 * r - 1) * 16] >>> 0) % N;  /* Integerify; N is a power of two */
    for (let k = 0; k < words; k++) X[k] ^= V[j * words + k];
    blockMix(X, Y, r, x, t);
    X.set(Y);
  }
  for (let i = 0; i < words; i++) {
    block[4 * i] = X[i]; block[4 * i + 1] = X[i] >> 8;
    block[4 * i + 2] = X[i] >> 16; block[4 * i + 3] = X[i] >> 24;
  }
}

async function scrypt(pass, salt, N, r, p, dkLen) {
  const B = await pbkdf2(pass, salt, p * 128 * r);
  for (let i = 0; i < p; i++) {
    const block = B.subarray(i * 128 * r, (i + 1) * 128 * r);
    roMix(block, N, r);
    if (p > 1) await new Promise(res => setTimeout(res, 0));    /* let the page breathe */
  }
  return pbkdf2(pass, B, dkLen);
}

/* ---- the file ------------------------------------------------------- */

const b64 = bytes => btoa(String.fromCharCode(...bytes));
const unb64 = s => Uint8Array.from(atob(s), c => c.charCodeAt(0));
const utf8 = s => new TextEncoder().encode(s);

async function aesKey(password, salt, k) {
  const raw = await scrypt(utf8(password), salt, k.N, k.r, k.p, 32);
  return crypto.subtle.importKey('raw', raw, 'AES-GCM', false, ['encrypt', 'decrypt']);
}

/* The file, as text. `password` is null for a board without one, and then
   the image is stored as it is -- no less safe than the board, which gives
   it to anyone who holds the pad. The serial and the date go outside the
   encryption so a file can be told apart from its neighbours; the serial is
   no secret, since every computer the board meets is told it. */
async function makeBackup(image, password, meta) {
  const head = { format: BACKUP_FORMAT, version: BACKUP_VERSION, map: BACKUP_MAP,
                 serial: meta.serial, firmware: meta.firmware, saved: meta.saved };
  if (password === null)
    return JSON.stringify({ ...head, image: b64(image) }, null, 1) + '\n';

  const salt = crypto.getRandomValues(new Uint8Array(16));
  const iv = crypto.getRandomValues(new Uint8Array(12));
  const key = await aesKey(password, salt, SCRYPT);
  /* The header is bound in as additional data, so it cannot be swapped onto
     another file's contents without the decryption noticing. */
  const ad = utf8(JSON.stringify(head));
  const ct = new Uint8Array(await crypto.subtle.encrypt(
    { name: 'AES-GCM', iv, additionalData: ad }, key, image));
  return JSON.stringify({ ...head,
    kdf: { name: 'scrypt', ...SCRYPT, salt: b64(salt) },
    cipher: { name: 'AES-256-GCM', iv: b64(iv) },
    image: b64(ct) }, null, 1) + '\n';
}

/* What is inside a file, without opening it: whether it wants a password. */
function readBackupHeader(text) {
  let f;
  try { f = JSON.parse(text); } catch (e) { throw new Error('backup-not-one'); }
  if (!f || typeof f !== 'object' || f.format !== BACKUP_FORMAT)
    throw new Error('backup-not-one');
  if (f.version !== BACKUP_VERSION || f.map !== BACKUP_MAP)
    throw new Error('backup-other-map');
  return { encrypted: !!f.kdf, serial: f.serial, firmware: f.firmware, saved: f.saved };
}

/* The image, or an error whose message is a key in i18n.js: anything a
   malformed file trips over -- a missing field, base64 that is not -- is
   "not a backup", not a raw error in English. */
async function openBackup(text, password) {
  try {
    return await openBackupInner(text, password);
  } catch (e) {
    throw e.message.startsWith('backup-') ? e : new Error('backup-not-one');
  }
}

async function openBackupInner(text, password) {
  const h = readBackupHeader(text);
  const f = JSON.parse(text);
  if (!h.encrypted) {
    const image = unb64(f.image);
    if (image.length !== 512) throw new Error('backup-not-one');
    return image;
  }
  if (f.kdf.name !== 'scrypt' || f.cipher.name !== 'AES-256-GCM')
    throw new Error('backup-other-map');
  /* The cost comes from the file, so a file must not be able to name one
     that asks the page for gigabytes. */
  const { N, r, p } = f.kdf;
  if (![N, r, p].every(Number.isInteger) || N < 2 || (N & (N - 1)) !== 0
      || r < 1 || p < 1 || N * r * p > SCRYPT.N * SCRYPT.r * SCRYPT.p)
    throw new Error('backup-not-one');      /* no dearer than this page makes */
  const head = { format: f.format, version: f.version, map: f.map,
                 serial: f.serial, firmware: f.firmware, saved: f.saved };
  /* Decoded first, so that a file whose base64 is broken is "not a backup",
     not "wrong password": only the decryption itself means the latter. */
  const salt = unb64(f.kdf.salt), iv = unb64(f.cipher.iv), ct = unb64(f.image);
  if (iv.length !== 12) throw new Error('backup-not-one');
  const key = await aesKey(password, salt, f.kdf);
  let image;
  try {
    image = new Uint8Array(await crypto.subtle.decrypt(
      { name: 'AES-GCM', iv, additionalData: utf8(JSON.stringify(head)) }, key, ct));
  } catch (e) {
    throw new Error('backup-wrong-password');
  }
  if (image.length !== 512) throw new Error('backup-not-one');
  return image;
}

if (typeof module !== 'undefined')
  module.exports = { scrypt, makeBackup, openBackup, readBackupHeader, SCRYPT };
