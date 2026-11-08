#!/usr/bin/env python3
"""The docstring law for packages: every `let NAME = fn` in packages/*.cz
carries a comment block directly above it ('%' or '#' lines, no blank line
between) — help(NAME) renders it in the builtins' format. This script
(1) LINTS: each such definition has a block with a description line and
    at least one "e.g." line (helpers may instead carry a block whose
    first line starts with "internal");
(2) VERIFIES: every "e.g. EXPR %= EXPECTED" line is executed through
    vmtest after load("packages/X.cz") and must print EXPECTED (the same
    marker and rule as the builtins' help examples).
demo.cz is generated from BOOK.md and is exempt from (1)."""
import re, subprocess, sys, os, glob
os.chdir(os.path.join(os.path.dirname(__file__), '..'))
VMTEST = os.environ.get('VMTEST', './vmtest')

def doc_block_before(lines, i):
    """comment lines directly above line i (0-based), as stripped text"""
    j = i - 1
    block = []
    while j >= 0:
        s = lines[j].strip()
        if s.startswith('%') or s.startswith('#'):
            block.append(s.lstrip('%#').strip())
            j -= 1
        else:
            break
    block.reverse()
    return block

lint_bad = ok = bad = checked = 0
for path in sorted(glob.glob('packages/*.cz')):
    pkg = os.path.basename(path)
    lines = open(path).read().split('\n')
    for i, line in enumerate(lines):
        m = re.match(r'^let\s+([A-Za-z_][A-Za-z_0-9]*)\s*=\s*fn\b', line)
        if not m:
            continue
        name = m.group(1)
        block = doc_block_before(lines, i)
        egs = [b[4:].strip() for b in block if b.startswith('e.g.')]
        desc = [b for b in block if not b.startswith('e.g.')]
        internal = bool(block) and block[0].lower().startswith('internal')
        if pkg != 'demo.cz' and not internal:
            if not desc:
                lint_bad += 1; print(f"DOC MISSING ({pkg}): {name} has no description")
            if not egs:
                lint_bad += 1; print(f"DOC MISSING ({pkg}): {name} has no e.g. line")
        for eg in egs:
            if '%=' not in eg:
                continue
            checked += 1
            expr, _, expected = eg.partition('%=')
            expected = expected.split('%')[0].strip()
            prog = f'load("{path}")\n{expr.rstrip()}\n'
            r = subprocess.run([VMTEST], input=prog, capture_output=True, text=True, timeout=60,
                               env=dict(os.environ, COZY_PLOT_TERM='ascii'))
            got = (r.stdout + r.stderr).strip()
            last = got.split('\n')[-1].strip() if got else ''
            if last == expected:
                ok += 1
            else:
                bad += 1
                print(f"PKG EXAMPLE MISMATCH ({pkg} {name}): {expr.strip()}\n  claimed: {expected!r}\n  actual : {last!r}")
print(f"package docstrings: {ok} of {checked} examples verified; {lint_bad} lint problem(s)")
sys.exit(1 if (bad or lint_bad) else 0)
