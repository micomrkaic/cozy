#!/usr/bin/env python3
"""Generate or verify docs/dsge-standalone.html, the DSGE demo page as ONE
file: docs/dsge.html with the interpreter bundle (docs/cozy.js, which
already embeds the WebAssembly binary and the packages) written inline, so
the page can be saved, mailed, or opened from a disk with no network.

    python3 tools/gen_dsge_page.py           # regenerate
    python3 tools/gen_dsge_page.py --check   # fail if it is out of date

The standalone page is a function of two files and nothing else; `make wasm`
regenerates it after rebuilding the bundle, and --check (wired into
`make test`) refuses a tree where either source changed without it. The
verdict is the exit code."""
import sys

SRC, BUNDLE, OUT = "docs/dsge.html", "docs/cozy.js", "docs/dsge-standalone.html"
TAG = '<script src="cozy.js"></script>'


def build():
    page = open(SRC, encoding="utf-8").read()
    js = open(BUNDLE, encoding="utf-8").read()
    if page.count(TAG) != 1:
        raise SystemExit(f"gen_dsge_page: expected exactly one {TAG} in {SRC}")
    if page.count('<html lang="en">') != 1:
        raise SystemExit(f"gen_dsge_page: cannot find the <html> tag in {SRC}")
    # inline script text must not contain a closing tag or an HTML comment opener
    js = js.replace("</script", "<\\/script").replace("<!--", "<\\!--")
    page = page.replace('<html lang="en">', '<html lang="en" data-standalone>')
    return page.replace(TAG, "<script>\n" + js + "\n</script>")


def main():
    want = build()
    if "--check" in sys.argv:
        try:
            have = open(OUT, encoding="utf-8").read()
        except FileNotFoundError:
            have = None
        if have != want:
            print(f"dsge page: {OUT} is out of date — run python3 tools/gen_dsge_page.py")
            sys.exit(1)
        print(f"dsge page: standalone copy current ({len(want)} bytes)")
        return
    open(OUT, "w", encoding="utf-8").write(want)
    print(f"dsge page: wrote {OUT} ({len(want)} bytes)")


if __name__ == "__main__":
    main()
