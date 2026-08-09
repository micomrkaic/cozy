# sparselin.nu — iterative sparse linear algebra for Cozy
# Conjugate gradient (SPD solve) and power iteration (dominant eigenpair),
# built entirely on the sparse founding kernel S * v — the design's answer
# to sparse \ and sparse eig (design entry 1: solvers are packages).
# Both accept dense matrices too; anything with * and a column works.
# See tests/57_sparselin.test.

let sl_dot = fn x, y -> sum(x .* y)
let sl_norm = fn x -> sqrt(sum(x .* x))

# cg(A, b): solve A x = b for symmetric positive definite A.
# Plain CG from x0 = 0; stops at relative residual 1e-10 or 200*n steps.
# Returns {x, iters, relres}.
let cg = fn A, b -> (
  let b2 = sl_norm(b);
  if b2 == 0 then return {x = 0 * b, iters = 0, relres = 0} end;
  let x = 0 * b;
  let r = b;
  let p = r;
  let rs = sl_dot(r, r);
  let it = 0;
  let cap = 200 * size(b)[1];
  while sqrt(rs) > 1e-10 * b2 & it < cap do
    let Ap = A * p;
    let alpha = rs / sl_dot(p, Ap);
    x = x + alpha * p;
    r = r - alpha * Ap;
    let rs2 = sl_dot(r, r);
    p = r + (rs2 / rs) * p;
    rs = rs2;
    it = it + 1
  end;
  {x = x, iters = it, relres = sqrt(rs) / b2})

# powerit(A): dominant eigenpair {value, vector, iters} by power iteration
# with a Rayleigh-quotient estimate. Deterministic start (normalized ones);
# if the dominant eigenvector is orthogonal to ones, perturb the start
# yourself, e.g. powerit_from(A, rand(size(A)[1], 1)).
let powerit_from = fn A, x0 -> (
  let x = x0 / sl_norm(x0);
  let lam = 0;
  let it = 0;
  let done = false;
  while !done & it < 1000 do
    let y = A * x;
    let ny = sl_norm(y);
    if ny == 0 then return {value = 0, vector = x, iters = it} end;
    let x2 = y / ny;
    let lam2 = sl_dot(x2, A * x2);
    if abs(lam2 - lam) <= 1e-12 * (1 + abs(lam2)) then (done = true) end;
    lam = lam2;
    x = x2;
    it = it + 1
  end;
  {value = lam, vector = x, iters = it})

let powerit = fn A -> (
  let n = size(A)[1];
  powerit_from(A, ones(n, 1)))
