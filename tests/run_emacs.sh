#!/usr/bin/env bash
# Emacs-mode batch tests; skips politely when emacs is unavailable.
cd "$(dirname "$0")/.."
# The drift check is pure python: it must run EVERYWHERE, or it is a dead
# guard in any emacs-less environment (which it was, 0.0.12–0.0.16 — the
# primary dev container lacks emacs; the owner's X1 caught the drift).
# Only the byte-compile smoke test below needs emacs itself.
python3 tools/gen_emacs_mode.py --check || exit 1
command -v emacs >/dev/null 2>&1 || { echo "emacs-mode: emacs not installed, skipping batch test"; exit 0; }
emacs --batch -Q -l tests/run_emacs.el 2>&1 | grep '^emacs-mode' || exit 1
