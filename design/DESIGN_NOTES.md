# Cozy Design Notes

*Parked designs in the Neutrino tradition: each waits with a written
trigger; each ends SHIPPED or rejected, never silently dropped.*

## 1. Sparse matrices — CORE SHIPPED 0.0.8 (owner override, 2026-08-08)

**Override recorded.** The trigger ("a workload a dense matrix cannot
hold") had not fired; the owner pulled it consciously the same day the
design was ratified, adding sprand/sprandn to the founding set. Recorded
per the parked-design law: overrides are overrides, not silent drops.

**v1 residue.** Indexing reads are scalar-only (S[i, j]); slice reads
returning sparse remain designed-but-unshipped. Trigger: the first
session that wants a submatrix.

**Representation.** A separate value kind (SparseObj), never a flag on
ArrObj: a distinct kind inverts the default so every builtin that does
not know sparse rejects it by type, and behaviors replace tested
refusals one at a time — the mechanism that made the strings retrofit
safe, applied at the foundation. CSR; float and complex elements from
day one (the Cplx machinery exists).

**The promotion law, stated once.** Zero-preserving ops stay sparse
(S .* k, S + S, S .* S, unary negation, transpose); zero-breaking ops
gate with a teaching error naming the way through ("sparse + scalar
would densify — wrap in dense(S) if intended"). matmul: sparse × dense
vector -> dense vector (the founding kernel); sparse × sparse -> sparse;
sparse \ b errors, pointing at dense() or an iterative solver.

**Indexing.** Reads yes (S[i,j], slices return sparse); writes no —
indexed assignment into CSR is a quadratic trap, so it errors pointing
at the triplet constructor. Construct-then-use, like the records
convention.

**Print form.** Summary line (`sparse 3x3, nnz = 2`) then `(i,j) v`
triplet lines, elided past a cap; `who` shows `sparse RxC, nnz = N`.
Deterministic, golden-able in fresh sessions.

**Surface.** Builtins only in v1 — sparse(A), sparse(i,j,v,m,n),
dense(S), nnz(S). No new grammar; sparse literals, if ever, are a
separate parked entry through the additive-syntax rite.

**Kernel scope.** Exactly one kernel at founding: sparse-matvec. It
serves every trigger workload (Markov chain = matvec iteration,
network = matvec, PDE = matvec + CG), and CG/pcg become packages
written in Cozy on top of it — one kernel, the whole capability class.

**Rejections recorded.** Int/bool sparse (no workload; revisit only
with a friction transcript). Silent densification anywhere. Sparse
indexed assignment. Direct sparse solve at founding.

**Sub-park.** Whether LinalgKernels (entry 2's table) grows sparse
entries or sparse gets its own table. Trigger: the first profile
showing tier0 matvec as the bottleneck.

**Trigger (unchanged, governs implementation).** The first real
workload (a Markov chain, a PDE grid, a network) that a dense matrix
cannot hold.

## 2. External LAPACK — SHIPPED (seam 0.0.5, tier-1 OpenBLAS 0.0.11; 990 goldens + 606 transcripts byte-identical under both backends)

**Requirement (owner, restated).** Cozy is a production-level tool: the
native REPL is fully performant; WASM remains available for reach and
teaching, correct but slower. Performance is a build-time choice, never
a language-visible one.

**Architecture: three backend tiers, one dispatch seam.**
- Tier 0 — the hand-rolled kernels inherited from Neutrino: always
  compiled, zero dependencies, so make on a bare machine works forever.
- Tier 1 — system BLAS/LAPACK selected at build time: the Accelerate
  framework on macOS (-framework Accelerate, zero install, Apple-tuned),
  OpenBLAS on Linux, MKL optional. dgesv/dsyev/dgesvd/dpotrf behind the
  existing solve/eig/svd/chol/inv/det names.
- Tier 2 — WASM: reference LAPACK (f2c/CLAPACK) through emscripten.
All routes pass through ONE kernel table chosen at build time; the
language never knows which backend answered.

**Testing doctrine (Neutrino's scars, promoted to law).** The
conformance suite doubles as a backend-equivalence harness: the same
goldens run against every tier. Backends WILL differ in eigenvalue
ordering, eigenvector signs, and last-ulp noise — therefore: never
golden noise (property-assert; the macOS libm lesson), never assume
eigen ordering (select by find on values; the Markov lesson), and all
backend-path goldens are tolerance-based by law since threaded BLAS can
vary run to run (reduction order).

**Introspection.** A buildinfo builtin reporting backend, threading, and
versions — a production tool whose user cannot tell Accelerate from the
fallback kernels is not verifiable.

**Trigger.** The first eigenproblem where the hand-rolled kernel's size
ceiling or accuracy is actually hit — or the fork itself, since the
dispatch seam is cheapest to cut before kernels multiply.

## 3. Optimization — SHIPPED 0.0.13 (packages/optim.cz on autodiff: BFGS +
Armijo `minimize`/`maximize`; box bounds by projection; general eq/ineq by
augmented Lagrangian whose inner solver is minimize itself — the owner's
constrained-maximization requirement is the Cobb-Douglas golden). PARKED
RESIDUE: the `minimize[x = x0] f(x)` index-bound binder syntax — pure
desugar onto the function form when it comes; trigger: the first session
where wrapping the objective in fn is the visible friction.

## 3-old. Optimization

**Sketch.** `minimize[x = x0] f(x)` on the index-binder surface;
Nelder–Mead and a quasi-Newton to start; constrained later or never.
**Trigger.** The first optimization done by hand-rolled iteration at the
prompt that deserved a verb.

## 4. First-class differentiation — 4a SHIPPED 0.0.12; HESSIAN INCREMENT
SHIPPED 0.0.24 (VAL_HDUAL/ELT_HDUAL hyper-duals — the fixed-size jet the
Hessian actually needs; hess(f) and minimize_newton/maximize_newton on top;
Newton solves a PD quadratic in one iteration, golden-pinned. Residue:
gamma/lgamma refuse hyper-duals pending a trigamma implementation — trigger:
the first second-derivative-of-gamma need, likely an MLE with gamma-family
likelihoods. jet(k>2) remains parked: Taylor territory, no named user.)
## 4-was. First-class differentiation — 4a SHIPPED 0.0.12 (dual scalars + ELT_DUAL
dense element + autodiff.cz d/grad; complex×dual a recorded rejection with a
teaching gate; jet(k) parked — dual is its k=1 instantiation, trigger: the
first Hessian or Taylor-series need). 4b SHIPPED 0.0.30: ast(f) reparses the
closure's retained source into a symb-shaped record tree ({op="pow", l,
n} with numeric exponents, unary minus as mul(-1), single-arg calls as
{op=name, l}); v1 scope = the symb expression subset, everything else a
teaching gate. The chartered payoff is a golden: ddx(ast(fn x -> x^2 +
sin(x)).body) simplifies to 2x + cos(x). The residue trigger FIRED the day it
shipped (the owner quoted op_dot, whose body uses .*) and was paid at
0.0.31: quotation is now TOTAL over the expression and statement grammar
— elementwise ops, comparisons, logic, pipes, calls of any arity (argc +
a1..aN fields), indexing, fields, ranges, matrices, transposes, if/let/
assign/blocks/loops/break/return, nested fn — with symb's shapes kept
where symb has opinions (pow-with-constant carries n; single-arg calls
are {op=name, l}). ENTRY 3 (optimization) IS NOW UNBLOCKED: grad exists.

**Motivation.** Neutrino's symb.cz proved symbolic differentiation is
expressible with expression trees as records — but only via constructor
entry (add(powc(X, 3), ...)), because functions' syntax is invisible and
operators cannot be overloaded. Cozy should differentiate *functions as
the user wrote them*. Two additive mechanisms, complementary:

**4a. Dual numbers — exact derivatives of arbitrary functions.**
A core value kind a + b·eps with eps^2 = 0, mirroring the existing
complex plumbing (value kind, arithmetic tables, promotion row,
transcendental extensions: sin(a + b eps) = sin a + b cos a eps, etc.).
Builtins: dual(a, b), dualval(x), dualeps(x). Then forward-mode AD is a
one-line package: d = fn f -> fn x -> dualeps(f(dual(x, 1))) —
machine-exact, no step size, works through closures, conditionals, and
composition. **This is also the gradient infrastructure the optimization
capability (entry 3) requires**; implement duals before minimize.
Estimated scope: the complex kind's footprint again — well-trodden.

**4b. Quotation — ast(f) exposes the tree the function already carries.**
One reflection builtin returning a function's body as nested records in
the symb.cz encoding ({op, l, r, v, n}), differentiation-by-recursion
staying entirely in packages. Rules: the tree is reported with respect to
the lambda's parameters; free and captured names appear as named
constants; ast of a builtin is an error. Optional inverse (defn(tree))
can wait — symb.cz's evalx already interprets trees. Deleting symb.cz's
constructors and pointing ddx at ast(f) upgrades the package from clever
hack to real symbolic engine with no grammar change.

**Gradients, Hessians, and the jet family (supersedes dual + hyper-dual
as separate designs).** The general construction is the order-k JET:
arithmetic on polynomials truncated at t^(k+1). A dual is jet(1); the
hyper-dual is its two-variable cousin; one parameterized family replaces
both. Payoffs, in increasing order:
- grad(f): n jet(1) passes assembled into a vector-returning function.
- Exact Hessians: two-variable jets (eps1, eps2) for Newton curvature.
- ARBITRARY-ORDER DIRECTIONAL TAYLOR IN ONE PASS: for F: R^n -> R^m and
  direction h, evaluating F on a jet(k) argument at x0 + t h yields the
  entire degree-k Taylor expansion of F along h — machine-exact, no
  differencing, no tensors. Covers trajectories, perturbation analysis,
  series solutions of ODEs.
- The full Taylor POLYNOMIAL as an object (mixed partials stored,
  contracted against directions later) is the tensor kind's consumer:
  rank-k coefficient tensors from multiple jet passes, evaluated by
  entry 6's contraction.
**Promotion-table consequence (must be written before implementation):**
vector F means jets flow through matrix arithmetic — dense array element
types extend from {float, complex} to {float, complex, jet}. A table
row, not a new kind, but an ambush if undocumented.

**Trigger.** 4a: the first minimize design review (gradients needed), or
the first user derivative that numeric differencing handles poorly.
4b: the first session where constructor-entry of an expression tree is
the visible friction.

**Freeze compliance.** Both are additive builtins/value kinds; no legal
Neutrino program changes meaning; the conformance suite is untouched.

## 8. Tier-1 optimization backend (industrial-strength robustness)

**Status: parked, but WHEN, not if — the owner has ruled that industrial-
strength robustness will eventually be required; this entry exists so the
revisit starts from a design, not a blank page.**

**Motivation.** optim.cz (entry 3, shipped 0.0.13) is the tier-0: pure
Cozy, transparent, zero-dependency, every failure mode ours. Its known
limits, recorded honestly: the augmented Lagrangian runs a fixed penalty
schedule with no infeasibility detection; BFGS uses Armijo only (no Wolfe
curvature condition); no derivative-free path exists, so an objective
that cannot flow duals cannot be optimized at all today.

**Design sketch.** Mirror the linalg seam at the package contract level:
a tier-1 answers the same {x, fx, iters, converged} record behind the
same names. Candidate: NLopt (the nearest thing to a standard C API;
libnlopt-dev is in Ubuntu; SLSQP and interior-point for constraints,
COBYLA/BOBYQA/Nelder-Mead for derivative-free). Differences from the
LAPACK case, so the implementer is not surprised: (1) the performance
argument is weak — objective evaluations run in Cozy either way, and
the FFI crosses PER EVALUATION via callbacks from C into call_value, an
inversion the linalg seam never needed; the win is robustness and
algorithm breadth, not speed. (2) License diligence required: NLopt
mixes MIT and LGPL parts; select algorithms accordingly or document the
linking story. (3) Selection: a BACKEND-style Makefile seam or a
separate package (optim1.cz) loading a native-backed builtin — decide
at implementation time; the record contract is the invariant either way.

**Triggers (either fires the revisit):** the first real problem where
optim.cz fails to converge or is painfully slow on a legitimate
formulation; or the first objective that cannot flow duals (external
code, gated builtins) and therefore needs derivative-free.

## 5. Record reflection (the trio)

**Motivation.** Neutrino's fields(r) can see a record's names, but no
code can use a name dynamically: field access is spelled with literals
only. Friction transcripts: no package can write a generic key=value
parser, serializer, record merge, or field-mapped utility; readtable
returns records only because C builds them. String-to-record conversion
is impossible in userland (string-to-array, by contrast, is a pipeline —
strsplit ~> trim ~> num — and needed nothing).

**Sketch.** Two additions plus one inheritance, functional in style:
- getfield(r, name) — dynamic read; error on missing (strict, like keep).
- setfield(r, name, v) — returns a NEW record with the field added or
  replaced (records stay immutable values).
- {} — the empty record, which Neutrino already has (conformance carries
  it): construction is a FOLD, growing {} by setfield one field at a
  time.
**Repair note (caught by a usage question before a line was written):**
the first draft specified record(names, vals) — construction from
parallel arrays — which is incoherent: passing vals requires a container
of mixed-kind values, i.e. the heterogeneous list entry 6 rejects as
Octave's cell array. Two docket entries in quiet contradiction, exposed
by asking "how do I iterate columns?". The setfield fold needs no such
container and replaces the constructor outright. Canonical idioms:
  fields(t) ~> (fn n -> mean(getfield(t, n)))          % consume, uniform
  colmap = fold {} over names with setfield             % produce a table
table.cz's colmap/select/filter/summary are all the second shape.
With these, generic record map/filter/merge, k=v parsing, and
serialization round-trips (str one way, parse + record the other) all
live in packages. Same reflection family as ast(f) (entry 4b): the core
exposes structure; the mathematics of it stays in userland.

**Trigger.** The first table-shaped package (statistics on named
columns) or the first serialization need that str() alone cannot
round-trip.

**Freeze compliance (inherited-syntax contract).** Three builtins, no
grammar change; every Neutrino program keeps its meaning.

## 6. The type inventory (decided; rejections recorded)

**The test.** A type earns kind-hood — a value-enum slot and promotion-
table rows — only if (a) it needs its own arithmetic, or (b) its
representation must be invisible for performance. Everything else is
records plus packages. Enforcement is structural: the charter requires
ONE promotion table stated once, and every numeric kind multiplies it;
parsimony is what keeps that document readable.

**Kinds (eleven total).** The Neutrino nine, frozen by the conformance
contract: int, float, bool, string, complex, array (2-D, dense; a matrix
IS a 2-D array — keep the unification), record, function, null. Plus
two additions: **dual** (entry 4a — own arithmetic, and the gradient
engine for optimization) and **sparse as a representation of array**
(entry 1 — legible in who, invisible in signatures, one promotion
table).

**Patterns, not kinds.**
- *Tables* = record-of-columns (readtable's existing shape) + the
  reflection trio (entry 5) + a table.cz package for select/filter/
  group-by. R's data.frame is a library convention; so is Cozy's table.
  Core owes at most a pretty-printer.
- *Sets* = set.cz over sorted arrays (union is unique of concat). A set
  kind waits for a hashing-performance friction transcript that does not
  yet exist.
- *Dates* = Julian day numbers as floats — finance.cz is the friction
  transcript FOR parsimony; no datetime kind.

**Rejected: lists (heterogeneous sequences).** This is Octave's cell
array — the most-regretted wart of the family Neutrino fled. It splits
the ecosystem (every function forever answers array-or-list) and its use
cases are covered: named heterogeneity is records; generic record work
is entry 5. Do not relitigate without a friction transcript that records
plus reflection cannot serve.

**Tensors (N-D): motivated, designed minimal, sequenced last.** The
owner's argument upgrades these from unmotivated to structural: the
derivative is a rank-raising operator (scalar field -> gradient ->
Jacobian -> rank 3), so a closed differential calculus generates
unbounded rank; and econometrics uses rank-3/4 moment tensors
(co-skewness, co-kurtosis) as a matter of course. Two design decisions
tame this:
- *Functions return tensors; there are no arrays of functions.* grad(f)
  is fn x -> vector, jac(F) is fn x -> matrix — rank-raising lives in
  function space (Neutrino's own d operator was the 1-D proof), and the
  type system only meets values at evaluation points. Optimization needs
  gradient + Hessian = rank <= 2: minimize never touches tensors.
- *When rank >= 3 values are needed* (third derivatives at a point,
  moment tensors), tensors arrive as a SEPARATE kind — never a
  generalization of array, which would shift edge behaviors under every
  builtin and endanger the conformance suite — with a deliberately
  minimal algebra: construction, indexing, slicing down to matrices,
  elementwise ops with scalars, and CONTRACTION. No general
  broadcasting: that is NumPy's complexity tax, declined by design.
**Trigger.** The first co-skewness/co-kurtosis workload, or the first
third-order derivative a session actually needs.

## 7. Namespaces — REVIEWED 0.0.23 (design confirmed; all claims re-verified
by execution in Cozy: sibling recursion via the record's global name, the
getfield dynamic door — getfield LANDED at 0.0.6, so "dot is literal-only
until getfield lands" below is satisfied — who's load groups inherited from
upstream 2.28 (clutter solved orthogonally, exactly as the baton predicted),
and the pack-then-prune trap reproduced verbatim: keep() of a record of
optimizer faces killed the first call on the helpers it hid. RULING, same
logic as the Neutrino anti-decision: the three instrument packages
(autodiff, optim, sparselin) are GRANDFATHERED flat — short daily-typed
faces are the feature, helpers are already tag-prefixed — and the record
namespace is the recommendation for future large-API packages. The law is
now written in PACKAGES.md "Writing your own", where authors will read it.)

## 7-old. Namespaces: records are the module system (with one law attached)

**Decision.** Cozy gets no module kind, no import statement, no namespace
syntax. Packages that want a namespace pack their public API into a
record — dist.cz's norm.cdf proved the pattern in Neutrino production —
and the reflection pair (entry 5) makes such records programmable:
fields(pkg) is the manifest, getfield the dynamic door. One mechanism,
no new kind: the type inventory's patterns-over-kinds philosophy applied
to modules.

**What the interpreter taught us (verified 2.19.3, and inherited by Cozy
unchanged).** Functions resolve global names at CALL time, not
definition time. Consequences, both directions:
- The pack-then-prune idiom is a trap: bundle helpers' callers into a
  record, keep() the record, and every call dies with "undefined name" —
  the namespace hides the face, never the body. Internal helpers remain
  globals forever; encapsulation by record is cosmetic. Convention, not
  mechanism, hides helpers: prefix them (pkg_helper) and document that
  keep() must spare them.
- The same late binding gives mutual recursion inside a namespace for
  free: fields may call sibling fields through the record's own global
  name ({isev = fn n -> ... p.isod(n-1), ...} works today).

**Ergonomics, recorded honestly:** aliasing restores brevity
(let ddx = symb.ddx — functions are values); dot is literal-only until
getfield lands; records are closed at construction (no plugin
registration without setfield); hot loops should alias fields to locals.

**Anti-decision.** Neutrino's packages stay flat-plus-subrecords as
evolved; migration would churn hundreds of verified transcript lines for
zero behavioral gain. Cozy-original packages should prefer the record
namespace from birth, with the helper-prefix convention in the package
authoring guide from day one.

## 9. The workbench — SHIPPED 0.0.32 (owner's ruling: "we are ready for the
GUI — it will facilitate econometric testing"). Dual engine, one page: cozy
--workbench is a ~180-line localhost HTTP server (127.0.0.1 only, no TLS or
auth — a loopback tool by declared scope) serving docs/ and running POST
/eval through the native interpreter; the page detects it and routes evals
there, falling back to the embedded wasm engine on GitHub Pages or file://.
New panes: Workspace (live who after every eval) and Packages (one-click
loads), plus native plot polling (COZY_PLOT_TERM=svg; the pane diffs GET
/plots). The browser is the rendering surface, not necessarily the compute
engine. v1 excludes debugger, editor completion, data viewer — each waits
on its own friction.

## 10. Real-typed LAPACK fast path (trigger FIRED by the owner: inv() is
slightly slower than Octave under OpenBLAS). Cause, confirmed in the
architecture: the tier-1 funnels ALL matrices through the six COMPLEX
routines (zgetrf/zgetri/zgesv/...), so real matrices pay ~2x memory
traffic and 2-4x flops versus Octave's dgetrf/dgetri dispatch. Design:
when elt == FLOAT, dispatch to real routines (dgesv/dgetrf/dgetri first —
inv and mldivide are the hot pair — then dsyev/dgeev/dgesvd), sharing the
existing invariance conventions; goldens are already backend-invariant so
the change is measured by the stress battery plus benchmarks, not new
goldens. PHASE 1
SHIPPED 0.0.36 (owner's ruling: real data on real routines — faster AND
more accurate): solve_d/det_d seam entries; OpenBLAS dispatches dgesv/
dgetrf when both operands are real (covers \, /, inv, det, negative
matrix powers via the one mldivide funnel); tier0 provides parity
wrappers. Measured on inv(700): 0.051s real vs 0.097s complex — 1.9x,
the predicted flop ratio — and the result is exactly real by
construction, no snap involved. PHASE 2 SHIPPED
0.0.37 (owner: "now do phase 2"): eig_sym_d/svd_d/chol_d seam entries;
OpenBLAS runs dsyev/dgesvd/dpotrf on real inputs; tier0 parity wrappers.
The real kernels do the O(n^3) work, then results convert INTO the
existing complex buffers so all downstream shaping (eigenvalue sort,
phase anchor, snap, demotion) stays single-path — no new divergence
class. Measured under OpenBLAS at n=400: symmetric eig 3.5x, svd 3.1x;
residuals at machine precision. PHASE 3 SHIPPED
0.0.47 (the owner's profiling found A*A at 12 SECONDS — matrix multiply
had never left the boxed interpreter loop, one refcounted scalar_arith
per multiply): gemm_d/gemm_z seam entries (row-major via the
C^T = B^T A^T transpose identity, zero copies); typed fallback loops so
even tier0 gains 8x (12.1s -> 1.49s); int matmul stays exact int64 with
documented wrap; dual/hyper-dual keep the boxed loop for chain rules.
OpenBLAS measured: A*A(1000) 12.1s -> 0.045s, 267x. One golden repaired
on principle: dgemm leaves -1e-17 where the boxed sum left +0, so the
rounding family now canonicalizes its zero (round(-eps) must not print
-0). ALSO: per-eval worker threads with explicit QOS_CLASS_USER_
INTERACTIVE on Darwin (A*A parity across surfaces proved the residual
workbench inv gap is Accelerate's pool QoS, not process scheduling).
REMAINING residue: real nonsymmetric
eig stays complex-funneled until dgeev's paired-column eigenvector
format earns its unpacking (trigger: a profiled real nonsymmetric eig
hot path).

## 11. Session arena retention — SHIPPED 0.0.38 (owner: "do entry 11 now").
Two coordinated changes: (1) the env OWNS its binding names — strndup at
define, freed at env_free/env_clear/clear/keep's five drop sites — so a
let no longer pins its line's source; (2) the compiler sets
I->line_borrows_src when compiling anything whose runtime values point
into source (lambdas via chunk->src; record literals AND the fan-out
desugar's OP_RECORD, whose keys are non-owning) and all four session
hosts (repl, vmtest, wasm, workbench server) retain arena+src only when
flagged. Found-by-goldens: the session-block suite caught the env-name
borrow instantly (use-after-free on 'acc'), and the BOOK caught the
fan-out record emission the AST-level flag missed — the lattice working
exactly as designed. Peak RSS over 2000 closure-free evals: 110 MB ->
2.6 MB; the stress plateau bound is now ENFORCING.

## 11-was. Session arena retention (trigger FIRED by the stress suite on its
first run: peak RSS 8.6 -> 112.7 MB over 2000 closure-free evals). Every
line's parse arena + source is retained for closure-source lifetime,
unconditionally. Design: free the arena/src when the compiled line created
no closure (cheap flag from the compiler), or refcount chunks so retention
follows references; the workbench server shares the fix. Acceptance: the
stress long-session tier's plateau bound flips from reporting to
enforcing.

## 12. Workbench warm compute thread — PARKED. If the owner's experiments
(VECLIB_MAXIMUM_THREADS=1 both surfaces; a tight eval loop in the
workbench) implicate cold-start, the design: one dedicated compute
thread created at server startup at USER_INTERACTIVE QoS, fed evals
through a condition variable, optionally kept warm with a micro-gemm
heartbeat during idle — so Accelerate's pool never parks. Trigger: the
experiments; not another blind lever. The 0.0.47 worker and 0.0.48
dispatch entry stay (harmless, and the dispatch door is the correct
context regardless).

## 13. Default parameter values — PARKED (owner: "add the entry", 0.0.53).
The asymmetry: builtins have arity ranges (median(A) | median(A, dim));
user closures are fixed-arity, which has bitten once in 53 releases
(demo() vs demo(k), resolved by a better zero-arg interactive design).
The minimal extension closing it: fn x, tol = 1e-8 -> ... — defaults on
trailing parameters give closures the same min-to-max arity builtins
enjoy. Additive syntax (previously illegal shape, per the charter's
inheritance contract); moderate cost: parser accepts = expr after a
parameter, the closure carries an arity range plus default thunks or
values, calls below max arity fill from defaults. Design questions to
settle at implementation: evaluate defaults at definition (value
semantics, consistent with value-capture upvalues) or at call (Python's
scar says definition-time, but definition-time capture of MUTABLE state
is a non-issue here — values are immutable); defaults referencing
earlier parameters (fn a, b = a -> ...) — probably yes, evaluated
left-to-right at call. NOT in scope: full variadics — nothing has asked.
TRIGGER: friction transcripts — a package API sprouting an awkward name
family (f / f_tol / f_tol_maxit) or callers passing null placeholders
where one optional would do. One demo() incident, already better-
solved, does not fire it.
