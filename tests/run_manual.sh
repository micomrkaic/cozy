#!/usr/bin/env bash
# Verify that every REPL transcript in MANUAL.md matches the interpreter.
set -u
cd "$(dirname "$0")/.."
[[ -x ./vmtest ]] || { echo "run_manual: ./vmtest not built" >&2; exit 2; }
for src in *.c *.h; do
  [[ "$src" == repl.c || "$src" == main.c ]] && continue
  [[ -e "$src" && "$src" -nt ./vmtest ]] && { echo "run_manual: WARNING: $src newer than ./vmtest (stale binary?)" >&2; break; }
done
python3 tests/verify_manual.py MANUAL.md || exit 1
python3 tests/verify_manual.py PACKAGES.md || exit 1
python3 tests/verify_manual.py BOOK.md || exit 1
python3 tools/gen_book_index.py --check || exit 1

# stray-escape guard: the REPL renderer must consume markdown \| escapes
if ./cozy 2>/dev/null <<< manual | grep -q 'on\\|off'; then
  echo "manual render: stray backslash-pipe leaked"; exit 1
fi
echo "manual render: escapes clean"

# worked-example tables in PACKAGES.md must match the interpreter
python3 tools/gen_package_tables.py --check || exit 1

# structural lint: no duplicated problem numbers in any book
dups=$(grep -oE '\*\*Problem [0-9]+\.[0-9]+' BOOK.md | sort | uniq -d)
if [ -n "$dups" ]; then echo "BOOK.md: duplicated problem numbers: $dups"; exit 1; fi
# ...and numbering is consecutive from 1 within each chapter (gaps are the
# 9.9 bug: an inserted problem numbered by vibes; the demo replays the books,
# so the books' numbering is user-facing twice over)
python3 - <<'PYLINT'
import re, sys
book = open('BOOK.md').read()
chap, probs = None, {}
for m in re.finditer(r'^## (\d+)\. |^\*\*Problem (\d+\.\d+)', book, re.M):
    if m.group(1): chap = int(m.group(1))
    else: probs.setdefault(chap, []).append(int(m.group(2).split('.')[1]))
bad = [c for c, ns in probs.items() if ns != list(range(1, len(ns) + 1))]
if bad:
    print("BOOK.md: non-consecutive problem numbering in chapter(s):", bad); sys.exit(1)
PYLINT
if [ $? -ne 0 ]; then exit 1; fi
