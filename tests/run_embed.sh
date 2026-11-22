#!/usr/bin/env bash
# The embedding library (cozy.h / embed.c): build libcozy.a, then compile and
# run the API test and the manual's example against it. The verdict is the
# exit code.
set -e
cd "$(dirname "$0")/.."
: "${CC:=cc}"
: "${STD:=c2x}"
make -s lib BACKEND="${BACKEND:-tier0}" OPTIM="${OPTIM:-none}" >/dev/null
EXTRA="${EMBED_LIBS:-}"
$CC -std=$STD -O1 -I. tests/embed_test.c build/libcozy.a -lm $EXTRA -o build/embed_test
./build/embed_test
$CC -std=c11 -O1 -I. doc/embed_example.c build/libcozy.a -lm $EXTRA -o build/embed_example
out=$(./build/embed_example)
echo "$out" | grep -q "inflation's response to a policy shock: -0.287729" || { echo "embed FAIL: the manual's example printed:"; echo "$out"; exit 1; }
echo "$out" | grep -q 'the model is "indeterminate"' || { echo "embed FAIL: the manual's example printed:"; echo "$out"; exit 1; }
echo "embed: the manual's example builds against build/libcozy.a and prints the closed form"
