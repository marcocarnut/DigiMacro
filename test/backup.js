/* Checks web/backup.js: that its scrypt is scrypt, and that a backup only
 * opens for the right password and only as the file it was saved as.
 *
 * scrypt is written out by hand there, since browsers do not have it, so it is
 * checked against node's own -- the one implementation here that nobody on
 * this project wrote -- at small costs and at the real one.
 */
const nodeCrypto = require('crypto');
/* The page uses the browser's Web Crypto; node 18 has the same thing, but
   only gives it to scripts as require('crypto').webcrypto. */
if (!globalThis.crypto) globalThis.crypto = nodeCrypto.webcrypto;
const bk = require('../web/backup.js');

let fails = 0;
const check = (ok, what) => { if (!ok) { console.log('  FAIL: ' + what); fails++; } };
const hex = b => Buffer.from(b).toString('hex');

(async () => {
  console.log('1. scrypt agrees with node\'s');
  for (const [pass, salt, N, r, p] of [
    ['password', 'NaCl', 1024, 8, 16],                  /* RFC 7914's second vector */
    ['pleaseletmein', 'SodiumChloride', 16384, 8, 1],   /* ...and its third */
    ['x', 'y', 16, 1, 1],
    ['coração', 'sal', 64, 2, 3],                       /* not ASCII */
  ]) {
    const mine = await bk.scrypt(Buffer.from(pass), Buffer.from(salt), N, r, p, 64);
    const theirs = nodeCrypto.scryptSync(pass, salt, 64, { N, r, p, maxmem: 256 * 1024 * 1024 });
    check(hex(mine) === hex(theirs), `${pass}/${salt} N=${N} r=${r} p=${p}`);
  }
  /* RFC 7914, section 12, written out so this does not only trust node */
  const rfc = await bk.scrypt(Buffer.from('password'), Buffer.from('NaCl'), 1024, 8, 16, 64);
  check(hex(rfc).startsWith('fdbabe1c9d3472007856e7190d01e9fe7c6ad7cbc8237830e77376634b373162'),
        'RFC 7914 vector 2');

  console.log('2. at the real cost, too');
  const t0 = Date.now();
  const real = await bk.scrypt(Buffer.from('test1234'), Buffer.from('0123456789abcdef'),
                               bk.SCRYPT.N, bk.SCRYPT.r, bk.SCRYPT.p, 32);
  const ms = Date.now() - t0;
  check(hex(real) === hex(nodeCrypto.scryptSync('test1234', '0123456789abcdef', 32,
          { ...bk.SCRYPT, maxmem: 256 * 1024 * 1024 })), 'BIP38 parameters');
  console.log(`   (${ms} ms, here)`);

  const image = new Uint8Array(512).map((_, i) => (i * 37 + 11) & 0xFF);
  const meta = { serial: '7DD5DC7E', firmware: 6, saved: '2026-10-08' };

  console.log('3. a backup opens with its password, and only with it');
  const file = await bk.makeBackup(image, 'correct horse', meta);
  check(bk.readBackupHeader(file).encrypted, 'it says it is encrypted');
  check(!file.includes(Buffer.from(image.slice(0, 32)).toString('base64').slice(0, 20)),
        'and the image is not in it in the clear');
  check(hex(await bk.openBackup(file, 'correct horse')) === hex(image), 'round trip');
  let msg = null;
  try { await bk.openBackup(file, 'correct horsE'); } catch (e) { msg = e.message; }
  check(msg === 'backup-wrong-password', `wrong password: ${msg}`);

  console.log('4. the header cannot be swapped onto other contents');
  const f = JSON.parse(file);
  f.serial = 'DEADBEEF';
  msg = null;
  try { await bk.openBackup(JSON.stringify(f), 'correct horse'); } catch (e) { msg = e.message; }
  check(msg === 'backup-wrong-password', `a changed serial must not open: ${msg}`);

  console.log('5. a board with no password saves the image as it is');
  const plain = await bk.makeBackup(image, null, meta);
  check(!bk.readBackupHeader(plain).encrypted, 'it says it is not encrypted');
  check(hex(await bk.openBackup(plain, null)) === hex(image), 'and it comes back');

  console.log('6. what is not a backup, or not one this page understands');
  for (const [text, want] of [
    ['not json', 'backup-not-one'],
    ['{"format":"something-else"}', 'backup-not-one'],
    [JSON.stringify({ ...JSON.parse(plain), map: 2 }), 'backup-other-map'],
    [JSON.stringify({ ...JSON.parse(file), kdf: { ...JSON.parse(file).kdf, N: 1 << 24 } }),
     'backup-not-one'],                                 /* a cost meant to hang the page */
  ]) {
    msg = null;
    try { await bk.openBackup(text, 'correct horse'); } catch (e) { msg = e.message; }
    check(msg === want, `${text.slice(0, 30)}...: ${msg}`);
  }

  console.log(fails ? '\nSOME CHECKS FAILED' : '\nall checks passed');
  process.exit(fails ? 1 : 0);
})();
