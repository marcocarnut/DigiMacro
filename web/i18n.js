/* i18n.js -- what the app says, in each language it says it.
 *
 * One entry per string, so a missing translation is a missing key rather than
 * a sentence half in the wrong language; test/i18n.js checks that the two
 * dictionaries hold exactly the same keys. A string may be a template, where
 * {name} is filled from the parameters, or a function where a language needs
 * to decide something for itself -- plurals, mostly.
 *
 * The encoder's messages are here too. They reach the person editing a macro,
 * so they are part of the interface, not diagnostics: web/table.js keeps the
 * English as its default and takes a translator through setMessages().
 */

const STRINGS = {

en: {
  'lang-name':      'English',
  tagline:          'Tap a code, type a macro.',
  'whats-this':     "what's this?",
  'vk-toggle':      'On-screen keyboard: a phone with the board plugged in has '
                  + 'no other',
  'vk-space':       'space',
  'readme-url':     'https://github.com/marcocarnut/DigiMacro#readme',
  keyboard:         'keyboard',
  language:         'language',
  connect:          'Connect',
  macros:           'Macros',
  'th-code':        'Code',
  'th-types':       'Types',
  'ph-types':       'what this code types',
  'add-macro':      '+ Add a macro',
  'load-example':   'Load the example',
  'reload-table':   'Reload the table from the board',
  'table-reloaded': 'The table as the board holds it.',
  startup:          'Startup',
  'startup-why':    'One macro can type itself when the board is plugged in, '
                  + 'once the computer is ready for it. "Write to the board", '
                  + 'at the top, saves this along with the table.',
  budget:           '{n} of {max} bytes',
  keys:             'Keys',
  'on-plug':        'when plugged in, type',
  nothing:          'nothing',
  'auto-dangling':  'The macro this board typed on power-up is not in the '
                  + 'table any more, so it will type nothing. Choose another, '
                  + 'or leave it.',
  'mods-hint':      'held for the next key',
  'a-key':          'a key',
  insert:           'insert',
  pause:            'pause',
  repeat:           'repeat',
  'pad-hint':       'Inserts at the cursor in whichever macro you were last '
                  + 'editing. A modifier stays pressed until the next key.',
  write:            'Write to the board',

  'sec-summary':    'Password',
  'sec-none':       'This board has no password, which is the usual thing and '
                  + 'perfectly fine: holding the pad is what keeps a web page '
                  + 'out, and plenty of macros are not secret. Set one if the '
                  + 'table is worth protecting from somebody holding the '
                  + 'board.',
  save:             'Save',
  'clear-password': 'Remove it',
  'ph-password':    'a new password, up to 12 characters',
  'ph-gate-password': 'password',
  'gate-hold':      'Hold the pad for four seconds to let this page in. The '
                  + 'board stays a keyboard the whole time; the hold is the one '
                  + 'thing a web page cannot do for itself.',
  'gate-hold-password': 'This board has a password. Type it here, and hold the '
                  + 'pad for four seconds — in either order.',
  'gate-type-password': 'The pad was held. Now the password.',
  'gate-waiting':   'Got it. Now hold the pad for four seconds.',
  'gate-reading':   'Reading the board…',
  'password-empty-unlock': 'Type the password first.',
  'hold-now':       'Hold the pad for four seconds to let this through — the '
                  + 'last hold has run out.',
  'hold-timeout':   'Nobody held the pad, so nothing was done.',
  writing:          'Writing…',
  'sec-has':        'This board has a password. Change it here, or remove it '
                  + 'to make the board open to anyone who can hold the pad.',
  'board-left':     'the board was unplugged',
  'read-none':      'The board has no macros on it yet. Add some, and "Write '
                  + 'to the board", at the top, puts them there.',
  'sample-say':     'An example of each thing a macro can do. "Write to the '
                  + 'board", at the top, puts it on the board over what is '
                  + 'there now.',
  unlock:           'Unlock',
  'wrong-password': 'That is not the password. Try again in a second; with the '
                  + 'lockout on, each wrong one counts as a wrong code.',
  'password-empty': 'Type a password first, or use Remove it.',
  'password-set':   'Password set. This page keeps it until the board is '
                  + 'unplugged, and the board types its macros without it.',
  'password-cleared': 'Password removed. The board is open to anyone who can '
                  + 'hold the pad.',
  'lock-summary':   'Lockout',
  'lock-why':       'Wrong codes count. After too many the board ignores every '
                  + 'tap, right or wrong, for a while — a soft lock, which ends '
                  + 'by itself and which unplugging does not shorten. After too '
                  + 'many soft locks it ignores taps until the password is given '
                  + 'here — a hard lock. Only the password clears the counts, or '
                  + 'a macro with {clear} in it: a code that works does not, or '
                  + 'anyone who knew one could guess for ever. A wrong password '
                  + 'counts as a wrong code, but the password itself is never '
                  + 'shut out.',
  'lock-enable':    'Lock out wrong codes',
  'lock-soft':      'wrong codes before a pause',
  'lock-wait':      'pause, in seconds',
  'lock-hard':      'pauses before a hard lock (0: never)',
  'lock-needs-password': 'The lockout needs a password, since giving it is the '
                  + 'only way to lift a hard lock. Set one above first.',
  'lock-state-soft': 'The board is pausing after too many wrong codes. It '
                  + 'ignores every tap until the pause is over, and unplugging '
                  + 'it starts the pause again.',
  'lock-state-hard': 'The board is locked hard and ignores every tap. Hold the '
                  + 'pad for four seconds, type the password and press '
                  + 'Unlock.',
  'lock-odds':      p => `Someone guessing gets at most ${p.tries} `
                  + `${p.tries == 1 ? 'code' : 'codes'} before the board locks `
                  + `hard, with a ${p.wait} pause after `
                  + `${p.soft == 1 ? 'each wrong one' : `every ${p.soft} wrong`}. `
                  + `With ${p.n} of the 62 codes in use, each guess has about a `
                  + `${p.pct}% chance.`,
  'lock-odds-nohard': p => `With no hard lock, someone guessing gets a `
                  + `${p.wait} pause after `
                  + `${p.soft == 1 ? 'each wrong code' : `every ${p.soft} wrong codes`}, `
                  + `for as long as they keep at it. With ${p.n} of the 62 codes `
                  + `in use, each guess has about a ${p.pct}% chance.`,
  'lock-bad':       'Wrong codes before a pause must be 1 to 254, pauses before '
                  + 'a hard lock 0 to 254, and the pause 10 to 2400 seconds in steps '
                  + 'of ten.',
  'lock-counts':    'Wrong codes now: {misses}. Pauses: {softs}. Hard '
                  + 'locks lifted with the password, ever: {cleared}.',
  'lock-saved':     'Lockout saved. Its counts start again from zero.',
  'lock-off-saved': 'Lockout off. Wrong codes cost nothing now.',
  'lock-lifted':    'Hard lock lifted.',
  'bk-summary':     'Backup',
  'bk-why':         'Everything on the board — the macros, the startup macro, '
                  + 'the lockout, the key check, the password and the serial '
                  + 'number — saved to a file, and put back. A board erased with '
                  + 'a programmer — to start over, or to install with one — loses '
                  + 'all of it; restored, it comes back as itself. With a password the file '
                  + 'is encrypted with it, so keep that password: without it the '
                  + 'file is no use to anyone, you included.',
  'bk-save':        'Save a backup',
  'bk-restore':     'Restore',
  'bk-restore-head': 'Restore',
  'un-summary':     'Back to a plain Digispark',
  'un-why':         'This takes DigiMacro off the board and puts back '
                  + 'Micronucleus, the bootloader a Digispark comes with, so the '
                  + 'Arduino IDE and other tools can program it again. First the '
                  + 'board erases its EEPROM: the macros, the secrets, the password '
                  + 'and the serial number are gone for good, and no program '
                  + 'put on it afterwards can read them. Save a backup above if you '
                  + 'may want them back. Set up a new board brings DigiMacro back '
                  + 'whenever you like.',
  'un-sure':        'I understand: the macros, the secrets and the password are '
                  + 'erased for good.',
  'un-go':          'Erase and go back to Micronucleus',
  'un-what':        'the uninstaller and Micronucleus',
  'un-done':        'Done: the board is a plain Digispark again, with Micronucleus '
                  + 'waiting for a program (16d0:0753).',
  'un-seen':        'Micronucleus answered.',
  'un-low':         'low fuse {v}',
  'un-high':        'high fuse {v}',
  'un-ext':         'extended fuse {v}',
  'un-lock':        'lock bits {v}',
  'un-fuses':       'Its chip is not set as a Digispark comes ({what}). It works as '
                  + 'one regardless, but only a programmer can put that back — '
                  + 'which erases the chip, Micronucleus included, so it goes back '
                  + 'on with the programmer too:',
  'un-fuses-hv':    'Its chip is not set as a Digispark comes ({what}), and its '
                  + 'reset pin is off, so a programmer like DigiISP cannot reach it: '
                  + 'that takes a high-voltage programmer first. It works as a '
                  + 'Digispark regardless:',
  'un-howto':       'how',
  'un-howto-href':  'https://github.com/marcocarnut/DigiMacro/blob/main/HARDENED_MODE.md#changing-things-afterwards',
  'ph-bk-pass':     'the password it was saved with',
  'bk-short':       'The password is shorter than eight characters. A copied file '
                  + 'can be guessed at offline, far faster than the board allows, '
                  + 'and a short password will not hold for long. Press Save a '
                  + 'backup again to save it anyway.',
  'bk-plain':       'This board has no password, so the backup would hold every '
                  + 'macro in the clear. Press Save a backup again to save it '
                  + 'anyway, or set a password first.',
  'bk-deriving':    'Working out the encryption from the password — a second or two, '
                  + 'on purpose…',
  'bk-saved':       'Saved as {file}, encrypted with the board\'s password.',
  'bk-saved-plain': 'Saved as {file}, in the clear. Keep it somewhere you would '
                  + 'keep the macros themselves.',
  'bk-file-locked': 'A backup of board {serial}, firmware {firmware}, saved on '
                  + '{saved}. It is encrypted: type the password it was saved '
                  + 'with.',
  'bk-file-plain':  'A backup of board {serial}, firmware {firmware}, saved on '
                  + '{saved}. It is not encrypted.',
  'bk-needs-pass':  'Type the password the backup was saved with.',
  'bk-confirm':     'This replaces everything on this board with board {serial}\'s '
                  + 'backup: macros, startup, lockout, key check, password and '
                  + 'serial number. A restore installs whatever the file holds, '
                  + 'power-on macro included, so only restore backups you made. '
                  + 'Press Restore again to go ahead.',
  'bk-restored-replug': 'Restored, and read back. Unplug the board and plug '
                  + 'it back in, so that the restored serial number takes effect.',
  'bk-restarting':  'Restored, and read back. The board is restarting, to come '
                  + 'back with its restored serial number…',
  'bk-restarted':   'Restored. The board has restarted with its restored serial '
                  + 'number — press Connect and pick it again.',
  'backup-not-one': 'That file is not a DigiMacro backup.',
  'backup-other-map': 'That backup was made by a different version of DigiMacro, '
                  + 'and this page cannot read it.',
  'backup-wrong-password': 'That is not the password this backup was saved with.',
  'key-summary':    'Secrets',
  'key-why':        'What follows {secret} in a macro is kept encrypted on the '
                  + 'board, and the key is not kept there at all: the macro '
                  + 'plays it only while a small program on this computer has '
                  + 'sent the key lately — tools/hostauth, given the file this '
                  + 'page downloads. Plugged in anywhere else, or stolen, the '
                  + 'board holds only scrambled bytes. {secret!} stops the '
                  + 'macro as if the code were not there at all — three '
                  + 'blinks, and a wrong code for the lockout.',
  'key-wait':       'held for, in seconds, after the key is sent',
  'key-new':        'Make a key',
  'key-replace':    'Make a new key',
  'key-load':       'Load the key file',
  'key-remove':     'Remove the key',
  'key-none':       'This board has no key for secrets.',
  'key-none-secrets': 'Some macros have secrets, but this board has no key for '
                  + 'them: make one, and the page encrypts them when it writes.',
  'key-has':        'This board has a key for secrets. No computer has sent it '
                  + 'lately.',
  'key-held':       'This board has a key for secrets, and a computer sent it '
                  + 'lately: secrets play for now.',
  'key-loaded':     'The key is loaded in this page: secrets show as text, and '
                  + 'are encrypted again when written.',
  'key-load-to-open': 'Load the key file to see or change the secrets.',
  'key-made':       'A new key is in use, and saved as {file}. Add its line to '
                  + '~/.config/digimacro/host.key (on Windows, '
                  + '%APPDATA%\\digimacro\\host.key) on each computer the '
                  + 'secrets should play for; any that had the old key need this '
                  + 'one. Keep the file: without it, the secrets cannot be read '
                  + 'or changed again.',
  'key-removed':    'The key is gone from the board.',
  'key-opened':     'The key fits: the secrets are open in this page.',
  'key-not-this':   'That key is not this board\'s: it opens none of its secrets.',
  'key-not-in-file': 'That file has no key for board {serial}.',
  'key-v2-host':    'That is a v2 host key, which firmware 16 no longer uses. '
                  + 'Make a key instead.',
  'key-sealed-left': 'Some secrets were encrypted with the board\'s key, and '
                  + 'this page does not have it. Load the key file first, so '
                  + 'they can be encrypted again with the new one — or remove '
                  + 'them.',
  'key-write-first': 'Write the macros first: a new key encrypts the secrets '
                  + 'again, and writes them with it.',
  'key-secrets-left': 'Some macros have secrets, which nothing could read again '
                  + 'without the key. Take them out first.',
  'key-needed':     'Some macros have secrets to encrypt. Make a key in '
                  + 'Secrets below, or load the key file.',
  'key-mismatch':   'the key check read back differently from what was written',
  'key-wait-saved': 'Saved: the key is held for {wait} after it is sent.',
  'key-wait-bad':   'The time runs from 10 to 2400 seconds, in steps of ten.',
  'key-file-comment': 'DigiMacro key for board {serial}: add this line to '
                  + '~/.config/digimacro/host.key, or on Windows to '
                  + '%APPDATA%\\digimacro\\host.key',
  'sealed':         '🔒 then a secret, {n} keystrokes: load the key file to see '
                  + 'or change it',
  'sealed-loud':    '🔒 then a {secret!}, {n} keystrokes: load the key file to '
                  + 'see or change it',
  'secret-startup': 'The power-on macro has a secret. It runs before any '
                  + 'program on the computer can have sent the key, so it will '
                  + 'stop there every time.',
  'secret-old-fw':  'Secrets need firmware {fw} or later: update the board\'s '
                  + 'firmware first.',
  'host-convert':   'Turn {host} into {secret}',
  'host-converted': 'Done: each {host} is a {secret} now. Write the macros to '
                  + 'put them on the board.',
  'host-converted-key': 'Done: each {host} is a {secret} now. Make a key in '
                  + 'Secrets below, then write the macros.',
  'ref-secret':     'the rest of the macro is secret: encrypted on the board, '
                  + 'and typed only while this computer has sent the key lately',
  'ref-secret-loud': '...and without it, stop as if the code were not there',
  'host-late':      'Something shows before {host!}, so a guesser on the wrong '
                  + 'computer learns this code exists. Put the check first.',
  'secret-late':    'Something shows before {secret!}, so a guesser on the '
                  + 'wrong computer learns this code exists. Put the secret '
                  + 'first.',
  'ref-ir':         'where the IR remote\'s button 00 45 starts typing, on to '
                  + 'the end of the macro; typed from the code, it is nothing',
  'ref-point':      'the mouse pointer, in percent of the screen; {0,0} drives '
                  + 'it into the corner, wherever it was',
  'ref-point-scale': '...that move a times faster, 0.75 to 3.75, for a '
                  + 'computer that scales the pointer',
  'ref-click':      'a mouse click',
  'ref-hold':       'a button held down, and let up again; {right-hold} too',
  'ref-clear':      'forget the lockout\'s wrong codes, as the password does',
  'ir-learn':       'remote button',
  'ir-title':       'A remote\'s button',
  'ir-how':         'Point the remote at the board and press the button within '
                  + 'ten seconds: the board types its two bytes in the box below, '
                  + 'so leave the focus there. The remote has to speak the NEC '
                  + 'protocol, as many LED strip and TV remotes do.',
  'ir-receiver':    'Only a board with an IR receiver, on P2, hears anything.',
  'ir-heard':       'the board types here',
  'ir-listening':   'listening… {s} s',
  'ir-silent':      'Nothing heard. The board may have no receiver, the remote '
                  + 'may not speak NEC, or the box lost the focus. Listen again '
                  + 'to retry.',
  'ir-paste':       'Copy this and paste it into the macro the button should '
                  + 'type, where the typing should start: usually at the very '
                  + 'beginning. A macro only the remote should reach can have a '
                  + 'code from IR1 to IR30 instead of dots and dashes.',
  'ir-copy':        'Copy',
  'ir-copied':      '{ir} is on the clipboard.',
  'ir-copy-failed': 'The browser did not allow copying: it is selected, so copy '
                  + 'it yourself.',
  'ir-again':       'Listen again',
  'ir-noboard':     'Connect the board first.',
  'ir-refused':     'The board did not take it: its firmware has no remote. '
                  + 'Update it.',
  'fw-summary':     'Firmware',
  'fw-how':         'The board restarts into its bootloader, Micronucleus, and '
                  + 'the browser asks you to pick it — a second picker, because '
                  + 'the bootloader is a different kind of USB device. Pick it '
                  + 'within a few seconds or it gives up and goes back to the '
                  + 'old firmware. The macros, the password, the lockout, the '
                  + 'key check and the serial number all stay as they are. On Windows the bootloader needs a '
                  + 'driver once: Zadig, installing WinUSB for "Micronucleus".',
  'fw-update':      'Update the firmware',
  'fw-who':         'updating the firmware…',
  'fw-too-old':     'Firmware {on} cannot restart itself into the bootloader, '
                  + 'so write v2\'s core by hand -- "micronucleus --run '
                  + 'web/firmware/digimacro-v2-core.hex", then plug the board '
                  + 'in -- and this page takes it from there.',
  'fw-none-here':   'The board runs firmware {on}. This page has no firmware to '
                  + 'offer; serve it from the project\'s web folder.',
  'fw-newer':       'The board runs firmware {on}, and firmware {here} is '
                  + 'available.',
  'fw-same':        'The board runs firmware {on}, which is the latest this page '
                  + 'has ({here}). Updating would write it again.',
  'fw-no-webusb':   'navigator.usb is missing. The bootloader is reached over '
                  + 'WebUSB, which needs Chrome or Edge.',
  'fw-ready':       'Ready. Press Update the firmware again within half a '
                  + 'minute.',
  'fw-rebooting':   'Restarting the board into its bootloader…',
  'fw-pick':        'Pick "Micronucleus" in the browser\'s list.',
  'fw-found':       'Micronucleus {version} found.',
  'fw-erasing':     'Erasing, for {what}…',
  'fw-writing':     'Writing {what}…',
  'fw-no-bootloader': 'No bootloader was picked, so nothing was written. The '
                  + 'board goes back to its old firmware by itself after a few '
                  + 'seconds.',
  'fw-failed':      'The update stopped: {msg}',
  'fw-udev':        'On Linux the bootloader needs the Micronucleus line from '
                  + 'web/60-digimacro.rules (github.com/marcocarnut/DigiMacro) '
                  + 'in /etc/udev/rules.d.',
  'fw-zadig':       'On Windows the bootloader needs a driver, once: run Zadig '
                  + '(zadig.akeo.ie) while the board is in its bootloader, pick '
                  + 'it — USB ID 16D0 0753 — and install WinUSB. Then unplug '
                  + 'the board and start over.',
  'fw-done':        'Firmware {fw} written. The board is starting it now, and '
                  + 'will ask for the hold again.',
  'mn-too-big':     'the firmware is {n} bytes and this bootloader leaves {max}',
  'mn-no-jump':     'the firmware does not start with a jump, so it cannot be '
                  + 'laid out for the bootloader',
  'mn-unsupported': 'Micronucleus {version} is not one this page knows',
  'mn-no-info':     'the bootloader did not say how big it is',
  'mn-deaf':        'the bootloader is there but no longer answering: it waits '
                  + 'only a few seconds after the board is plugged in. Unplug '
                  + 'the board, press again, and plug it in while the '
                  + 'browser\'s list is open',
  'mn-not-t85':     'that bootloader is not on an ATtiny85',
  'ref-summary':    'What can go in a macro',
  'ref-anything':   'typed as itself',
  'ref-escapes':    'newline, tab, backslash',
  'ref-brace':      'a literal {',
  'ref-named':      'a key that is not a character',
  'ref-mods':       'modifiers on any key or character',
  'ref-combined':   '...combined, in any order',
  'ref-mod-alone':  'a modifier pressed alone: Windows\' Start, a TV\'s home '
                  + 'screen. Gui is Win and Cmd',
  'ref-pause':      'wait, in seconds, 0.1 to 25.5',
  'ref-repeat':     'eleven of them in all, as in a regular expression',
  'ref-group':      '...of a whole group rather than one thing',
  'ref-paren':      'a parenthesis that is only a parenthesis',
  'ref-accent':     'on a dead-key keyboard: the accent, then the letter',
  'ref-bare-mark':  'the accent key alone, with nothing joined to it',
  'ref-anything-ex': 'anything',
  'code-summary':   'Codes',
  'code-help':      'A code is one to five dots and dashes, written here as . '
                  + 'and -. On the pad a dot is a short touch, under a quarter '
                  + 'of a second, and a dash a longer one; 700 ms with no touch '
                  + 'ends the code. The only other codes are IR1 to IR30: they '
                  + 'work like any other, but no tap makes them, so they are '
                  + 'for macros that only the remote, or a {goto}, reaches.',
  'code-guess':     'Sixty-two codes exist, so a table that uses many of them '
                  + 'is easier to guess — which is what the lockout is for.',
  'code-morse':     'Morse code, for codes easy to remember:',
  'ref-packed':     'Macros are packed end to end, so the only limit is 480 '
                  + 'bytes in total: two bytes of overhead each, one byte per '
                  + 'keystroke, two per pause or repeat, four per {IR}.',
  'ref-layout':     'The board sends key presses, not letters, so what appears '
                  + 'is decided by the keyboard layout of the machine it is '
                  + 'plugged into — which is what the keyboard setting beside '
                  + 'Macros says. '
                  + 'On US International and on Brazil\'s ABNT2 an accented '
                  + 'character is two presses, the accent and then the letter '
                  + '(ç has a key of its own on ABNT2), and a bare accent mark is '
                  + 'that key followed by a space. On a plain US keyboard there is '
                  + 'no way to type one at all, so the editor says so rather '
                  + 'than typing something else.',

  'no-webhid':      'This browser cannot reach the board. It needs Chrome or '
                  + 'Edge on a computer, or Chrome on Android, over https or '
                  + 'localhost — not Safari, Firefox, or any browser on an '
                  + 'iPhone or iPad.',
  refused:          'The board said no. Hold the pad for four seconds and try '
                  + 'again — reaching the table needs somebody at the board.',
  'udev-hint':      'On Linux a hidraw device is root-only until a udev rule '
                  + 'says otherwise. web/60-digimacro.rules, in the source at '
                  + 'github.com/marcocarnut/DigiMacro, is that rule: copy it into /etc/udev/rules.d, run "sudo udevadm control '
                  + '--reload", then unplug the board and plug it back in.',
  'connect-failed': 'could not connect: {msg}',
  connected:        'connected — firmware {fw}{state}, protocol {proto}',
  'fw-link-update': '(update…)',
  'fw-link-core':   '(a new core, and firmware {fw}…)',
  'fw-link-available': '(update available)',
  'fw-link-latest': '(latest)',
  'fw-link-noboot': '(hardened: no bootloader)',
  'hd-ok':          '(hardened)',
  'hd-not':         '(not fully hardened…)',
  'hd-summary':     'Hardening',
  'hd-why':         'This board was built without a bootloader, for hardened '
                  + 'mode, and the chip says it is missing something hardened '
                  + 'mode needs. v1.0\'s HARDENED_MODE.md explains each (git show '
                  + 'v1.0:HARDENED_MODE.md). Before its step 6, '
                  + 'the lock bits are expected to be missing; after it, nothing '
                  + 'here should be.',
  'hd-boot':        'no bootloader',
  'hd-lock':        'lock bits set, lock byte 0xFC: a programmer cannot read the '
                  + 'chip',
  'hd-eesave':      'EESAVE unprogrammed: erasing the chip erases the EEPROM too',
  'hd-dwen':        'debugWIRE off',
  'fw-how-v2':      'The board writes the new firmware itself, over the same '
                  + 'connection, and restarts into it: no bootloader, no second '
                  + 'picker, no driver. It needs the hold, and the password if '
                  + 'there is one. Cut off half way, the board comes back with no '
                  + 'firmware and asks for it again here. The macros, the '
                  + 'password, the lockout, the key check and the serial number '
                  + 'all stay as they are.',
  'fw-api-other':   'This page\'s firmware is for core API {app}, and the board\'s '
                  + 'core speaks {core}: it cannot go on this board.',
  'fw-core-update': 'Update the core',
  'fw-core-old':    'The board runs firmware {on} on an older core, API {core}. '
                  + 'DigiMacro {fw} needs the new one, which has room for the '
                  + 'mouse and for fixes. So the core goes first: the board '
                  + 'writes it itself, in about half a second, and comes back '
                  + 'with no firmware, and then this page installs DigiMacro '
                  + '{fw}. It needs the hold, and the password if there is one. '
                  + 'Do not unplug the board while it writes the core: cut off in '
                  + 'that half second, it needs a programmer to come back. The '
                  + 'macros, the password, the lockout, the key check and the '
                  + 'serial number all stay as they are.',
  'cu-what':        'the new core',
  'fw-core-done':   'The new core is in place. The board is coming back with no '
                  + 'firmware: install DigiMacro {fw} to finish.',
  'rec-core-moved': 'The new core is in place, with no DigiMacro on top of it '
                  + 'yet: install it to finish.',
  'rec-core-old':   'Its core is an older one, API {core}, and DigiMacro {fw} '
                  + 'needs the new one first. The board writes it itself, in '
                  + 'about half a second -- do not unplug it then, or it needs a '
                  + 'programmer -- and then this page installs DigiMacro {fw}.',
  'cu-crowded':     'something else keeps asking the board at the same time -- '
                  + 'another tab with this page, or another program. Close it '
                  + 'and try again.',
  'cu-region':      'the firmware, {start}–{end}, does not fit the board\'s '
                  + 'region for it, {rs}–{re}',
  'cu-no-header':   'that is not DigiMacro firmware for a v2 board: it has no '
                  + 'header',
  'cu-api':         'that firmware was built for core API {app}, and the board\'s '
                  + 'core speaks {core}',
  'cu-first-refused': 'the board would not take the first page: the password '
                  + 'was not given, or the six seconds it allows had passed',
  'cu-page-refused': 'the board refused the page at {at}',
  'cu-not-held':    'the page at {at} did not read back as written',
  'cu-commit-refused': 'the board would not start the new firmware: it did not '
                  + 'check out',
  'rec-summary':    'DigiMacro is not running on this board',
  'rec-pass-refused': 'The board said no: the password is wrong, or came within '
                  + 'a second of a wrong one. Try again in a moment.',
  'rec-none':       'The board\'s core is running, with no DigiMacro on top of '
                  + 'it: either this is a first install, or an update was cut '
                  + 'off half way. Either way, it picks up from here.',
  'rec-halted':     'DigiMacro stopped answering, or crashed, and the board reset '
                  + 'it, so the core did not start it again. Install it again '
                  + 'from here -- or unplug the board and plug it back in, to '
                  + 'give it another try.',
  'rec-updating':   'The board is in the middle of an update. If none is running '
                  + 'here, it was cut off: install again.',
  'rec-pass':       'The board has a password: give it, to install.',
  'rec-install':    'Install DigiMacro {fw}',
  'rec-no-fw':      'This page has no DigiMacro for this board\'s core.',
  'hd-pass':        'a password set: without one, anyone can put firmware on '
                  + 'the board that reads it out',
  'hd-why-v2':      'A board keeps its secrets only as well as it is hardened: '
                  + 'the password guards the table from the page, and the lock '
                  + 'bits and fuses below guard it from a programmer. '
                  + 'HARDENED_MODE.md explains each.',
  'hd-nag':         'This board has a password, so it holds something worth '
                  + 'guarding -- and until every line below is ticked, anyone '
                  + 'with a programmer can read it out, password and all.',
  'hd-selfprg':     'SELFPRGEN programmed: the board can update itself',
  'hd-bod':         'brown-out detection on: a sagging supply cannot corrupt '
                  + 'the flash the board writes',
  'auto-saved':     'Saved: the board types it whenever it is plugged in.',
  'auto-saved-none': 'Saved: the board types nothing when plugged in.',
  'auto-write-first': 'Write the macros first: the board does not hold this '
                  + 'one yet.',
  'serial-tag':     'serial {serial}',
  'fw-link-move':   '(move to v2…)',
  'fw-move-why':    'The board runs v1\'s firmware {on}; this moves it to v2, '
                  + 'DigiMacro {fw}. v2 has no bootloader: the board updates '
                  + 'itself from here, '
                  + 'over the same connection, and is safe from a cut-off '
                  + 'update. This writes v2\'s core through Micronucleus; the '
                  + 'board then comes back asking for DigiMacro {fw}, which goes '
                  + 'on over Micronucleus, and Micronucleus is gone. The macros, '
                  + 'the password and the serial number all stay. '
                  + 'A power cut in the first moments of that second step needs '
                  + 'a programmer to put right, as with any upload through '
                  + 'Micronucleus.',
  'fw-moved':       'v2\'s core is on. The board is coming back asking for '
                  + 'DigiMacro {fw}: install it below.',
  'core-name':      'v2\'s core',
  'new-board':      'Set up a new board',
  'new-plug':       'Plug the new board in now — unplugged first, if it already '
                  + 'was — and pick it in the browser\'s list as soon as it '
                  + 'shows up: as "Digispark" or "Micronucleus". It waits only a '
                  + 'few seconds.',
  'new-none':       'Micronucleus was not picked, so nothing was written. Plug '
                  + 'the board in again and start over.',
  'new-done':       'v2\'s core is on. Press Connect and pick the board: it is '
                  + 'DigiMacro now, asking for its firmware.',
  'new-no-v2':      'This page has no v2 firmware to set a board up with.',
  'hd-raw':         'As the chip reads them: low fuse {low}, high {high}, '
                  + 'extended {ext}, lock {lock}.',
  close:            'Close',
  remove:           'remove',
  goto:             'goto',
  'bk-no-pass':     'This page does not know the board\'s password, so it cannot '
                  + 'encrypt the backup. Unplug the board, plug it back in, and '
                  + 'give the password when asked.',
  'backup-bad-password': 'That backup holds a password other than the one it was '
                  + 'opened with, so restoring it would lock the board against '
                  + 'you. It was not made by this page; nothing was written.',
  'password-too-long': 'That is more than 12 bytes. Letters with accents take two '
                  + 'each, so shorten it a little.',
  'wrong-magic':    'that device did not answer "DM"',
  'write-ok':       'Wrote {n} bytes and read every one of them back.',
  'write-mismatch': 'mismatch at {at}: wrote {wrote}, read {read}',

  'lone-backslash': 'a lone \\ at the end',
  'bad-escape':     '\\{what} means nothing',
  unclosed:         'a { with no }',
  'empty-braces':   '{} is empty',
  'pause-range':    '{{what}}: a pause runs from 0.1 to 25.5 seconds',
  'point-range':    '{{what}}: the pointer goes from 0 to 100, in percent of the screen',
  'point-scale':    '{{what}}: a scale runs from 0.75 to 3.75, in quarters',
  'repeat-range':   '{{what}}: a repeat runs from 1 to 256',
  'group-empty':    '() has nothing in it',
  'nested-repeat':  'a count inside a group: the firmware has one mark, so the '
                  + 'inner count would take the outer one over',
  'goto-nowhere':   '{{goto {what}}}: there is no macro with that code',
  'ir-button':      '{{what}}: a remote\'s button is {IR} and two bytes in hex, '
                  + 'as in {IR 00 45}',
  'loop-empty':     '{what}: these only jump to each other and never type or '
                  + 'pause, so the firmware would give up on them rather than '
                  + 'run them',
  'ref-goto':       'carry on with another macro, and never come back',
  'nothing-to-rep': '{{what}}: nothing to repeat yet',
  'no-key-after':   '{{what}}: no key after the +',
  'not-a-modifier': '"{what}" is not a modifier',
  'not-a-key':      '"{what}" is not a key',
  'no-key-for':     'no key for "{what}"',
  'needs-dead-keys':'"{what}" needs a keyboard with dead keys — see the '
                  + 'keyboard setting beside Macros',
  'not-a-code':     'not a code: "{what}"',
  'code-twice':     '"{what}" is used twice',
  'ir-only':        'Not tappable: only the remote reaches it, or a {goto}. '
                  + 'Codes IR1 to IR30 are for IR-only macros.',
  'not-a-byte':     '"{what}" has a byte that is not a byte',
  'has-a-zero':     '"{what}" contains a zero, which would end it early',
  'too-big':        '{n} bytes of table, and only {fits} fit',
  'secret-no-key':  '{secret} needs the key, to encrypt what follows it',
  'secret-twice':   'one {secret} per macro: everything after the first is '
                  + 'secret already',
  'secret-in-group':'a {secret} inside a group: it takes the rest of the macro, '
                  + 'so it cannot be repeated',
  'secret-ir':      'an {IR} after a {secret}: the remote could not find its '
                  + 'button in encrypted text',
  'secret-sealed-last': 'nothing can follow a sealed {secret}: it is the rest '
                  + 'of the macro already',
  'secret-blob':    'a sealed {secret} that is not one: it was changed since it '
                  + 'was read',
  'secret-too-long':'a secret of {n} keystrokes: at most {max} fit',
  'host-gone':      '{host} is gone: put {secret} where it was, and the rest of '
                  + 'the macro is encrypted as well as guarded',
  'secret-wrong-key':'this is not the key these secrets were sealed with',
  'secret-no-nonce':'no nonce would seal this secret — try again',
},

pt: {
  'lang-name':      'Português',
  tagline:          'Toque um código, digite uma macro.',
  'whats-this':     'o que é isto?',
  'vk-toggle':      'Teclado na tela: um celular com a placa conectada não tem '
                  + 'outro',
  'vk-space':       'espaço',
  'readme-url':     'https://github.com/marcocarnut/DigiMacro/blob/main/README.pt-BR.md',
  keyboard:         'teclado',
  language:         'idioma',
  connect:          'Conectar',
  macros:           'Macros',
  'th-code':        'Código',
  'th-types':       'Digita',
  'ph-types':       'o que este código digita',
  'add-macro':      '+ Adicionar macro',
  'load-example':   'Carregar o exemplo',
  'reload-table':   'Recarregar a tabela da placa',
  'table-reloaded': 'A tabela como está na placa.',
  startup:          'Inicialização',
  'startup-why':    'Uma macro pode se digitar sozinha quando a placa é '
                  + 'conectada, assim que o computador estiver pronto. "Mandar '
                  + 'para a placa", lá em cima, salva isto junto com a tabela.',
  budget:           '{n} de {max} bytes',
  keys:             'Teclas',
  'on-plug':        'ao conectar, digitar',
  nothing:          'nada',
  'auto-dangling':  'A macro que esta placa digitava ao ligar não está mais na '
                  + 'tabela, então ela não digitará nada. Escolha outra, ou '
                  + 'deixe como está.',
  'mods-hint':      'fica pressionado até a próxima tecla',
  'a-key':          'uma tecla',
  insert:           'inserir',
  pause:            'pausa',
  repeat:           'repetir',
  'pad-hint':       'Insere no cursor da macro que você estava editando. Um '
                  + 'modificador fica pressionado até a próxima tecla.',
  write:            'Mandar para a placa',

  'sec-summary':    'Senha',
  'sec-none':       'Esta placa não tem senha, que é o normal e está ótimo: '
                  + 'segurar o sensor é o que mantém uma página web de fora, e '
                  + 'muitas macros não são secretas. Defina uma se a tabela '
                  + 'merecer proteção contra quem estiver com a placa na mão.',
  save:             'Salvar',
  'clear-password': 'Remover',
  'ph-password':    'uma senha nova, até 12 caracteres',
  'ph-gate-password': 'senha',
  'gate-hold':      'Segure o sensor por quatro segundos para deixar esta '
                  + 'página entrar. A placa continua sendo teclado o tempo '
                  + 'todo; segurar é a única coisa que uma página não consegue '
                  + 'fazer sozinha.',
  'gate-hold-password': 'Esta placa tem senha. Digite-a aqui e segure o sensor '
                  + 'por quatro segundos — em qualquer ordem.',
  'gate-type-password': 'O sensor foi segurado. Agora a senha.',
  'gate-waiting':   'Certo. Agora segure o sensor por quatro segundos.',
  'gate-reading':   'Lendo a placa…',
  'password-empty-unlock': 'Digite a senha primeiro.',
  'hold-now':       'Segure o sensor por quatro segundos para continuar — o '
                  + 'último toque longo já expirou.',
  'hold-timeout':   'Ninguém segurou o sensor, então nada foi feito.',
  writing:          'Gravando…',
  'sec-has':        'Esta placa tem senha. Troque-a aqui, ou remova-a para '
                  + 'deixar a placa aberta a quem conseguir segurar o sensor.',
  'board-left':     'a placa foi desconectada',
  'read-none':      'A placa ainda não tem macros. Adicione algumas, e "Mandar '
                  + 'para a placa", lá em cima, as coloca nela.',
  'sample-say':     'Um exemplo de cada coisa que uma macro sabe fazer. "Mandar '
                  + 'para a placa", lá em cima, o coloca na placa por cima do '
                  + 'que houver.',
  unlock:           'Destravar',
  'wrong-password': 'Essa não é a senha. Tente de novo daqui a um segundo; com '
                  + 'o bloqueio ligado, cada uma errada conta como código errado.',
  'password-empty': 'Digite uma senha primeiro, ou use Remover.',
  'password-set':   'Senha definida. Esta página a guarda até a placa ser '
                  + 'desconectada, e a placa digita as macros sem ela.',
  'password-cleared': 'Senha removida. A placa está aberta a quem conseguir '
                  + 'segurar o sensor.',
  'lock-summary':   'Bloqueio',
  'lock-why':       'Códigos errados contam. Depois de muitos, a placa ignora '
                  + 'todo toque, certo ou errado, por um tempo — um bloqueio '
                  + 'leve, que acaba sozinho e que desconectar não encurta. '
                  + 'Depois de muitos bloqueios leves, ela ignora os toques até '
                  + 'a senha ser dada aqui — um bloqueio forte. Só a senha zera '
                  + 'as contagens, ou uma macro com {clear}: um código que '
                  + 'funciona não zera, senão quem conhecesse um poderia tentar '
                  + 'para sempre. Uma senha errada conta como código errado, mas '
                  + 'a senha em si nunca fica bloqueada.',
  'lock-enable':    'Bloquear códigos errados',
  'lock-soft':      'códigos errados antes de uma pausa',
  'lock-wait':      'pausa, em segundos',
  'lock-hard':      'pausas antes do bloqueio forte (0: nunca)',
  'lock-needs-password': 'O bloqueio precisa de uma senha, porque dá-la é o '
                  + 'único jeito de desfazer um bloqueio forte. Defina uma acima '
                  + 'primeiro.',
  'lock-state-soft': 'A placa está em pausa depois de códigos errados demais. '
                  + 'Ela ignora todo toque até a pausa acabar, e desconectá-la '
                  + 'recomeça a pausa.',
  'lock-state-hard': 'A placa está em bloqueio forte e ignora todo toque. '
                  + 'Segure o sensor por quatro segundos, digite a senha e '
                  + 'clique em Destravar.',
  'lock-odds':      p => `Quem tentar adivinhar tem no máximo ${p.tries} `
                  + `${p.tries == 1 ? 'código' : 'códigos'} antes do bloqueio `
                  + `forte, com uma pausa de ${p.wait} `
                  + `${p.soft == 1 ? 'a cada erro' : `a cada ${p.soft} erros`}. `
                  + `Com ${p.n} dos 62 códigos em uso, cada tentativa tem cerca `
                  + `de ${p.pct}% de chance.`,
  'lock-odds-nohard': p => `Sem bloqueio forte, quem tentar adivinhar leva uma `
                  + `pausa de ${p.wait} `
                  + `${p.soft == 1 ? 'a cada código errado' : `a cada ${p.soft} códigos errados`}, `
                  + `pelo tempo que insistir. Com ${p.n} dos 62 códigos em uso, `
                  + `cada tentativa tem cerca de ${p.pct}% de chance.`,
  'lock-bad':       'Códigos errados antes de uma pausa devem ser de 1 a 254, '
                  + 'pausas antes do bloqueio forte de 0 a 254, e a pausa de 10 a 2400 '
                  + 'segundos, de dez em dez.',
  'lock-counts':    'Códigos errados agora: {misses}. Pausas: '
                  + '{softs}. Bloqueios fortes desfeitos com a senha, desde '
                  + 'sempre: {cleared}.',
  'lock-saved':     'Bloqueio salvo. As contagens recomeçam do zero.',
  'lock-off-saved': 'Bloqueio desligado. Códigos errados não custam mais nada.',
  'lock-lifted':    'Bloqueio forte desfeito.',
  'bk-summary':     'Cópia de segurança',
  'bk-why':         'Tudo o que está na placa — as macros, a macro de '
                  + 'inicialização, o bloqueio, a verificação da chave, a senha e '
                  + 'o número de série — salvo num arquivo, e restaurado. Uma '
                  + 'placa apagada com um gravador — para recomeçar, ou para '
                  + 'instalar com ele — perde tudo isso; restaurada, ela volta a '
                  + 'ser a mesma. '
                  + 'Com senha, o arquivo é cifrado com ela, então guarde essa '
                  + 'senha: sem ela o arquivo não serve para ninguém, nem para '
                  + 'você.',
  'bk-save':        'Salvar uma cópia',
  'bk-restore':     'Restaurar',
  'bk-restore-head': 'Restaurar',
  'un-summary':     'De volta a um Digispark comum',
  'un-why':         'Isto tira o DigiMacro da placa e põe de volta o Micronucleus, '
                  + 'o bootloader com que o Digispark vem, para que a IDE do '
                  + 'Arduino e outras ferramentas possam gravá-la de novo. Antes, a '
                  + 'placa apaga a EEPROM: as macros, os segredos, a senha '
                  + 'e o número de série se vão de vez, e nenhum '
                  + 'programa gravado depois consegue lê-los. Salve uma cópia acima '
                  + 'se puder querê-los de volta. Preparar uma placa nova traz o '
                  + 'DigiMacro de volta quando você quiser.',
  'un-sure':        'Entendi: as macros, os segredos e a senha são '
                  + 'apagados de vez.',
  'un-go':          'Apagar e voltar ao Micronucleus',
  'un-what':        'o desinstalador e o Micronucleus',
  'un-done':        'Pronto: a placa voltou a ser um Digispark comum, com o '
                  + 'Micronucleus esperando um programa (16d0:0753).',
  'un-seen':        'O Micronucleus respondeu.',
  'un-low':         'fuse baixo {v}',
  'un-high':        'fuse alto {v}',
  'un-ext':         'fuse estendido {v}',
  'un-lock':        'bits de trava {v}',
  'un-fuses':       'O chip dela não está configurado como o Digispark vem ({what}). '
                  + 'Funciona como um mesmo assim, mas só um gravador desfaz isso — '
                  + 'o que apaga o chip, Micronucleus inclusive, então ele volta '
                  + 'pelo gravador também:',
  'un-fuses-hv':    'O chip dela não está configurado como o Digispark vem ({what}), '
                  + 'e o pino de reset está desligado, então um gravador como o '
                  + 'DigiISP não o alcança: antes é preciso um gravador de alta '
                  + 'tensão. Funciona como um Digispark mesmo assim:',
  'un-howto':       'como',
  'un-howto-href':  'https://github.com/marcocarnut/DigiMacro/blob/main/HARDENED_MODE.pt-BR.md#mudando-as-coisas-depois',
  'ph-bk-pass':     'a senha com que foi salva',
  'bk-short':       'A senha tem menos de oito caracteres. Um arquivo copiado pode '
                  + 'ser atacado fora da placa, muito mais rápido do que a placa '
                  + 'permite, e uma senha curta não aguenta muito. Clique em '
                  + 'Salvar uma cópia de novo para salvar assim mesmo.',
  'bk-plain':       'Esta placa não tem senha, então a cópia teria todas as '
                  + 'macros às claras. Clique em Salvar uma cópia de novo para '
                  + 'salvar assim mesmo, ou defina uma senha antes.',
  'bk-deriving':    'Calculando a cifra a partir da senha — um ou dois segundos, '
                  + 'de propósito…',
  'bk-saved':       'Salva como {file}, cifrada com a senha da placa.',
  'bk-saved-plain': 'Salva como {file}, às claras. Guarde-a onde você guardaria '
                  + 'as próprias macros.',
  'bk-file-locked': 'Uma cópia da placa {serial}, firmware {firmware}, salva em '
                  + '{saved}. Está cifrada: digite a senha com que foi salva.',
  'bk-file-plain':  'Uma cópia da placa {serial}, firmware {firmware}, salva em '
                  + '{saved}. Não está cifrada.',
  'bk-needs-pass':  'Digite a senha com que a cópia foi salva.',
  'bk-confirm':     'Isto substitui tudo nesta placa pela cópia da placa '
                  + '{serial}: macros, inicialização, bloqueio, verificação '
                  + 'da chave, senha e número de série. Clique em Restaurar de '
                  + 'novo para continuar. Uma restauração instala o que houver no '
                  + 'arquivo, macro de inicialização inclusive, então só restaure '
                  + 'cópias feitas por você.',
  'bk-restored-replug': 'Restaurada, e lida de volta. Desconecte a placa e '
                  + 'conecte de novo, para o número de série restaurado valer.',
  'bk-restarting':  'Restaurada, e lida de volta. A placa está reiniciando, para '
                  + 'voltar com o número de série restaurado…',
  'bk-restarted':   'Restaurada. A placa reiniciou com o número de série '
                  + 'restaurado — clique em Conectar e escolha-a de novo.',
  'backup-not-one': 'Esse arquivo não é uma cópia de segurança do DigiMacro.',
  'backup-other-map': 'Essa cópia foi feita por outra versão do DigiMacro, e esta '
                  + 'página não consegue lê-la.',
  'backup-wrong-password': 'Essa não é a senha com que esta cópia foi salva.',
  'key-summary':    'Segredos',
  'key-why':        'O que vem depois de {secret} numa macro fica cifrado na '
                  + 'placa, e a chave não fica nela de jeito nenhum: a macro só '
                  + 'o digita enquanto um pequeno programa neste computador '
                  + 'tiver enviado a chave há pouco — o tools/hostauth, com o '
                  + 'arquivo que esta página baixa. Ligada em qualquer outro, ou '
                  + 'roubada, a placa só guarda bytes embaralhados. {secret!} '
                  + 'interrompe a macro como se o código não existisse — três '
                  + 'piscadas, e um código errado para o bloqueio.',
  'key-wait':       'guardada por, em segundos, depois do envio da chave',
  'key-new':        'Criar uma chave',
  'key-replace':    'Criar uma chave nova',
  'key-load':       'Carregar o arquivo da chave',
  'key-remove':     'Remover a chave',
  'key-none':       'Esta placa não tem chave para segredos.',
  'key-none-secrets': 'Algumas macros têm segredos, mas esta placa não tem chave '
                  + 'para eles: crie uma, e a página os cifra ao gravar.',
  'key-has':        'Esta placa tem uma chave para segredos. Nenhum computador '
                  + 'a enviou há pouco.',
  'key-held':       'Esta placa tem uma chave para segredos, e um computador a '
                  + 'enviou há pouco: os segredos rodam por ora.',
  'key-loaded':     'A chave está carregada nesta página: os segredos aparecem '
                  + 'como texto, e são cifrados de novo ao gravar.',
  'key-load-to-open': 'Carregue o arquivo da chave para ver ou mudar os segredos.',
  'key-made':       'Uma chave nova está em uso, e salva como {file}. Acrescente '
                  + 'a linha dela a ~/.config/digimacro/host.key (no Windows, '
                  + '%APPDATA%\\digimacro\\host.key) em cada computador para o '
                  + 'qual os segredos devem rodar; os que tinham a chave antiga '
                  + 'precisam desta. Guarde o arquivo: sem ele, os segredos não '
                  + 'podem mais ser lidos nem mudados.',
  'key-removed':    'A chave saiu da placa.',
  'key-opened':     'A chave serve: os segredos estão abertos nesta página.',
  'key-not-this':   'Essa chave não é desta placa: não abre nenhum segredo dela.',
  'key-not-in-file': 'Esse arquivo não tem chave para a placa {serial}.',
  'key-v2-host':    'Essa é uma chave de computador da v2, que o firmware 16 não '
                  + 'usa mais. Crie uma chave.',
  'key-sealed-left': 'Alguns segredos foram cifrados com a chave da placa, e esta '
                  + 'página não a tem. Carregue o arquivo da chave antes, para '
                  + 'que sejam cifrados de novo com a nova — ou remova-os.',
  'key-write-first': 'Grave as macros antes: uma chave nova cifra os segredos de '
                  + 'novo, e os grava com ela.',
  'key-secrets-left': 'Algumas macros têm segredos, que nada leria de novo sem a '
                  + 'chave. Tire-os antes.',
  'key-needed':     'Algumas macros têm segredos a cifrar. Crie uma chave em '
                  + 'Segredos, abaixo, ou carregue o arquivo da chave.',
  'key-mismatch':   'a verificação da chave lida de volta é diferente da gravada',
  'key-wait-saved': 'Salvo: a chave fica guardada por {wait} depois de enviada.',
  'key-wait-bad':   'O tempo vai de 10 a 2400 segundos, de dez em dez.',
  'key-file-comment': 'chave DigiMacro da placa {serial}: acrescente esta linha '
                  + 'a ~/.config/digimacro/host.key, ou no Windows a '
                  + '%APPDATA%\\digimacro\\host.key',
  'sealed':         '🔒 e então um segredo, {n} teclas: carregue o arquivo da '
                  + 'chave para ver ou mudar',
  'sealed-loud':    '🔒 e então um {secret!}, {n} teclas: carregue o arquivo da '
                  + 'chave para ver ou mudar',
  'secret-startup': 'A macro de inicialização tem um segredo. Ela roda antes que '
                  + 'qualquer programa no computador possa ter enviado a chave, '
                  + 'então vai parar ali toda vez.',
  'secret-old-fw':  'Segredos precisam do firmware {fw} ou mais novo: atualize o '
                  + 'firmware da placa antes.',
  'host-convert':   'Trocar {host} por {secret}',
  'host-converted': 'Pronto: cada {host} agora é um {secret}. Grave as macros '
                  + 'para levá-los à placa.',
  'host-converted-key': 'Pronto: cada {host} agora é um {secret}. Crie uma chave '
                  + 'em Segredos, abaixo, e então grave as macros.',
  'ref-secret':     'o resto da macro é segredo: cifrado na placa, e digitado só '
                  + 'enquanto este computador tiver enviado a chave há pouco',
  'ref-secret-loud': '...e sem ela, parar como se o código não existisse',
  'host-late':      'Algo aparece antes de {host!}, então quem tenta adivinhar '
                  + 'num computador errado descobre que este código existe. '
                  + 'Ponha a verificação primeiro.',
  'secret-late':    'Algo aparece antes de {secret!}, então quem tenta '
                  + 'adivinhar num computador errado descobre que este código '
                  + 'existe. Ponha o segredo primeiro.',
  'ref-ir':         'onde o botão 00 45 do controle remoto começa a digitar, até '
                  + 'o fim da macro; digitada pelo código, não faz nada',
  'ref-point':      'o ponteiro do mouse, em porcentagem da tela; {0,0} o '
                  + 'empurra para o canto, onde quer que esteja',
  'ref-point-scale': '...aquele movimento a vezes mais rápido, de 0,75 a 3,75, '
                  + 'para um computador que muda a escala do ponteiro',
  'ref-click':      'um clique do mouse',
  'ref-hold':       'um botão pressionado, e solto de novo; {right-hold} também',
  'ref-clear':      'esquece os códigos errados do bloqueio, como a senha faz',
  'ir-learn':       'botão do controle',
  'ir-title':       'Um botão do controle',
  'ir-how':         'Aponte o controle para a placa e aperte o botão em até dez '
                  + 'segundos: a placa digita os dois bytes dele na caixa abaixo, '
                  + 'então deixe o foco nela. O controle precisa falar o '
                  + 'protocolo NEC, como muitos controles de fita de LED e de TV.',
  'ir-receiver':    'Só uma placa com receptor infravermelho, no P2, ouve alguma '
                  + 'coisa.',
  'ir-heard':       'a placa digita aqui',
  'ir-listening':   'ouvindo… {s} s',
  'ir-silent':      'Nada foi ouvido. Talvez a placa não tenha receptor, o '
                  + 'controle não fale NEC, ou a caixa tenha perdido o foco. '
                  + 'Ouça de novo para tentar outra vez.',
  'ir-paste':       'Copie isto e cole na macro que o botão deve digitar, onde a '
                  + 'digitação deve começar: em geral, bem no início. Uma macro '
                  + 'que só o controle deve alcançar pode ter um código de IR1 a '
                  + 'IR30 em vez de pontos e traços.',
  'ir-copy':        'Copiar',
  'ir-copied':      '{ir} está na área de transferência.',
  'ir-copy-failed': 'O navegador não deixou copiar: o texto está selecionado, '
                  + 'então copie você mesmo.',
  'ir-again':       'Ouvir de novo',
  'ir-noboard':     'Conecte a placa primeiro.',
  'ir-refused':     'A placa não aceitou: o firmware dela não tem controle '
                  + 'remoto. Atualize-o.',
  'fw-summary':     'Firmware',
  'fw-how':         'A placa reinicia no seu bootloader, o Micronucleus, e o '
                  + 'navegador pede para você escolhê-lo — uma segunda lista, '
                  + 'porque o bootloader é outro tipo de dispositivo USB. '
                  + 'Escolha em poucos segundos, ou ele desiste e volta ao '
                  + 'firmware antigo. As macros, a senha, o bloqueio, a chave de '
                  + 'computador e o número de série continuam como estão. No Windows o bootloader precisa de um driver '
                  + 'uma vez: o Zadig, instalando WinUSB para "Micronucleus".',
  'fw-update':      'Atualizar o firmware',
  'fw-who':         'atualizando o firmware…',
  'fw-too-old':     'O firmware {on} não sabe reiniciar no bootloader, então '
                  + 'grave o núcleo da v2 à mão -- "micronucleus --run '
                  + 'web/firmware/digimacro-v2-core.hex", e conecte a placa -- e '
                  + 'esta página continua daí.',
  'fw-none-here':   'A placa roda o firmware {on}. Esta página não tem firmware '
                  + 'para oferecer; sirva-a a partir da pasta web do projeto.',
  'fw-newer':       'A placa roda o firmware {on}, e o firmware {here} está '
                  + 'disponível.',
  'fw-same':        'A placa roda o firmware {on}, que é o mais recente desta '
                  + 'página ({here}). Atualizar gravaria o mesmo de novo.',
  'fw-no-webusb':   'navigator.usb não existe. O bootloader é acessado por '
                  + 'WebUSB, que precisa do Chrome ou do Edge.',
  'fw-ready':       'Pronto. Clique em Atualizar o firmware de novo em até meio '
                  + 'minuto.',
  'fw-rebooting':   'Reiniciando a placa no bootloader…',
  'fw-pick':        'Escolha "Micronucleus" na lista do navegador.',
  'fw-found':       'Micronucleus {version} encontrado.',
  'fw-erasing':     'Apagando, para {what}…',
  'fw-writing':     'Gravando {what}…',
  'fw-no-bootloader': 'Nenhum bootloader foi escolhido, então nada foi gravado. '
                  + 'A placa volta sozinha ao firmware antigo em alguns '
                  + 'segundos.',
  'fw-failed':      'A atualização parou: {msg}',
  'fw-udev':        'No Linux o bootloader precisa da linha do Micronucleus de '
                  + 'web/60-digimacro.rules (github.com/marcocarnut/DigiMacro) '
                  + 'em /etc/udev/rules.d.',
  'fw-zadig':       'No Windows o bootloader precisa de um driver, uma vez: rode '
                  + 'o Zadig (zadig.akeo.ie) com a placa no bootloader, '
                  + 'escolha-a — ID USB 16D0 0753 — e instale o WinUSB. Depois '
                  + 'desconecte a placa e recomece.',
  'fw-done':        'Firmware {fw} gravado. A placa está iniciando agora, e vai '
                  + 'pedir o toque longo de novo.',
  'mn-too-big':     'o firmware tem {n} bytes e este bootloader deixa {max}',
  'mn-no-jump':     'o firmware não começa com um salto, então não dá para '
                  + 'prepará-lo para o bootloader',
  'mn-unsupported': 'Micronucleus {version} não é um que esta página conheça',
  'mn-no-info':     'o bootloader não disse qual é o seu tamanho',
  'mn-deaf':        'o bootloader está lá mas não responde mais: ele só espera '
                  + 'alguns segundos depois que a placa é conectada. Desconecte '
                  + 'a placa, clique de novo e conecte-a com a lista do '
                  + 'navegador aberta',
  'mn-not-t85':     'esse bootloader não está num ATtiny85',
  'ref-summary':    'O que pode entrar numa macro',
  'ref-anything':   'digitado como está',
  'ref-escapes':    'nova linha, tabulação, barra invertida',
  'ref-brace':      'uma chave { literal',
  'ref-named':      'uma tecla que não é um caractere',
  'ref-mods':       'modificadores em qualquer tecla ou caractere',
  'ref-combined':   '...combinados, em qualquer ordem',
  'ref-mod-alone':  'um modificador sozinho: o Iniciar do Windows, a tela '
                  + 'inicial de uma TV. Gui é o Win e o Cmd',
  'ref-pause':      'espera, em segundos, de 0,1 a 25,5',
  'ref-repeat':     'onze deles ao todo, como numa expressão regular',
  'ref-group':      '...de um grupo inteiro, em vez de uma coisa só',
  'ref-paren':      'um parêntese que é só um parêntese',
  'ref-accent':     'num teclado com acentos mortos: o acento e depois a letra',
  'ref-bare-mark':  'a tecla de acento sozinha, sem nada ligado a ela',
  'ref-anything-ex': 'qualquer coisa',
  'code-summary':   'Códigos',
  'code-help':      'Um código são de um a cinco pontos e traços, escritos aqui '
                  + 'como . e -. No sensor, um ponto é um toque curto, de menos '
                  + 'de um quarto de segundo, e um traço um mais longo; 700 ms '
                  + 'sem toque encerram o código. Os únicos outros códigos são '
                  + 'IR1 a IR30: funcionam como qualquer outro, mas nenhum toque '
                  + 'os produz, então servem para macros que só o controle '
                  + 'remoto, ou um {goto}, alcança.',
  'code-guess':     'Existem sessenta e dois códigos, então uma tabela que use '
                  + 'muitos deles é mais fácil de adivinhar — que é para isso '
                  + 'que serve o bloqueio.',
  'code-morse':     'Código Morse, para códigos fáceis de lembrar:',
  'ref-packed':     'As macros ficam empacotadas uma após a outra, então o '
                  + 'único limite são 480 bytes no total: dois bytes extras '
                  + 'cada, um byte por tecla, dois por pausa ou '
                  + 'repetição, quatro por {IR}.',
  'ref-layout':     'A placa envia pressionamentos de tecla, não letras, então '
                  + 'o que aparece é decidido pelo layout de teclado da '
                  + 'máquina em que ela está ligada — que é o que a opção de '
                  + 'teclado ao lado de Macros informa. No US International e '
                  + 'no ABNT2 um caractere '
                  + 'acentuado são dois pressionamentos, o acento e depois a '
                  + 'letra (no ABNT2, o ç tem tecla própria), e um acento '
                  + 'sozinho é a tecla dele seguida de espaço. '
                  + 'Num teclado US puro não há como digitar um acento, então '
                  + 'o editor avisa em vez de digitar outra coisa.',

  'no-webhid':      'Este navegador não consegue falar com a placa. Ela '
                  + 'precisa do Chrome ou do Edge num computador, ou do Chrome '
                  + 'no Android, por https ou localhost — não do Safari, do '
                  + 'Firefox, nem de navegador de iPhone ou iPad.',
  refused:          'A placa recusou. Segure o sensor por quatro segundos e '
                  + 'tente de novo — chegar à tabela exige alguém na placa.',
  'udev-hint':      'No Linux um dispositivo hidraw é só do root até que uma '
                  + 'regra do udev diga o contrário. web/60-digimacro.rules, no '
                  + 'código em github.com/marcocarnut/DigiMacro, é essa regra: copie para /etc/udev/rules.d, rode "sudo '
                  + 'udevadm control --reload", depois desconecte e reconecte '
                  + 'a placa.',
  'connect-failed': 'não foi possível conectar: {msg}',
  connected:        'conectada — firmware {fw}{state}, protocolo {proto}',
  'fw-link-update': '(atualizar…)',
  'fw-link-core':   '(um novo núcleo, e o firmware {fw}…)',
  'fw-link-available': '(atualização disponível)',
  'fw-link-latest': '(o mais recente)',
  'fw-link-noboot': '(reforçada: sem bootloader)',
  'hd-ok':          '(reforçada)',
  'hd-not':         '(não totalmente reforçada…)',
  'hd-summary':     'Reforço',
  'hd-why':         'Esta placa foi gravada sem bootloader, para o modo '
                  + 'reforçado, e o chip diz que falta algo de que o modo '
                  + 'reforçado precisa. O HARDENED_MODE.md da v1.0 explica cada '
                  + 'item (git show v1.0:HARDENED_MODE.md). Antes do passo 6 dele é normal faltarem os bits de trava; depois '
                  + 'dele, nada aqui deveria faltar.',
  'hd-boot':        'sem bootloader',
  'hd-lock':        'bits de trava gravados, byte de trava 0xFC: um gravador não '
                  + 'consegue ler o chip',
  'hd-eesave':      'EESAVE não programado: apagar o chip apaga a EEPROM também',
  'hd-dwen':        'debugWIRE desligado',
  'fw-how-v2':      'A placa grava o novo firmware ela mesma, pela mesma '
                  + 'conexão, e reinicia nele: sem bootloader, sem segundo '
                  + 'seletor, sem driver. É preciso segurar o sensor, e dar a '
                  + 'senha se houver uma. Interrompida no meio, a placa volta sem '
                  + 'firmware e pede de novo aqui. As macros, a senha, o '
                  + 'bloqueio, a verificação da chave e o número de série ficam '
                  + 'como estão.',
  'fw-api-other':   'O firmware desta página é para a API de núcleo {app}, e o '
                  + 'núcleo da placa fala {core}: não pode ir nesta placa.',
  'fw-core-update': 'Atualizar o núcleo',
  'fw-core-old':    'A placa roda o firmware {on} sobre um núcleo mais antigo, '
                  + 'API {core}. O DigiMacro {fw} precisa do novo, que tem espaço '
                  + 'para o mouse e para correções. Então o núcleo vai primeiro: '
                  + 'a placa o grava ela mesma, em cerca de meio segundo, e volta '
                  + 'sem firmware, e então esta página instala o DigiMacro {fw}. '
                  + 'É preciso segurar o sensor, e dar a senha se houver uma. Não '
                  + 'desconecte a placa enquanto ela grava o núcleo: interrompida '
                  + 'nesse meio segundo, ela precisa de um gravador para voltar. '
                  + 'As macros, a senha, o bloqueio, a verificação da chave e o '
                  + 'número de série ficam como estão.',
  'cu-what':        'o novo núcleo',
  'fw-core-done':   'O novo núcleo está no lugar. A placa está voltando sem '
                  + 'firmware: instale o DigiMacro {fw} para terminar.',
  'rec-core-moved': 'O novo núcleo está no lugar, ainda sem o DigiMacro em cima '
                  + 'dele: instale-o para terminar.',
  'rec-core-old':   'O núcleo dela é mais antigo, API {core}, e o DigiMacro {fw} '
                  + 'precisa do novo antes. A placa o grava ela mesma, em cerca de '
                  + 'meio segundo -- não a desconecte nesse momento, ou ela vai '
                  + 'precisar de um gravador -- e então esta página instala o '
                  + 'DigiMacro {fw}.',
  'cu-crowded':     'outra coisa está falando com a placa ao mesmo tempo -- outra '
                  + 'aba com esta página, ou outro programa. Feche-a e tente de '
                  + 'novo.',
  'cu-region':      'o firmware, {start}–{end}, não cabe na região da placa '
                  + 'para ele, {rs}–{re}',
  'cu-no-header':   'isso não é firmware do DigiMacro para uma placa v2: não tem '
                  + 'cabeçalho',
  'cu-api':         'esse firmware foi feito para a API de núcleo {app}, e o '
                  + 'núcleo da placa fala {core}',
  'cu-first-refused': 'a placa não aceitou a primeira página: a senha não foi '
                  + 'dada, ou os seis segundos que ela permite já tinham passado',
  'cu-page-refused': 'a placa recusou a página em {at}',
  'cu-not-held':    'a página em {at} não foi lida de volta como gravada',
  'cu-commit-refused': 'a placa não iniciou o novo firmware: ele não foi '
                  + 'validado',
  'rec-summary':    'O DigiMacro não está rodando nesta placa',
  'rec-pass-refused': 'A placa recusou: a senha está errada, ou veio menos de um '
                  + 'segundo depois de uma errada. Tente de novo daqui a pouco.',
  'rec-none':       'O núcleo da placa está rodando, sem o DigiMacro em cima '
                  + 'dele: ou esta é a primeira instalação, ou uma atualização '
                  + 'foi interrompida no meio. Em qualquer caso, continua daqui.',
  'rec-halted':     'O DigiMacro parou de responder, ou travou, e a placa o '
                  + 'reiniciou, então o núcleo não o iniciou de novo. Instale de '
                  + 'novo daqui -- ou desconecte a placa e conecte de novo, para '
                  + 'dar outra chance.',
  'rec-updating':   'A placa está no meio de uma atualização. Se nenhuma está '
                  + 'rodando aqui, ela foi interrompida: instale de novo.',
  'rec-pass':       'A placa tem senha: informe-a, para instalar.',
  'rec-install':    'Instalar o DigiMacro {fw}',
  'rec-no-fw':      'Esta página não tem DigiMacro para o núcleo desta placa.',
  'hd-pass':        'uma senha definida: sem ela, qualquer um pode gravar na '
                  + 'placa um firmware que a lê inteira',
  'hd-why-v2':      'Uma placa guarda seus segredos só na medida em que está '
                  + 'reforçada: a senha protege a tabela da página, e os bits de '
                  + 'trava e os fuses abaixo a protegem de um gravador. O '
                  + 'HARDENED_MODE.md explica cada item.',
  'hd-nag':         'Esta placa tem senha, então guarda algo que vale proteger '
                  + '-- e enquanto cada linha abaixo não estiver marcada, '
                  + 'qualquer um com um gravador consegue lê-la, senha e tudo.',
  'hd-selfprg':     'SELFPRGEN programado: a placa pode se atualizar',
  'hd-bod':         'detecção de queda de tensão ligada: uma alimentação fraca '
                  + 'não corrompe a flash que a placa grava',
  'auto-saved':     'Salvo: a placa digita isso sempre que for conectada.',
  'auto-saved-none': 'Salvo: a placa não digita nada ao ser conectada.',
  'auto-write-first': 'Grave as macros antes: a placa ainda não tem esta.',
  'serial-tag':     'série {serial}',
  'fw-link-move':   '(mudar para a v2…)',
  'fw-move-why':    'A placa roda o firmware {on} da v1; isto a muda para a v2, '
                  + 'DigiMacro {fw}. A v2 não tem bootloader: a placa se '
                  + 'atualiza daqui, pela '
                  + 'mesma conexão, e resiste a uma atualização interrompida. '
                  + 'Isto grava o núcleo da v2 pelo Micronucleus; a placa então '
                  + 'volta pedindo o DigiMacro {fw}, que é gravado por cima do '
                  + 'Micronucleus, e o Micronucleus some. As macros, a senha '
                  + 'e o número de série ficam. Uma queda de '
                  + 'energia nos primeiros instantes desse segundo passo precisa '
                  + 'de um gravador para consertar, como qualquer gravação pelo '
                  + 'Micronucleus.',
  'fw-moved':       'O núcleo da v2 está gravado. A placa está voltando e pedindo '
                  + 'o DigiMacro {fw}: instale-o abaixo.',
  'core-name':      'o núcleo da v2',
  'new-board':      'Preparar uma placa nova',
  'new-plug':       'Conecte a placa nova agora — desconectando-a antes, se já '
                  + 'estava — e escolha-a na lista do navegador assim que ela '
                  + 'aparecer: como "Digispark" ou "Micronucleus". Ela só espera '
                  + 'alguns segundos.',
  'new-none':       'O Micronucleus não foi escolhido, então nada foi gravado. '
                  + 'Conecte a placa de novo e recomece.',
  'new-done':       'O núcleo da v2 está gravado. Clique em Conectar e escolha a '
                  + 'placa: agora ela é um DigiMacro, pedindo o seu firmware.',
  'new-no-v2':      'Esta página não tem firmware v2 para preparar uma placa.',
  'hd-raw':         'Como o chip os lê: fuse baixo {low}, alto {high}, '
                  + 'estendido {ext}, trava {lock}.',
  close:            'Fechar',
  remove:           'remover',
  goto:             'ir para',
  'bk-no-pass':     'Esta página não sabe a senha da placa, então não consegue '
                  + 'cifrar a cópia. Desconecte a placa, conecte de novo e dê a '
                  + 'senha quando ela for pedida.',
  'backup-bad-password': 'Essa cópia guarda uma senha diferente daquela com que '
                  + 'foi aberta, então restaurá-la trancaria a placa para você. '
                  + 'Ela não foi feita por esta página; nada foi gravado.',
  'password-too-long': 'Isso passa de 12 bytes. Letras acentuadas ocupam dois '
                  + 'cada; encurte um pouco.',
  'wrong-magic':    'esse dispositivo não respondeu "DM"',
  'write-ok':       'Gravados {n} bytes, e todos lidos de volta.',
  'write-mismatch': 'divergência em {at}: gravado {wrote}, lido {read}',

  'lone-backslash': 'uma \\ sozinha no fim',
  'bad-escape':     '\\{what} não significa nada',
  unclosed:         'um { sem }',
  'empty-braces':   '{} está vazio',
  'pause-range':    '{{what}}: uma pausa vai de 0,1 a 25,5 segundos',
  'point-range':    '{{what}}: o ponteiro vai de 0 a 100, em porcentagem da tela',
  'point-scale':    '{{what}}: a escala vai de 0,75 a 3,75, em quartos',
  'repeat-range':   '{{what}}: uma repetição vai de 1 a 256',
  'group-empty':    '() não tem nada dentro',
  'nested-repeat':  'uma contagem dentro de um grupo: o firmware tem uma marca '
                  + 'só, então a contagem de dentro tomaria o lugar da de fora',
  'goto-nowhere':   '{{goto {what}}}: não existe macro com esse código',
  'ir-button':      '{{what}}: um botão do controle remoto é {IR} e dois bytes '
                  + 'em hexa, como em {IR 00 45}',
  'loop-empty':     '{what}: estas só pulam umas para as outras e nunca '
                  + 'digitam nem pausam, então o firmware desiste delas em vez '
                  + 'de executá-las',
  'ref-goto':       'continua em outra macro, e não volta',
  'nothing-to-rep': '{{what}}: não há nada para repetir ainda',
  'no-key-after':   '{{what}}: falta a tecla depois do +',
  'not-a-modifier': '"{what}" não é um modificador',
  'not-a-key':      '"{what}" não é uma tecla',
  'no-key-for':     'não há tecla para "{what}"',
  'needs-dead-keys':'"{what}" precisa de um teclado com acentos mortos — veja '
                  + 'a opção de teclado ao lado de Macros',
  'not-a-code':     'não é um código: "{what}"',
  'code-twice':     '"{what}" está sendo usado duas vezes',
  'ir-only':        'Não se digita no toque: só o controle remoto chega aqui, ou um {goto}. '
                  + 'Os códigos IR1 a IR30 são para macros só do controle.',
  'not-a-byte':     '"{what}" tem um byte que não é um byte',
  'has-a-zero':     '"{what}" contém um zero, que a encerraria antes da hora',
  'too-big':        '{n} bytes de tabela, e só cabem {fits}',
  'secret-no-key':  '{secret} precisa da chave, para cifrar o que vem depois',
  'secret-twice':   'um {secret} por macro: tudo depois do primeiro já é '
                  + 'secreto',
  'secret-in-group':'um {secret} dentro de um grupo: ele toma o resto da macro, '
                  + 'então não pode ser repetido',
  'secret-ir':      'um {IR} depois de um {secret}: o controle remoto não '
                  + 'acharia seu botão em texto cifrado',
  'secret-sealed-last': 'nada pode vir depois de um {secret} já cifrado: ele já '
                  + 'é o resto da macro',
  'secret-blob':    'um {secret} cifrado que não é um: foi alterado depois de '
                  + 'lido',
  'secret-too-long':'um segredo de {n} teclas: cabem no máximo {max}',
  'host-gone':      '{host} não existe mais: ponha {secret} no lugar dele, e o '
                  + 'resto da macro fica cifrado além de protegido',
  'secret-wrong-key':'esta não é a chave com que estes segredos foram cifrados',
  'secret-no-nonce':'nenhum nonce conseguiu cifrar este segredo — tente de novo',
},

};

let lang = 'en';

function setLang(l) { lang = STRINGS[l] ? l : 'en'; }
function getLang() { return lang; }

/* {name} is filled from the parameters. A language that has to decide
   something for itself -- a plural, usually -- gets a function instead. */
function t(key, p = {}) {
  const s = STRINGS[lang][key] ?? STRINGS.en[key];
  if (s === undefined) return key;
  if (typeof s === 'function') return s(p);
  return s.replace(/\{(\w+)\}/g, (m, k) => (k in p ? p[k] : m));
}

/* Everything with data-t in the markup, so the page can change language
   without being rebuilt. */
function applyLang(root) {
  (root || document).querySelectorAll('[data-t]').forEach(el => {
    el.textContent = t(el.dataset.t);
  });
  (root || document).querySelectorAll('[data-ph]').forEach(el => {
    el.placeholder = t(el.dataset.ph);
  });
  if (typeof document !== 'undefined')
    document.documentElement.lang = lang === 'pt' ? 'pt-BR' : 'en';
}

/* Portuguese if the browser lists it at all, even after English: a
   Brazilian's browser often says en-US first. Otherwise the first language
   there are strings for. */
function preferredLang(langs = typeof navigator !== 'undefined'
                                ? navigator.languages || [] : []) {
  const short = langs.map(l => l.toLowerCase().split('-')[0]);
  if (short.includes('pt')) return 'pt';
  return short.find(l => STRINGS[l]) || 'en';
}

if (typeof module !== 'undefined')
  module.exports = { STRINGS, setLang, getLang, t, applyLang, preferredLang };
