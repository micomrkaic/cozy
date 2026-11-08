#!/usr/bin/env python3
"""pkgdoc_fill.py — fill docstring examples whose expected value is '?'.

Package docstrings carry verified examples: `% e.g. EXPR %= EXPECTED`.
Write the example with `%= ?` and run this tool; it executes EXPR after
loading the package (through ./vmtest, so the output format is the
suite's) and writes the printed value in place of the '?'. Review the
value before committing — the tool records what the interpreter says,
which is only right if the expression is. Usage:

    python3 tools/pkgdoc_fill.py packages/finance.cz [more.cz ...]
"""
import re, subprocess, sys, os
os.chdir(os.path.join(os.path.dirname(__file__), '..'))
VMTEST = os.environ.get('VMTEST', './vmtest')
for path in sys.argv[1:]:
    src = open(path).read()
    out = []
    filled = 0
    for line in src.split('\n'):
        m = re.match(r'^(\s*[%#]\s*e\.g\.\s*)(.*?)(\s*%=\s*)\?\s*$', line)
        if m:
            expr = m.group(2).rstrip()
            prog = f'load("{path}")\n{expr}\n'
            r = subprocess.run([VMTEST], input=prog, capture_output=True, text=True, timeout=60,
                               env=dict(os.environ, COZY_PLOT_TERM='ascii'))
            got = (r.stdout + r.stderr).strip()
            last = got.split('\n')[-1].strip() if got else ''
            if 'error' in r.stderr:
                first_err = [l for l in r.stderr.split('\n') if 'error' in l][0].strip()
                print(f"  {path}: {expr} -> {first_err}", file=sys.stderr)
                last = '?'
            pad = max(1, 44 - len(m.group(1)) - len(expr))
            line = f"{m.group(1)}{expr}{' ' * pad}%= {last}"
            filled += 1
        out.append(line)
    open(path, 'w').write('\n'.join(out))
    print(f"{path}: {filled} example(s) filled")
