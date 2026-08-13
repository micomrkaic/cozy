#!/usr/bin/env python3
"""Verify MANUAL.md: run every `cozy> ` transcript line through vmtest
(one session per fenced block) and check the shown output. Aligned multi-line
matrices in the manual are compared against vmtest's single-line form."""
import re, subprocess, sys, os
os.chdir(os.path.join(os.path.dirname(__file__), '..'))
doc = sys.argv[1] if len(sys.argv) > 1 else 'MANUAL.md'
text = open(doc).read()
blocks = re.findall(r'```([a-z-]*)\n(.*?)```', text, re.S)
# build-conditional transcripts (entry 14 residue): a fence tagged
# cozy-nlopt verifies only when the binary carries the backend; on other
# builds it is skipped, counted, and announced — never silently ignored.
import subprocess as _sp
_bi = _sp.run(['./vmtest'], input='buildinfo().optim\n', capture_output=True, text=True)
HAVE_NLOPT = '"nlopt"' in _bi.stdout
skipped = 0
ok = bad = total = 0
for tag, b in blocks:
    if tag == 'cozy-nlopt' and not HAVE_NLOPT:
        skipped += 1
        continue
    lines = b.rstrip('\n').split('\n')
    entries, i = [], 0
    while i < len(lines):
        if lines[i].startswith('cozy> '):
            inp = lines[i][len('cozy> '):]
            exp = []
            i += 1
            while i < len(lines) and not lines[i].startswith('cozy> '):
                exp.append(lines[i]); i += 1
            entries.append((inp, '\n'.join(exp).rstrip()))
        else:
            i += 1
    if not entries:
        continue
    # Sentinel via print(): outputs without echoing, so it cannot touch ans.
    # (An echoing sentinel poisons ans-dependent transcripts — and worse,
    # a capture harness sharing the same sentinel certifies the poison.)
    prog = ''.join(inp + '\nprint("@@S@@")\n' for inp, _ in entries)
    # Deterministic plotting in verified transcripts: the ascii backend has a
    # fixed canvas, so seeded plots are exact text (browser users see SVG).
    env = dict(os.environ, COZY_PLOT_TERM='ascii')
    r = subprocess.run(['./vmtest'], input=prog, capture_output=True, text=True, timeout=30, env=env)
    parts = r.stdout.split('@@S@@')
    err_lines = r.stderr.splitlines()
    ei = 0
    for k, (inp, exp) in enumerate(entries):
        total += 1
        def norm_ws(t):                    # symmetric: rstrip lines, strip block,
            # and drop % annotations from BOTH sides (help() prints its own,
            # and transcripts may add asides; symmetry keeps it a
            # normalization, not an expected-side rewrite)
            return '\n'.join(re.sub(r'\s+%(?!=).*$', '', l).rstrip()
                              for l in t.split('\n')).strip()
        got = norm_ws(parts[k]).strip('"').strip() if k < len(parts) else '<missing>'
        exp_clean = norm_ws(exp)   # '#' annotations died at 0.0.50: they ate
        # the '#' series glyphs of 4-series ascii plots (Problem 15.8)
        if exp_clean.startswith('error:'):
            got = (re.sub(r'^\s*(?:parse )?error at \d+:\d+: ', 'error: ', err_lines[ei])
                   if ei < len(err_lines) else got)
            ei += 1
        def canon(s):
            s = s.strip()
            if s.startswith('[') and '\n' in s:
                rows = [q.strip().strip('[]').strip() for q in s.split('\n')]
                return '[' + '; '.join(', '.join(q.split()) for q in rows) + ']'
            return s
        if exp_clean == '' or got == exp_clean or canon(exp_clean) == canon(got) \
           or got == exp_clean.strip('"'):
            ok += 1
        else:
            bad += 1
            print(f"MANUAL MISMATCH: {inp}\n  manual : {exp_clean!r}\n  actual : {got!r}")
note = f" ({skipped} nlopt-only block(s) skipped: this build has no backend)" if skipped else ""
print(f"{doc}: {ok} of {total} transcript examples verified{note}")
sys.exit(1 if bad else 0)
