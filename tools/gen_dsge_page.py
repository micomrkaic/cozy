#!/usr/bin/env python3
"""Generate or verify the standalone pages: docs/dsge-standalone.html and
docs/nowcast-standalone.html, each its page (docs/dsge.html,
docs/nowcast.html) as ONE file, with the interpreter bundle (docs/cozy.js,
which already embeds the WebAssembly binary and the packages) written
inline, so the page can be saved, mailed, or opened from a disk with no
network. The nowcast page runs Cozy in a Web Worker: its inline bundle
carries data-cozy-bundle, and the page builds the worker from that text.

    python3 tools/gen_dsge_page.py           # regenerate
    python3 tools/gen_dsge_page.py --check   # fail if it is out of date

Each standalone page is a function of two files and nothing else; `make wasm`
regenerates it after rebuilding the bundle, and --check (wired into
`make test`) refuses a tree where either source changed without it. The
verdict is the exit code."""
import hashlib
import re
import sys

BUNDLE = "docs/cozy.js"
PAGES = [("dsge", "docs/dsge.html", "docs/dsge-standalone.html", "<script>"),
         ("nowcast", "docs/nowcast.html", "docs/nowcast-standalone.html", "<script data-cozy-bundle>")]
TAG = '<script src="cozy.js"></script>'


def build(SRC, opener):
    page = open(SRC, encoding="utf-8").read()
    js = open(BUNDLE, encoding="utf-8").read()
    if page.count(TAG) != 1:
        raise SystemExit(f"gen_dsge_page: expected exactly one {TAG} in {SRC}")
    if page.count('<html lang="en">') != 1:
        raise SystemExit(f"gen_dsge_page: cannot find the <html> tag in {SRC}")
    # inline script text must not contain a closing tag or an HTML comment opener
    js = js.replace("</script", "<\\/script").replace("<!--", "<\\!--")
    page = page.replace('<html lang="en">', '<html lang="en" data-standalone>')
    return page.replace(TAG, opener + "\n" + js + "\n</script>")


STAMP = re.compile(r'var COZY_BUNDLE = "cozy\.js\?v=[0-9a-f]+";')


STAMPED = ["docs/nowcast.html", "docs/index.html"]


def stamp(check):
    """The pages whose worker loads the engine (the nowcast page, the
    playground) ask for cozy.js?v=<hash of cozy.js>, so a cached older engine
    is never used with a newer page."""
    h = hashlib.sha256(open(BUNDLE, "rb").read()).hexdigest()[:12]
    bad = 0
    for path in STAMPED:
        page = open(path, encoding="utf-8").read()
        if len(STAMP.findall(page)) != 1:
            raise SystemExit(f"gen_dsge_page: expected one COZY_BUNDLE line in {path}")
        want = STAMP.sub(f'var COZY_BUNDLE = "cozy.js?v={h}";', page)
        if want == page:
            continue
        if check:
            print(f"{path}: the cozy.js stamp is stale — run python3 tools/gen_dsge_page.py")
            bad = 1
            continue
        open(path, "w", encoding="utf-8").write(want)
        print(f"{path}: stamped cozy.js?v={h}")
    return bad


def main():
    bad = stamp("--check" in sys.argv)
    for name, SRC, OUT, opener in PAGES:
        want = build(SRC, opener)
        if "--check" in sys.argv:
            try:
                have = open(OUT, encoding="utf-8").read()
            except FileNotFoundError:
                have = None
            if have != want:
                print(f"{name} page: {OUT} is out of date — run python3 tools/gen_dsge_page.py")
                bad = 1
            else:
                print(f"{name} page: standalone copy current ({len(want)} bytes)")
            continue
        open(OUT, "w", encoding="utf-8").write(want)
        print(f"{name} page: wrote {OUT} ({len(want)} bytes)")
    sys.exit(bad)


if __name__ == "__main__":
    main()
