#!/bin/bash
# Takes one screenshot of the real Kennel running on the demo environment.
#   capture.sh <home> <first-session|-> <demo-mode|-> <out.png> [demo-text] [wait-seconds]
# demo-mode needs the demo hook (media/demo/demo-hook.patch): switch | goto | explorer | review
# Needs the Screen Recording permission for the terminal that runs it.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
APP=${KENNEL_APP:-$HERE/../../.build-release/kennel.app}
HOME_DIR=$1; FIRST=$2; MODE=$3; OUT=$4; TXT=${5:-}; WAIT=${6:-9}

if [ "$FIRST" != "-" ]; then   # the session that is selected at the start = the first of "recent"
python3 - "$HOME_DIR" "$FIRST" <<'PY'
import sys, json
home, name = sys.argv[1:3]
p = home + '/.kennel/workspace.json'
w = json.load(open(p)); ws = w['workspace']
first = [r for r in ws['recent'] if r['name'] == name]
ws['recent'] = first + [r for r in ws['recent'] if r['name'] != name]
json.dump(w, open(p, 'w'))
PY
fi

ENVARGS=(--env HOME="$HOME_DIR")
[ "$MODE" != "-" ] && ENVARGS+=(--env KENNEL_DEMO="$MODE")
[ -n "$TXT" ] && ENVARGS+=(--env KENNEL_DEMO_TEXT="$TXT")
open -n "${ENVARGS[@]}" "$APP"
sleep "$WAIT"
PID=$(pgrep -n -x kennel)
read -r ID X Y < <(osascript -l JavaScript "$HERE/winpos.js" "$PID")
if [ "$MODE" = "-" ]; then
  screencapture -x -o -l "$ID" "$OUT"            # only the window
else
  screencapture -x -R"$X,$Y,2000,1128" "$OUT"    # the window and the dialog on top of it
fi
kill "$PID"; sleep 1
echo "$OUT"
