#!/usr/bin/env bash
# The tour must always play: demo() -> a replays every book problem, no errors.
set -e
cd "$(dirname "$0")/.."
out=$(printf 'load("packages/demo.cz")\ndemo()\na\n' | COZY_PLOT_TERM=ascii ./vmtest 2>&1)
echo "$out" | grep -q 'error' && { echo "demo: errors in the tour"; echo "$out" | grep error | head -3; exit 1; }
echo "$out" | grep -q 'same examples by construction' || { echo "demo: sync banner missing"; exit 1; }
echo "$out" | grep -q 'Tour complete' || { echo "demo: closing line missing"; exit 1; }
n=$(echo "$out" | grep -c '── Problem')
echo "demo: the tour plays clean — $n book problems replayed"
