# Cozy changelog

## 0.1.9 — default parameter values (entry 13)

### Added
- **fn x, tol = 1e-8 -> ...** — trailing parameters may carry
  defaults, evaluated at call time, left to right, in the function's
  own scope, so a later default can reference any earlier parameter
  (fn a, b = a * 2, c = a + b -> ...). An absent argument takes the
  default; so does an explicit null, which skips a middle parameter
  while supplying a later one. Arity becomes a range everywhere: the
  <fn/1..3> display, who, and both call paths' error messages
  ("expects 1 to 2 argument(s)").
- **One earned opcode** (the desugar-first ledger stays honest):
  OP_ARGDEF slot,off — the VM pads absent arguments with null and a
  compiled prologue fills null slots from their defaults. Sections,
  elementwise-pipe rewrites, and binder desugars build lambdas
  without defaults and compile exactly as before.
- Non-trailing defaults are a parse error with a teaching message;
  a 658-program fuzz batch mixing defaults with sections, pipes, and
  higher-order calls runs ASan-clean; the manual's Functions section
  gains an executed transcript.

## 0.1.8 — dates (entry 16)

### Added
- **The Date kind, thirteenth of the family**: days since 1970-01-01
  UTC in a double, fractional time-of-day, printing as
  "2024-03-15" — so comparison, sorting, ranges, mask filtering, and
  lag-by-subtraction all worked the moment the kind existed. Strict
  dimensional algebra in the strictness-doctrine tradition:
  Date + number is a Date, Date - Date is days, anything else is a
  caught mistake ("dates: only date +- number ... are defined").
- **date("2024-03-15") / date(y, m, d), datestr, today, and the
  extractors** year/month/day/quarter/weekday (Monday = 1), all
  column-aware, on Hinnant's civil-calendar algorithms (leap day
  golden included).
- **readtable auto-types ISO date columns** (mixed columns fall back
  to strings; holes are NaN) — with entry 15, a CSV of dated, holey
  data now loads into the workday view directly: t.date is dates,
  quarter(t.date) extracts, mean(t.gdp, "omit") summarizes,
  dropmissing(t) cleans.
- Residue for the next session, recorded in the docket: plot date
  tick labels, and the time-series book problem.

## 0.1.7 — missing values, the composable way (entry 15)

### Added
- **ismissing** — NaN worn with intent on numerics, "" on strings;
  composes with mask indexing (x[!ismissing(x)] was already the skip
  idiom and remains it).
- **readtable reads NA / NaN / nan cells as NaN** (empty cells already
  did — a quiet inheritance the audit surfaced); a hole no longer
  poisons the parse.
- **dropmissing(t)** — row-wise deletion on a record of columns, the
  data-frame convention Cozy already had.
- **"omit" on mean, sum, std, and median** — one shared filter hook,
  so the one-liner works when the mask idiom is ceremony.
  No new value kind, no validity bitmaps: missing IS NaN, per entry
  15's recorded design and the data-frame lesson (the best feature
  required no features).

## 0.1.6 — entries 15 and 16: missing values and dates, designed

### Design
- **The next capability arc, designed and docketed** (owner's ruling:
  "dates + missing values is critical"): entry 15 — missing IS NaN,
  embraced, with a thin layer (ismissing, hole-tolerant readtable,
  dropmissing, "omit" reductions) and no new storage; entry 16 — a
  Date kind represented as an epoch-day double, adding display and
  strict dimensional algebra (Date - Date is days; Date * anything is
  an error) atop machinery that already works on doubles. Missing
  first, dates second; each ships with a book problem on real holey
  time-series data. Docket-only release so the designs survive the
  session.

## 0.1.5 — integer overflow becomes mathematics, not wraparound

### Changed (owner's catch: 3^84 wrapped to a misleadingly signed int
### while fzero, entering through floats, saw the truth)
- **Integer overflow now PROMOTES to float** in scalar add/sub/mul/pow
  (checked via __builtin_*_overflow) and in the int matmul fast path
  (which falls to the double path on first overflow). Ints remain
  exact within +-2^63; beyond it results become mathematics —
  f(3) = -1.19725e+40 in the REPL now agrees with what fzero always
  saw. Neutrino's LESSONS marked the Int/Float split "undecided,
  honestly"; this decides it, retiring the documented-wrap footgun.
- **Conformance edit, recorded**: the v1.0 fuzzing-campaign goldens
  that pinned wrap semantics (INT64_MAX * 2 -> -2 and kin) are
  amended to pin promotion — per the charter's provision that
  conformance goldens may be amended as recorded edits. The range
  guards and UB-safety those goldens also protect are untouched.
- The manual's Int row states the new rule.

## 0.1.4 — the binder reads as mathematics

### Added (ledger arc 4)
- **minimize[x = x0] f(x)** — the charter's own sketch of capability
  3's surface, live: for minimize, maximize, and nlmin the
  index-bound notation desugars to name(fn x -> body, x0). Every
  other callable keeps sigma's reduction meaning; the optimizer
  phrases previously always arity-erred, so the redirect changes the
  meaning of zero working programs (the chain-release tradition).
  Goldens cover both readings side by side; the manual states the
  rule. Entry 13 (default parameters) is queued as the next session's
  opening task — a language-core change owed a fresh context's full
  care.

## 0.1.3 — dgeev, and the first build-conditional transcript

### Added (ledger arcs 1-2, owner's overrides recorded as overrides)
- **Real nonsymmetric eig runs dgeev** (entry 10's last residue): the
  paired-column eigenvector format is unpacked (conjugate pairs from
  Re/Im column pairs) into the existing complex buffers, so the
  sort/phase-anchor/snap downstream stays single-path. 1.16x at n=400
  — eigenwork dominates, unlike the d-family's big wins — with
  reconstruction at 3.6e-14 and zero golden casualties.
- **Build-conditional transcripts, and Problem 15.10**: a fence tagged
  cozy-nlopt verifies only on builds carrying the backend and is
  SKIPPED WITH AN ANNOUNCEMENT elsewhere — never silently. The demo
  leaves such fences book-only. First use: least absolute deviations
  by BOBYQA — an objective with a kink at every data point, minimized
  without a derivative, landing on the sample median to 1e-3.

## 0.1.2 — deploy stops deploying with its predecessor

### Fixed
- **The v0.1.1 deploy built tier0 despite shipping autodetection**
  (owner's catch, buildinfo in hand): deploy.sh untars the new tree
  over itself, and bash keeps executing the OLD inode — so a deploy
  improvement always ran one release late. deploy.sh now re-execs
  from a temp copy of itself before touching the tree; the running
  script can never be the one being replaced. Trap recorded in the
  PLAYBOOK. Immediate remedy on any machine that deployed v0.1.1:
  make clean && make BACKEND=openblas OPTIM=nlopt (or simply run the
  next deploy — the script on disk is already the detecting one).

## 0.1.1 — deploy assembles the best machine it can find

### Changed
- **./deploy.sh now builds the fastest configuration the host
  supports, in one step** (owner's ruling): Darwin gets Accelerate;
  Linux probes for OpenBLAS by link test; both probe for nlopt.h —
  and every fallback prints a NOTE naming the exact install command
  (libopenblas-dev, libnlopt-dev, brew install nlopt). The deploy's
  own suite run uses the same detected flags, so the verdict
  certifies the binary that was actually built. Plain make is
  unchanged (tier0, zero dependencies). The wasm bundle needs no
  compilation at deploy time: it ships prebuilt in every tarball and
  deploy publishes docs/ to GitHub Pages as-is.

## 0.1.0 — the four capabilities, real

This is a milestone, not a diff. The charter named four founding
capabilities; as of this version all four are implemented, tested, and
in daily-driver shape:

- **Sparse matrices** — a first-class sparse kind with legible
  sparsity, explicit dense/sparse crossings, and iterative solvers
  that never densify (Problem 15.1 is the demo).
- **External LAPACK** — build-time backends (tier0, OpenBLAS,
  Accelerate); real inputs run real routines: solve, det, symmetric
  eig, svd, chol, and gemm, with measured speedups from 3x to 267x
  and one result-shaping path shared by every backend.
- **First-class differentiation** — dual and hyper-dual numbers as
  value kinds (exact gradients and Hessians through user code), and
  total ast(f) quotation for symbolic work in packages.
- **Optimization** — OPTIM=nlopt links NLopt behind a seam: nlmin
  (SLSQP, L-BFGS, BOBYQA, COBYLA) fed exact dual derivatives, with
  the optim package dispatching automatically and the pure augmented
  Lagrangian retained as tier0 and the browser story.

Acceptance at the milestone: 1107 goldens, 429 book transcripts, the
demo replaying all 64 problems, ASan/UBSan clean, the suite green
under both OPTIM=none and OPTIM=nlopt on two Linux machines. The one
open formality — the MacBook's accelerate+nlopt run — is recorded in
the charter baton and lands when the owner and the machine are in the
same city.

Also in this release: the manual's backends section documents
OPTIM=nlopt and the dispatch behavior; the charter carries the 0.1.0
baton.

## 0.0.62 — the missing header names its package

### Fixed
- **OPTIM=nlopt without the library died at a bare fatal error**
  (owner's Debian build; the apt package is libnlopt-dev, which
  nobody guesses from "nlopt"): the Makefile now probes for nlopt.h
  before compiling and fails with the install command for both
  Debian/Ubuntu and Homebrew. The trap-almanac class: a build flag's
  failure message must name its remedy.

## 0.0.61 — NLopt behind the seam (entry 14, steps 1-2)

### Added
- **OPTIM=nlopt build backend and the nlmin builtin**: SLSQP, L-BFGS,
  BOBYQA, COBYLA over one options record ({alg, eq, ineq, lb, ub,
  xtol, maxeval}), with Cozy's EXACT dual-number derivatives feeding
  the gradient algorithms — objective gradients and constraint
  Jacobians alike come from dual passes through the user's closures;
  no finite differences anywhere. The callback discipline from the
  docket entry is implemented: a runtime error in user code is
  trapped in the trampoline, force-stops NLopt cleanly, and re-raises
  after its frames unwind. buildinfo() gains the optim field.
- **minimize_con/maximize_con dispatch to SLSQP** when built with
  OPTIM=nlopt; the augmented Lagrangian remains the tier0 path and
  what wasm ships. Measured on the book's life-cycle problem:
  14631 iters unconverged (0.0.58) -> 0.07 s (0.0.59's tuned AL) ->
  0.00056 s with SLSQP + exact gradients. minimize_newton is NOT
  dispatched: exact hyper-dual Newton is Cozy's own jewel.
- **The suite is green under BOTH builds** — the 15.5/15.6 fences
  moved to build-invariant fixed precision per the transcript posture
  (pin invariants, not iterate tails); nlmin documented in the TSV
  (help, reference, emacs mode regenerate); buildinfo golden and
  manual transcript updated for the fourth field.
- Mac: brew install nlopt, then make BACKEND=accelerate OPTIM=nlopt.

## 0.0.60 — entry 14: the optimization backend seam, chartered

### Design
- **Docket entry 14 written and trigger FIRED** (owner's ruling after
  0.0.59's AL stall): a build-time optimization backend seam in the
  image of the linalg seam — OPTIM=nlopt, one nlmin builtin, optim.cz
  dispatching by buildinfo, tier0 pure implementations kept forever
  for wasm and conformance, and Cozy's exact dual gradients feeding
  NLopt's SLSQP/LBFGS with no finite differences anywhere. Callback
  error discipline, transcript posture (pin invariants, not
  iterates), and a five-step release ladder are in the entry. This is
  the charter's founding capability 3 arriving the chartered way:
  through a friction transcript.

## 0.0.59 — the augmented Lagrangian stops standing still

### Fixed (owner: Problem 15.5 visibly slow in the demo)
- **minimize_con burned its full budget at the exact answer**: under
  large rho the inner BFGS's gradient test is unreachable, so the
  outer loop's "viol tiny AND inner converged" gate never tripped —
  14631 iterations, converged=false, ~1.5 s of standing still. Two
  changes: convergence now also accepts constraints-satisfied plus
  iterate-stalled-between-outers (norm(x - xprev) <= 1e-10*(1+|x|)),
  and the initial penalty starts at rho=100 (was 10), pulling the
  constraint in fewer outer rounds. The life-cycle problem: 14631
  iters/1.5 s unconverged -> converged in 0.07 s. The book's 15.5
  and 15.6 fences were recaptured by execution (last-digit drift; the
  budget-residual bound honestly restated at 1e-8, which is what
  rho=100 delivers); every other golden and transcript passed
  unchanged.

## 0.0.58 — Enter advances, everywhere, under test

### Fixed (owner's third demo report — the instability ends here)
- **Plain Enter now advances the stepper in the terminal REPL too**:
  the menu promised "Enter steps" but only the workbench page mapped
  it; the REPL ignored blank lines. The REPL now sends next on a bare
  Enter whenever a section is mid-tour (globals hold demo_cursec > 0)
  — the same mapping the page applies, so the marker line is true on
  both surfaces.
- **The guided tour's menu prompt says what Enter does** ("q or Enter
  quits") — pressing Enter at the menu quit silently before, reading
  as breakage. An attempted "Enter re-shows the menu" variant is dead:
  it infinite-loops at EOF (input() returns "" forever) and cost one
  timed-out test run to learn.
- **The interaction paths are now suite-tested through the real REPL
  binary**: run_demo.sh drives Enter-stepping (demo9 + three blank
  lines -> three problems) and the guided tour (demo() -> 9 -> q)
  end to end, with timeouts. Three releases of interface churn came
  from changing behavior without a test on the behavior; the gestures
  themselves are now goldens.

## 0.0.57 — Problem 9.3, at last

### Fixed
- **Chapter 9's problems ran 9.1, 9.2, 9.9** (owner spotted it in the
  demo, which faithfully replays the book): the namespace problem was
  misnumbered at insertion in 0.0.27 — the same insertion-bug family
  as the historical two-10.6s incident, which taught the
  duplicate-numbers lint but not a GAP lint. Renumbered to 9.3
  (headers only; transcripts untouched), the full-book audit found no
  other chapter affected, and run_manual.sh now also requires
  consecutive numbering from 1 within each chapter — the demo replays
  the books, so their numbering is user-facing twice over. PDFs
  rebuilt per the 0.0.51 discipline.

## 0.0.56 — the demo interface, settled

### Fixed (owner's ruling after three releases of churn — owned)
- **One interface, both surfaces, one gesture**: demo() runs the
  guided tour (menu, pick a number, plain Enter advances via pause) —
  unchanged in the REPL since 0.0.53. In the workbench, where evals
  are one-shot HTTP and pause cannot block, the PAGE now maps a bare
  Enter to a step whenever a problem is waiting (armed by the
  "[ Enter for the next problem ]" marker) — so the gesture is
  identical: hit Enter, the next problem plays. demo3 (etc.) still
  jumps straight into a section on either surface; the menu text now
  says exactly this and nothing else. The interface churn across
  0.0.53-55 (demo(k) removed, next introduced, labels lagging) is
  owned in this entry; the lesson is the screen-is-the-spec principle
  applied to interaction: the gesture the user already knows (Enter
  advances, as in pause) beats any new word.

## 0.0.55 — the workbench speaks demo and pretty

### Fixed (owner's workbench session, three findings)
- **The workbench now steps the demo natively**: demo1..demo15 start a
  section (autocallable — type the word), and next plays one problem
  per eval — click-through, literally, one HTTP round-trip per step.
  The menu now advertises this real interface instead of the dead
  demo(k) form left over from the redesign. The generator emits the
  whole stepper, so the book remains the single source; the REPL's
  guided demo() tour is unchanged.
- **input() and pause() can no longer hang the workbench**: a one-shot
  HTTP eval reading the SERVER's stdin would block the server (and the
  browser) forever — under --workbench they now return immediately
  ("" and null). This is what made interactive demo() unusable there;
  the stepper needs neither.
- **pretty on works in the workbench** ("pretty on|off" is REPL
  frontend command sugar, absent from the server): serve.c gains a
  command layer mirroring repl.c's — pretty toggles the shared
  value-layer flag, and manual/more answer with pointers to their
  browser-native equivalents (the Docs tab; the scrollbar).

## 0.0.54 — docket entry 13: default parameter values, parked

### Design
- **Entry 13 added to the docket** (owner's ruling after the arity
  discussion): default parameter values — fn x, tol = 1e-8 -> ... —
  as the minimal extension closing the closure/builtin arity
  asymmetry. Additive syntax under the inheritance contract; design
  questions (definition- vs call-time evaluation, earlier-parameter
  references) recorded in the entry; variadics explicitly out of
  scope. Trigger written: friction transcripts — an API sprouting an
  awkward name family or null-placeholder calls. Docket-only release
  so the entry survives the session; the repository is the only
  memory the project has.

## 0.0.53 — demo() answers bare

### Fixed
- **demo() with no argument errored** (owner's catch): Cozy closures
  have fixed arity, and demo was fn(k). Redesigned better than the
  original: demo is now a ZERO-argument interactive loop — the menu
  prints, input() reads the section choice, the section plays with
  Enter advancing between problems, and control returns to the menu
  (a plays everything, q or Enter quits). Bare demo autocalls too.
  The suite pipes the a path headless; the generator emits the whole
  interface, so the book remains the single source.

## 0.0.52 — the demo becomes the book, mechanically

### Changed (owner's ruling: same structure, same examples, forever)
- **packages/demo.cz is now GENERATED from BOOK.md** by
  tools/gen_demo.py: book chapters are demo sections, problems are
  replayable steps. demo() lists the sections; demo(k) plays section
  k one problem at a time, Enter advancing; demo(0) plays everything
  (64 problems), which is what the suite runs headless. Every line
  shown is a transcript input from the book, executed live by eval —
  the "what you read is what runs" principle of the old demo, raised
  one level: now the BOOK is what you read.
- **The sync is a law with teeth**: gen_demo.py --check is wired into
  make test, so the demo cannot drift from the book — editing the
  book regenerates the demo; hand-editing the demo fails the suite.
- Session-state hygiene the replay taught: the demo maintains ans
  (eval does not) and resets format(6) at problem boundaries (book
  transcripts are fresh sessions; the demo is one long one); the two
  workspace-surgery problems (clear/keep) stay book-only by rule,
  since replaying them would destroy the demo itself.
- The old hand-written demo opened with a NEUTRINO banner — six
  releases of identity work missed a string inside a package. Hand-
  maintained parallels rot; generated ones cannot. PLAYBOOK entry.

## 0.0.51 — the documentation audit

### Fixed (owner's audit: "make sure nothing is stale — be thorough")
- **All three PDFs were 28 releases stale** (mtime Aug 9, era 0.0.22):
  make pdfs lives outside the suite by necessity (TeX), and the
  release sequence forgot it — rebuilt, and verified by EXTRACTION:
  BOOK.pdf contains Problems 15.5-15.9 (pdftotext-grepped), MANUAL.pdf
  the new type row, vignettes still embedded (pdfimages: 21). The
  discipline is now a PLAYBOOK entry naming both no-automatic-check
  derived artifacts (PDFs, wasm bundle).
- **The manual said "eleven value kinds" and omitted HDual from the
  type table** — stale since 0.0.24. Now twelve, with an HDual row
  (literals, print form, and what it is: two nilpotent directions
  whose product carries an exact second derivative — the engine under
  hess and minimize_newton).
- **ast(f) had a table row but no prose**: it now stands beside
  body(f) in the reflection paragraph, with the Taylor problem as its
  showcase pointer.
- Swept for other stale claims (kind counts, kernel counts, builtin
  counts, package counts) across MANUAL/BOOK/PACKAGES/READMEs: clean.

## 0.0.50 — five problems from the working econometrician

### Added (BOOK chapter 15, Problems 15.5-15.9, owner's brief:
### "maximal Cozy idiom"; every transcript captured by execution)
- **15.5 Life-cycle consumption**: five-period savings under a
  present-value budget — utility in sigma notation, one equality,
  maximize_con; the Euler equation (growth = βR to four digits) falls
  out rather than being imposed. Discussion carries a precedence
  lesson learned live: sigma's body extends through trailing additive
  terms, so (sum[...] ...) - W needs its parentheses — the first
  draft "converged" to a budget five times wealth.
- **15.6 Minimum variance**: the allocation solved numerically and
  checked against the closed form w* = Σ⁻¹1/1'Σ⁻¹1 to 1e-5.
- **15.7 GMM**: endogenous regressor generated, overidentified linear
  IV estimated in two steps, Hansen J ≈ 1.12 correctly unrejected.
  Design lesson from a gate: dual gradients cannot pass \, so the
  weight matrix inverts once OUTSIDE the objective — precompute what
  the parameter doesn't touch; autodiff flows through matmul.
- **15.8 Taylor by quotation**: ast quotes the typed sin, ddx
  differentiates the tree k times, show + eval read the derivative
  back at 0 — EXACT coefficients (1, -1/6, 1/120, -1/5040) — and a
  four-series ascii plot shows T1/T3/T7 against sin.
- **15.9 Probit by Newton**: the likelihood as the textbook writes it
  (sigma over observations, Φ from dist.cz), minimize_newton in five
  iterations for 600 observations, standard errors from the EXACT
  observed information inv(hess(-ℓ)) — no derivative differenced.

### Fixed
- **The transcript verifier's '#' annotation rule ate plot glyphs**:
  15.8's four-series plot uses '#' for its fourth series, and the
  verifier stripped \s+#.* from expected lines. Annotations now use
  %, symmetric on both sides (help() prints its own % examples), so
  it is a normalization rather than an expected-side rewrite.

## 0.0.49 — the hunt closes with a measurement, not a guess

### Documented
- **The workbench BLAS gap is now a documented platform cost, not an
  open bug**: the owner's instrumented sessions show a roughly fixed
  12-19 ms per-call overhead on Apple Silicon, AMORTIZING with size
  (2.1x at gemm n=1000, 1.18x at n=2000) — which falsifies the
  core-class and thread-count theories outright (both predict constant
  ratios) and survives both QoS levers (per-thread attribute, 0.0.47;
  dispatch-context entry, 0.0.48). Best remaining hypothesis is
  cold-start of Accelerate's parked pool between commands; the two
  discriminating experiments are recorded in KNOWN_LIMITATIONS, and a
  persistent warm compute thread is parked as docket entry 12 with
  those experiments as its trigger — per the house stop-rule: after
  two falsified levers, measurement and a written trigger beat a third
  guess. The terminal is the benchmarking surface.

## 0.0.48 — the eval enters through the front door of GCD

### Changed
- **Workbench evals on Darwin now enter through dispatch_sync_f on the
  USER_INTERACTIVE global queue** (plain C, no blocks runtime),
  replacing the 0.0.47 pthread worker: the owner's measurements show
  the 1.5x BLAS gap survives an explicit per-thread QoS attribute, and
  Accelerate parallelizes via GCD — whose pool QoS follows the
  DISPATCH context, the one lever not yet pulled. Falsifiable as
  before: workbench inv/gemm should match the terminal. If this lever
  also fails to move it, the remaining hypothesis is thread-COUNT
  policy, discriminated by VECLIB_MAXIMUM_THREADS=1 on both surfaces
  (documented in the changelog as the next experiment), and the gap
  becomes a documented platform cost rather than a bug hunt.

## 0.0.47 — matmul joins the fast world (entry 10, phase 3)

### Fixed
- **Matrix multiply ran the boxed interpreter loop — 12 seconds for
  A*A at n=1000** (the owner's profiling experiment found it: A*A was
  equally slow on both surfaces while inv flew, exonerating the
  scheduler and indicting the operator itself). Typed paths now:
  int*int stays exact int64 with the documented wrap; real goes
  through double buffers to dgemm (or a raw-double loop on tier0 —
  8x by de-boxing alone); complex to zgemm; dual/hyper-dual keep the
  boxed loop, whose scalar chain rules are the point. gemm rides the
  seam via the C^T = B^T A^T row-major identity, zero copies.
  Measured: OpenBLAS A*A(1000) 12.1s -> 0.045s (267x); tier0 1.49s.
- **One golden repaired on principle**: dgemm leaves -1e-17 where the
  boxed sum left +0, and round(-eps) printed -0 — the rounding family
  now canonicalizes its zero. Goldens define the language; -0 from
  round defines nothing.
- **Workbench inv gap, next instrument**: A*A parity across surfaces
  proved process scheduling innocent; each /eval now runs on a worker
  thread with explicit QOS_CLASS_USER_INTERACTIVE on Darwin, the
  strongest request the API offers for Accelerate's pool. Falsifiable
  on the owner's Mac as before.

## 0.0.46 — the Darwin rule, applied to its author

### Fixed
- **The QoS call failed to compile on macOS**: pthread_set_qos_class_
  self_np is an Apple extension, and strict _POSIX_C_SOURCE hides it —
  the exact class the 0.0.42 playbook trap documents, whose own remedy
  list ("_DARWIN_C_SOURCE alongside when BSD APIs are genuinely
  needed") the author then failed to apply one release later. serve.c
  now defines _DARWIN_C_SOURCE under __APPLE__ beside the strict
  macro; Linux unchanged, and the INADDR_LOOPBACK fallback stays
  (harmless, and it guards non-Darwin strict platforms too).

## 0.0.45 — the workbench claims a performance core

### Fixed
- **Workbench evals ran ~1.5x slower than the terminal on Apple
  Silicon, same binary, same backend** (owner's measurement: 0.042s vs
  0.028s for inv(1000), with tic/toc inside the process so HTTP is
  exonerated — and 0.042 is NOT the complex-funnel signature, so the
  real-dispatch fix stands). Cause: a process blocked on accept() reads
  as background work to the macOS scheduler and is relegated to
  efficiency cores. The server now declares QOS_CLASS_USER_INTERACTIVE
  on Darwin at startup; Linux is untouched. Untestable in this
  container — the owner's Mac remains the acceptance layer, third
  lesson in that ledger from one week of deploys.

## 0.0.44 — versions that agree with themselves

### Fixed, and owned
- **The workbench banner showed 0.0.41 on a 0.0.43 install** (owner's
  catch): the leading version belonged to the WASM BUNDLE, which the
  0.0.42 and 0.0.43 hotfix rites shipped STALE — the wasm-rebuild step
  was skipped twice while rushing Mac fixes, exactly the "stale bundle
  serves stale docs" law the rite exists to enforce. Owned in the
  ledger; the bundle is rebuilt at every bump again. The banner rewrite
  now takes the version from the native ping wholesale, so even a
  future skew shows the COMPUTING engine's number.
- **The workbench's slower inv() was a stale server process**: 0.047s
  at n=1000 is the complex-funnel signature — a pre-0.0.36 cozy
  --workbench still running from days earlier. The server's startup
  line now prints its version and backend (replacing a placeholder
  that literally said "this binary's backend"), so a stale server
  identifies itself in one glance at the terminal that started it.

## 0.0.43 — clang reads what gcc forgives

### Fixed
- **An uninitialized-bool load flagged by UBSan on the owner's Mac**
  (clang instruments what gcc folds away): two indexing paths declared
  bool rs, cs and the one-index branch silenced the unused cs with
  (void)cs — which is still an lvalue-to-rvalue LOAD of an
  indeterminate bool, and Darwin's clang found the 143 sitting there.
  Both declaration sites now initialize; behavior unchanged on every
  correct path. Second lesson from the same deploy: the Mac toolchain
  is an adversarial reviewer this Linux/gcc container cannot imitate —
  the owner's make test-asan under Apple clang is a lattice layer of
  its own.

## 0.0.42 — the Mac deploy unbroken

### Fixed
- **serve.c failed to compile on macOS** (owner's deploy transcript):
  strict _POSIX_C_SOURCE makes Darwin headers hide BSD-heritage
  symbols, and INADDR_LOOPBACK is one — glibc leaks it through, Apple
  does not, and the Linux container cannot catch what only a Mac
  header set refuses. Guarded fallback define (the loopback address is
  eternal); no behavior change anywhere. Trap recorded in PLAYBOOK:
  each new platform is an adversarial reviewer, and strict-POSIX on
  Darwin is a known interrogation technique.

## 0.0.41 — one banner, one truth

### Fixed
- **The workbench banner's version line now states the computing
  engine, not the booting module** (owner's third catch, and the right
  ruling: rewriting beats appending). When the native server is
  detected, the page rewrites "0.0.41 (wasm, built ...)" IN PLACE to
  "0.0.41 · openblas backend · native engine" in the warm accent, and
  the "packages are preloaded" line becomes the mode-neutral truth.
  On GitHub Pages / file:// nothing rewrites and the wasm line stands
  correctly. The two appended clarification lines from 0.0.39/0.0.40
  are gone — the banner itself is now honest.

## 0.0.40 — the engine line lands where eyes land

### Fixed
- **The native-engine announcement printed before the wasm banner** and
  was buried above the splash (owner's second catch: timings proved
  native OpenBLAS was computing while the visible banner still said
  wasm). Native detection resolves in milliseconds; the wasm module
  boots slower and writes its banner afterward. The announcement now
  waits for the terminal to boot, so it lands directly BELOW the
  splash in the warm accent — plus one honest clarification for native
  mode: packages are not preloaded there; the Packages tab or load(...)
  brings them in.

## 0.0.39 — the workbench names its engine

### Fixed
- **The workbench terminal claimed "(wasm)" while evals ran natively**
  (owner's catch): the banner belongs to the wasm module that boots the
  page, and the native-detection line neither named the backend nor
  stood out. /native-ping now reports "cozy <version> <backend>", and
  on detection the page prints, in the banner's warm accent: "native
  engine: v0.0.39 · openblas backend — evals run in the local cozy
  process, not wasm", plus an immediate workspace refresh. The wasm
  banner above it remains truthful about what booted the page; the
  engine line states what computes.

## 0.0.38 — entry 11: sessions stop hoarding their history

### Fixed
- **Session arena retention** (the stress suite's day-one catch, owner-
  scheduled): sessions retained every line's parse arena and source
  unconditionally — ~50 KB per eval, 110 MB peak over a 2000-eval
  session. Two coordinated changes: the environment now OWNS its
  binding names (copied at define, freed at all five drop sites), so a
  plain let no longer pins its line; and the compiler flags lines whose
  compiled output genuinely borrows source pointers — lambdas (via
  chunk->src), record literals, and the fan-out desugar's record
  emission (non-owning keys, a site the AST-level flag missed and the
  BOOK's verified transcripts caught) — with all four session hosts
  (REPL, vmtest, wasm, the workbench server) retaining arena+src only
  when flagged. Measured: 2000 closure-free evals now peak at 2.6 MB.
  The stress suite's plateau bound flips from reporting to ENFORCING —
  the acceptance criterion entry 11 wrote for itself.
- The lattice earned this one twice over: the 0.0.28 session-block
  goldens caught the env-name use-after-free within seconds of the
  first attempt, and the book harness caught the fan-out gap.

## 0.0.37 — real routines, phase 2: eig, svd, chol

### Changed
- **Symmetric eig, svd, and chol now run real LAPACK on real inputs**
  (entry 10 phase 2, owner's schedule): dsyev/dgesvd/dpotrf behind new
  eig_sym_d/svd_d/chol_d seam entries, tier0 carrying parity wrappers.
  Design choice that keeps invariance cheap: the real kernels do the
  O(n^3) work, then convert into the existing complex buffers, so every
  downstream shaping step — eigenvalue sorting, the tolerance-aware
  phase anchor, the singular-value snap, real demotion — remains
  single-path. Measured under OpenBLAS at n=400, same binary: symmetric
  eig 0.099s vs 0.342s complex (3.5x), svd 0.115s vs 0.356s (3.1x),
  chol 3.6ms; reconstruction residuals at machine precision.
- Real NONSYMMETRIC eig remains complex-funneled, recorded with its
  trigger: dgeev's paired-column eigenvector format earns its
  unpacking when a profiled hot path asks.

## 0.0.36 — real data on real routines (entry 10, phase 1)

### Changed
- **Real matrices now dispatch to real LAPACK** (owner's ruling: "run
  float on float/double matrices and complex algorithms on complex
  matrices alone — not only faster, but more accurate"): the kernel
  seam grows solve_d/det_d; OpenBLAS (and therefore Accelerate — same
  source) calls dgesv/dgetrf when both operands are real, and tier0
  carries parity wrappers. One funnel covers everything: \, /, inv,
  det, and negative matrix powers all ride mldivide. Measured on
  inv(700) under OpenBLAS, same binary, same matrix: 0.051s real path
  vs 0.097s complex-typed — 1.9x, the predicted flop ratio — with the
  result exactly real by construction (dgesv arithmetic has no
  imaginary residue to snap; the accuracy half of the ruling).
- Phase 2 recorded in the docket with triggers: dsyev/dgesvd/dpotrf
  real paths; real nonsymmetric eig stays complex-funneled until
  dgeev's paired-column format earns its unpacking.
- Full stress battery green under both backends on the new path.
- **One golden repaired for the right reason**: the poly table's worked
  example fit y = x^2 + 1, whose middle coefficient is analytically
  ZERO — the table was pinning ulp noise (complex funnel -8.3e-16,
  dgesv 6.1e-16), fragile under any backend change. The example now
  fits x^2 + x + 1: every coefficient O(1), printing-stable, verified
  byte-identical under tier0 and OpenBLAS. Goldens define the
  language; a golden pinning noise defines nothing.

## 0.0.35 — the workout: a stress tier above the rite

### Added
- **make stress** (owner's request: "give this version a proper
  workout"), heavier than make test and outside it, same law — the
  verdict is the exit code. Four tiers: a randomized, seeded PROPERTY
  BATTERY (inv/mldivide residuals, det·det(inv)=1, eig and svd
  reconstruction, transpose laws, reduction identities, at eight random
  sizes); a CALCULUS battery (dual derivatives and hyper-dual second
  derivatives against finite differences at a dozen points; Newton
  one-step on random PD quadratics against the analytic solution, BFGS
  agreeing); SPARSE at equivalence sizes against dense and cg on a
  3000-node system; and PARSER FUZZ under ASan — 400 seeded lines of
  token soup that must produce clean errors, never sanitizer hits —
  plus a 2000-eval long-session tier asserting peak RSS plateaus.
  Green under tier0 and OpenBLAS.

### Found by the new suite, on its first run
- **Session arena retention** (docket entry 11, KNOWN_LIMITATIONS): the
  long-session tier measured peak RSS climbing 8.6 -> 112.7 MB over
  2000 closure-free evals — every line's parse arena is retained for
  closure-source lifetime, unconditionally. The tier reports the growth
  with an explicit in-code waiver and flips to enforcing when the fix
  lands. A stress suite that finds a real retention cost on day one is
  a stress suite earning its keep.

### Design
- Docket entry 10 opened, trigger already fired by the owner's
  benchmark: the tier-1 complex funnel makes real inv() pay zgetrf/
  zgetri prices versus Octave's dgetrf/dgetri — a real-typed fast path
  is scheduled as the next backend session.

## 0.0.34 — the packages pane grows up: checkboxes, one truth

### Fixed
- **The packages pane's load button never worked**: its click handler
  called writeLine/scroll, which live inside the terminal IIFE — the
  same scope boundary that bit the pane's first build — so every click
  died on a silent ReferenceError. All pane wiring now goes through the
  window.nuTerm bridge that exists precisely for code outside the IIFE.

### Changed
- **Load buttons are now checkboxes whose state is DERIVED from the
  workspace** (the owner's design, and the better one): after every
  eval the pane re-reads who and ticks exactly the packages whose load
  groups exist — so a load("...") typed at the prompt ticks the box,
  and the box never lies. Ticking loads; unticking unloads via the
  group-aware clear("pkg") (verified: clears the package's names,
  leaves everything else, and the teaching hint returns for its names).
  A failed toggle reverts the box and prints the error. Simulated
  end-to-end under jsdom: load issued, state follows who both ways.

## 0.0.33 — the banner names its backend

### Changed
- **The splash's version line now includes the linalg backend** (e.g.
  "v0.0.33 · tier0 backend · built ..."), born from a real friction
  transcript: the owner benchmarked a plain `make` build on the X1 and
  read 2.6s inversions as breakage — it was tier0, the zero-dependency
  default, doing exactly what it does. The single fact that determines
  whether a session is fast is now on screen at startup; `make
  BACKEND=openblas` (or accelerate) remains the explicit opt-in, per
  the portability-first default.

## 0.0.32 — the workbench: RStudio ergonomics, Cozy engines

### Added
- **cozy --workbench [port]** (design entry 9, owner's ruling): a tiny
  localhost HTTP server — POSIX sockets, ~180 lines, zero dependencies,
  127.0.0.1 only by hard scope — serving the playground page and
  answering POST /eval through the NATIVE interpreter: Accelerate or
  OpenBLAS speed, real filesystem, persistent workspace. Verified over
  curl: stateful evals, package loads, and plot_N.svg landing for the
  plot pane (the server sets COZY_PLOT_TERM=svg).
- **The page grew panes**: Workspace (live who, refreshed after every
  eval) and Packages (the twelve packages with one-click loads) join
  Plots/Editor/Docs; eval routing is dual-engine — the page detects the
  local native server and says so in the terminal, and the embedded
  wasm engine remains the zero-install fallback for GitHub Pages and
  file://. Native plots arrive by polling GET /plots. The browser is
  the rendering surface; the compute engine is whichever is best
  available.
- tests/run_page.js extended for the async routing; both phases green.
  docs/README.md documents the workbench and its scope fence.

## 0.0.31 — quotation goes total

### Changed
- **ast(f) now quotes the whole grammar** — the v1 residue trigger
  fired on its first real encounter (the owner quoted op_dot, whose
  body uses .*): elementwise operators (emul/ediv/epow/eldiv), \ as
  ldiv, comparisons, and/or/not, all three pipes, calls of any arity
  ({op="call", f, argc, a1..aN} — single-argument named calls keep
  symb's {op=name, l} shape), indexing, field access, ranges, matrix
  literals (row/matrix lists), both transposes, if with and without
  else, let (and let..in), assignment, blocks, while/for/break/
  continue/return, nested fn (recursive, with its own params row), and
  non-constant exponents ({op="pow", l, r}; constants keep symb's n).
  Package bodies quote whole: ast(op_bt).body.op is "block".
  The validator remains as the seam for future node kinds. Nine new
  goldens; the symb integration golden unchanged.

## 0.0.30 — ast(f): the founding capability list, completed

### Added
- **ast(f) quotation** (design entry 4b, the last unbuilt item on the
  charter's capability list, by owner's schedule): a closure's body as
  a symb.cz-style record tree — {op = "add"/"sub"/"mul"/"div", l, r},
  {op = "pow", l, n} with symb's numeric exponent, {op = "const", v},
  {op = "var", name}, single-argument calls as {op = <name>, l}, unary
  minus as multiplication by -1 for symb compatibility; params exposed
  as a string row. Implementation reparses the closure's RETAINED
  SOURCE (the same store body() prints), so no AST lifetime machinery
  was needed. v1 scope is the symb expression subset; if/loops/
  indexing/multi-arg calls/non-constant exponents gate with teaching
  errors, residue trigger written.
- **The chartered payoff is a golden**: show(simp(ddx(ast(fn x -> x^2 +
  sin(x)).body))) == "((2 * x) + cos(x))" — symbolic differentiation
  of what you typed, symb.cz unmodified. tests/63_ast.test: 11 goldens.

With this, every capability on the founding list is shipped: sparse,
external LAPACK (three backends), optimization (constrained, Newton),
and first-class differentiation — duals, hyper-duals, and quotation.

## 0.0.29 — the doc table moves to a data file

### Changed
- **The 174-row builtin doc table now lives in doc/builtins.tsv** (the
  charter's second fix-from-day-one debt): plain tab-separated text —
  name, signature, description, category, help examples — with
  tools/gen_doc_table.py as THE one escaper producing doc_table.inc,
  which eval.c #includes. The escaping bug class the charter named is
  dead: nobody hand-writes C string literals for documentation again.
- **All readers read the one source**: gen_reference and gen_emacs_mode
  now parse the TSV instead of regexing eval.c — their --check passes
  byte-identically, closing the two-parsers-one-truth lesson for good
  — and gen_doc_table's own --check joins the lattice, so a TSV edit
  without regeneration refuses the suite. help() output is unchanged
  to the byte.

## 0.0.28 — session-block goldens: the charter's oldest debt, paid

### Added
- **Golden session blocks**: an input line beginning with '.. ' chains
  onto the previous case's session — lets, loads, ans, indexed
  assignment, and the seeded rng carry across the chain, so stateful
  behavior is finally golden-testable instead of hiding in the manual
  harness. The runner replays each chain's prefix into one vmtest
  process (vmtest already held one Interp across stdin — the
  fresh-session constraint lived in run.sh all along); every case still
  reports independently, error cases work mid-chain (the current line
  must add no stdout and its message must appear on stderr), and flat
  cases run byte-identically to before. tests/62_sessions.test
  demonstrates ten chained shapes. This was the first item on the
  charter's fix-from-day-one list.

## 0.0.27 — namespaces, taught where readers read

### Changed
- **The manual's namespace story grows from one sentence to the whole
  law** (owner's audit: "sufficiently described?" — honestly, no): the
  load section now covers the manifest (fields), the dynamic door
  (getfield), sibling calls through the record's own global name, and
  the law — a record namespace hides the face, never the body — with a
  pointer to the authoring convention in the packages guide.
- **BOOK gains Problem 9.9**, "A namespace is a record that grew up":
  the pattern demonstrated in four verified transcript lines, and the
  keep() trap staged deliberately in prose (the book harness pins
  stdout, so the error is narrated rather than transcribed) with the
  discussion explaining why it fires and how the tag-prefix convention
  defends against it.

## 0.0.26 — the error that teaches the load line

### Added
- **Undefined-name errors now teach package loads**, born from a real
  friction transcript: the owner typed minimize into a fresh session
  and got a bare "undefined name". The error now reads: undefined name
  'minimize' — it lives in packages/optim.cz; load("packages/optim.cz")
  first. The name -> package table (pkg_hints.h, 174 names) is
  GENERATED from packages/*.cz by tools/gen_pkg_hints.py with --check
  wired into make test, so it can never go stale — the generator
  pattern, twelfth application. Unknown names that match no package
  stay a plain error. Three goldens pin the shapes.

## 0.0.25 — Greek at last: UTF-8 identifiers

### Added
- **UTF-8 identifiers**, asked by the owner: any byte with the high bit
  set is an identifier character (start or continue), so α, β, θ, σ, λ,
  ℓ, π_hat, Δ — the whole econometrics alphabet — are ordinary names in
  variables, functions, parameters, and record fields. Two lines in the
  lexer, zero Unicode tables, and purely additive by construction:
  every such program was "unexpected character" before, so no legal
  program changes meaning. Names compare by BYTES (normalization is the
  user's concern — documented); keywords stay ASCII. Known cosmetic:
  who's column padding counts bytes, so multibyte names sit slightly
  off-grid. tests/61_unicode_idents.test pins six shapes including a
  Greek-named objective handed to minimize.

## 0.0.24 — hyper-duals: exact Hessians and true Newton

### Added
- **VAL_HDUAL + ELT_HDUAL** (entry 4a's Hessian increment, the owner's
  schedule): hyper-duals a + b*eps1 + c*eps2 + d*eps1eps2 as a fifth
  numeric rank and a dense element type — a fixed four-double immediate,
  the same architectural pattern that made dual land cleanly (the
  jet-the-Hessian-needs, not the general jet). One chain-rule helper
  (hd_chain with f, f', f'') serves every unary kernel; the macro
  families each gained a second-derivative expression. sizeof(Value)
  grows 32 -> 40; the setjmp-copy assert's bound moves with a recorded
  rationale (boxing would put refcount churn in every arithmetic op).
- **Builtins** hdual(x, s1, s2[, s12]) (elementwise with scalar
  broadcast — hdual(x, e_i, e_j) seeds two directions at once),
  hdualval, hdual12 — accessors total on plain numbers.
- **hess(f)** in autodiff.cz: the exact Hessian, one pass per index
  pair, symmetry filled. **minimize_newton / maximize_newton** in
  optim.cz: true Newton on exact derivatives with Levenberg damping;
  on a positive-definite quadratic the first step lands on the minimum
  — golden-pinned at iters == 1, the method's signature. Rosenbrock:
  21 iterations to the exact [1; 1].
- tests/60_hessian.test: 22 goldens, equalities throughout.

### Promotion law (extended, gated everywhere)
- int/float lift into hyper-dual; **hyper-dual mixes with neither
  complex nor dual** (seeding both directions is what hdual's two
  slots are for) — arithmetic, comparisons, literals, index-assign,
  sparse, and every linalg/norm/binary-kernel gate extended.
- gamma/lgamma refuse hyper-duals: their second derivative needs
  trigamma, not implemented — recorded docket residue with trigger.

## 0.0.23 — entry 7 reviewed; the rite gains a lint

### Design (entry 7: namespaces — the last unreviewed docket item)
- **Review conducted by execution, every claim re-verified in Cozy**:
  sibling recursion through a record's own global name works (late
  binding); getfield is the dynamic door (landed 0.0.6, satisfying the
  entry's one open condition); who's load groups arrived intact from
  upstream 2.28 (packages/optim.cz 11 names — clutter solved
  orthogonally, as the baton predicted); and the pack-then-prune trap
  reproduced verbatim — keep() of a record of optimizer faces killed
  the first call on the helpers the record hid. The design stands:
  records are the module system, no new kind.
- **Ruling recorded** (same logic as the entry's own anti-decision):
  the three instrument packages — autodiff, optim, sparselin — are
  grandfathered flat. Short daily-typed faces (d, grad, minimize, cg)
  are the feature; helpers are already tag-prefixed (ad_, op_, sl_).
  Record namespaces are the recommendation for future large-API
  packages.
- **The namespace law is now written where authors read**: PACKAGES.md
  "Writing your own" carries the pattern, the late-binding law, the
  helper-prefix convention, and the trap — "a record namespace hides
  the face, never the body." (The section also no longer claims the
  language has four packages.)

### Added
- **Release lint** (charter fix-from-day-one debt, paid):
  tools/check_release.py refuses the suite when CHANGELOG.md lacks an
  entry for version.h's version — wired into make test, python-only so
  it can never be environment-gated into a dead guard, and verified to
  FIRE before being trusted (run against the stale state first, per
  the 0.0.17 lesson).

## 0.0.22 — the owner's vignettes

### Changed
- **BOOK.md carries the owner's new vignette set**: a title-page plate
  and twenty chapter/appendix illustrations (chapters 1-14, appendices
  A-F), replacing the vin*.png placeholders; references carry alt text;
  the 14th file's "ifiom" typo corrected to "idiom" on import; PNGs
  losslessly optimized (34.3 -> 28.7 MB). Chapter 15 and Appendix G
  have no plates yet — flagged for the owner. Tarball and BOOK.pdf grow
  accordingly (~30 MB each; the art is the payload).
- **BOOK.pdf now genuinely embeds the art** (--resource-path resolves
  vignettes/ under docs/), verified by pdfimages: 21 images.
- **make pdfs**: all three books from one target, exit codes bare.

### Fixed, and owned
- **The 0.0.18 "PDFs rebuilt" claim was false**: lmodern.sty had been
  removed as collateral of the emscripten apt work, pandoc failed on
  every run — silently, behind > /dev/null — and releases 0.0.18-0.0.21
  shipped the Aug-8 Neutrino-era PDFs under a changelog saying
  otherwise. Caught at 0.0.22 by mtime; lmodern restored; all three
  PDFs truly rebuilt and image-verified. Ledgered in LESSONS.md as a
  second, self-inflicted occurrence of the grep-that-hid-the-verdict
  class, with the audit habit recorded: after "regenerated X", stat X.

## 0.0.21 — one word, warmly: the nancyj splash

### Changed
- **The splash is now just "Cozy"**, set in figlet's nancyj — the
  Cooper Black of ASCII fonts: fat, round, and unmistakably 1970s
  (owner's call: simplify, one word, seventies-cozy lettering). The
  hearth scene retires after one release; the wordmark, tagline, and
  version line share one left margin. Same face everywhere: REPL
  (color and plain), the web page's banner(), and both logos.
- print_banner rebuilt wholesale from a generated template rather than
  patched — the third banner edit in three releases had made the
  function regex-hostile; whole-function replacement is the durable
  editing strategy for generated-art code.

### Fixed
- **GitHub Pages instructions restored to docs/README.md** — the
  0.0.20 rewrite dropped the "Enabling Pages" section, and the owner
  hit exactly the failure it prevented: Pages enabled with the default
  root folder renders README.md as the site (no root index.html). The
  fix is Settings -> Pages -> branch main, folder /docs; now
  documented again, with the failure mode named.

## 0.0.20 — the trace sweep: the codebase speaks Cozy

### Changed
- **Systematic sweep of every Neutrino trace outside heritage and
  history**, prompted by the owner catching "Neutrino builtins" in the
  help header. Fixed: the help header itself; every C source and header
  file banner comment (~20 files); the --sample program's title;
  demo.cz's header and prompt; every package header ("for Neutrino" ->
  "for Cozy"); cozy-mode.el's docstrings, group, and URL (which still
  pointed at micomrkaic/Neutrino); docs/README.md (rewritten — it
  documented neutrino.js at the old Pages URL); .gitattributes (mapped
  the extinct .nu extension; now maps .cz); the Makefile and test-runner
  comments; run_dis.sh's NEUTRINO env override (now COZY).
- **The wasm API is now cozy_init/cozy_eval/cozy_version** (was nu_*),
  renamed in wasm_api.c, the Makefile export list, and index.html's
  call sites together; the page's internal module global is window.COZY
  (was NU) and the stream/pause latch globals are __cozy* (were __nu*).
  Bundle rebuilt; the renamed API is verified end to end by the page
  test against the real bundle.
- **Golden test data de-Neutrinoed** (conformance edits, recorded):
  the strings suite now exercises "cozy language" (same coverage:
  concatenation, indexing, end-arithmetic, case, containment — with
  the pleasing accident that s[1] + s[6:9] spells "clang"), and the
  records suite's sample name is "cozy". 1046 goldens green.
- **tests/run_page.js exercised for real for the first time in this
  container** (jsdom installed from the allowlisted npm registry): its
  stub still mocked window.Neutrino and _nu_* — the jsdom-gated cousin
  of 0.0.17's dead-guard lesson — and phase 2 caught a genuine
  sequencing bug during this work (bundle exporting _nu_* while the
  page called _cozy_*). Fixed and green against the real bundle.

Deliberate lineage survives on purpose: heritage/ is Neutrino's own
literature; CHANGELOG history is history; and prose that says
"inherited from Neutrino" says something true.

## 0.0.19 — the hearth: a new splash

### Changed
- **The banner is now a sleeping cat by the fire** — the owner's call:
  the mug read as tea, and TEA is the owner's econometrics package; a
  cat asleep at the hearth is coziness without the collision. Same
  scene everywhere: the REPL banner (color and plain branches, cat in
  grey, flames warm, log in ember), both logos (brand/ and docs/), and
  the web page's banner(), which — found during this work — was still
  drawing NEUTRINO in ASCII art: the 0.0.18 identity pass caught
  strings, not artwork. The page's tagline is also corrected ("a small
  functional array language" -> "a heavier numerical language, warmly
  held").
- **The wordmark is now figlet-standard**: the owner's eye caught the
  misalignment — the hand-drawn z carried a stray | (| (_) |/ /|) and
  row five doubled a slash (\___//___|). The glyphs are taken from
  figlet's standard font verbatim and the composed banner was verified
  by rendering, not by squinting at source.
- Version/tagline indentation aligned; wasm bundle rebuilt so the
  browser reports 0.0.19.

## 0.0.18 — the documentation becomes Cozy's own

### Changed
- **Full identity-and-accuracy pass over every document**, prompted by the
  owner: the manual, book, packages guide, README, and the served web
  page all titled and spoke as Neutrino, and some claims were actively
  wrong — NEUTRINO_PLOT_TERM/`_OUT` (renamed at 0.0.7), `./neutrino`,
  `~/.neutrino_history`, "nine value kinds", "frozen at 2.x", nine
  packages. Everything now states Cozy's truth: eleven value kinds
  (Dual and Sparse in the type table), COZY_ env vars, cozy binaries
  and history, twelve packages, active development under the charter.
  Deliberate lineage mentions remain (the oscillation pun is retired
  with a nod). The eval doc row and mode-line name are fixed at their
  generator sources.
- **README rewritten for Cozy**: capabilities (duals, sparse, three
  backends with the measured eig timings), status, the twelve packages,
  build matrix including BACKEND= and wasm targets, and a sample
  session captured by execution — including dualeps(dual(3,1)^2) = 6.
- **New logos**: brand/logo.png and docs/logo.png now render Cozy's own
  REPL banner (the steaming mug, warm on navy); the old assets spelled
  Neutrino, including the one GitHub Pages served. index.html identity
  updated (title, og tags, header, book titles, GitHub link).
- **All three PDFs rebuilt** from the current markdown (they predated
  even the rename).

### Added
- **BOOK chapter 15, "The Cozy instruments"**: four problems with
  verified transcripts — a sparse Laplacian solved by cg without
  densifying; exact derivatives (machine vs hand derivative agreeing
  digit for digit, the Rosenbrock gradient exactly zero at the
  minimum); a constrained portfolio (eq + ineq via the augmented
  Lagrangian); and the estimation idiom (closure factories, NLLS
  recovering truth from noise, the GMM/MLE generalization in prose).
  BOOK transcripts: 358 -> 381, all captured by execution.
- Manual: Dual and Sparse rows in the value-kind table; contents entry
  for the dual-numbers section.

## 0.0.17 — the guard behind the wrong gate

### Fixed
- **The emacs-mode drift check now runs in every environment**: it is
  pure python but sat behind the "is emacs installed" gate, so it was
  dead in the emacs-less dev container from birth — a 0.0.12 hand edit
  to cozy-mode.el (the exact drift the generator exists to prevent)
  shipped through four green suites until the owner's X1, which has
  emacs, caught it. The gate now covers only the emacs-batch smoke
  test. Ledgered: a guard that only runs where a tool happens to be
  installed certifies everywhere else.
- **gen_emacs_mode uses gen_reference's exact full-row regex**: its
  looser four-field pattern matched a { "y", "m", ... } unit-key array
  in datestr's implementation, so the editor list carried a phantom
  builtin named y and the two generators reported different counts
  (172 vs 171) from the same table. One table, one regex, one truth:
  171 builtins, everywhere.
- editors/cozy-mode.el regenerated by the (now trustworthy) generator.

## 0.0.16 — the Accelerate acceptance: two findings, both fixed

### Fixed
- **Eigenvector phase anchor is now tolerance-aware** (eval.c): the
  "largest entry real > 0" convention selected its anchor by strict
  comparison, so vectors with tied component magnitudes (any 2-state
  Markov stationary problem) let last-ulp noise choose — differently
  per backend, found by the owner's Accelerate run. The anchor is now
  the FIRST entry within 1e-12 relative of maximal: deterministic on
  tier0, OpenBLAS, and Accelerate. Same class and same medicine as
  0.0.11's conjugate-pair sort. One BOOK transcript recaptured to the
  canonical sign.
- **The Fourier chapters no longer pin architecture-dependent noise**:
  displayed ~1e-16 coefficients (x86 libm dust that ARM rounds
  differently) are now chopped by a zap helper defined in the
  transcripts — the spectra display as the exact patterns the
  mathematics says, identically on every platform, and the "floats
  saying zero" teaching moved into prose where it belongs. Ledgered in
  LESSONS.md: platform-dependent output is the time-dependent-golden
  class wearing a new coat.

### Verified
- Full suite green under tier0 and OpenBLAS in-session; the Accelerate
  verdict on the owner's MacBook is the acceptance for this release
  (expected: 1046 + 625 all green, exit 0). Benchmarks from the Mac,
  recorded: inv(rand(400)) 0.031s, eig(rand(300)) 0.088s — Accelerate
  beats our OpenBLAS eig timing by 1.5x; tier0 by 234x.

## 0.0.15 — the Accelerate backend (charter sentence, completed)

### Added
- **BACKEND=accelerate**: the charter's performance clause names it —
  "build-time LAPACK backends; Accelerate on macOS" — and the increment
  is exactly what the seam promised: linalg_openblas.c travels
  UNCHANGED, because Accelerate exports the same Fortran LAPACK symbols
  under the LP64 interface (32-bit ints, matching the declared
  externs). One name macro (COZY_LAPACK_NAME) and one Makefile branch
  (-framework Accelerate); buildinfo().backend reports "accelerate".

### Verification status (recorded honestly)
- tier0 and openblas: full suite green in-session, both backends, as
  always. accelerate: this container cannot execute macOS, so the
  acceptance run is the owner's — `make BACKEND=accelerate test` on
  the MacBook is the verdict, per the lattice's top layer. The goldens
  never pin a backend name (cross-tier law since 0.0.5), so a green
  run there certifies without edits.

## 0.0.14 — the browser catches up: wasm bundle rebuilt

### Changed
- **docs/cozy.js rebuilt** after ten releases of recorded staleness
  (since 0.0.4): the browser now carries sparse matrices, dual numbers,
  autodiff.cz, optim.cz, the .cz package extension, all five books at
  current text, and the cozyPlot hook — verified by execution under
  node: dualeps(dual(3,1)^2) = 6 and the Cobb-Douglas constrained
  maximum [1.8; 2.8] run inside the bundle.
- **EXPORT_NAME Neutrino -> Cozy** (the last machine-name rename
  residue, waiting on exactly this rebuild); docs/index.html updated.
- **make wasm-ubuntu**: the container recipe as a target — clang-15's
  four C23 shims plus NODE_PATH — so the wasm build is a session
  capability again, exactly as it was in the Neutrino era. The full
  apt/dpkg path is recorded in PLAYBOOK ("The Ubuntu emscripten
  recipe").

### Added
- Design docket entry 8: tier-1 optimization backend (NLopt), parked
  as WHEN, not if, by the owner's ruling — with the design sketch,
  the per-evaluation-callback caveat, license diligence note, and two
  written triggers.

## 0.0.13 — optimization: the capability stack completes

### Added
- **packages/optim.cz** (design entry 3, one release after its blocker
  fell): `minimize(f, x0)` / `maximize(f, x0)` — BFGS with an
  inverse-Hessian update and Armijo backtracking on autodiff's exact
  gradients, returning {x, fx, iters, converged}. Maximization is
  negation, shipped as a first-class name. `minimize_box` /
  `maximize_box(f, x0, lb, ub)` by gradient projection. `minimize_con`
  / `maximize_con(f, x0, cons)` for general constraints by augmented
  Lagrangian, cons = {eq, ineq} (either or both; record reflection
  checks which) — and the inner solver IS minimize: solvers as
  packages, composing with themselves. The AL's max(0, mu + rho g) is
  dual-differentiable one-sided at the kink, so the same autodiff
  machinery drives the constrained case with no special code.
- tests/59_optim.test: invariant asserts only (convergence flags,
  residual norms, known optima within tolerance). No linalg kernel is
  involved anywhere in the package, so the goldens are backend-
  invariant by construction. The chartered use case is a golden: the
  Cobb-Douglas budget maximum lands on the analytic (a m / p, b m / q).
- The pipeline that proves the thesis: Rosenbrock from (-1.2, 1) to
  [1; 1], and a constrained utility maximum, with no hand-written
  derivative or Lagrangian algebra anywhere in user code.

### Design
- Function form now; the `minimize[x = x0]` binder syntax is PARKED in
  the docket as pure desugar onto this core, with a written trigger —
  desugar first, syntax is earned. Entries 1-4a of the founding
  docket are now all shipped or reviewed.

## 0.0.12 — dual numbers: derivatives become language values

### Added
- **VAL_DUAL + ELT_DUAL** (design entry 4a, scoped by the owner's ruling
  that multivariate minimization drives everything): dual scalars a+b*eps
  with eps^2 = 0 as a fifth numeric rank, AND dual as a dense element
  type — because grad over array arguments IS the deliverable. The
  funnel architecture paid off completely: scalar_arith_k's rank-3 case
  makes every elementwise op, matmul, kron, dot, trace, and reduction
  flow duals with zero changes to those functions.
- **Chain rules across the whole kernel library**: sin, cos, tan, exp,
  log, sqrt, cbrt, the inverse and hyperbolic families, log2/log10,
  gamma/lgamma (via digamma), erf/erfc; pow with the exact integer rule
  and exp·log for real exponents; abs -> sign (one-sided at the kink,
  sign(0) = 0); floor/ceil/round/trunc/sign -> derivative exactly 0.
- **Builtins** dual(a, b) (elementwise; dual(x, seed) seeds a
  direction), dualval(x), dualeps(x) — the accessors TOTAL on plain
  numbers so constant branches differentiate to 0.
- **packages/autodiff.cz**: d(f) and grad(f). The engine is one line:
  d = fn f -> fn x -> dualeps(f(dual(x, 1))). grad seeds e_i per
  component in x's own shape. The Rosenbrock gradient vanishes at
  [1; 1] in the packages guide, no hand-written derivative anywhere.
  Entry 3 (optimization) is now unblocked.
- tests/58_autodiff.test: every derivative assert is EQUALITY — dual
  arithmetic is exact, so the goldens are too.

### Promotion law (stated once, gated everywhere)
- int/float lift into dual (eps = 0). **Dual and complex do not mix** —
  a recorded rejection (duals replace complex-step differentiation);
  arithmetic, comparisons, matrix literals, and index-assignment all
  gate with a teaching error naming dualval. Sparse and dual do not
  meet. Dense linalg kernels (eig/svd/\/det/lu/qr/chol), norm, and the
  real binary kernels (atan2/hypot/mod/rem) gate rather than silently
  drop the derivative — every refusal names the differentiable route.
- Comparisons read the VALUE PART: conditionals inside differentiated
  functions take the branch the values take.

### Fixed
- **value_retain's positional immediacy test** (`kind >= VAL_STRING`)
  dereferenced the new immediate kind's doubles as a pointer; replaced
  with an explicit kind_is_heap switch. Ledgered in LESSONS.md: range
  tests over append-only enums break at the end, where appends land.

## 0.0.11 — tier-1: the seam pays off

### Added
- **linalg_openblas.c** — the LinalgKernels table answered by LAPACK
  (OpenBLAS): solve->zgesv, det->zgetrf, eig_herm->zheev,
  eig_gen->zgeev, svd->zgesvd, chol->zpotrf. `make BACKEND=openblas`;
  eval.c unchanged, exactly as the 0.0.5 seam promised. Row/column-
  major marshalling lives in the backend; det exploits det(A)=det(A^T)
  to skip its transpose entirely.
- **Measured** (benchmarks beat adjectives): inv(rand(400)) 0.142s ->
  0.028s (5x); eig(rand(300)) 20.6s -> 0.131s (158x).

### Changed
- **Backend-invariance conventions in eval.c** (the screen is the spec;
  ordering and display are language law, not backend accident): (1)
  eigenvalue components within 1e-12 relative of zero snap to exact
  zero before the pair sort — zgeev's 2.8e-17 noise neither prints nor
  reorders; (2) singular values below 1e-12 of s[0] snap to zero; (3)
  the eigenpair sort treats real parts equal within tolerance as equal
  (a conjugate pair's 2±1e-16 no longer flips order with the backend's
  rounding). All three use the 1e-12 relative rule that already decided
  eigenvalue realness. tier0 output is bit-identical before and after.

### Verified
- The conformance suite as backend-equivalence harness, for real: all
  990 goldens AND all 606 verified transcripts pass byte-identically
  under BOTH backends. Four goldens initially failed under openblas —
  every one traced to the noise/ordering class above and fixed in the
  language layer, never by weakening a golden.

## 0.0.10 — packages speak .cz

### Changed
- **The package extension is .cz** (was .nu, the Neutrino residue kept
  deliberately at 0.0.7 and unkept deliberately now, by owner decision).
  20 files renamed (packages/, tests/dis/, tests/data/), 42 files
  edited: every load()/ls()/save() string in goldens, books, doc-table
  examples, test harnesses, generators, and the emacs mode's
  auto-mode-alist. load() itself never had extension logic — filenames
  are strings — so the language changed not at all.
- **Conformance edits, recorded per the charter**: inherited goldens
  and transcripts naming package files were amended (.nu -> .cz). These
  pin environmental facts, not semantics — the same amendable class as
  version strings and the exact package list. "Every Neutrino program
  is a valid Cozy program" survives: no grammar, builtin, or meaning
  changed; a Neutrino script calling load("dist.nu") needs its file
  renamed, nothing else.
- Fixed in passing: the emacs mode header still read "Editing support
  for Neutrino" — a straggler from the 0.0.7 rename.
- Deliberate keep: heritage/ verbatim, changelog history, and the
  charter's lineage prose ("symb.nu-style") — true statements about
  Neutrino, whose files were .nu.

## 0.0.9 — sparse linear algebra, the designed way

### Added
- **packages/sparselin.nu**: `cg(A, b)` (conjugate gradient, SPD solve
  -> {x, iters, relres}) and `powerit(A)` / `powerit_from(A, x0)`
  (dominant eigenpair by power iteration with Rayleigh estimate) — pure
  Cozy on the founding kernel S * v, per entry 1's law that solvers are
  packages. Goldens assert invariants only: residuals, known spectra
  (the path Laplacian's 2 + sqrt(2)), CG's finite termination (3x3 in
  3), never iteration internals.
- **Teaching gates on the dense kernels**: eig/svd/chol/det/lu/qr on a
  sparse argument now name the way through (sparselin.nu, or
  f(dense(S)) if it fits) via one central gate in the to_cplx
  marshaller; inv's gate also explains why sparse inverses are usually
  unwanted (they are dense). Fixed in passing: inv and det on sparse
  previously produced misleading "expected a square matrix" rejections.
- Suite: 990 goldens (976 + 14).

### Notes
- The 200x200 sprandn+CG transcript in PACKAGES.md is the trigger
  workload entry 1 originally waited for, now solved without forming a
  dense matrix — the founding-kernel scope validated.
## 0.0.8 — sparse lands (entry 1 core, owner-triggered)

### Added
- **The sparse value kind**: SpObj (CSR; float/complex), a separate kind
  so every builtin that does not know sparse rejects it by type — the
  strings-retrofit mechanism applied at the foundation. Print form and
  who legibility per the ratified design (`sparse RxC, nnz = N` +
  triplet lines, capped).
- **The promotion law, live**: S+S, S-S, S.*S, k*S, S/k, -S, S' stay
  sparse (exact cancellation drops entries; k=0 empties); S+scalar,
  mixed dense, S==S, S\b, S*S gate with teaching errors naming
  dense(S) or the iterative path. All in one sparse_binop above the
  math, mirroring the linalg seam split (sparse.c owns CSR, eval.c
  owns dims, messages, and the law).
- **The founding kernel**: sparse-matvec (S * dense column), complex-
  aware. CG-class solvers are now writable as packages.
- **Constructors**: sparse(A), sparse(i, j, v, m, n) (1-based,
  duplicates summed, zeros dropped), dense(S), nnz (sparse and dense),
  speye(n), and — the owner's additions — sprand/sprandn(m, n, d),
  drawing distinct positions from the reproducible session RNG.
  Their goldens assert nnz/dims/bounds, never values: the sampling
  stream is an implementation detail, not a contract.
- Scalar reads S[i, j] (in-bounds, strict); slice reads are recorded
  docket residue with their own trigger.
- Suite: 976 goldens (947 + 29); the new kind is invisible to all 947
  inherited ones by construction.

### Process
- Entry 1's trigger was overridden by the owner; the override is
  recorded in the docket per the parked-design law.

## 0.0.7 — the rename (neutrino baggage, once and for all)

### Changed
- **Every name the machine uses is now cozy.** The rename rule: machine
  names change, lineage prose stays. Renamed: deploy.sh's tarball path
  (cozy/version.h — the reference that broke the 0.0.6 deploy) and usage;
  COZY_VERSION/COZY_BUILT and every include guard (NEUTRINO_*_H ->
  COZY_*_H); env vars COZY_PLOT_TERM/COZY_PLOT_OUT/COZY_MANUAL (C and
  every test harness together); the transcript prompt in all three books
  and the verifier's prefix (the correlated pair changed atomically —
  947 goldens and 597 re-executed transcripts arbitrate); editors/
  cozy-mode.el with cozy- symbols throughout and run-cozy; docs/cozy.js
  and the page; the readline app name; /usr/local/share/cozy; workspace
  save headers; usage strings; truthful re-chosen doc examples
  (upper("cozy") %= "COZY" and friends — executed claims, so blind
  substitution was not an option).
- **Module.cozyPlot** replaces the wasm plot hook; docs/index.html
  defines the old name as an alias until the wasm bundle rebuild (the
  bundle is prebuilt and still calls the old hook — remove the alias
  after the next emcc build). The browser editor's localStorage key
  also renamed; old drafts are orphaned, acceptable at 0.0.x.

### Kept, deliberately
- The `.nu` extension and load("x.nu"): inherited language surface,
  referenced by conformance goldens — renaming it would break the
  machine-checked "every Neutrino program is a valid Cozy program"
  invariant. If ever revisited, it goes through the docket.
- heritage/ verbatim (the frozen record), changelog history, and every
  true lineage statement in comments and books.
- nrt.h: inert residue, nothing couples to it; recorded here so the
  keep is a choice, not an oversight.

## 0.0.6 — the userland-unblocking pair

### Added
- **`strfind(s, pat)`** — every 1-based start position of pat in s
  (overlapping counted), [] if none: the one genuine string-extraction
  gap from heritage/KNOWN_LIMITATIONS.md, chartered "fix from day one".
  Pattern-directed slicing is now indexing, not a character scan.
- **Record reflection (design entry 5)**: `getfield(r, name)` (dynamic
  read, strict error on missing — mirroring literal access) and
  `setfield(r, name, v)` (a NEW record, field replaced in place or
  appended; the source record untouched). Construction is the {} fold,
  per the entry's repair note — no parallel-array constructor, which
  would require the heterogeneous container entry 6 rejects. Generic
  record map/merge, k=v parsing, and serialization round-trips now
  live in userland. Ownership law inside: a reflection-built record
  strdups every key and sets owns_keys — the field name arrives as a
  refcounted string whose bytes do not outlive it, and owns_keys is
  record-wide, so mixed borrowed/owned keys would free source pointers.
- **Sparse design ratified (entry 1, 2026-08-08)**: separate value kind,
  the zero-preserving promotion law, reads-yes/writes-no indexing,
  triplet print form, builtins-only surface, matvec as the sole founding
  kernel with CG-class solvers as packages. Four rejections recorded;
  the seam question sub-parked; the implementation trigger unchanged.
- Suite: 947 goldens (930 + 17), 143 + 101 + 353 verified transcripts.

## 0.0.5 — the dispatch seam

### Added
- **The LAPACK dispatch seam (design entry 2), cut while cheapest.** New
  `linalg.h` / `linalg_tier0.c`: one `LinalgKernels` table — solve, det,
  eig_herm, eig_gen, svd, chol — operating on raw buffers with status-code
  failures; eval.c's six shells now marshal, dispatch through
  `cozy_linalg()`, and keep every error message and observable convention
  (the Hermitian test, eigenpair ordering, the general path's phase rule)
  above the seam. The hand-rolled kernels moved verbatim; `inv` still
  routes through solve, matching its future dgesv mapping. QR/lstsq stay
  in eval.c until a tier-1 backend gives the table a reason to widen.
- **`make BACKEND=tier0`** — the build-time backend switch. Only tier0
  exists; a future `linalg_accelerate.c`/`linalg_openblas.c` is a new
  Makefile branch and zero eval.c changes. The tier0 file also rides the
  wasm bundle (tier 2) until a CLAPACK build earns its way in.
- **`buildinfo()`** -> {backend, version, built}: the introspection entry 2
  declares non-negotiable. Six goldens (tests/53_buildinfo.test) assert
  structure and the version cross-check only — never the backend string,
  never the timestamp — so the conformance suite stays a backend-
  equivalence harness by law.

### Fixed
- **The unguarded reference generator (ledgered in LESSONS.md).**
  tools/gen_reference.py had no --check mode and nothing ran it in make
  test: a doc-table edit without regeneration would have shipped a stale
  builtin reference under a green suite — found when this release's
  buildinfo row needed the manual regenerated by hand. Now: --check wired
  into the test target, proven to fire red on a corrupted cell before
  being trusted.
- Suite: 930 goldens (924 + 6), 139 + 101 + 353 verified transcripts,
  ASan clean at -O1 including the new translation unit. Deferred to an
  emcc machine: the wasm rebuild (docs/neutrino.js still carries 0.0.4).

## 0.0.4 — re-sync at Neutrino 2.28.0: the full harvest

### Inherited
- Everything through Neutrino v2.28.0, notably: the invisible-alias whof
  fix and autocall explainer (2.19.3); systematic format modes —
  format("fixed"/"sci"/"auto", d) — with the five money problems
  recaptured in fixed-decimals (2.20.x); the golden-section integrator
  launch that survives symmetric-zero traps, found by the Fourier
  fan-out, plus Problem 10.6 (2.21.0); mean(mask) joining its reduction
  family (2.22.0); Appendix G "Two languages, five problems" with both
  fairness audits baked in (2.23.x); the demo tour rebuilt on eval —
  single-source honesty — with pause() pacing (2.24.0) and real browser
  waiting via Asyncify (2.26.0); the Fourier synthesis pair 10.7/10.8
  — a corner smooths, a jump rings (2.25.x); who's load groups — 
  packages collapse to shelves (2.27.0); and clear("package") unloading
  a shelf whole (2.28.0). 924 goldens, 136 + 101 + 353 verified
  transcripts, versioned-tarball delivery law.
- Cozy's own soul carries forward unchanged: CHARTER, the seven-entry
  design docket (design/DESIGN_NOTES.md), the mug banner, cozy> prompt,
  .cozy_history, and the heritage/ mirrors refreshed to current law.
- Prior cozy release notes preserved in COZY_CHANGELOG_HISTORY.md.


---

*Neutrino lineage below (inherited changelog):*

## 0.0.3 — the namespace law

### Added
- **Design entry 7: records are the module system.** No module kind, no
  import syntax — package namespaces are records plus the reflection
  pair, with dist.nu as the production precedent. The entry records the
  interpreter-verified law that shapes it: globals resolve at call time,
  so pack-then-prune breaks packages (helpers stay global; hide them by
  convention), while the same late binding grants free mutual recursion
  through the record's own name. Neutrino itself stays flat-plus-
  subrecords by explicit anti-decision (transcript churn buys nothing).

## 0.0.2 — the re-sync

### Added
- **Everything Neutrino learned since the fork.** Cozy 0.0.1 froze at
  Neutrino 2.13.1; this release re-syncs to 2.19.2, inheriting by the
  conformance contract: the symb parser and textbook printer with
  tofun/ffun/dfun (string in, function out), the demo tour package,
  elseif/eval/names/input/pause (the five owner-sanctioned additions —
  eval("r." + name) is the reflection wall's first door), the lit svg
  legend, the vignette plates and og-card, the two-workstation deploy
  (merge -s ours), and the PLAYBOOK's verdict law. 905 goldens, 159
  builtins, 113 + 99 + 318 verified transcripts — all green under the
  Cozy name before any Cozy-original code exists.
- **strfind remains the recorded string gap** (indexing and slicing were
  inherited all along); the reflection pair getfield/setfield remains
  design entry 5, with eval as the interim read-half.

## 0.0.1 — the fork

### Added
- **Cozy exists.** The full Neutrino 2.13.1 tree forked under the Cozy
  name, per the charter's founding task: binary renamed (cozy), REPL
  prompt (cozy> ), a new banner (the mug — the machine that handles the
  particles gently), --version, history file (.cozy_history), and the
  workbench page retitled. Version 0.0.1.
- **The founding documents merged into the repository**: CHARTER.md at
  the root, design/DESIGN_NOTES.md (six designs: sparse, LAPACK backend
  tiers, optimization, the jet family, record reflection, the type
  inventory — every one carrying a Neutrino friction receipt), and
  heritage/ (PLAYBOOK, LESSONS, KNOWN_LIMITATIONS, the Neutrino README).
- **Conformance inherited whole**: the complete golden suite (895),
  the three verified books (105 + 92 + 304 transcripts), doclint, the
  generators, ASan — all green under the Cozy name before any new code.
  Deliberately unrenamed: NEUTRINO_* environment variables and the
  version.h macro names (lift compatibility; a recorded task), and every
  heritage document verbatim.
