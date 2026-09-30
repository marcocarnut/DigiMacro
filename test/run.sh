#!/usr/bin/env bash
# Compile DigiMacro.ino for the host, against stub hardware, and exercise it.
#
# It builds the *real* sketch -- the table walk, playback with its escapes,
# the morse decoding -- not a copy. Anything that does not need a USB bus or a
# timer can be tested here in a second, which beats reflashing a board to find
# out that an escape argument was read one byte early.
set -euo pipefail
cd "$(dirname "$0")"
OUT="${TMPDIR:-/tmp}/digimacro"

echo "== table, playback and tapping =="
g++ -w -I. -I.. -o "$OUT"_test test.cpp
"$OUT"_test

echo
echo "== the sample table tools/loadtable flashes =="
g++ -w -I. -I.. -o "$OUT"_table table_test.cpp
"$OUT"_table

# Both sensing methods, since they differ in polarity as well as in arithmetic
echo
echo "== touch pad, pull-up method =="
g++ -w -I. -I.. -o "$OUT"_touch touch_test.cpp
"$OUT"_touch

echo
echo "== touch pad, bleed-resistor method =="
g++ -w -I. -I.. -DTOUCH_PULLUP=0 -o "$OUT"_touch_rc touch_test.cpp
exec "$OUT"_touch_rc
