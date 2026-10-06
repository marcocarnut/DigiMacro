# Test pages

WebHID needs a secure context, and `localhost` counts as one:

```
cd web && python3 -m http.server 8000
```

then, in Chrome or Edge:

| | |
|---|---|
| <http://localhost:8000/app.html> | **the app**: edit the macros and write them |
| <http://localhost:8000/hid.html> | the spike's own page: INFO, a dump, a write read back |

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

The board can type its pad readings twice a second (`./build.sh touchdebug`),
so open a text editor first and check the numbers move when you touch it. `F8` that never changes means nothing is charging the pad;
`00` means it charges faster than one loop pass. Only once the readings respond
can a four-second hold mean anything.

The board is `16c0:27db` and stays a keyboard throughout; connect whenever you
like, and hold the pad for four seconds when the app asks, which opens the
window during which the table may be read and written.

On Linux the board needs a udev rule to be reachable without root. It is in
`60-digimacro.rules`:

```
sudo cp web/60-digimacro.rules /etc/udev/rules.d/
sudo udevadm control --reload
```

then unplug the board and plug it back in.

**WebHID fails without it.** Every `/dev/hidraw`
node on Linux is root-only by default, so Chrome reports only `Failed to open
the device` — which reads like a browser problem and is a file permission.
`ls -l /dev/hidraw*` will show which node is the board, and a `+` on the mode
means the rule took. Note `ATTRS` rather than `ATTR` in that line: the vendor
and product ids belong to the USB device, which is the hidraw node's
grandparent, and a rule written with `ATTR` matches nothing and says nothing.

Windows needs none of this: HID devices are openable as they are, which is the
same reason WebHID wants no MS OS 2.0 descriptor.

**Updating the firmware is the exception, on both.** The board restarts into
Micronucleus, which is plain USB rather than HID, so the app reaches it over
WebUSB. On Linux that needs the rule's last line; on Windows it needs a driver
once, WinUSB installed with [Zadig](https://zadig.akeo.ie/) while the board is
in its bootloader.

## The spike

`spikehid/` is what showed the mode switch was not needed. Flash it with
`./build.sh spikehid upload` and open <http://localhost:8000/hid.html>.

There is no pad to hold: the device stays a keyboard the whole time, and the
page talks to a second top-level collection on vendor usage page `0xFF00`,
which is what WebHID will hand over. Touch the pad first and check it types a
line — that is the half of the question about report IDs. **3350 bytes**, less
than the first spike, with the EEPROM protocol included.

Its write test uses the reserved area at `0x1E0`, so a macro table on the board
survives it.
