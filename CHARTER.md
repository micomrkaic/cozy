# The Cozy Charter

*Founding document of the successor to Neutrino. Read by every collaborator
— human or AI — at the start of every session. This file, PLAYBOOK.md, and
LESSONS.md are the transferred knowledge of the Neutrino project
(2025–2026, frozen at 2.x); together they are the operating agreement.*

## What Cozy is

A heavier numerical language built on Neutrino's constitution — a
production-level instrument: fully performant in the native REPL
(build-time LAPACK backends; Accelerate on macOS), with WASM kept for
reach and teaching, correct but slower. Neutrino
proved the methodology on a calculator-scale core; Cozy applies it where
weight belongs: in the numerics, not the semantics. The syntax, value
philosophy, and pipe algebra carry over from Neutrino nearly whole — a
Neutrino user should feel at home in the first minute.

**The three founding capabilities** (each waits in DESIGN_NOTES until its
design is written and its trigger is real, per the parked-design
discipline):

1. **Sparse matrices** — a sparse value kind with legible sparsity: `who`
   shows `sparse 10000x10000, nnz = 31415`; dense/sparse crossings are
   explicit (`dense()`), never silent; one promotion table for
   {scalar, dense, sparse} binary ops, stated once.
2. **External LAPACK** — real `eig`/`svd`/`solve` at scale, replacing the
   hand-rolled kernels. Budget the browser story early (f2c'd LAPACK or
   CLAPACK for wasm) — toolchain fragility is a recorded Neutrino debt.
3. **Optimization** — minimizers and root-finders beyond `fminbnd`/`fzero`;
   Neutrino's index-bound binder syntax is the natural surface
   (`minimize[x = x0] f(x)`).
4. **First-class differentiation** — dual numbers (a core value kind
   mirroring complex; exact forward-mode AD of arbitrary functions, and
   the gradient engine for capability 3) plus `ast(f)` quotation (a
   reflection builtin exposing function bodies as symb.nu-style record
   trees, so symbolic work stays in packages). Both additive; see
   design/DESIGN_NOTES.md entry 4.

## What transfers, and how

- **heritage/** — PLAYBOOK.md (the engineering constitution: principles
  with their scars, the verification lattice, the release rite, the trap
  almanac, the lift-vs-re-derive manifest), LESSONS.md (the case law:
  every catch with its mechanism), KNOWN_LIMITATIONS.md (Neutrino debts;
  several are Cozy's birthright to fix — per-point plot styling, the
  golden format's fresh-session limit, the doc table in C strings, the
  two binding regimes).
- **lift/** — code and machinery that travels nearly verbatim: arena.c;
  value.c/.h (value model, refcounting, the env protection boundary —
  keep the stated ownership conventions and enforce them in review);
  the harness family (golden runner, transcript verifier with its inert
  print-sentinel, doclint); the generator pattern (tools/*.py, every one
  with --check wired into make test); the Makefile shape (dep-tracked
  parallel objects, separate ASan tree, pinned default goal); deploy.sh's
  tag discipline.
- **design/** — an empty DESIGN_NOTES.md in the Neutrino format: parked
  ideas with written triggers, marked SHIPPED or rejected, never silently
  dropped. The three founding capabilities are its first entries.

## Syntax inheritance

Cozy speaks Neutrino. This is a contract, not a mood:

- **The syntax is inherited whole** — lexer, Pratt parser, AST, and every
  desugar (pipes, tee, fan-out, chained comparisons, where clauses,
  index-bound reductions) come from Neutrino 2.5.0 in syntax/. Start Cozy
  as a fork of the full Neutrino tree, then diverge by *adding* — value
  kinds, builtins, packages — never by changing what parses.
- **The executable specification travels with it**: conformance/ holds
  Neutrino's complete golden suite. It is Cozy's inherited acceptance
  test, run by make test forever. "Every Neutrino program is a valid Cozy
  program with the same meaning" is thereby a machine-checked invariant:
  a Cozy change that breaks a Neutrino golden is a regression, full stop.
  (Goldens that pin Neutrino's version string or its exact package list
  may be amended — as conformance edits, recorded in the CHANGELOG.)
- **New surface syntax** (e.g. sparse literals, `minimize[x = x0]`) goes
  through the parked-design rite and must be *additive*: previously
  illegal shapes only, in the tradition of Neutrino's own chain and sigma
  releases, which changed the meaning of zero legal programs.
- The three books remain true of Cozy's core by construction; Cozy's own
  literature documents only what it adds.

**Re-derive, do not lift** (PLAYBOOK §VI, amended for syntax inheritance):
the type story (if sparse wants real types, that is a design, not a
port); the module system (Neutrino's load-into-session is right for a
calculator, wrong here — the parked load-path sketch is the starting
point). The grammar is *not* on this list: it is inherited under the
contract above.

**Fix from day one** (PLAYBOOK §VII, chosen consciously this time): golden
sessions get an explicit session-block format; one binding regime, not
two; the documentation table lives in a data file the build embeds; pin
the emscripten toolchain; lint the rites (release refuses if CHANGELOG
lacks the version); and add strfind (pattern position — the one genuine
string-extraction gap; indexing and slicing turned out to be inherited
all along, a lesson recorded in heritage/KNOWN_LIMITATIONS.md: the
goldens outrank the maintainer's memory).

## The operating agreement

The house rules that made Neutrino trustworthy, restated as obligations:

1. **Verify before asserting.** No claim about behavior without executing
   it. Grep before designing. Rebuild before testing — a passing stale
   binary is a lie.
2. **The screen is the spec.** Observable state couples to what the user
   saw, mechanically, at one site.
3. **No document ships unexecuted.** Transcripts are captured by running
   them and re-verified forever. Capture and verification must not share
   machinery (the correlated-sentinel incident, LESSONS §8).
4. **Desugar first; opcodes are earned.** Features compile into the small
   trusted kernel until they demonstrably cannot.
5. **Every changed artifact is a new version.** Full rite each release:
   implement → exercise → goldens → fuzz if grammar → suite + ASan →
   docs by execution → regenerate → CHANGELOG → bump → bundle → clean
   room → deliver.
6. **Parked designs wait for friction transcripts.** Overrides are
   recorded as overrides.
7. **Peer collaboration.** Corrections are immediate and welcome in both
   directions; difficulty estimates are honest; catches are ledgered in
   LESSONS.md with their mechanisms, not buried.

## Session bootstrap

Each working session begins from this repository's state: read CHARTER
(this file) and the current DESIGN_NOTES docket; obtain the working tree
— clone the GitHub repository (the canonical source; sessions have
network access to github.com) or, as fallback, extract a tarball the
owner uploads; rebuild; run the suite before touching anything. AI
sessions hold no storage between conversations and no access to other
projects' files: the repository is the only memory the code has. The first session's task is written below and replaced as the
project advances.

**Current task:** 0.0.5 shipped the dispatch seam (entry 2): the
LinalgKernels table, BACKEND=tier0, buildinfo, and the reference
generator's missing --check (ledgered). Next: the sparse design (entry 1)
to review, then the first tier-1 backend when its trigger fires. strfind and
the reflection pair shipped in 0.0.6; the docket holds the ratified
sparse design awaiting its trigger.

---

*Neutrino took its name from the particle; Cozy from the machine that
handles them — and from what a daily instrument should be. The methodology
was the product; this is its second application.*

## Baton — 2026-08-03, re-sync at Neutrino 2.28.0

This fork carries the complete 2.19.3–2.28.0 harvest (see CHANGELOG).
Design docket stands at seven entries; next chartered steps unchanged:
the LAPACK dispatch seam (entry 2), then the sparse review (entry 1).
Namespace law (entry 7) now has production tooling upstream — who's
load groups and shelf-aware clear — worth studying before Cozy's
module-system work begins: the clutter problem namespaces were reached
for was solved orthogonally, so Cozy's records-as-modules can be about
architecture, not tidiness. Delivery convention: cozy-vX.Y.Z.tar.gz,
restore via
  tar xzf "$(ls /mnt/user-data/outputs/cozy-v*.tar.gz | sort -V | tail -1)" --strip-components=1

## Baton — 2026-08-08, 0.0.5: the seam is cut

Entry 2's dispatch seam is real: linalg.h/linalg_tier0.c, six kernels
behind one table, eval.c owning marshalling and every observable
convention. A tier-1 file now costs a Makefile branch, nothing else.
buildinfo's goldens are deliberately structure-only — the conformance
suite is the backend-equivalence harness, so no golden may ever pin a
backend name. The wasm bundle still carries 0.0.4 (no emcc in the
session container); rebuild on an emcc machine before the pages deploy.

## Baton — 2026-08-13, v0.1.0

The four founding capabilities are real: sparse (sparselin, cg at
scale), external LAPACK (entry 10 phases 1-3; solve/det/eig_sym/svd/
chol/gemm on real fast paths, both platforms), first-class
differentiation (duals, hyper-duals, total ast(f) quotation), and
optimization (entry 14: OPTIM=nlopt, nlmin with exact dual gradients,
optim.cz dispatching; the AL retained as tier0 and the wasm story).
Acceptance: full suite green under tier0 and openblas+nlopt on two
Linux machines (container + owner's Debian). ONE OPEN FORMALITY:
make BACKEND=accelerate OPTIM=nlopt test on the owner's MacBook
(in Alexandria at the milestone; the owner in Duluth GA) — record its
verdict in LESSONS when the reunion happens. Version counter: 0.1.x
for the remaining ladder steps (a derivative-free book problem once
transcripts can be build-conditional, global methods on a transcript);
0.2.0 at the next capability boundary the docket defines.