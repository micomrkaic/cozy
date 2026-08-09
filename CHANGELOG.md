# Cozy changelog

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
