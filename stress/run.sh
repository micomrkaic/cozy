#!/usr/bin/env bash
# The workout (0.0.35): heavier than the rite, same law — the verdict is the
# exit code. Three tiers: property battery, scale, fuzz-under-ASan.
set -e
cd "$(dirname "$0")/.."
echo "== stress: identities (randomized property battery)"
./cozy stress/identities.cz
echo "== stress: calculus (derivatives vs finite differences; optimizers vs truth)"
./cozy stress/calculus.cz
echo "== stress: sparse at scale"
./cozy stress/sparse_scale.cz
echo "== stress: parser fuzz under ASan (400 lines of token soup; clean errors only)"
python3 - <<'PY'
import random
random.seed(47)
frags = ['let','fn','->','if','then','else','end','while','do','for','(',')','[',']','{','}',
         '+','-','*','/','^','.*','|>','~>',';',',','=','==','<','&','!',':',"'",'"str','x','y',
         '1','2.5','1e','@','.','ab','..','...','end]','[;','load("','")','sin','%']
open('/tmp/fuzz.txt','w').write('\n'.join(' '.join(random.choice(frags)
    for _ in range(random.randint(1,14))) for _ in range(400)) + '\n')
PY
./vmtest-asan < /tmp/fuzz.txt > /dev/null 2>/tmp/fuzz.err || true
if grep -q "AddressSanitizer\|LeakSanitizer" /tmp/fuzz.err; then
  echo "stress: FUZZ FOUND A SANITIZER HIT"; grep -m1 "SUMMARY" /tmp/fuzz.err; exit 1
fi
echo "== stress: long session (2000 evals; heap must plateau)"
python3 -c "print(chr(10).join('let z%d = sum(rand(30) * rand(30)); mem()' % i for i in range(2000)))" \
  | ./cozy > /tmp/mem.txt 2>&1
python3 - <<'PY'
import re, sys
# "process: peak N MB resident" — peak RSS must plateau, not climb with churn
peaks = [float(m) for m in re.findall(r'peak (\d+(?:\.\d+)?) MB', open('/tmp/mem.txt').read())]
early, late = peaks[100], peaks[-1]
print(f"  peak RSS: eval 100 = {early} MB  eval 2000 = {late} MB")
sys.exit(0 if late < early + 30 else 1)   # entry 11 landed: bound ENFORCED
PY
echo "stress: all tiers green"
