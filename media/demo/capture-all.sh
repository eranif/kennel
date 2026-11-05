#!/bin/bash
# Takes all the screenshots of the promo and saves them as media/screenshots/*.jpg.
# Run media/demo/setup-demo.sh first, and apply media/demo/demo-hook.patch + build.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
OUT=$HERE/../screenshots
HOME_DIR=${DEMO_ROOT:-/tmp/kennel-demo}/home
FRESH=${DEMO_ROOT:-/tmp/kennel-demo}/home-fresh
TMP=$(mktemp -d)
shot() { # shot <name> <first-session|-> <mode|-> [text] [wait]
  "$HERE/capture.sh" "$HOME_DIR" "$2" "$3" "$TMP/$1.png" "${4:-}" "${5:-10}" >/dev/null
  sips -s format jpeg -s formatOptions 82 --resampleWidth 2560 "$TMP/$1.png" --out "$OUT/$1.jpg" >/dev/null
  echo "$1.jpg"
}
shot api-refactor api-refactor -
shot docs-site    docs-site    -
shot infra        infra        -
shot web          web          -
shot switcher     api-refactor switch
shot goto-anything api-refactor goto "new term"
shot explorer-api  api-refactor explorer
shot explorer-docs docs-site    explorer
shot review-buddy  api-refactor review "" 16
# The first start: a HOME with no agents (the scan is quick: take it right after the start).
APP=${KENNEL_APP:-$HERE/../../.build-release/kennel.app}
open -n --env HOME="$FRESH" "$APP"; sleep 1
PID=$(pgrep -n -x kennel)
read -r ID X Y < <(osascript -l JavaScript "$HERE/winpos.js" "$PID")
screencapture -x -o -l "$ID" "$TMP/first-scan.png"; kill "$PID"
sips -s format jpeg -s formatOptions 82 --resampleWidth 2560 "$TMP/first-scan.png" --out "$OUT/first-scan.jpg" >/dev/null
echo first-scan.jpg
