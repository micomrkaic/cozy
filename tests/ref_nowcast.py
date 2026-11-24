#!/usr/bin/env python3
"""Independent references for packages/nowcast.cz (not run by `make test`;
it needs numpy, scipy and the simksmoother package for the fixture).

    python3 tests/ref_nowcast.py --sks PATH    # PATH: Kalman-Smoother/python

(A) app2: regenerates the data of simksmoother's nowcasting application
    (examples/applications/app2_nowcasting.py, seed 33) into
    tests/data/app2_nowcast.csv and prints its three nowcasts and sds,
    computed the package's way (companion states, Q regularized with
    1e-10) — the expectations in tests/87_nowcast.test.
(B) EM: the same model and start, estimated by an EM written here from
    the definitions over a DENSE posterior (J inverted with numpy), with
    no ksmooth and no sufficient statistics; parameters and the
    log-likelihood path compared iteration by iteration.
(C) news: Banbura and Modugno's formula taken literally — the joint
    Gaussian of all the data and the target, conditional expectations,
    innovations I = y_N - E[y_N | revised old data], weights
    b = Cov(target, I) Var(I)^{-1} — against nowcast.news.

It drives ./cozy; the verdict is the exit code.
"""
import os
import subprocess
import sys
import tempfile

import numpy as np

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
from ref_ksmooth import dense_reference, lit   # the dense smoother (shares nothing with kalman.c)

COZY = os.environ.get("COZY", "./cozy")


def cozy(src, wd):
    path = os.path.join(wd, "case.cz")
    open(path, "w").write('load("packages/nowcast.cz");\n' + src)
    r = subprocess.run([COZY, path], capture_output=True, text=True)
    if r.returncode != 0 or r.stderr.strip():
        raise RuntimeError(f"cozy failed: {r.stdout}{r.stderr}")
    return r.stdout


def rd(path):
    return np.atleast_2d(np.loadtxt(path, delimiter=","))


# ---- (A) the app2 fixture ------------------------------------------------
def app2(sks_path):
    sys.path.insert(0, sks_path)
    import simksmoother as sks
    rng = np.random.default_rng(33)
    T, phi, se, K = 120, 0.70, 0.5, 5
    lam = np.array([1.0, 0.8, 0.6, 1.2, 0.4]); Rm = np.diag(np.array([0.30, 0.50, 0.80, 0.40, 1.50]) ** 2)
    F = np.zeros((3, 3)); F[0, 0] = phi; F[1, 0] = 1; F[2, 1] = 1
    Q = np.zeros((3, 3)); Q[0, 0] = se ** 2; Q[1, 1] = 1e-10; Q[2, 2] = 1e-10
    H = np.zeros((6, 3)); H[:K, 0] = lam; H[K, :] = np.array([1, 1, 1]) / 3
    R = np.zeros((6, 6)); R[:K, :K] = Rm; R[K, K] = 0.3 ** 2
    P0 = (se ** 2 / (1 - phi ** 2)) * np.eye(3)
    Y, _ = sks.simulate_const(T, F, H, Q, R, np.zeros(3), P0, rng=rng)
    Yo = Y.copy()
    for k, lag in enumerate([0, 1, 1, 0, 0]):
        if lag > 0: Yo[k, T - lag:] = np.nan
    for t in range(T):
        if (t + 1) % 3 != 0: Yo[K, t] = np.nan
    Yo[K, T - 1] = np.nan
    out = "tests/data/app2_nowcast.csv"
    with open(out, "w") as fh:
        for row in Yo:
            fh.write(",".join("" if np.isnan(v) else repr(float(v)) for v in row) + "\n")
    print(f"(A) wrote {out}")
    for scen in range(3):
        Ys = Yo.copy(); Ys[:K, T - 3 + scen + 1:] = np.nan
        Xs, _, _, info = sks.smooth_const(Ys, F, H, Q, R, np.zeros(3), P0, return_cov=True)
        nc = Xs[0, T - 2:T + 1].mean()
        w = np.ones(3) / 3
        sd = float(np.sqrt(w @ info["Ptt"][T] @ w))
        print(f"    scenario {scen + 1}: nowcast {nc:.10f}  sd {sd:.10f}")


# ---- the model, dense --------------------------------------------------------
def build_H(W, Lam):
    m, L = W.shape; r = Lam.shape[1]
    return np.hstack([W[:, [k]] * Lam for k in range(L)])


def dense_estep(Z, W, A, Q, Lam, R, P0):
    r = A.shape[0]
    H = build_H(W, Lam)
    X, Sig, ll, _, _ = dense_reference(Z, A, Q, H, R, np.zeros(r), P0)
    return X, Sig, ll


def dense_mstep(Z, W, X, Sig, Lam, R, Rmin=1e-4):
    """M-step from the definitions: expectations straight off the dense posterior."""
    r = X.shape[0]; m, T = Z.shape; L = W.shape[1]
    blk = lambda a, b: Sig[a * r:(a + 1) * r, b * r:(b + 1) * r]
    E = lambda a, b: np.outer(X[:, a], X[:, b]) + blk(a, b)        # E[f_a f_b']
    S11 = sum(E(t, t) for t in range(1, T + 1))
    S00 = sum(E(t - 1, t - 1) for t in range(1, T + 1))
    S10 = sum(E(t, t - 1) for t in range(1, T + 1))
    A = S10 @ np.linalg.inv(S00)
    Q = (S11 - A @ S10.T) / T; Q = (Q + Q.T) / 2
    Lam = Lam.copy(); R = R.copy()
    for i in range(m):
        lags = [k for k in range(L) if W[i, k] != 0]
        Ezz = np.zeros((r, r)); Eyz = np.zeros(r); yy = 0.0; n = 0
        for t in range(1, T + 1):
            y = Z[i, t - 1]
            if np.isnan(y) or max(lags) > t:
                continue
            ez = sum(W[i, k] * X[:, t - k] for k in lags)
            Ezz += sum(W[i, k] * W[i, l] * E(t - k, t - l) for k in lags for l in lags)
            Eyz += y * ez; yy += y * y; n += 1
        if n:
            lam = np.linalg.solve(Ezz, Eyz)
            Lam[i] = lam
            R[i] = max((yy - Eyz @ lam) / n, Rmin)
    return A, Q, Lam, R


def em_check(wd):
    rng = np.random.default_rng(11)
    T, r = 48, 2
    f = np.zeros((r, T)); A0 = np.array([[0.7, 0.1], [0.0, 0.5]])
    for t in range(1, T): f[:, t] = A0 @ f[:, t - 1] + rng.standard_normal(r) * 0.6
    m = 7
    Lt = rng.standard_normal((m, r))
    Y = Lt @ f + 0.4 * rng.standard_normal((m, T))
    q = np.zeros(m); q[5:] = 1                       # two quarterly series
    w = np.array([1, 2, 3, 2, 1]) / 3
    for i in (5, 6):
        g = np.array([sum(w[k] * f[:, t - k] for k in range(5)) if t >= 4 else np.full(r, np.nan) for t in range(T)]).T
        Y[i] = Lt[i] @ g + 0.3 * rng.standard_normal(T)
        Y[i, [t for t in range(T) if (t + 1) % 3]] = np.nan
    Y[0, 30:36] = np.nan; Y[3, -4:] = np.nan; Y[1, :5] = np.nan; Y[6, -1] = np.nan; Y[0, -2:] = np.nan
    K = 6
    src = (f"let d = nowcast.data({lit(Y)}, {{quarterly = {lit(q[:, None])}}});\n"
           f"let s = nowcast.model(d, {{r = {r}}});\n"
           f"writecsv(\"{wd}/Z.csv\", s.Z); writecsv(\"{wd}/W.csv\", s.W); writecsv(\"{wd}/P0.csv\", s.P0);\n"
           f"writecsv(\"{wd}/A.csv\", s.params.A); writecsv(\"{wd}/Q.csv\", s.params.Q);\n"
           f"writecsv(\"{wd}/L.csv\", s.params.Lam); writecsv(\"{wd}/R.csv\", s.params.R);\n"
           f"let g = nowcast.fit(d, {{r = {r}, maxit = {K}, tol = 0}});\n"
           f"writecsv(\"{wd}/gA.csv\", g.params.A); writecsv(\"{wd}/gQ.csv\", g.params.Q);\n"
           f"writecsv(\"{wd}/gL.csv\", g.params.Lam); writecsv(\"{wd}/gR.csv\", g.params.R);\n"
           f"writecsv(\"{wd}/gll.csv\", g.loglik);\n")
    cozy(src, wd)
    Z = rd(f"{wd}/Z.csv"); W = rd(f"{wd}/W.csv"); P0 = rd(f"{wd}/P0.csv")
    A = rd(f"{wd}/A.csv"); Q = rd(f"{wd}/Q.csv"); Lam = rd(f"{wd}/L.csv"); R = rd(f"{wd}/R.csv").ravel()
    lls = []
    for _ in range(K):
        X, Sig, ll = dense_estep(Z, W, A, Q, Lam, R, P0)
        lls.append(ll)
        A, Q, Lam, R = dense_mstep(Z, W, X, Sig, Lam, R)
    got = dict(A=rd(f"{wd}/gA.csv"), Q=rd(f"{wd}/gQ.csv"), Lam=rd(f"{wd}/gL.csv"), R=rd(f"{wd}/gR.csv").ravel())
    want = dict(A=A, Q=Q, Lam=Lam, R=R)
    err = max(np.max(np.abs(got[k] - want[k])) / (1 + np.max(np.abs(want[k]))) for k in want)
    ell = np.max(np.abs(rd(f"{wd}/gll.csv").ravel() - np.array(lls)))
    mono = np.all(np.diff(lls) > -1e-9)
    print(f"(B) EM, {K} iterations, 7 series (2 quarterly, Mariano-Murasawa), 2 factors: "
          f"max rel param error {err:.1e}, max |dloglik| {ell:.1e}, monotone {mono}")
    return (err > 1e-8) + (ell > 1e-7) + (not mono), (Z, W, P0, A, Q, Lam, R, Y, q)


# ---- (C) news, Banbura-Modugno literally ---------------------------------------
def joint(Z, W, A, Q, Lam, R, P0, tgt_i, tgt_t):
    """Mean-zero joint covariance of all (y_it, i, t) cells and the target signal."""
    m, T = Z.shape; r = A.shape[0]; L = W.shape[1]
    # states f_0..f_T: Cov from the VAR
    Sf = np.zeros(((T + 1) * r, (T + 1) * r)); Sf[:r, :r] = P0
    for t in range(1, T + 1):
        for s in range(t):
            Sf[t*r:(t+1)*r, s*r:(s+1)*r] = A @ Sf[(t-1)*r:t*r, s*r:(s+1)*r]
            Sf[s*r:(s+1)*r, t*r:(t+1)*r] = Sf[t*r:(t+1)*r, s*r:(s+1)*r].T
        Sf[t*r:(t+1)*r, t*r:(t+1)*r] = A @ Sf[(t-1)*r:t*r, (t-1)*r:t*r] @ A.T + Q
    rows = []
    rowlag = [max(k for k in range(L) if W[i, k] != 0) for i in range(m)]
    def loading(i, t):
        v = np.zeros((T + 1) * r)
        for k in range(L):
            if W[i, k] != 0:
                v[(t - k)*r:(t - k + 1)*r] += W[i, k] * Lam[i]
        return v
    cells = [(i, t) for t in range(1, T + 1) for i in range(m) if rowlag[i] <= t]
    C = np.array([loading(i, t) for i, t in cells])
    Sy = C @ Sf @ C.T + np.diag([R[i] for i, _ in cells])
    cst = C @ Sf @ loading(tgt_i, tgt_t)
    return cells, Sy, cst


def news_check(wd, pack):
    Z, W, P0, A, Q, Lam, R, Y, q = pack
    m, T = Y.shape
    Y2 = Y.copy()
    Y2[0, 30:33] = Y[0, 29] + np.array([0.3, -0.2, 0.1])         # new releases in a gap
    Y2[3, -4:-2] = [0.5, -0.7]                                     # the ragged edge advances
    Y2[0, -2:] = [1.2, 0.9]
    Y2[6, -1] = 0.4                                                # the other quarterly series' release
    Y2[2, -5] += 0.8; Y2[4, -3] -= 0.5                             # two revisions near the end
    t_target, i_target = T, 5
    src = (f"let d = nowcast.data({lit(Y)}, {{quarterly = {lit(q[:, None])}, names = [\"a\"; \"b\"; \"c\"; \"d\"; \"e\"; \"gq\"; \"hq\"]}});\n"
           f"let s = nowcast.model(d, {{r = 2, params = {{A = {lit(A)}, Q = {lit(Q)}, Lam = {lit(Lam)}, R = {lit(R[:, None])}}}, P0 = {lit(P0)}}});\n"
           f"let n = nowcast.news(s, {lit(Y2)}, \"gq\", {t_target});\n"
           f"writecsv(\"{wd}/nn.csv\", n.news); writecsv(\"{wd}/nr.csv\", n.revisions);\n"
           f"writecsv(\"{wd}/no.csv\", [n.old, n.new]); writecsv(\"{wd}/mu.csv\", s.mu); writecsv(\"{wd}/sd.csv\", s.sd);\n")
    cozy(src, wd)
    mu = rd(f"{wd}/mu.csv").ravel(); sd = rd(f"{wd}/sd.csv").ravel()
    Zo = (Y - mu[:, None]) / sd[:, None]; Zn = (Y2 - mu[:, None]) / sd[:, None]
    cells, Sy, cst = joint(Zo, W, A, Q, Lam, R, P0, i_target, t_target)
    val = lambda Zx, sel: np.array([Zx[i, t - 1] for (i, t) in [cells[k] for k in sel]])
    obs_o = [k for k, (i, t) in enumerate(cells) if not np.isnan(Zo[i, t - 1])]
    E = lambda Zx, sel: cst[sel] @ np.linalg.solve(Sy[np.ix_(sel, sel)], val(Zx, sel))
    Zr = Zo.copy(); rev = (~np.isnan(Zo)) & (Zo != Zn); Zr[rev] = Zn[rev]
    old = E(Zo, obs_o); er = E(Zr, obs_o)
    revs = np.zeros(m)
    for j in range(m):
        Zj = Zo.copy(); Zj[j][rev[j]] = Zn[j][rev[j]]
        revs[j] = E(Zj, obs_o) - old
    N = [k for k, (i, t) in enumerate(cells) if np.isnan(Zo[i, t - 1]) and not np.isnan(Zn[i, t - 1])]
    SON = Sy[np.ix_(obs_o, N)]; SOO = Sy[np.ix_(obs_o, obs_o)]
    EyN = SON.T @ np.linalg.solve(SOO, val(Zr, obs_o))
    I = val(Zn, N) - EyN
    VI = Sy[np.ix_(N, N)] - SON.T @ np.linalg.solve(SOO, SON)
    CI = cst[N] - SON.T @ np.linalg.solve(SOO, cst[obs_o])
    b = np.linalg.solve(VI, CI)
    news = np.zeros(m)
    for k, n_ in enumerate(N):
        news[cells[n_][0]] += b[k] * I[k]
    s = sd[i_target]
    got_n = rd(f"{wd}/nn.csv").ravel(); got_r = rd(f"{wd}/nr.csv").ravel(); got_o = rd(f"{wd}/no.csv").ravel()
    err = max(np.max(np.abs(got_n - s * news)), np.max(np.abs(got_r - s * revs)),
              abs(got_o[0] - (mu[i_target] + s * old)),
              abs(got_o[1] - (mu[i_target] + s * (er + news.sum()))))
    print(f"(C) news: 8 new releases in 3 series, 2 revisions: max |difference| {err:.1e} "
          f"(news {np.round(s * news, 4)}, revisions {np.round(s * revs, 4)})")
    return int(err > 1e-9)


def main():
    fails = 0
    if "--sks" in sys.argv:
        app2(sys.argv[sys.argv.index("--sks") + 1])
    with tempfile.TemporaryDirectory() as wd:
        f, pack = em_check(wd)
        fails += f
        fails += news_check(wd, pack)
    print("nowcast reference:", "AGREES" if fails == 0 else f"{fails} DISAGREEMENT(S)")
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(main())
