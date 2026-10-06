/* Checks web/micronucleus.js against the firmware the app actually ships.
 *
 * An upgrade that lays the reset vector out wrong is a board that sits in its
 * bootloader for ever, and nothing short of hardware would say so -- unless the
 * jumps are decoded here and followed to where they land.
 */
const fs = require('fs');
const path = require('path');
const mn = require('../web/micronucleus.js');

let fails = 0;
const check = (ok, what) => { if (!ok) { console.log('  FAIL: ' + what); fails++; } };

const FW = path.join(__dirname, '../web/firmware');
const T85_FLASH = 8192, BOOT = 0x1A00, APP_MAX = BOOT - 6;

/* Where an rjmp at byte address `at` lands, as a byte address. */
const target = (img, at) => {
  const w = img[at] | (img[at + 1] << 8);
  if ((w & 0xF000) !== 0xC000) return null;
  return ((((at >> 1) + (w & 0x0FFF) + 1) & 0x0FFF) << 1);
};

console.log('1. the shipped firmware parses, and fits');
const app = mn.parseIntelHex(fs.readFileSync(path.join(FW, 'digimacro.hex'), 'utf8'), T85_FLASH);
check(app.end > 1000, `suspiciously small: ${app.end} bytes`);
check(app.end <= APP_MAX, `${app.end} bytes, and Micronucleus 2.6 leaves ${APP_MAX}`);
const version = JSON.parse(fs.readFileSync(path.join(FW, 'digimacro.json'), 'utf8')).fw;
const header = fs.readFileSync(path.join(__dirname, '../DigiHidUSB/src/hidusb.h'), 'utf8');
check(version === Number(/#define DM_FW_VERSION\s+(\d+)/.exec(header)[1]),
      `web/firmware says ${version}, hidusb.h disagrees: rebuild with ./build.sh release`);

console.log('2. laid out for Micronucleus 2, reset goes to the bootloader');
const img = mn.layoutApplication(app, APP_MAX, BOOT);
check(img.length === BOOT, `image is ${img.length} bytes`);
check(target(img, 0) === BOOT, `reset lands at 0x${(target(img, 0) || 0).toString(16)}`);

console.log("3. ...and the bootloader's exit lands where the firmware's reset did");
const own = target(app.data, 0);
check(own !== null, "the firmware's own reset vector is not an rjmp");
check(target(img, BOOT - 4) === own,
      `exit lands at 0x${(target(img, BOOT - 4) || 0).toString(16)}, reset was 0x${(own || 0).toString(16)}`);

console.log('4. nothing else moved');
let moved = 0;
for (let a = 2; a < app.end; a++) if (img[a] !== app.data[a]) moved++;
check(moved === 0, `${moved} bytes of the firmware changed`);
check(img[BOOT - 6] === 0xFF && img[BOOT - 5] === 0xFF,
      'the OSCCAL bytes must stay erased for the bootloader to fill');

console.log('5. too big is refused, not truncated');
let threw = false;
try { mn.layoutApplication(app, app.end - 1, BOOT); } catch (e) { threw = true; }
check(threw, 'an application one byte too big must be refused');

console.log('6. the upgrader is an application that fits below the bootloader');
const up = mn.parseIntelHex(fs.readFileSync(
  path.join(FW, 'micronucleus-2.6-t85_default-upgrade.hex'), 'utf8'), T85_FLASH);
check(up.end > 0 && up.end <= APP_MAX, `the upgrader is ${up.end} bytes`);
check(target(mn.layoutApplication(up, APP_MAX, BOOT), 0) === BOOT,
      'and lays out like any other application');

console.log('7. a damaged hex file is refused');
const good = ':0400000001020304F2';
threw = false;
try { mn.parseIntelHex(good.replace('F2', 'F3'), 16); } catch (e) { threw = true; }
check(threw, 'a bad checksum must be refused');
check(mn.parseIntelHex(good, 16).data[3] === 4, 'and a good line read');

console.log(fails ? '\nSOME CHECKS FAILED' : '\nall checks passed');
process.exit(fails ? 1 : 0);
