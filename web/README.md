# Spike test page

WebUSB needs a secure context, and `localhost` counts as one:

```
cd web && python3 -m http.server 8000
```

then open <http://localhost:8000/spike.html> in Chrome or Edge.

Hold the pad four seconds so the board is in vendor mode before connecting —
in keyboard mode it is `16c0:27db` and the filter will not offer it, which is
itself part of what the spike is checking.

On Linux you may need a udev rule to reach the device without root:

```
SUBSYSTEM=="usb", ATTR{idVendor}=="16c0", ATTR{idProduct}=="05dc", MODE="0660", TAG+="uaccess"
```

in `/etc/udev/rules.d/60-digimacro.rules`, then `sudo udevadm control --reload`.
