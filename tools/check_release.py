#!/usr/bin/env python3
"""Release lint (charter debt, paid 0.0.23): the CHANGELOG must contain an
entry for the version version.h declares. A release whose changelog does not
know its own number is two artifacts sharing a name — refused at the suite,
so the rite cannot skip it. The verdict is the exit code."""
import re, sys

vh = open("version.h").read()
m = re.search(r'#define COZY_VERSION "([^"]+)"', vh)
if not m:
    print("release lint: cannot find COZY_VERSION in version.h"); sys.exit(1)
v = m.group(1)
if f"## {v}" not in open("CHANGELOG.md").read():
    print(f"release lint: CHANGELOG.md has no entry for v{v} — write the "
          f"changelog before the bump ships"); sys.exit(1)
print(f"release lint: CHANGELOG has v{v}")
