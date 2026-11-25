#!/usr/bin/env python3
"""TEMPORARY macOS diagnostic: Ctrl-C during a running loop at a pty."""
import os, pty, select, signal, subprocess, sys, time, termios

def rd(fd, secs):
    out, end = b"", time.time() + secs
    while time.time() < end:
        r, _, _ = select.select([fd], [], [], 0.05)
        if r:
            try: out += os.read(fd, 65536)
            except OSError: break
    return out

def cpu(pid):
    return subprocess.run(["ps", "-o", "time=,stat=", "-p", str(pid)], capture_output=True, text=True).stdout.strip()

for variant in ("plain", "settle"):
    pid, fd = pty.fork()
    if pid == 0:
        os.environ.update(TERM="xterm-256color", NO_COLOR="1", HOME="/tmp")
        os.execv("./cozy", ["./cozy"])
    print(f"=== {variant}: banner {len(rd(fd, 2.0))} bytes")
    if variant == "settle":
        os.write(fd, b"while true do 1 end"); rd(fd, 0.5); os.write(fd, b"\r")
    else:
        for ch in b"while true do 1 end\r":
            os.write(fd, bytes([ch])); time.sleep(0.03)
    b = rd(fd, 1.0)
    print("after submit:", repr(b[-160:]), "| cpu/stat:", cpu(pid))
    lflag = termios.tcgetattr(fd)[3]
    print("lflag ICANON", bool(lflag & termios.ICANON), "ECHO", bool(lflag & termios.ECHO), "ISIG", bool(lflag & termios.ISIG))
    os.write(fd, b"\x03")
    b = rd(fd, 1.5)
    print("after ^C:", repr(b[-200:]), "| cpu/stat:", cpu(pid))
    lflag = termios.tcgetattr(fd)[3]
    print("lflag ICANON", bool(lflag & termios.ICANON), "ECHO", bool(lflag & termios.ECHO), "ISIG", bool(lflag & termios.ISIG))
    for ch in b"40 + 1\r":
        os.write(fd, bytes([ch])); time.sleep(0.03)
    b = rd(fd, 1.5)
    print("after 40+1:", repr(b[-200:]), "| cpu/stat:", cpu(pid))
    os.kill(pid, signal.SIGKILL); os.waitpid(pid, 0); os.close(fd)
