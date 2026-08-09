# Lessons — a retrospective on building Neutrino

*Written at v1.0, while the scars are fresh. Neutrino is a functional array
language with Octave-flavoured syntax: a lexer, arena, Pratt parser, AST
evaluator, bytecode compiler, and stack VM in portable C23, with 113 builtins
spanning linear algebra, special functions, statistics, solvers, plotting,
and data I/O. Final state: 588 golden tests, 3 codegen goldens, 56 verified
manual transcripts, 103 verified help examples, ~33,000 fuzzed inputs clean
under ASan+UBSan, zero dependencies beyond libm. This document records what
that cost and what it taught.*

---

## 1. Founding decisions that paid off

**Zero dependencies.** `make` works on a bare Ubuntu or Mac. Every algorithm
— LU, QR, SVD, Hessenberg-QR eigensolver, incomplete gamma/beta, Brent's
solvers — is code we wrote and sanitizers walked through. The cost is
performance (hand-rolled `inv` measured ~30x slower than LAPACK-backed
Octave); the payoff is that the whole system fits in one head and one
debugger. For a learning project this trade is correct without qualification.

**Records as the multi-value convention.** `eig(A) -> {values, vectors}`,
`lu -> {L, U, p}`, `fminbnd -> {x, fx}`. One idea, used everywhere, and it
composed into an unplanned bonus: a record of column vectors turned out to
*be* the data frame — `readtable` plus existing mask indexing gave
`d.cpi[d.year >= 2021]` with no new types at all. The best feature of the
data-frame story is that it required no features.

**Reproducible-by-default RNG.** Fixed seed at startup, `rng(k)` to reseed.
Made stochastic goldens possible, made bug reports deterministic, and cost
nothing. Any language with a test suite should do this.

**The pipe with a placeholder.** `x |> f(@) |> g` earned its keep daily.
Notably, the `@` placeholder also *prevented* a feature: matrix
multiplication never needed an operator beyond `*` because the pipe absorbed
the composition patterns that tempt people into operator zoos.

**Strict Bool discipline.** `1 == true` is an error; conditions must be
Bool. Caught real mistakes in real sessions and cost one paragraph of
documentation.

**Autocall for zero-argument callables, statement position only.** `who`,
`tic`, bare closures. The restriction to statement position is what kept it
sound — names in expression position stay values, so `map(sqrt, x)` works.
The lesson is the shape of the compromise: convenience at the top level,
purity in expressions.

## 2. Founding decisions we would change

**Strings should have been first-class from day one.** The single
load-bearing regret. "Inert strings" looked like admirable scope discipline
and the friction never stopped compounding: `readtable` must reject a
country-code column by name; plot legends needed the `label1..labelN`
contortion because there are no string arrays; `==` on strings is an error
that surprises everyone. The reason it is a *founding* decision: retrofitting
touches every builtin's type dispatch, the array element tower, comparison,
sorting, and display. Scope discipline applied to a foundation is not
discipline, it is deferral with interest.

**The error-handling strategy is the architecture.** The largest bug source
of the entire project, by an order of magnitude, was setjmp/longjmp
unwinding over manually managed memory:

- Four independent setjmp-clobber bugs (handlers reading register-cached
  locals — including one in the oldest indexing code, exposed only when the
  sanitizer build gained -O1).
- Error-path leaks in every generation of code: elementwise ops, slice
  assignment, `mrdivide`/`inv`/`mpow`, CSV readers, plotting, and parser
  scratch vectors on *every* parse error.
- A guard idiom (`saved`/`setjmp`/`volatile`/`array_build_abort`) that must
  be re-remembered at every allocation site, forever.

None of these are individually hard; the point is the *class* never closes.
A successor should make one foundational move that eliminates it: garbage
collection (error paths become trivially safe — the unreachable is
collected), an arena-per-evaluation ownership model, or a host language
where unwinding is safe by construction. This one decision is worth more
than any amount of syntax.

**Sizes were int-shaped when they should have been one guarded path.** The
fuzzer found uint32 truncations that amounted to a latent heap overflow
(`1:4294967306` allocated 10 slots and would write billions), signed-overflow
UB in the documented wraparound semantics, and an `ipow` that would "wrap"
astronomically large exponents sometime next century. The fix — one
`DIM_MAX`, one `as_dim`, one `check_cells`, unsigned arithmetic for
documented wrap — took an afternoon. It should have been the first
afternoon, not the last.

**Undecided, honestly: the Int/Float split.** Exact integers were pleasant;
they paid in wrap semantics, promotion rules, and the `4 / 2 -> 2.0`
explanation. Lua's doubles-only heresy deletes the entire category. For a
numerical language this is a genuine trade, not a mistake — but a successor
should decide it consciously rather than inherit it.

## 3. The methodology that worked — portable to any project

**Machine-verified documentation is the project's best invention.** Three
documents cannot drift from the implementation: the manual (every REPL
transcript executed and diffed by `make test`), the builtin reference
(generated from the interpreter's own doc table), and the help examples
(every `%=` line executed and compared). The system caught the *author*
confabulating four separate times: a matrix operator that did not exist, a
comment character misremembered as modulo, ten trailing-zero format errors,
and a random value invented outright. Write-from-memory is not a personal
failing to overcome; it is a constant to engineer around.

**Goldens run per-feature, sanitizers run per-commit, fuzzers run
per-milestone.** The division of labour was right. What was wrong until the
end: the sanitizer build at -O0, which hid every optimization-dependent bug
(the clobber class) while giving full confidence. **Sanitizers test the code
the compiler generated, not the code you wrote — build them at -O1.**

**Error paths are the primary attack surface.** Essentially every leak and
both memory-safety findings lived on paths where something had already gone
wrong. The habit that works: when writing any error check after an
allocation, stop and trace the unwind. The habit that works better:
architecture where the trace is unnecessary (see §2).

**The clean-room ritual.** Every deliverable was rebuilt from the packed
tarball in a fresh directory and the full suite run there. It caught missing
files, stale binaries, and untracked fixtures roughly once a week for the
life of the project. Cost: ~30 seconds per session.

**Fake the external tool, assert on what you send.** When gnuplot output
was unverifiable (occluded legend in a terminal render), substituting a
`gnuplot` that captured stdin settled in one command what rendering
speculation could not. Test the interface you control.

**Fix the message, not just the bug.** `plot: gnuplot failed (exit 127) —
is gnuplot installed?` exists because the failure was once a silent SIGPIPE
death. Every hard-won diagnostic came from actually running the failure.

## 4. Process lessons

**Wait-for-real-need survived contact with enthusiasm — barely.** The
policy ("features earn their way in through transcripts of friction") said
no to QZ, LAPACK, sparse matrices, date/time, an inline-assignment operator,
and, at the very end, an entire list of appealing syntax (recorded in
DESIGN_NOTES.md with named triggers). Each refusal looks obvious in
retrospect; none felt obvious at the time. The mechanism that made refusal
possible was writing the design down anyway — a parked idea does not nag.

**The predictable failure mode is forgetting your own codebase.** `kron`
was proposed and half-reimplemented while a tested version already existed
(silver lining: the new one fixed a real overflow bug in the old). Memory of
one's own project is as unreliable as memory of facts; grep before design.

**A false-positive warning is worse than no warning.** The staleness check
that cried wolf across binaries got scoped per-binary the day it first
misfired. Warnings survive only while they are always right.

**Benchmarks beat adjectives.** "Hand-rolled is slower" settled nothing;
`tic; inv(rand(400)); toc` -> 0.11 s settled everything, including the
decision *not* to act yet.

## 5. If there is a successor

Inherit unchanged: the verification-first documentation system, the golden
suite discipline, reproducible RNG, records for multi-returns, the
wait-for-need policy, fuzzing from week one, sanitizers at -O1, the
clean-room ritual, one central size guard.

Decide consciously at founding: memory/error architecture (the big one),
first-class strings, the numeric tower, and — before any of those — what the
language is *for*. Neutrino's answer was "recreation, learning, a personal
Octave," and every good decision above traces back to the clarity of that
answer. A successor built to "apply lessons" has no answer; a successor
built because building is the hobby should be maximally *different* — lazy,
stack-based, APL-family, anything that teaches new lessons rather than
re-teaching these.

And mind the second-system effect. This file is its antidote: the ambition
is written down, so it does not have to be built.


## Postscript (v1.3.0): the strings ledger, closed

The founding regret was repaid in three phased releases — scalar operations
(1.1.0), string arrays with refcounted elements (1.2.0), and the payoff
(1.3.0): `readtable` string columns, quote-aware CSV, `strsplit`/`strjoin`,
composable `fields`. The retrofit worked *because* of the other founding
decisions: the strictness doctrine meant every operation already had a
tested refusal to replace with a behavior, and the golden suite plus
sanitizer-and-fuzz discipline caught a use-after-free, a double-free, and
two silent-garbage paths before any shipped. strings ledger: closed.

## 7. The documentation lattice, and six catches (v1.9.0–v1.12.0)

The releases from `ans` through the pipe family turned documentation from
text into tested claims. The pattern that emerged: every claim a document
makes should be either executed (transcripts, help examples, worked-example
tables — all machine-verified in `make test`) or structurally audited
(doclint checks every table in every document). When generated artifacts
misrender, audit the generator before the renderer: the escaped-pipe saga
had three possible homes — renderer, source, generator — and the generator
was the origin both times.

The catch ledger from this arc, each now guarded by a test:
- A verified transcript embedded `datestr(today())`'s literal output — a
  test designed to fail at midnight. Time-dependent output in goldens is a
  bomb; assert monotone properties instead.
- `run_manual.sh` had an `exec` mid-file, making two later guards dead code
  since the day they were added. A guard that never runs is worse than no
  guard: it certifies. Verify that verifiers fire.
- The rewritten Makefile's mkdir rule silently became the default goal —
  bare `make` built a directory and reported success. `.DEFAULT_GOAL` is
  cheap insurance; also: equal mtimes read as up-to-date, so dependency
  tests need a sleep before the touch.
- Debugging backslash escapes through repr/JSON display layers doubled the
  backslashes twice; two rounds were lost to phantom bugs. When the bug is
  bytes, count bytes (`od -c`), not representations.
- The pipe release: `@` resolves by name at runtime but parameters live in
  stack slots (hence the `_@e` rewrite); a borrowed builtin placed in an
  owning constant pool over-released on chunk teardown (segfault); and
  shadow-proofing `~>` via a globals lookup failed because `let map = 7`
  replaces the binding — immunity had to come from minting the primitive,
  not finding it.

## 8. Correlated verification (v2.1.0)

The gravest catch of the project, found by writing the book. The transcript
*capture* harness and the transcript *verifier* shared the same session
sentinel — a bare string expression, which echoes, and since v1.9.0 an echo
sets `ans`. Every ans-dependent transcript was therefore captured corrupted
(outputs missing or displaced onto neighboring lines) and then verified as
correct, because the verifier replayed the identical poison. The manual
shipped visibly wrong output for three releases with a green check beside
it.

The principle: **a verifier that shares machinery with the generator it
checks can certify the shared defect.** Independence is the point of
verification; correlation quietly converts it into agreement. The fix is an
inert sentinel (`print(...)` outputs without echoing) — and the lasting
rule: when capture and check must share a harness, the harness belongs on
the trap list, and any new observable state (like `ans`) demands an audit
of every instrument that touches a session.

### The symmetric-zero quadrature trap

integral(fn x -> x * sin(2 * x), -pi, pi) returned 3e-17 — for an
integral whose true value is -pi — because adaptive Simpson launched
from the midpoint probes a, m, b and the quarter points, and x sin 2x
vanishes at every one of them on [-pi, pi]: the estimate and its
refinement agreed at zero, so the routine declared convergence on
garbage. Found not by a test but by a USER IDEA (the Fourier fan-out,
Problem 10.6) whose b2 coefficient came back wrong. Fix: launch from a
golden-section split (c = a + (b-a)/phi) — no simple symmetry survives
an irrational fraction — at three extra evaluations per call; all 914
existing goldens passed unchanged. The class: any fixed initial
partition can be aligned with by some integrand; commensurate nodes are
the enemy, and pretty intervals like [-pi, pi] are where they ambush.

### The rite ran past a corpse

The v2.25.0 insertion script died mid-pipeline on a justified
assertion (the book already had a Problem 10.7 — itself the residue of
an earlier insertion that never renumbered its chapter, leaving two
10.6s across three shipped releases). Everything downstream proceeded
anyway: changelog written, version bumped, wasm built, tarball shipped
— internally inconsistent, changelog promising content the book lacked.
Two failures, two fences. The numbering class: structural invariants
that no transcript checks (problem numbering, section ordering) need
their own lint — run_manual.sh now fails on duplicate problem numbers.
The rite class: a multi-step release is a chain, and a chain that
continues after a broken link ships the break. Steps must gate on the
success of what they depend on — and when a verification line DOES
print the evidence (the pdf grep said "10.8 in pdf: 0" plainly), the
operator must read it before packaging, not after. The suite's exit
code discipline exists precisely so green means green; the same law
applies to every ad-hoc pipeline that touches a deliverable.

### The generator nobody checked (Cozy 0.0.5)

tools/gen_reference.py — the builtin-reference generator — shipped from
the fork with no --check mode and no invocation anywhere in make test,
while the PLAYBOOK's law ("every generator with --check wired into make
test") read as if it were true. The gap surfaced only because adding
buildinfo's doc-table row forced a manual regeneration, and the suite
went red on the book INDEX, not the reference: the neighboring guard
fired, the missing one could not. A doc-table edit without regeneration
would have shipped a stale reference under a green suite indefinitely.
The mechanism is the dead-guard class with a twist: nothing was broken,
so nothing could reveal the guard's absence — laws inherited by prose
must be audited against the tree, not assumed from the book. Fix:
--check added, wired into the test target, and proven to fire (corrupt
a cell, watch it go red) before being trusted — a guard that has never
fired certifies nothing.

### The overlay that could not delete (Cozy 0.0.10)

deploy.sh untarred the release over the repo working tree — "adds and
overwrites; never deletes," a comment that read as a safety feature and
was in fact a contradiction of the snapshot law it served. The model
says the tarball is the COMPLETE intended state; an overlay enforces
only half of that, and the unenforced half stayed invisible until the
first release that renamed files: the .cz rename deployed onto a repo
still holding every .nu ghost, ls("packages") saw twenty files where
the manual verified ten, and the suite halted the deploy at the gate
(correctly — the rite's test-before-push earned its keep). The class:
a sync mechanism that cannot express deletion silently diverges from
any source of truth that can; the first rename or removal is the
detonator. Fix: extract to a temp dir and rsync -a --delete into the
tree (git excluded) — the tree is now byte-for-byte the snapshot,
deletions included. The general law: when a model says "X is the
complete state," audit every mechanism that applies X for the
operations it cannot express.

### The positional immediacy test (Cozy 0.0.12)

value_retain decided heap-vs-immediate with `kind >= VAL_STRING` — a
range test that encoded the enum's ORDER as if it were a property of
the kinds. Every appended kind before VAL_DUAL happened to be a heap
object, so the test kept passing and looked like a law. VAL_DUAL is an
immediate (two doubles), landed numerically above VAL_STRING, and the
first retain of a dual dereferenced 2.0 as a pointer — segfault, found
in the very first smoke test, with a beautiful red herring: duals whose
value part was 0.0 worked, because 0.0 aliases NULL and the null check
swallowed it. The class: a classification implemented as a range over
an enum breaks the day the enum grows past the range's assumption, and
appended-for-ABI-stability enums (ours, by design) grow at the END,
exactly where `>=` tests live. Fix: an explicit kind_is_heap switch —
-Werror=switch now guards the classification the way it already guards
every other kind dispatch. Audit rule: grep for comparison operators
applied to enum values whenever a kind, element type, or opcode is
appended; the sparse rename found ghost files, this found ghost
pointers.

### The third backend's two findings (Cozy 0.0.16)

The Accelerate acceptance run — 1046 goldens and 615 transcripts against
a third independent LAPACK on a different architecture — came back green
everywhere except seven BOOK lines, and the seven decomposed into
exactly two lessons.

ONE: transcripts had pinned architecture-dependent noise. The Fourier
chapters displayed ~1e-16 coefficients — mathematically zero, computed
by integral + trig, whose last-ulp digits differ between x86 and ARM
libm. The book had captured x86's dust as truth. The class extends the
time- and path-dependent golden family: PLATFORM-dependent output is a
bomb on the day the second platform arrives. Fix in the document layer:
a zap chop (|v| < 1e-12 -> exact 0) in the transcripts, which also
improved the pedagogy — the spectra now display as the clean patterns
the mathematics says, identical on every machine.

TWO: the eigenvector phase convention had a tie hole. The anchor —
"largest entry made real positive" — selected by strict comparison, so
a vector with two equal-magnitude components (every 2-state Markov
chain's [0.7071; -0.7071]) let last-ulp noise pick the anchor, and
Accelerate picked differently than tier0/openblas had. Same disease as
the 0.0.11 conjugate-pair sort, same medicine: the selection is now
tolerance-aware (first entry within 1e-12 relative of maximal), and the
convention is deterministic on every backend. One book transcript
recaptured to the now-canonical sign; goldens and manual never pinned
the tie.

The meta-lesson: each new backend is an adversarial reviewer of every
convention the language thought it had settled. Two backends agreed by
luck; the third found both holes in one run.

### The guard behind the wrong gate (Cozy 0.0.17)

run_emacs.sh checked for emacs and skipped everything when absent —
including the pure-python drift check that needed no emacs at all. The
primary dev container has no emacs, so the editor-mode drift guard was
dead there from the day it was written; a hand edit to cozy-mode.el at
0.0.12 (wrong order, wrong layout, exactly what the generator exists to
prevent) shipped through four releases of green suites and was caught
by the owner's X1, which has emacs. Same family as the stale exec and
the correlated sentinel: A GUARD THAT ONLY RUNS WHERE A TOOL HAPPENS TO
BE INSTALLED CERTIFIES EVERYWHERE ELSE. The gate now covers only the
emacs-batch test; the drift check runs unconditionally. Audit rule:
when a check script starts with "command -v X || exit 0", every line
after it that does not need X is a dead guard in X-less environments.

Second, smaller: the fix run exposed that gen_emacs_mode and
gen_reference parsed the same doc table with DIFFERENT regexes — the
looser one matched a { "y", "m", ... } unit-key array and had been
highlighting a phantom builtin named y in the editor forever (and
reporting 172 where the reference said 171 — two truths from one
table). Both generators now use the identical full-row pattern. The
class: two parsers of one source of truth will disagree exactly when
it matters; share the pattern or generate one from the other.
