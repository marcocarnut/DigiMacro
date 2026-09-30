#!/usr/bin/env bash
# Build (and optionally flash) a DigiMacro sketch.
#
#   ./build.sh spike [upload]     the mode-switch derisking spike
#   ./build.sh spike isp          ...against the whole 8192 bytes
#
# Needs arduino-cli and the Digistump AVR core; see DigiMacroCDC/build.sh for
# the installation lines.
set -euo pipefail
cd "$(dirname "$0")"

FQBN="digistump:avr:digispark-tiny:clock=clock165"
SKETCH=spike
ISP=0
DO_UPLOAD=0
for a in "$@"; do
    case "$a" in
        spike)  SKETCH=spike ;;
        isp)    ISP=1 ;;
        upload) DO_UPLOAD=1 ;;
        *)      echo "unknown argument: $a" >&2; exit 1 ;;
    esac
done

CLI="$(command -v arduino-cli || echo "$HOME/.cache/digimacro/arduino-cli")"
[ -x "$CLI" ] || { echo "arduino-cli not found" >&2; exit 1; }

EXTRA=()
[ "$ISP" = 1 ] && EXTRA=(--build-property "upload.maximum_size=8192")

"$CLI" compile --fqbn "$FQBN" --libraries . --output-dir "$SKETCH/build" \
    "${EXTRA[@]}" "$SKETCH"

if [ "$DO_UPLOAD" = 1 ]; then
    echo
    echo "Unplug the Digispark now; plug it back in when micronucleus asks."
    "$CLI" upload --fqbn "$FQBN" --input-dir "$SKETCH/build" "$SKETCH"
fi
