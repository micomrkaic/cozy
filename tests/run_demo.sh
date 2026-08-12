#!/usr/bin/env bash
# The tour must always play: demo() -> a replays every book problem, no errors.
set -e
cd "$(dirname "$0")/.."
out=$(printf 'load("packages/demo.cz")\ndemo()\na\n' | COZY_PLOT_TERM=ascii ./vmtest 2>&1)
echo "$out" | grep -q 'error' && { echo "demo: errors in the tour"; echo "$out" | grep error | head -3; exit 1; }
echo "$out" | grep -q 'same examples by construction' || { echo "demo: sync banner missing"; exit 1; }
echo "$out" | grep -q 'Tour complete' || { echo "demo: closing line missing"; exit 1; }
n=$(echo "$out" | grep -c '── Problem')
# the interaction paths themselves, through the REAL repl binary:
# Enter-steps through a section, and the guided tour picks and quits
s9=$(printf 'load("packages/demo.cz")\ndemo9\n\n\n\n' | COZY_PLOT_TERM=ascii timeout 60 ./cozy 2>&1 | grep -c '── Problem 9') || true
[ "$s9" = "3" ] || { echo "demo: Enter-stepping broken (played $s9 of 3)"; exit 1; }
g9=$(printf 'load("packages/demo.cz")\ndemo()\n9\nq\n' | COZY_PLOT_TERM=ascii timeout 60 ./cozy 2>&1 | grep -c '── Problem 9') || true
[ "$g9" = "3" ] || { echo "demo: guided tour broken (played $g9 of 3)"; exit 1; }
echo "demo: the tour plays clean — $n book problems replayed; stepping and guided paths OK"
