#!/usr/bin/env bash
# Build (and optionally flash) DigiMacro.
#
# Arguments combine freely, in any order:
#
#   (none)      the firmware: morse in, keystrokes out, a four-second hold
#               for the app
#   upload      compile, then flash over USB with micronucleus
#   button      take the input from a pushbutton (P0 to ground) instead of
#               a touch pad
#   rc          read the pad with a bleed resistor rather than the pull-up
#   ledecho     LED follows the input while tapping -- for working on the
#               input path only, since it shows a bystander the code
#   touchdebug  type the pad readings twice a second, to see what the input
#               is actually doing: raw, average, touched?, press open?
#   isp         build for a chip with no bootloader, as HARDENED_MODE.md
#               describes: all 8192 bytes, the clock calibrated from USB
#               since no bootloader has done it, no REBOOT, and a .hex to
#               feed to a programmer
#   release     the firmware as it ships, copied into web/firmware/ with its
#               version, which is what the app's "Update the firmware" writes.
#               Commit the result: the page carries it, so serving the page
#               needs no AVR toolchain.
#
# Needs arduino-cli and the Digistump AVR core; it says how to get them if
# either is missing.
set -euo pipefail
cd "$(dirname "$0")"

FQBN="digistump:avr:digispark-tiny:clock=clock165"
ISP=0
DO_UPLOAD=0
RELEASE=0
BUTTON=0
RC=0
LEDECHO=0
TOUCHDEBUG=0
for a in "$@"; do
    case "$a" in
        button)    BUTTON=1 ;;
        rc)        RC=1 ;;
        ledecho)   LEDECHO=1 ;;
        touchdebug) TOUCHDEBUG=1 ;;
        isp)       ISP=1 ;;
        upload)    DO_UPLOAD=1 ;;
        release)   RELEASE=1 ;;
        *)         echo "unknown argument: $a" >&2; exit 1 ;;
    esac
done

if [ "$RELEASE" = 1 ] && [ "$ISP$BUTTON$RC$LEDECHO$TOUCHDEBUG" != 00000 ]; then
    echo "release is the stock firmware, so it takes no other options" >&2; exit 1
fi

if [ "$RC" = 1 ] && [ "$BUTTON" = 1 ]; then
    echo "rc is about the pad, so it does not go with button" >&2; exit 1
fi

if [ "$ISP" = 1 ] && [ "$DO_UPLOAD" = 1 ]; then
    echo "isp is for a programmer, not for micronucleus: build it, then see" >&2
    echo "HARDENED_MODE.md" >&2; exit 1
fi

DIGISTUMP_INDEX=https://raw.githubusercontent.com/ArminJo/DigistumpArduino/master/package_digistump_index.json
CLI="$(command -v arduino-cli || echo "$HOME/.cache/digimacro/arduino-cli")"
if [ ! -x "$CLI" ]; then
    cat >&2 <<MSG
arduino-cli was not found. Install it from

    https://arduino.github.io/arduino-cli/latest/installation/

then add the core this is built against. The index is ArminJo's fork, the
original digistump.com one having been dead for years:

    arduino-cli config add board_manager.additional_urls $DIGISTUMP_INDEX
    arduino-cli core update-index
    arduino-cli core install digistump:avr

The Digistump core is deprecated upstream in favour of ATTinyCore. This
project has not been moved to it or tested there.
MSG
    exit 1
fi

FLAGS=""
[ "$BUTTON" = 1 ]  && FLAGS="$FLAGS -DHAVE_TOUCH=0"
[ "$RC" = 1 ]      && FLAGS="$FLAGS -DTOUCH_PULLUP=0"
[ "$LEDECHO" = 1 ] && FLAGS="$FLAGS -DLED_ECHO=1"
[ "$TOUCHDEBUG" = 1 ] && FLAGS="$FLAGS -DTOUCH_DEBUG=1"
[ "$ISP" = 1 ]     && FLAGS="$FLAGS -DDM_NO_BOOTLOADER=1"

EXTRA=()
[ -n "$FLAGS" ] && EXTRA=(--build-property "build.extra_flags=${FLAGS# }")
[ "$ISP" = 1 ]  && EXTRA+=(--build-property "upload.maximum_size=8192")

# --libraries . makes DigiHidUSB/ visible without installing it into
# ~/Arduino/libraries, so the whole project stays in this directory.
# --output-dir pins the artefacts somewhere upload can find them without
# having to resolve the sketch again (upload takes neither flag).
BUILD=build
"$CLI" compile --fqbn "$FQBN" --libraries . --output-dir "$BUILD" \
    "${EXTRA[@]}" .

if [ "$ISP" = 1 ]; then
    echo
    echo "Flash $BUILD/DigiMacro.ino.hex with a programmer, and set the fuses,"
    echo "as HARDENED_MODE.md says: low 0xE1, high 0xDD, extended 0xFE. High"
    echo "0xDD leaves EESAVE unprogrammed, so a chip erase takes the EEPROM with"
    echo "it -- which is the point. Lock it with 0xFC only once it is set up."
fi

if [ "$RELEASE" = 1 ]; then
    FW=$(sed -n 's/^#define DM_FW_VERSION *\([0-9]*\).*/\1/p' DigiHidUSB/src/hidusb.h)
    mkdir -p web/firmware
    cp "$BUILD/DigiMacro.ino.hex" web/firmware/digimacro.hex
    printf '{ "fw": %s }\n' "$FW" > web/firmware/digimacro.json
    echo
    echo "Firmware $FW is in web/firmware/; commit it, and the app offers it."
fi

if [ "$DO_UPLOAD" = 1 ]; then
    echo
    echo "Unplug the Digispark now; plug it back in when micronucleus asks."
    "$CLI" upload --fqbn "$FQBN" --input-dir "$BUILD" .
fi
