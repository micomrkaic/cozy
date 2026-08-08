# Cozy Design Notes

*Parked designs in the Neutrino tradition: each waits with a written
trigger; each ends SHIPPED or rejected, never silently dropped.*

## 1. Sparse matrices — DECIDED 2026-08-08 (design ratified; waiting on trigger)

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

## 2. External LAPACK

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

## 3. Optimization

**Sketch.** `minimize[x = x0] f(x)` on the index-binder surface;
Nelder–Mead and a quasi-Newton to start; constrained later or never.
**Trigger.** The first optimization done by hand-rolled iteration at the
prompt that deserved a verb.

## 4. First-class differentiation (dual numbers + quotation)

**Motivation.** Neutrino's symb.nu proved symbolic differentiation is
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
the symb.nu encoding ({op, l, r, v, n}), differentiation-by-recursion
staying entirely in packages. Rules: the tree is reported with respect to
the lambda's parameters; free and captured names appear as named
constants; ast of a builtin is an error. Optional inverse (defn(tree))
can wait — symb.nu's evalx already interprets trees. Deleting symb.nu's
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
table.nu's colmap/select/filter/summary are all the second shape.
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
  reflection trio (entry 5) + a table.nu package for select/filter/
  group-by. R's data.frame is a library convention; so is Cozy's table.
  Core owes at most a pretty-printer.
- *Sets* = set.nu over sorted arrays (union is unique of concat). A set
  kind waits for a hashing-performance friction transcript that does not
  yet exist.
- *Dates* = Julian day numbers as floats — finance.nu is the friction
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

## 7. Namespaces: records are the module system (with one law attached)

**Decision.** Cozy gets no module kind, no import statement, no namespace
syntax. Packages that want a namespace pack their public API into a
record — dist.nu's norm.cdf proved the pattern in Neutrino production —
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
