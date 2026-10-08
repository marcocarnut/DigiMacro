#!/usr/bin/env bash
# Compile DigiMacro.ino for the host, against stub hardware, and exercise it.
#
# It builds the *real* sketch -- the table walk, playback with its escapes, the
# morse decoding -- not a copy. Anything that does not need a USB bus or a timer
# can be tested here in a second, which beats reflashing a board to find out
# that an escape argument was read one byte early.
#
# Then the web side's own suites, the page's v2 updates against a fake board,
# and the v2 core itself, compiled for the host (core/test).
#
# The last suite crosses the language boundary: web/table.js encodes a table,
# the firmware plays it, and the result is compared with webtable.expected. The
# table is the interface between the two halves of the project, and a wrong
# header byte in the app looks exactly like a firmware bug from the outside.
set -euo pipefail
cd "$(dirname "$0")"
OUT="${TMPDIR:-/tmp}/digimacro"

echo "== table, playback and tapping =="
g++ -w -I. -I.. -o "$OUT"_test test.cpp
"$OUT"_test


# Both sensing methods, since they differ in polarity as well as in arithmetic
echo
echo "== touch pad, pull-up method =="
g++ -w -I. -I.. -o "$OUT"_touch touch_test.cpp
"$OUT"_touch

echo
echo "== touch pad, bleed-resistor method =="
g++ -w -I. -I.. -DTOUCH_PULLUP=0 -o "$OUT"_touch_rc touch_test.cpp
"$OUT"_touch_rc

echo
echo "== the app's macro language, on its own =="
if command -v node >/dev/null 2>&1; then
    node webtable.js
else
    echo "node not found, skipped"
fi

echo
echo "== the languages say the same things =="
if command -v node >/dev/null 2>&1; then
    node i18n.js
else
    echo "node not found, skipped"
fi

echo
echo "== v1 boards: firmware 9, laid out for Micronucleus =="
if command -v node >/dev/null 2>&1; then
    node micronucleus.js
else
    echo "node not found, skipped"
fi

echo
echo "== v2 updates: the page's side, against a fake board =="
if command -v node >/dev/null 2>&1; then
    node coreupdate.js
else
    echo "node not found, skipped"
fi

echo
echo "== the v2 core, and the app library over it =="
make -s -C ../core/test

echo
echo "== backups: scrypt, and only the right password opens one =="
if command -v node >/dev/null 2>&1; then
    node backup.js
else
    echo "node not found, skipped"
fi

echo
echo "== ...and against the firmware's decoder =="
if command -v node >/dev/null 2>&1; then
    g++ -w -I. -I.. -o "$OUT"_hexplay hexplay.cpp
    node webtable.js --emit \
      | "$OUT"_hexplay . - .. .- -. -- ... 2>/dev/null > "$OUT"_played
    if diff -u webtable.expected "$OUT"_played; then
        echo "web/table.js and DigiMacro.ino agree"
    else
        echo "the app encodes something the firmware plays differently"
        exit 1
    fi
else
    echo "node not found, skipped"
fi
