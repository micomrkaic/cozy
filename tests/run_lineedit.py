#!/usr/bin/env python3
"""The REPL at a real terminal: drives ./cozy in a pseudo-terminal, key by
key, as a person would, and checks what comes back. Covers the line editor
(isocline, 0.1.50) and the REPL around it:

  editing keys, history (and across sessions), Ctrl-R search, tab completion
  of names and of file paths inside quotes, multi-line entries, Ctrl-C at the
  prompt and during evaluation, input() at the terminal, the demo's bare-Enter
  stepping, Ctrl-D.

Stage 2 runs the same key scripts against the browser terminal, so these are
also the reference for "the browser behaves like the terminal". Skips (exit 0)
where there is no pty. The verdict is the exit code.

    python3 tests/run_lineedit.py
"""
import os
import re
import select
import shutil
import signal
import sys
import tempfile
import time

try:
    import pty
except ImportError:
    print("lineedit: no pty on this platform, skipping"); sys.exit(0)
if not os.path.exists("./cozy"):
    print("lineedit: ./cozy not built, skipping"); sys.exit(0)

ANSI = re.compile(r"\x1b\[[0-9;?]*[a-zA-Z]|\x1b\][^\x07]*\x07|\x1b[=>]|\r")
UP, LEFT, CTRL_A, CTRL_C, CTRL_D, CTRL_K, CTRL_R, CTRL_W, TAB = "\x1b[A", "\x1b[D", "\x01", "\x03", "\x04", "\x0b", "\x12", "\x17", "\t"


class Session:
    def __init__(self, home):
        self.pid, self.fd = pty.fork()
        if self.pid == 0:
            os.environ.update(HOME=home, TERM="xterm-256color", NO_COLOR="1")
            os.environ.pop("LINES", None); os.environ.pop("COLUMNS", None)
            os.execv("./cozy", ["./cozy"])
        self.buf = ""
        self.read_until("cozy> ", 10)

    def _pump(self, timeout):
        r, _, _ = select.select([self.fd], [], [], timeout)
        if not r:
            return False
        try:
            data = os.read(self.fd, 65536)
        except OSError:
            return False
        if not data:
            return False
        self.buf += data.decode(errors="replace")
        return True

    def read_until(self, text, timeout=5.0, since=0):
        """wait for text to appear after position since (a mark())"""
        end = time.time() + timeout
        while time.time() < end:
            if text in self.clean()[since:]:
                return True
            self._pump(0.05)
        return text in self.clean()[since:]

    def settle(self, quiet=0.25, timeout=5.0):
        end = time.time() + timeout
        while time.time() < end:
            if not self._pump(quiet):
                return

    def send(self, keys, pause=0.03):
        for chunk in re.findall(r"\x1b\[[A-D]|.", keys, re.S):
            os.write(self.fd, chunk.encode())
            time.sleep(pause)

    def clean(self):
        return ANSI.sub("", self.buf)

    def mark(self):
        return len(self.clean())

    def since(self, m):
        return self.clean()[m:]

    def close(self):
        try:
            os.kill(self.pid, signal.SIGKILL)
        except OSError:
            pass
        try:
            os.waitpid(self.pid, 0)
        except OSError:
            pass
        os.close(self.fd)

    def exited(self, timeout=3.0):
        end = time.time() + timeout
        while time.time() < end:
            pid, _ = os.waitpid(self.pid, os.WNOHANG)
            if pid:
                return True
            self._pump(0.05)
        return False


fails = []


def check(name, ok, detail=""):
    if not ok:
        fails.append(name)
        print(f"lineedit FAIL ({name}){': ' + detail if detail else ''}")


def result_lines(text):
    """the lines that are not prompts or echoes"""
    return [l.strip() for l in text.split("\n") if l.strip() and "cozy>" not in l and "...>" not in l]


def run(s, keys, want, name, timeout=5.0):
    m = s.mark()
    s.send(keys)
    ok = s.read_until(want, timeout, m)
    s.settle(0.15, 1.0)
    check(name, ok, f"sent {keys!r}, wanted {want!r}, got {s.since(m)[-200:]!r}")
    return s.since(m)


home = tempfile.mkdtemp(prefix="cozy-lineedit-")
try:
    s = Session(home)
    run(s, "1 + 2\r", "\n3", "evaluate a line")
    run(s, "2 + 2\n", "\n4", "a line feed (Ctrl-J) is Enter too, as in readline")
    run(s, "2 * 3" + CTRL_A + "10 + \r", "\n16", "Ctrl-A, then insert at the start")
    run(s, "12" + LEFT + "+\r", "\n3", "left arrow, insert in the middle")
    run(s, "999 + 1" + CTRL_A + CTRL_K + "5\r", "\n5", "Ctrl-K kills to the end")
    run(s, "7 * 8 9" + CTRL_W + "\r", "\n56", "Ctrl-W deletes a word")
    run(s, "let hv = 40 + 2 + 0\r", "cozy> ", "a definition")
    out = run(s, "hv\r", "\n42", "the definition took")
    m = s.mark(); s.send(UP + "\r"); s.read_until("\n42", 3, m); s.settle(0.15, 1)
    check("up arrow recalls the last entry", "42" in s.since(m), repr(s.since(m)[-120:]))
    m = s.mark(); s.send("1\r"); s.settle(0.15, 1)
    # Ctrl-R: Enter takes the entry found into the line, a second Enter runs it
    run(s, "let hv = 1\r", "cozy> ", "redefine")
    m = s.mark(); s.send(CTRL_R + "+ 2 +"); s.settle(0.2, 1); s.send("\r"); s.settle(0.2, 1); s.send("\r"); s.settle(0.2, 1)
    run(s, "hv * 2\r", "\n84", "Ctrl-R found and ran an earlier entry")
    # multi-line: the parser asks for more, the prompt says so
    out = run(s, "let y = [1,\r", "...>", "continuation prompt")
    run(s, "2]\r", "cozy> ", "close the entry")
    run(s, "y\r", "[1, 2]", "the multi-line entry took")
    # Ctrl-C at the prompt abandons a half-typed multi-line entry
    run(s, "let zz = [1,\r", "...>", "a second continuation")
    m = s.mark(); s.send(CTRL_C); s.read_until("cozy> ", 3, m); s.settle(0.15, 1)
    run(s, "7\r", "\n7", "Ctrl-C returned to the main prompt")
    run(s, "zz\r", "undefined name 'zz'", "the abandoned entry was not evaluated")
    # Ctrl-C during evaluation: wait until the loop is surely running (the
    # editor has finished with the line), then interrupt; a fresh prompt and
    # a working session must follow
    s.send("while true do 1 end\r"); s.settle(0.3, 3); time.sleep(1.0)
    m = s.mark(); s.send(CTRL_C)
    ok = s.read_until("cozy> ", 5, m); s.settle(0.2, 1)
    check("Ctrl-C interrupts a running loop", ok, repr(s.since(m)[-160:]))
    run(s, "40 + 1\r", "\n41", "and the session goes on")
    # type-ahead: what you type while a computation runs is kept for the next prompt
    m = s.mark(); s.send("let ta = 0; for i = 1:400000 do ta = ta + 1 end; ta\r", 0.002); s.send("ta + 1\r", 0.002)
    ok = s.read_until("400001", 20, m); s.settle(0.2, 1)
    check("typing ahead during a computation is kept", ok and "400000" in s.since(m), repr(s.since(m)[-200:]))
    # completion: names, and file names inside a string
    m = s.mark(); s.send("sqr" + TAB); s.settle(0.3, 2)
    check("Tab completes a builtin name", "sqrt" in s.since(m), repr(s.since(m)[-120:]))
    m = s.mark(); s.send("(16)\r"); s.read_until("\n4", 3, m); s.settle(0.15, 1)
    m = s.mark(); s.send("hv" + TAB); s.settle(0.3, 2)
    s.send("\r"); s.settle(0.2, 1)
    check("Tab completes a name you defined", "42" in s.since(m), repr(s.since(m)[-120:]))
    m = s.mark(); s.send('readtable("tests/da' + TAB); s.settle(0.3, 2)
    check("Tab completes a file path inside quotes", "tests/data/" in s.since(m), repr(s.since(m)[-160:]))
    s.send(CTRL_C); s.settle(0.2, 1)
    # input() is edited like the prompt
    m = s.mark(); s.send('let who = input("name? ")\r'); s.read_until("name? ", 3, m)
    s.send("Adx\x7fa" + CTRL_A + "\x1b[C" + "\r"); s.settle(0.2, 1)     # typo, backspace, move about
    run(s, "who\r", '"Ada"', "input() at the terminal, with editing")
    # the demo's bare Enter steps to the next problem
    run(s, 'load("demo")\r', "demo15", "load the demo", 10)
    run(s, "demo5\r", "Problem 5.1", "a demo section starts")
    run(s, "\r", "Problem 5.2", "a bare Enter steps the demo")
    # Ctrl-D leaves; the history is saved
    s.send(CTRL_D)
    check("Ctrl-D exits", s.exited(5))
    s.close()
    hist = os.path.join(home, ".cozy_history")
    check("the history file is written", os.path.exists(hist) and "let hv = 40 + 2 + 0" in open(hist, errors="replace").read())
    if os.path.exists(hist):
        text = open(hist, errors="replace").read()
        check("a multi-line entry is one history item, continuation lines are not separate",
              "let y = [1," in text and not re.search(r"(?m)^2\]$", text), repr(text[-300:]))
    # a new session recalls the last one's history
    s2 = Session(home)
    m = s2.mark(); s2.send(UP); s2.settle(0.3, 2)
    check("history persists across sessions", "demo5" in s2.since(m), repr(s2.since(m)[-120:]))
    s2.close()
finally:
    shutil.rmtree(home, ignore_errors=True)

if fails:
    print(f"lineedit: {len(fails)} check(s) failed")
    sys.exit(1)
print("lineedit: editing keys, history (and across sessions), Ctrl-R, completion of names and paths, "
      "multi-line entries, Ctrl-C at the prompt and while running, input(), demo stepping, Ctrl-D — all at a real pty")
