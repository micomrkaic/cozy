#!/usr/bin/env python3
"""Regenerate MANUAL.md's builtin reference from eval.c's doc table.

Cell contents are markdown-escaped: a pipe inside a signature or description
becomes \\| so it cannot shatter the table (doclint guards this in make test).
Run from the repo root:  python3 tools/gen_reference.py
With --check, verify instead of write: exit 1 if MANUAL.md's reference is not
what the doc table generates (wired into make test — the Cozy 0.0.5 catch:
this generator shipped for years with no guard, so a doc-table edit without
regeneration would have shipped a stale reference under a green suite).
"""
import re
import sys
from collections import OrderedDict

TITLES = {
    "core": "Core & introspection", "const": "Constants", "io": "Data files", "solve": "Solvers",
    "plot": "Plotting", "make": "Array construction", "reduce": "Reductions",
    "array": "Array utilities", "math": "Mathematical functions",
    "linalg": "Linear algebra", "trig": "Trigonometric & hyperbolic",
    "complex": "Complex accessors", "random": "Random numbers",
    "test": "Predicates", "hof": "Higher-order functions",
    "string": "Strings", "repl": "REPL commands",
}

def md_cell(text):
    """Escape table-breaking characters for a markdown cell."""
    return text.replace("|", "\\|")

def main():
    # One source, many readers: rows come from doc/builtins.tsv (0.0.29).
    cats = OrderedDict()
    for line in open("doc/builtins.tsv"):
        if line.startswith("#") or not line.strip():
            continue
        name, sig, desc, cat, _help = line.rstrip("\n").split("\t")
        cats.setdefault(cat, []).append((sig, desc))

    total = sum(len(v) for v in cats.values())
    ref = []
    for cat, items in cats.items():
        ref.append(f"### {TITLES.get(cat, cat)}\n")
        ref.append("| Signature | Description |\n|---|---|")
        for sig, desc in items:
            ref.append(f"| `{md_cell(sig)}` | {md_cell(desc)} |")
        ref.append("")

    m = open("MANUAL.md").read()
    pat = r"(## 17\. Builtin reference\n\n\*Generated[^*]*\*\n\n).*?(## 18\. Grammar summary)"
    if not re.search(pat, m, flags=re.S):
        raise SystemExit("gen_reference: reference section anchors not found")
    m2 = re.sub(pat, lambda mo: mo.group(1) + "\n".join(ref) + "\n" + mo.group(2), m, flags=re.S)
    if "--check" in sys.argv:
        if m2 == m:
            print(f"builtin reference: current ({total} builtins, {len(cats)} sections)")
        else:
            raise SystemExit("builtin reference: STALE — run tools/gen_reference.py")
    elif m2 == m:
        print(f"reference already current: {total} builtins, {len(cats)} sections")
    else:
        open("MANUAL.md", "w").write(m2)
        print(f"reference regenerated: {total} builtins, {len(cats)} sections, cells escaped")

if __name__ == "__main__":
    main()
