#!/usr/bin/env python3
"""compare_check.py — verify the Python and Octave transcripts of COMPARE.md.

COMPARE.md sets Cozy beside NumPy and Octave. Its Cozy transcripts are part
of `make test` (tests/verify_manual.py). This script runs the other two
sides, so that a claim about what NumPy raises or what Octave prints is a
thing that was executed, not remembered:

  ```python   doctest transcripts (>>> and ...), one session for the whole
              document, compared with ELLIPSIS and NORMALIZE_WHITESPACE
  ```octave   lines beginning ">> " are fed to one Octave process; the
              text under each is what it must print (whitespace-insensitive)

It is `make compare`, not part of `make test`: the language's own suite
must not depend on NumPy, SciPy and Octave being installed, nor on their
versions. A missing tool is announced and counted, never silently passed
over. The verdict is the exit code.

    python3 tools/compare_check.py [COMPARE.md]
"""
import doctest
import os
import re
import shutil
import subprocess
import sys

os.chdir(os.path.join(os.path.dirname(os.path.abspath(__file__)), ".."))
doc = sys.argv[1] if len(sys.argv) > 1 else "COMPARE.md"
text = open(doc).read()
blocks = re.findall(r"```([a-z-]*)\n(.*?)```", text, re.S)
SENTINEL = "@@S@@"
bad = 0
skipped = []


def squash(s):
    """Whitespace-insensitive form: Octave pads its output, a document does not have to."""
    return "\n".join(" ".join(line.split()) for line in s.strip().split("\n") if line.strip())


# ---- Python --------------------------------------------------------------------
py = [b for tag, b in blocks if tag == "python"]
try:
    import numpy
    import scipy
    have_py = True
except ImportError as e:
    have_py = False
    skipped.append(f"python ({e.name} is not installed)")

if have_py:
    flags = doctest.ELLIPSIS | doctest.NORMALIZE_WHITESPACE
    parser = doctest.DocTestParser()
    runner = doctest.DocTestRunner(optionflags=flags, verbose=False)
    globs = {}
    report = []
    n_py = 0
    for k, b in enumerate(py):
        test = parser.get_doctest(b, {}, f"{doc} python block {k + 1}", doc, 0)
        test.globs = globs                    # one session: a DocTest would otherwise take a copy
        n_py += len(test.examples)
        runner.run(test, out=report.append, clear_globs=False)
    if runner.failures:
        bad += runner.failures
        print("".join(report).rstrip())
    print(f"{doc}: {n_py - runner.failures} of {n_py} python examples verified "
          f"(NumPy {numpy.__version__}, SciPy {scipy.__version__})")

# ---- Octave --------------------------------------------------------------------
entries = []
for tag, b in blocks:
    if tag != "octave":
        continue
    lines = b.rstrip("\n").split("\n")
    i = 0
    while i < len(lines):
        if lines[i].startswith(">> "):
            inp, exp = lines[i][3:], []
            i += 1
            while i < len(lines) and not lines[i].startswith(">> "):
                exp.append(lines[i])
                i += 1
            entries.append((inp, "\n".join(exp)))
        else:
            i += 1

octave = shutil.which("octave")
if not octave:
    skipped.append("octave (not on PATH)")
elif entries:
    prog = "".join(f'{inp}\ndisp("{SENTINEL}")\n' for inp, _ in entries)
    r = subprocess.run([octave, "--no-gui", "--quiet", "--no-init-file", "--no-history"],
                       input=prog, capture_output=True, text=True, timeout=300)
    parts = r.stdout.split(SENTINEL)
    ok = 0
    for k, (inp, exp) in enumerate(entries):
        got = parts[k] if k < len(parts) - 1 else "<nothing: octave stopped before this line>"
        if squash(got) == squash(exp):
            ok += 1
        else:
            bad += 1
            print(f"OCTAVE MISMATCH: {inp}\n  document: {squash(exp)!r}\n  octave  : {squash(got)!r}")
    if ok < len(entries) and r.stderr.strip():
        print("octave said:\n  " + "\n  ".join(r.stderr.strip().split("\n")[:8]))
    v = subprocess.run([octave, "--version"], capture_output=True, text=True).stdout.split("\n")[0]
    print(f"{doc}: {ok} of {len(entries)} octave examples verified ({v.strip()})")

for s in skipped:
    print(f"{doc}: SKIPPED {s} -- those transcripts were not checked")
sys.exit(1 if bad else 0)
