#!/usr/bin/env bash
# Note on tag listing: versions past 1.9 sort wrongly as strings (v1.10 before
# v1.9). One-time fix in this clone:  git config tag.sort version:refname
# (and 'sort -V' for shell work on release files).
# deploy.sh — release a Cozy tarball to GitHub, tagged with its version.
#   usage: ./deploy.sh ~/Downloads/cozy-vX.Y.Z.tar.gz [--no-test]
# Steps: extract the tarball's version, untar over this repo, build and run
# the full test suite, commit, push, and tag vX.Y.Z (same as version.h).
set -euo pipefail

TARBALL="${1:?usage: ./deploy.sh path/to/cozy-vX.Y.Z.tar.gz [--no-test]}"
RUN_TESTS=1
[[ "${2:-}" == "--no-test" ]] && RUN_TESTS=0

[[ -f "$TARBALL" ]] || { echo "deploy: no such file: $TARBALL" >&2; exit 1; }
[[ -d .git ]] || { echo "deploy: run from the repo root (no .git here)" >&2; exit 1; }

# 0. Read the version out of the tarball BEFORE touching the tree.
VERSION=$(tar xzf "$TARBALL" -O cozy/version.h | sed -n 's/.*COZY_VERSION "\([0-9.]*\)".*/\1/p')
[[ -n "$VERSION" ]] || { echo "deploy: could not read COZY_VERSION from the tarball" >&2; exit 1; }
TAG="v$VERSION"
echo "deploy: releasing $TAG"

if git rev-parse "$TAG" >/dev/null 2>&1; then
    echo "deploy: tag $TAG already exists — bump the version in the tarball first" >&2
    exit 1
fi

# 1. Make the working tree EXACTLY the snapshot. The tarball is the complete
# intended state (PLAYBOOK law), so files present here but absent from it are
# deleted — a plain overlay never deletes, which left .nu ghosts beside their
# renamed .cz files at v0.0.10 and failed the suite (LESSONS: the overlay
# that could not delete). .git is preserved.
TMPD=$(mktemp -d)
tar xzf "$TARBALL" --strip-components=1 -C "$TMPD"
rsync -a --delete --exclude='.git' "$TMPD"/ .
rm -rf "$TMPD"
echo "deploy: tree synced to snapshot in $(pwd)"

# 2. Build and verify before anything touches the remote.
if [[ $RUN_TESTS == 1 ]]; then
    # ---- best-available native build (owner's ruling, 0.1.1): deploy
    # detects backends and assembles the fastest configuration this
    # machine supports, flagging every fallback with its remedy. -------
    BACKEND=tier0
    if [ "$(uname -s)" = "Darwin" ]; then
        BACKEND=accelerate
    elif echo 'int main(void){return 0;}' | cc -x c - -lopenblas -o /dev/null 2>/dev/null; then
        BACKEND=openblas
    else
        echo "deploy: NOTE — no OpenBLAS found; building tier0 (hand-rolled kernels)."
        echo "        For LAPACK speed: sudo apt install libopenblas-dev"
    fi
    OPTIM=none
    if echo '#include <nlopt.h>' | cc -E -xc - >/dev/null 2>&1; then
        OPTIM=nlopt
    else
        echo "deploy: NOTE — no NLopt found; optimization runs the pure tier0 path."
        echo "        For the professional backend: sudo apt install libnlopt-dev  (macOS: brew install nlopt)"
    fi
    echo "deploy: building BACKEND=$BACKEND OPTIM=$OPTIM"
    make clean >/dev/null && make BACKEND="$BACKEND" OPTIM="$OPTIM" -j"$(nproc 2>/dev/null || sysctl -n hw.ncpu)" >/dev/null
    make BACKEND="$BACKEND" OPTIM="$OPTIM" test
    make test-asan
    echo "deploy: all tests green"
else
    echo "deploy: tests SKIPPED (--no-test)"
fi

# 3. Commit, push, tag.
git add -A
if git diff --cached --quiet; then
    echo "deploy: nothing to commit (tree already at this state)"
else
    git commit -m "release $TAG"
fi
# Two-workstation safety: releases are full snapshots, so if the remote is
# ahead (a deploy from another machine), the correct resolution is a merge
# with strategy "ours" — record the remote history, keep this tree byte-
# for-byte (its content supersedes all earlier snapshots). Any work unique
# to another machine that never entered a tarball would be discarded by
# this; keep unique work out of the release repo.
if ! git push origin main 2>/dev/null; then
  echo "deploy: remote is ahead (another machine deployed); merging with -s ours"
  git fetch origin
  git log --oneline HEAD..origin/main | sed 's/^/deploy:   superseding /'
  git merge -s ours origin/main -m "merge remote history (superseded by $VERSION snapshot)"
  git push origin main
fi
git tag -a "$TAG" -m "Cozy $VERSION"
git push origin "$TAG"

echo "deploy: done — $TAG is live (GitHub Pages redeploys docs/ automatically)"
