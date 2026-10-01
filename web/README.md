# Test pages

WebUSB needs a secure context, and `localhost` counts as one:

```
cd web && python3 -m http.server 8000
```

then, in Chrome or Edge:

| | |
|---|---|
| <http://localhost:8000/app.html> | **the app**: edit the macros and write them |
| <http://localhost:8000/eeprom.html> | the low-level tool: dump, write and verify raw bytes |
| <http://localhost:8000/spike.html> | the original derisking page: connect and ask INFO |

`table.js` turns macro text into the bytes the firmware plays, and back again
so a board's table can be edited rather than retyped. It is shared with
`test/run.sh`, which checks the round trip and then plays what it encodes
against the real firmware — so the app's encoder and the firmware's decoder are
checked against each other rather than each being trusted alone.

`i18n.js` holds every string the app says, in English and Brazilian
Portuguese, and `test/i18n.js` checks that the two agree on which strings
exist. The other two pages are developer tools and stay in English.

**`python3 -m http.server` sends no cache headers**, so Chrome applies its own
heuristic freshness and will happily keep running an old `table.js` after you
edit it. If a change seems to have no effect, hard-reload with
<kbd>Ctrl</kbd>+<kbd>Shift</kbd>+<kbd>R</kbd>. It cost me twenty minutes of
looking for a bug that was not there.

`eeprom.html` also has a bounds test: a read that runs off the end should come
back short, and a write that would cross the end should be refused. Both are
about what sits above the table, which is the password.

The board types its pad readings twice a second in keyboard mode (`TOUCH_DEBUG`
in `spike/spike.ino`), so open a text editor first and check the numbers move
when you touch it. `F8` that never changes means nothing is charging the pad;
`00` means it charges faster than one loop pass. Only once the readings respond
can a four-second hold mean anything.

Hold the pad four seconds so the board is in vendor mode before connecting —
in keyboard mode it is `16c0:27db` and the filter will not offer it, which is
itself part of what the spike is checking.

On Linux you may need a udev rule to reach the device without root:

```
SUBSYSTEM=="usb", ATTR{idVendor}=="16c0", ATTR{idProduct}=="05dc", MODE="0660", TAG+="uaccess"
```

in `/etc/udev/rules.d/60-digimacro.rules`, then `sudo udevadm control --reload`.

## The second spike

`spikehid/` asks whether the mode switch is needed at all. Flash it with
`./build.sh spikehid upload` and open <http://localhost:8000/hid.html>.

There is no pad to hold: the device stays a keyboard the whole time, and the
page talks to a second top-level collection on vendor usage page `0xFF00`,
which is what WebHID will hand over. Touch the pad first and check it types a
line — that is the half of the question about report IDs. **3350 bytes**, less
than the first spike, with the EEPROM protocol included.

Its write test uses the reserved area at `0x1E0`, so a macro table on the board
survives it.
