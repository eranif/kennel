#!/usr/bin/env bash
# Regenerates assets/kennel.icns from assets/kennel.svg (macOS only).
#
# The .icns is a committed build artifact — run this only when kennel.svg
# changes, then commit the updated kennel.icns. Requires macOS `sips` and
# `iconutil` (both ship with the OS).
set -euo pipefail

here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
svg="$here/kennel.svg"
icns="$here/kennel.icns"
iconset="$(mktemp -d)/kennel.iconset"
mkdir -p "$iconset"

for sz in 16 32 128 256 512; do
  sips -s format png -z "$sz" "$sz" "$svg" --out "$iconset/icon_${sz}x${sz}.png" >/dev/null
  dbl=$((sz * 2))
  sips -s format png -z "$dbl" "$dbl" "$svg" --out "$iconset/icon_${sz}x${sz}@2x.png" >/dev/null
done

iconutil -c icns "$iconset" -o "$icns"
echo "Wrote $icns"
