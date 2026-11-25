#!/usr/bin/env python3
"""The tutor, played through the REPL: every lesson must still be true.

packages/tutor.cz holds the lessons and, for every step, its answer
(tutor_solutions() prints them, one per line; a page's is empty, Enter).
Fed to ./cozy after `tutor restart`, the answers must pass every step that
checks something — so a change to the language that breaks a lesson
breaks the build — raise exactly the errors the lessons teach, finish, and
save the place. Then the tutor's words: hint twice (the answer), skip,
back, again, lessons, tutor off and the resume, tutor N, a lesson that
does not exist, and the finished state. HOME is a temporary directory:
the learner's own ~/.cozy_tutor is never touched.

    python3 tests/run_tutor.py
"""
import os
import re
import subprocess
import sys
import tempfile

if not os.path.exists("./cozy") or not os.path.exists("./vmtest"):
    print("tutor: ./cozy or ./vmtest not built, skipping"); sys.exit(0)

fails = []


def check(name, ok, detail=""):
    if not ok:
        fails.append(name)
        print(f"tutor FAIL ({name}){': ' + detail if detail else ''}")


def cozy(lines, home):
    env = dict(os.environ, HOME=home, NO_COLOR="1")
    r = subprocess.run(["./cozy"], input="\n".join(lines) + "\n", capture_output=True, text=True, env=env, timeout=600)
    return r.stdout + r.stderr


sols = subprocess.run(["./vmtest"], input='load("tutor"); tutor_solutions()\n', capture_output=True, text=True, timeout=60).stdout
sols = sols.split("\n")
if sols and sols[-1] == "":
    sols = sols[:-1]
nsteps = len(sols)
ndo = sum(1 for s in sols if s.strip())

with tempfile.TemporaryDirectory() as home:
    out = cozy(["tutor restart"] + sols, home)
    passed = out.count("✓")
    check("every step passes with its own answer", passed == ndo and "✗" not in out,
          f"{passed} of {ndo} passed; first miss: " + (out[max(0, out.find('✗') - 300):out.find('✗') + 80] if "✗" in out else ""))
    errs = re.findall(r"error at \d+:\d+: (.*)", out)
    check("the errors are the two the lessons teach",
          len(errs) == 2 and "undefined name 'rte'" in errs[0] and "must be Bool" in errs[1], repr(errs))
    check("the tutor ends", "That is the tutor." in out, out[-400:])
    wide = [l for l in out.split("\n") if len(l) > 80 and "error at" not in l and not l.startswith("cozy>")]
    check("the tutor's text fits an 80-column terminal", not wide, repr(wide[:3]))
    saved = open(os.path.join(home, ".cozy_tutor")).read().strip()
    check("the place is saved past the last step", saved == str(nsteps + 1), saved)
    out = cozy(["tutor"], home)
    check("a finished tutor says so", "You finished the tutor" in out, out[-200:])

with tempfile.TemporaryDirectory() as home:
    out = cozy(["tutor", "", "hint", "hint", "skip", "back", "again", "lessons", "tutor off", "tutor"], home)
    check("hint, then the answer", "hint: type it exactly as shown" in out and re.search(r"the answer:\s*\n\s+2 \+ 3 \* 4", out) is not None, out[-600:])
    check("lessons lists them, with the place", "16  Why Cozy" in out and "← you are here" in out)
    check("tutor off pauses", "Tutor paused at lesson 1" in out)
    check("tutor resumes", "Welcome back: lesson 1" in out)
    out = cozy(["tutor 6", "tutor 99", "again"], home)
    check("tutor N opens lesson N", "Lesson 6 · Pipes" in out, out[:400])
    check("a lesson that does not exist keeps the place",
          "There is no lesson 99" in out and out.rstrip().count("Pipes") >= 1 and "|> passes the whole value on" in out.split("There is no lesson 99")[1], out[-400:])
    out = cozy(["tutor off"], home)
    check("tutor off when it is not running", "not running" in out)
    out = cozy(["hint"], home)
    check("the tutor's words are ordinary names when it is not running", "undefined name 'hint'" in out, out[-200:])

if fails:
    print(f"tutor: {len(fails)} check(s) failed")
    sys.exit(1)
print(f"tutor: {nsteps} steps in 16 lessons played at the prompt, every one of the {ndo} checks passes with its answer; "
      "hint, skip, back, again, lessons, off and resume, tutor N")
