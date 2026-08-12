#!/usr/bin/env python3
"""gen_demo.py — packages/demo.cz is GENERATED from BOOK.md.

The law (0.0.52, owner's ruling): the demo and the book of examples
contain exactly the same examples, forever. Enforcement is structural:
this script parses BOOK.md's chapters and **Problem N.M — title.**
blocks, extracts each transcript's input lines, and emits the demo as
code that replays them live. --check regenerates and diffs, and lives
in make test, so a book edit that forgets the demo (or vice versa —
the demo cannot be edited at all) fails the suite.
"""
import re, sys, io, os

os.chdir(os.path.join(os.path.dirname(__file__), '..'))
book = open('BOOK.md').read()

# ---- parse: chapters -> problems -> input lines ----------------------------
chap_re = re.compile(r'^## (\d+)\. (.+?)\s*$', re.M)
prob_re = re.compile(r'^\*\*Problem (\d+\.\d+) — (.+?)\.?\*\*', re.M)
fence_re = re.compile(r'```[a-z]*\n(.*?)```', re.S)

chapters = []           # (num, title, start, end)
marks = list(chap_re.finditer(book))
for i, m in enumerate(marks):
    end = marks[i + 1].start() if i + 1 < len(marks) else len(book)
    chapters.append((m.group(1), m.group(2), m.end(), end))

sections = []           # (chapter title, [(prob id, prob title, [lines])])
for num, title, s, e in chapters:
    body = book[s:e]
    pms = list(prob_re.finditer(body))
    probs = []
    for j, pm in enumerate(pms):
        pend = pms[j + 1].start() if j + 1 < len(pms) else len(body)
        lines = []
        for fence in fence_re.findall(body[pm.end():pend]):
            for l in fence.split('\n'):
                if l.startswith('cozy> '):
                    lines.append(l[6:].rstrip())
        # Workspace-surgery problems (clear/keep) are book-only: replayed
        # in the shared demo session they would destroy the demo itself.
        surgical = any(re.match(r'\s*(clear|keep)\s*\(', l) or l.strip() == 'clear'
                       for l in lines)
        if lines and not surgical:
            probs.append((pm.group(1), pm.group(2), lines))
    if probs:
        sections.append((f"{num}. {title}", probs))

# ---- emit ------------------------------------------------------------------
def cz_str(s):
    return '"' + s.replace('\\', '\\\\').replace('"', '\\"') + '"'

out = io.StringIO()
w = out.write
w('% demo.cz — GENERATED from BOOK.md by tools/gen_demo.py. Do not edit.\n')
w('% The demo and the book of examples are the same examples by construction:\n')
w('% every line below is a transcript input from the book, replayed live.\n')
w('% make test regenerates and diffs this file (the sync is a suite check).\n')
w('%\n')
w('%   demo()    the interactive tour: menu, pick a section, Enter advances\n')
w('%   (a at the menu plays everything; the suite pipes that path headless)\n\n')
w('let praw = fn t -> print(strrep(strrep(t, "{", "{{"), "}", "}}"))\n')
w('let isnil = fn v -> str(v) == "null"\n')
w('let ans = null\n')
w('let dshow = fn s -> (praw("cozy> " + s); let v = eval(s); '
  'if !isnil(v) & !endswith(s, ";") then (ans = v; praw(str(v))) end; null)\n')
w('let dwait = fn -> pause("        [ Enter for the next problem ] ")\n')
# each problem starts from default numeric formatting: transcripts are
# fresh sessions in the book, and format() state must not leak between them
w('let dhead = fn t -> (format(6); print(""); praw("  ── Problem " + t); print(""))\n\n')

for si, (title, probs) in enumerate(sections, 1):
    w(f'let demo_sec{si} = fn -> (\n')
    w(f'  print("");\n  praw("  ═══ Section {si}: {title} ═══");\n')
    for pi, (pid, ptitle, lines) in enumerate(probs):
        w(f'  dhead({cz_str(pid + " — " + ptitle)});\n')
        for l in lines:
            w(f'  dshow({cz_str(l)});\n')
        if pi < len(probs) - 1:
            w('  dwait();\n')
    w('  null)\n\n')

w('let demo_menu = fn -> (\n')
w('  print("");\n  print("  The Cozy tour — the book of examples, live.");\n')
w('  print("  The demo and the book contain the same examples by construction.");\n')
w('  print("");\n')
for si, (title, probs) in enumerate(sections, 1):
    label = f"  demo{si}  {title}  ({len(probs)} problem" + ("s" if len(probs) != 1 else "") + ")"
    w(f'  praw({cz_str(label)});\n')
w('  print("");\n')
w('  print("  demo() starts the guided tour: pick a number, Enter advances.");\n')
w('  print("  Or jump straight in: demo3 (etc.) starts a section; Enter steps.");\n  null)\n\n')

# workbench-native stepping: demoK starts a section (plays its first
# problem); next advances one problem per call — one eval per click.
w('let demo_seclens = [' + ', '.join(str(len(p)) for _, p in sections) + ']\n')
w('let demo_cursec = 0\nlet demo_curprob = 0\n')
w('let demo_playprob = fn s2, k2 -> (\n')
for si, (_, probs) in enumerate(sections, 1):
    for pi, (pid, ptitle, lines) in enumerate(probs, 1):
        w(f'  if s2 == {si} & k2 == {pi} then (dhead({cz_str(pid + " — " + ptitle)});\n')
        for l in lines:
            w(f'    dshow({cz_str(l)});\n')
        w('    null) end;\n')
w('  null)\n\n')
w('let next = fn -> (\n')
w('  if demo_cursec == 0 then print("  no section running — demo15 (etc.) starts one")\n')
w('  elseif demo_curprob >= demo_seclens[demo_cursec] then '
  '(print("  section finished — pick another (demo1..demo' + str(len(sections)) + ')"); demo_cursec = 0)\n')
w('  else (demo_curprob = demo_curprob + 1; demo_playprob(demo_cursec, demo_curprob);\n')
w('    if demo_curprob < demo_seclens[demo_cursec] then '
  'print("        [ Enter for the next problem ]") '
  'else (print("        [ section finished — demo for the menu ]"); demo_cursec = 0) end)\n')
w('  end; null)\n\n')
for si in range(1, len(sections) + 1):
    w(f'let demo{si} = fn -> (demo_cursec = {si}; demo_curprob = 0; next())\n')
w('\n')
w('let demo = fn -> (\n')
w('  let going = true;\n')
w('  while going do\n')
w('    demo_menu();\n')
w('    let s = trim(input("  section (number, a = all, q = quit): "));\n')
w('    if s == "q" | s == "" then (going = false)\n')
w('    elseif s == "a" then (')
for si in range(1, len(sections) + 1):
    w(f'demo_sec{si}(); ')
w('print(""); print("  Tour complete — the book has the prose."); going = false)\n')
for si in range(1, len(sections) + 1):
    w(f'    elseif s == "{si}" then demo_sec{si}()\n')
w('    else praw("  (unrecognized: " + s + ")") end\n')
w('  end; null)\n\n')
w('demo_menu()\n')
text = out.getvalue()

if '--check' in sys.argv:
    cur = open('packages/demo.cz').read() if os.path.exists('packages/demo.cz') else ''
    if cur != text:
        print('demo: STALE — packages/demo.cz does not match BOOK.md '
              '(run tools/gen_demo.py; the demo and the book are the same examples by law)')
        sys.exit(1)
    n = sum(len(p) for _, p in sections)
    print(f'demo: current ({len(sections)} sections, {n} problems from BOOK.md)')
    sys.exit(0)

open('packages/demo.cz', 'w').write(text)
n = sum(len(p) for _, p in sections)
print(f'demo: generated {len(sections)} sections, {n} problems from BOOK.md')
