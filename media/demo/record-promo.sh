#!/bin/bash
# Records media/promo/kennel-promo.html playing in Chrome and saves media/promo/kennel-promo.mp4.
# Needs the Screen Recording permission and Google Chrome. Do not use the mouse or the keyboard
# for the next 80 seconds, keep the window in front, and close windows with private content
# that could be behind it (the recording is a rectangle of the screen).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
PROMO=$HERE/../promo
CHROME="/Applications/Google Chrome.app"
TMP=$(mktemp -d)
now_ms() { python3 -c 'import time; print(int(time.time() * 1000))'; }
pkill -f "user-data-dir=/tmp/chrome-rec" 2>/dev/null || true
rm -rf /tmp/chrome-rec

# The page starts its clock at START (epoch ms) and shows its first frame until then.
START=$(( $(now_ms) + 12000 ))
open -na "$CHROME" --args --user-data-dir=/tmp/chrome-rec --no-first-run \
  --app="file://$PROMO/kennel-promo.html#rec@$START" \
  --window-size=1280,748 --window-position=60,60

# Wait until the window is up (a window of the process that is 1280 points wide).
ID=""
for _ in $(seq 1 40); do
  sleep 0.5
  PID=$(pgrep -f "user-data-dir=/tmp/chrome-rec" | head -1 || true)
  [ -n "$PID" ] || continue
  if OUT=$(osascript -l JavaScript "$HERE/winpos.js" "$PID" 2>/dev/null) && [ -n "$OUT" ]; then
    read -r ID X Y <<<"$OUT"
    break
  fi
done
[ -n "$ID" ] || { echo "the Chrome window did not appear"; exit 1; }
sleep 1.5   # the page is drawn

osascript -l JavaScript "$HERE/park-mouse.js" >/dev/null 2>&1 || true   # the pointer shows in the video
T0=$(now_ms)
LEAD_MS=$(( START - T0 ))                      # how long the recording runs before the page starts
[ "$LEAD_MS" -gt 2500 ] || { echo "too late: the window was slow to start"; exit 1; }
SECONDS_TO_RECORD=$(( LEAD_MS / 1000 + 64 ))
# The window is 1280x748 points: 28 points of title bar, then the 1280x720 page.
screencapture -x -v -V "$SECONDS_TO_RECORD" -R"$X,$((Y + 28)),1280,720" "$TMP/raw.mov"
pkill -f "user-data-dir=/tmp/chrome-rec" || true

# screencapture needs about 0.6 seconds to start; keep 0.8 seconds of the first frame.
TRIM=$(python3 -c "print(max(0, $LEAD_MS / 1000 - 0.6 - 0.8))")
swift "$HERE/trim-video.swift" "$TMP/raw.mov" "$PROMO/kennel-promo.mp4" "$TRIM" 61 3000 1920 1080 2>&1 | grep -E "^(ok|failed|cannot)"
ls -la "$PROMO/kennel-promo.mp4"
