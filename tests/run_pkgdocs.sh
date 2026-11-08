#!/usr/bin/env bash
# The docstring law: lint every package function's doc block and execute
# every "e.g. ... %= expected" example after loading its package.
set -u
cd "$(dirname "$0")/.."
[[ -x ./vmtest ]] || { echo "run_pkgdocs: ./vmtest not built" >&2; exit 2; }
exec python3 tests/verify_pkgdocs.py
