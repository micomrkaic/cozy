#!/usr/bin/env python3
"""Independent reference for the ksmooth builtin (kalman.c; not run by
`make test` — it needs numpy and, for the last check, the simksmoother
package). It shares nothing with the kernel: J and h are built DENSE from
the stacked residuals of the model, solved and inverted with numpy, and the
log-likelihood comes straight from the Gaussian marginal of the observed y
(no completing the square) whenever the prior is proper.

    python3 tests/ref_ksmooth.py                # randomized agreement, 200 models
    python3 tests/ref_ksmooth.py --sks PATH     # also against simksmoother (PATH/python)

It drives ./cozy on each case and prints the worst discrepancy per output;
the verdict is the exit code.
"""
import os
import subprocess
import sys
import tempfile

import numpy as np

COZY = os.environ.get("COZY", "./cozy")
LOG2PI = np.log(2 * np.pi)


# ---- the dense reference -------------------------------------------------
def dense_reference(Y, F, Q, H, R, a0, P0, c=None, d=None, wy=None, wx=None):
    """Return x (n x T+1), Sigma (dense), loglik, logdetJ, nobs."""
    m, T = Y.shape
    n = F.shape[0]
    p = H.shape[1] // n - 1
    Hk = [H[:, k * n:(k + 1) * n] for k in range(p + 1)]
    N = n * (T + 1)
    Rd = R.ndim == 1
    rowlag = [max([k for k in range(p + 1) if np.any(Hk[k][i] != 0)] or [0]) for i in range(m)]
    cc = (lambda t: c[:, 0] if c.shape[1] == 1 else c[:, t - 1]) if c is not None else (lambda t: np.zeros(n))
    dd = (lambda t: d[:, 0] if d.shape[1] == 1 else d[:, t - 1]) if d is not None else (lambda t: np.zeros(m))

    rows_A, rows_b, precs, ldets = [], [], [], []   # residual = A X - b, precision Omega
    # prior + transitions (square, lower block triangular when the prior is proper)
    if P0 is not None:
        A = np.zeros((n, N)); A[:, :n] = np.eye(n)
        rows_A.append(A); rows_b.append(a0); precs.append(np.linalg.inv(P0)); ldets.append(np.linalg.slogdet(P0)[1])
    for t in range(1, T + 1):
        s = 1.0 if wx is None else wx[t - 1]
        A = np.zeros((n, N)); A[:, t * n:(t + 1) * n] = np.eye(n); A[:, (t - 1) * n:t * n] = -F
        rows_A.append(A); rows_b.append(cc(t)); precs.append(s * np.linalg.inv(Q)); ldets.append(np.linalg.slogdet(Q / s)[1])
    n_state_rows = len(rows_A)
    nobs = 0
    for t in range(1, T + 1):
        o = [i for i in range(m) if not np.isnan(Y[i, t - 1]) and rowlag[i] <= t]
        if not o:
            continue
        nobs += len(o)
        A = np.zeros((len(o), N))
        for k in range(p + 1):
            if t - k >= 0:
                A[:, (t - k) * n:(t - k + 1) * n] += Hk[k][o]
        Roo = np.diag(R[o]) if Rd else R[np.ix_(o, o)]
        if wy is None:
            V = Roo
        elif wy.ndim == 1:
            V = Roo / wy[t - 1]
        else:
            V = np.diag(R[o] / wy[o, t - 1])
        rows_A.append(A); rows_b.append(Y[o, t - 1] - dd(t)[o]); precs.append(np.linalg.inv(V)); ldets.append(np.linalg.slogdet(V)[1])

    Abig = np.vstack(rows_A); bbig = np.concatenate(rows_b)
    from scipy.linalg import block_diag
    Om = block_diag(*precs)
    J = Abig.T @ Om @ Abig; h = Abig.T @ Om @ bbig
    Sig = np.linalg.inv(J)
    x = np.linalg.solve(J, h)
    ldJ = np.linalg.slogdet(J)[1]
    if P0 is not None:
        # marginal of y directly: X = As^{-1}(bs + e), y_o = C X + d + v
        k0 = sum(r.shape[0] for r in rows_A[:n_state_rows])
        As, bs = Abig[:k0], bbig[:k0]
        Vs = np.linalg.inv(block_diag(*precs[:n_state_rows]))
        Asi = np.linalg.inv(As)
        mu = Asi @ bs; SX = Asi @ Vs @ Asi.T
        C = Abig[k0:]; yv = bbig[k0:]
        Vy = np.linalg.inv(block_diag(*precs[n_state_rows:]))
        Sy = C @ SX @ C.T + Vy
        r = yv - C @ mu
        ll = -0.5 * (r @ np.linalg.solve(Sy, r) + np.linalg.slogdet(Sy)[1] + len(yv) * LOG2PI)
    else:
        # diffuse: the improper prior's n/2 log 2pi dropped, as in simksmoother
        Kq = bbig @ Om @ bbig - h @ x
        ll = -0.5 * (Kq + nobs * LOG2PI + sum(ldets) + ldJ)
    return x.reshape(T + 1, n).T, Sig, ll, ldJ, nobs


# ---- driving cozy ---------------------------------------------------------
def lit(A):
    A = np.atleast_2d(A)
    f = lambda v: "0/0" if np.isnan(v) else repr(float(v))
    return "[" + "; ".join(", ".join(f(v) for v in row) for row in A) + "]"


def run_cozy(Y, F, Q, H, R, a0, P0, c, d, wy, wx, workdir):
    fields = [f"F = {lit(F)}", f"Q = {lit(Q)}", f"H = {lit(H)}", f"R = {lit(R if R.ndim == 2 else R[:, None])}",
              f"a0 = {lit(a0[:, None])}"]
    if P0 is not None: fields.append(f"P0 = {lit(P0)}")
    if c is not None: fields.append(f"c = {lit(c)}")
    if d is not None: fields.append(f"d = {lit(d)}")
    opts = []
    if wy is not None: opts.append(f"wy = {lit(wy if wy.ndim == 2 else wy[None, :])}")
    if wx is not None: opts.append(f"wx = {lit(wx[None, :])}")
    out = os.path.join(workdir, "o")
    src = (f"let s = ksmooth({lit(Y)}, {{{', '.join(fields)}}}, {{{', '.join(opts) or 'cov = true'}}});\n"
           f"writecsv(\"{out}_x.csv\", s.x); writecsv(\"{out}_P.csv\", s.P);\n"
           + "".join(f"writecsv(\"{out}_C{k}.csv\", s.P{k});\n" for k in range(1, max(1, H.shape[1] // F.shape[0] - 1) + 1))
           +            f"writecsv(\"{out}_s.csv\", [s.loglik, s.logdetJ, s.nobs, s.lags]);\n")
    path = os.path.join(workdir, "case.cz")
    open(path, "w").write(src)
    r = subprocess.run([COZY, path], capture_output=True, text=True)
    if r.returncode != 0 or r.stderr.strip():
        raise RuntimeError(f"cozy failed: {r.stdout}{r.stderr}")
    rd = lambda f: np.atleast_2d(np.loadtxt(f, delimiter=","))
    s = rd(out + "_s.csv").ravel()
    C = [rd(f"{out}_C{k}.csv") for k in range(1, int(s[3]) + 1)]
    return rd(out + "_x.csv"), rd(out + "_P.csv"), C, s


def random_case(rng):
    n = int(rng.integers(1, 5)); m = int(rng.integers(1, 7)); p = int(rng.integers(0, 4)); T = int(rng.integers(3, 25))
    A = rng.standard_normal((n, n)); F = 0.9 * A / max(1.0, np.max(np.abs(np.linalg.eigvals(A))))
    B = rng.standard_normal((n, n)); Q = B @ B.T + 0.3 * np.eye(n)
    H = rng.standard_normal((m, n * (p + 1))) * (rng.random((m, n * (p + 1))) < 0.7)
    if rng.random() < 0.5:
        R = rng.random(m) + 0.2
    else:
        C = rng.standard_normal((m, m)); R = C @ C.T + 0.5 * np.eye(m)
    a0 = rng.standard_normal(n)
    P0 = None if rng.random() < 0.3 else (lambda D: D @ D.T + np.eye(n))(rng.standard_normal((n, n)))
    Y = rng.standard_normal((m, T)) * 2
    Y[rng.random((m, T)) < rng.choice([0.0, 0.2, 0.5])] = np.nan
    if P0 is None:                   # a diffuse prior needs x_0 pinned: observe the first periods fully
        Y[:, :min(T, 3)] = rng.standard_normal((m, min(T, 3)))
        H[:, :n] += np.eye(m, n) * 2  # contemporaneous loadings on every state
    c = None if rng.random() < 0.5 else rng.standard_normal((n, rng.choice([1, T])))
    d = None if rng.random() < 0.5 else rng.standard_normal((m, rng.choice([1, T])))
    wy = None
    u = rng.random()
    if u < 0.25: wy = rng.random(T) + 0.1
    elif u < 0.4 and R.ndim == 1 and m > 1: wy = rng.random((m, T)) + 0.1
    wx = None if rng.random() < 0.6 else rng.random(T) + 0.1
    return Y, F, Q, H, R, a0, P0, c, d, wy, wx


def main():
    rng = np.random.default_rng(20261124)
    worst = dict(x=0.0, P=0.0, C=0.0, ll=0.0, ldJ=0.0)
    fails = tested = 0
    with tempfile.TemporaryDirectory() as wd:
        for case in range(200):
            Y, F, Q, H, R, a0, P0, c, d, wy, wx = random_case(rng)
            try:
                xr, Sig, llr, ldr, nob = dense_reference(Y, F, Q, H, R, a0, P0, c, d, wy, wx)
            except np.linalg.LinAlgError:
                continue
            if np.linalg.cond(np.linalg.inv(Sig)) > 1e10:
                continue
            x, P, C, s = run_cozy(Y, F, Q, H, R, a0, P0, c, d, wy, wx, wd)
            tested += 1
            n = F.shape[0]; T = Y.shape[1]
            scale = 1 + np.max(np.abs(xr))
            ex = np.max(np.abs(x - xr)) / scale
            Pr = np.hstack([Sig[t * n:(t + 1) * n, t * n:(t + 1) * n] for t in range(T + 1)])
            eP = np.max(np.abs(P - Pr)) / (1 + np.max(np.abs(Pr)))
            eC = 0.0
            for k, Ck in enumerate(C, start=1):
                Cr = np.hstack([Sig[t * n:(t + 1) * n, (t - k) * n:(t - k + 1) * n] if t >= k else np.full((n, n), np.nan)
                                for t in range(T + 1)])
                ok = ~np.isnan(Cr)
                if np.any(np.isnan(Ck) != np.isnan(Cr)):
                    eC = np.inf
                eC = max(eC, np.max(np.abs(Ck[ok] - Cr[ok])) / (1 + np.max(np.abs(Pr))))
            ell = abs(s[0] - llr) / (1 + abs(llr)); eld = abs(s[1] - ldr) / (1 + abs(ldr))
            for k, v in zip(worst, (ex, eP, eC, ell, eld)):
                worst[k] = max(worst[k], v)
            if max(ex, eP, eC, ell, eld) > 1e-9 or int(s[2]) != nob:
                fails += 1
                print(f"case {case}: n={n} m={Y.shape[0]} p={H.shape[1]//n-1} T={T} "
                      f"err x={ex:.2e} P={eP:.2e} C={eC:.2e} ll={ell:.2e} ldJ={eld:.2e} nobs {int(s[2])} vs {nob}")
    print(f"worst relative error over {tested} random models:",
          ", ".join(f"{k} {v:.1e}" for k, v in worst.items()))
    if "--sks" in sys.argv:
        fails += against_simksmoother(sys.argv[sys.argv.index("--sks") + 1])
    print("ksmooth reference:", "AGREES" if fails == 0 else f"{fails} DISAGREEMENT(S)")
    return 1 if fails else 0


def against_simksmoother(path):
    """The original package on its own nowcasting model (companion states, Q
    regularized with 1e-10), and the same model in measurement-lag form."""
    sys.path.insert(0, path)
    import simksmoother as sks
    rng = np.random.default_rng(33)
    T, phi, se = 120, 0.7, 0.5
    lam = np.array([1.0, 0.8, 0.6, 1.2, 0.4]); Rm = np.array([0.3, 0.5, 0.8, 0.4, 1.5]) ** 2
    F3 = np.zeros((3, 3)); F3[0, 0] = phi; F3[1, 0] = 1; F3[2, 1] = 1
    Q3 = np.diag([se ** 2, 1e-10, 1e-10])
    H3 = np.zeros((6, 3)); H3[:5, 0] = lam; H3[5] = 1 / 3
    R6 = np.diag(np.r_[Rm, 0.09])
    P03 = se ** 2 / (1 - phi ** 2) * np.eye(3)
    Y, _ = sks.simulate_const(T, F3, H3, Q3, R6, np.zeros(3), P03, rng=rng)
    for t in range(T):
        if (t + 1) % 3: Y[5, t] = np.nan
    Y[1:3, -1] = np.nan
    fails = 0
    with tempfile.TemporaryDirectory() as wd:
        # (1) the identical companion model: two implementations, one answer
        X3, _, _, info = sks.smooth_const(Y, F3, H3, Q3, R6, np.zeros(3), P03, return_cov=True)
        x, P, C, s = run_cozy(Y, F3, Q3, H3, np.diag(R6), np.zeros(3), P03, None, None, None, None, wd)
        Pr = np.hstack(info["Ptt"])
        e1 = np.max(np.abs(x - X3)); e2 = np.max(np.abs(P - Pr))
        e3 = np.max(np.abs(C[0][:, 3:] - np.hstack(info["Ptt1"])))
        print(f"simksmoother, companion form: max |dx| {e1:.1e}, |dP| {e2:.1e}, |dP1| {e3:.1e}")
        # cond(J) is about 3e10 here (the 1e-10 variances), so ~1e-7 is the
        # agreement two correct solvers can have; against a 50-digit solve
        # ksmooth's error is 2.9e-8 and SuperLU's 5.7e-8.
        fails += max(e1, e2, e3) > 1e-6
        # (2) the scalar factor with GDP on (f_t + f_{t-1} + f_{t-2})/3 as measurement lags.
        # The companion prior on (f_0, f_-1, f_-2) has no counterpart, so compare the
        # factor away from the start.
        H1 = np.zeros((6, 3)); H1[:5, 0] = lam; H1[5] = 1 / 3
        x1, P1, _, s1 = run_cozy(Y, np.array([[phi]]), np.array([[se ** 2]]), H1, np.diag(R6), np.zeros(1),
                                 np.array([[se ** 2 / (1 - phi ** 2)]]), None, None, None, None, wd)
        e4 = np.max(np.abs(x1[0, 6:] - X3[0, 6:]))
        nc_lag = x1[0, -3:].mean(); nc_cmp = X3[0, -1] / 3 + X3[1, -1] / 3 + X3[2, -1] / 3
        print(f"measurement-lag form vs companion: max |df_t| (t > 5) {e4:.1e}; "
              f"nowcast {nc_lag:.6f} vs {nc_cmp:.6f}")
        fails += e4 > 1e-3
    return int(fails)


if __name__ == "__main__":
    sys.exit(main())
