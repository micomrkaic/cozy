# Cozy changelog

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
