#!/bin/sh
# packeur harness: four key scenarios through the real binary, headless.
# NOTE: octal \033 in printf — dash does not understand \x1b.
set -e
BIN="${1:-./cozy}"
fail() { echo "packeur harness: FAIL ($1)"; exit 1; }

# 1. EOF cancels
out=$(printf 'packeur\n' | "$BIN" 2>&1)
echo "$out" | grep -q "packeur: cancelled" || fail "EOF-cancel"

# 2. Enter with no toggles: applies, changes nothing
out=$(printf 'packeur\n\n' | "$BIN" 2>&1)
echo "$out" | grep -q "packeur: loaded 0; cleared 0" || fail "enter-no-changes"

# 3. space toggles the first package, Enter loads it
out=$(printf 'packeur\n \nwho\n' | "$BIN" 2>&1)
echo "$out" | grep -q "packeur: loaded 1; cleared 0" || fail "toggle-load"

# 4. q cancels even after a toggle
out=$(printf 'packeur\n q\n' | "$BIN" 2>&1)
echo "$out" | grep -q "packeur: cancelled" || fail "q-cancel"

echo "packeur harness: 4 scenarios OK"
