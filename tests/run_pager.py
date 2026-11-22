#!/usr/bin/env python3
"""The manual pages in color under any PAGER setting: drives ./cozy in a pty.

`manual` renders the books with ANSI colors and pipes them to $PAGER. less
shows color escapes as literal "ESC[1;36m" unless it is told -R, and a bare
PAGER=less does not say so — the manual came out as escape-code noise on the
owner's machine (0.1.28). The REPL now appends -R to $LESS for the pager it
starts; this checks (1) the pager's environment, with LESS unset and with a
LESS the user already set, and (2) the real thing, PAGER=less on a pty.
Skips (exit 0 with a note) where there is no pty or no less."""
import os, re, select, shutil, signal, sys, time

if not os.path.exists('./cozy'):
    print("pager: ./cozy not built, skipping"); sys.exit(0)
try:
    import pty
except ImportError:
    print("pager: no pty on this platform, skipping"); sys.exit(0)


def drive(env, keys, until, timeout=15.0):
    """Run ./cozy on a pty with env changes, type keys, read until `until`."""
    pid, fd = pty.fork()
    if pid == 0:
        for k, v in env.items():
            if v is None:
                os.environ.pop(k, None)
            else:
                os.environ[k] = v
        os.environ['TERM'] = 'xterm'
        os.execv('./cozy', ['./cozy'])
    out, sent, end = b'', False, time.time() + timeout
    while time.time() < end and until not in out:
        r, _, _ = select.select([fd], [], [], 0.2)
        if r:
            try:
                chunk = os.read(fd, 65536)
            except OSError:
                break
            if not chunk:
                break
            out += chunk
        if not sent and b'cozy> ' in out:
            os.write(fd, keys); sent = True
    try:
        os.kill(pid, signal.SIGKILL); os.waitpid(pid, 0)
    except OSError:
        pass
    os.close(fd)
    return out


fail = 0
# (1) what the pager is handed: a stand-in pager that prints its $LESS
probe = "sh -c 'echo \"LESSENV=[$LESS]\"; cat > /dev/null'"
for label, less, want in (("LESS unset", None, "[-R]"), ("LESS already set", "-F -X", "[-F -X -R]")):
    out = drive({'PAGER': probe, 'LESS': less}, b'manual\n', b']').decode(errors='replace')
    m = re.search(r'LESSENV=(\[[^\]]*\])', out)
    if not m or m.group(1) != want:
        fail += 1; print(f"pager FAIL ({label}): pager saw LESS={m.group(1) if m else 'nothing'}, want {want}")

# (2) the owner's case: PAGER=less, nothing else — colors, not ESC[ text
if shutil.which('less'):
    out = drive({'PAGER': 'less', 'LESS': None}, b'manual\n', b'Getting started')
    if b'ESC[' in out or b'\x1b[1;' not in out:
        fail += 1; print("pager FAIL: PAGER=less shows the manual's color escapes as text")
    note = "PAGER=less pages the manual in color"
else:
    note = "less not installed, real-pager check skipped"

if fail:
    sys.exit(1)
print(f"pager: -R reaches $LESS (unset and preset); {note}")
