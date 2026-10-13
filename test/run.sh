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
echo "== v2's core, laid out for Micronucleus =="
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
echo "== WebUSB in place of WebHID, for Android: against a fake board =="
if command -v node >/dev/null 2>&1; then
    node webusb.js
else
    echo "node not found, skipped"
fi

echo
echo "== the page's own keyboard, held to what the board can type =="
if command -v node >/dev/null 2>&1; then
    node vkbd.js
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
echo "== the published page: one file, the firmware in it =="
if command -v node >/dev/null 2>&1; then
    node site.js
else
    echo "node not found, skipped"
fi

echo
echo "== ...and against the firmware's decoder =="
if command -v node >/dev/null 2>&1; then
    g++ -w -I. -I.. -o "$OUT"_hexplay hexplay.cpp
    node webtable.js --emit \
      | "$OUT"_hexplay . - .. .- -. -- ... ..- ...- ir0045 ir80c5 ir0145 ir0046 2>/dev/null \
      > "$OUT"_played
    if diff -u webtable.expected "$OUT"_played; then
        echo "web/table.js and DigiMacro.ino agree"
    else
        echo "the app encodes something the firmware plays differently"
        exit 1
    fi
    # Secrets: sealed by the page, opened by the firmware. Without the key
    # each stops where its secret starts; with it, it types what the same
    # macro in the clear does.
    CODES=". - .. .- -. ir0045"
    node webtable.js --emit-clear | "$OUT"_hexplay key=00000000000000000000000000000000 $CODES \
      2>/dev/null | sed 1d > "$OUT"_clear
    node webtable.js --emit-secret > "$OUT"_sealed
    KEY=$(tail -1 "$OUT"_sealed)
    head -1 "$OUT"_sealed | "$OUT"_hexplay $CODES $KEY $CODES 2>/dev/null > "$OUT"_opened
    if [ "$(sed -n 7p "$OUT"_opened)" = "key held" ] &&
       diff -u "$OUT"_clear <(sed 1,7d "$OUT"_opened) &&
       [ "$(head -6 "$OUT"_opened | tr '\n' '|')" = ". <00/18><00/16><00/08><00/15><00/2B>|- |.. <00/13><00/0F><00/04><00/0C><00/11><00/1B>|.- |-. <00/04><00/05><00/06><00/06><00/06><00/06>|ir0045 <00/1B>|" ]; then
        echo "secrets sealed by web/table.js open in DigiMacro.ino"
    else
        echo "a secret the page sealed does not play as it should"
        sed -n 1,7p "$OUT"_opened
        exit 1
    fi
else
    echo "node not found, skipped"
fi

echo
echo "== on a simulated board: secrets, xtea.S and all, and a hung app =="
SIMLIB=$(ls ../../simavr-softusb/simavr/simavr/obj-*/libsimavr.a 2>/dev/null | head -1)
if [ -n "$SIMLIB" ] && command -v node >/dev/null 2>&1; then
    if make -s -C .. simtest > "$OUT"_sim 2>&1 && tail -1 "$OUT"_sim | grep -qx PASS; then
        grep '^  \[' "$OUT"_sim | sed -n '/no key held at first/,$p'
        echo "all checks passed"
    else
        cat "$OUT"_sim
        echo "the simulated board fails a check"
        exit 1
    fi
else
    echo "../simavr-softusb not built (see its PLAN.md), skipped"
fi

echo
echo "== digimacro-host: PROVE first, and the key only to the real board =="
cc -w -I../tools/hostauth -o "$OUT"_hostclient hostclient.c && "$OUT"_hostclient 2>/dev/null | tail -1
"$OUT"_hostclient >/dev/null 2>&1 || exit 1
