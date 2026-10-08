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
#   spikehid    build spikehid/ instead -- the second spike: a keyboard that
#               stays a keyboard, with a vendor collection WebHID can reach
#   loadtable   build tools/loadtable/ instead -- writes a sample table into
#               EEPROM so playback can be tested before the app exists
#
# Needs arduino-cli and the Digistump AVR core; see DigiMacroCDC/build.sh for
# the installation lines.
set -euo pipefail
cd "$(dirname "$0")"

FQBN="digistump:avr:digispark-tiny:clock=clock165"
SKETCH=.
ISP=0
DO_UPLOAD=0
RELEASE=0
BUTTON=0
RC=0
LEDECHO=0
TOUCHDEBUG=0
for a in "$@"; do
    case "$a" in
        spikehid)  SKETCH=spikehid ;;
        loadtable) SKETCH=tools/loadtable ;;
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

if [ "$RELEASE" = 1 ] && { [ "$SKETCH" != . ] || [ "$ISP$BUTTON$RC$LEDECHO$TOUCHDEBUG" != 00000 ]; }; then
    echo "release is the stock firmware, so it takes no other options" >&2; exit 1
fi

if [ "$RC" = 1 ] && [ "$BUTTON" = 1 ]; then
    echo "rc is about the pad, so it does not go with button" >&2; exit 1
fi

CLI="$(command -v arduino-cli || echo "$HOME/.cache/digimacro/arduino-cli")"
[ -x "$CLI" ] || { echo "arduino-cli not found" >&2; exit 1; }

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
BUILD="${SKETCH%/}/build"
[ "$SKETCH" = "." ] && BUILD=build
"$CLI" compile --fqbn "$FQBN" --libraries . --output-dir "$BUILD" \
    "${EXTRA[@]}" "$SKETCH"

if [ "$ISP" = 1 ]; then
    echo
    echo "Flash $BUILD/*.hex with your programmer."
    echo "Keep the fuses as they are, and check EESAVE (high fuse bit 3): left"
    echo "unprogrammed, as a Digispark ships, a chip erase takes the macro"
    echo "table and the password with it on every reflash."
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
    "$CLI" upload --fqbn "$FQBN" --input-dir "$BUILD" "$SKETCH"
fi
