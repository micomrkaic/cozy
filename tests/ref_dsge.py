#!/usr/bin/env python3
"""Independent reference for packages/dsge.cz (not run by `make test`; it
needs numpy, scipy and sympy). It shares nothing with the package: the
steady state comes from scipy's fsolve, the Jacobians from sympy's symbolic
derivatives, and the decision rule from an ordered generalized Schur (QZ)
decomposition of the companion pencil — where the package uses Newton with
dual numbers, dual-number Jacobians and cyclic reduction. The numbers this
prints are the expectations in tests/71_dsge.test.

    python3 tests/ref_dsge.py
"""
import numpy as np
import sympy as sp
from scipy.linalg import ordqz, solve_discrete_lyapunov
from scipy.optimize import fsolve

np.set_printoptions(precision=10, suppress=True, linewidth=200)


def solve_model(names, shocks, params, eqs, guess, sd):
    """eqs(lead, cur, lag, shock, par) -> list of residual expressions."""
    n, k = len(names), len(shocks)
    F = sp.symbols([f"{v}_f" for v in names])
    Y = sp.symbols([f"{v}_0" for v in names])
    L = sp.symbols([f"{v}_b" for v in names])
    E = sp.symbols(shocks)
    as_map = lambda syms: dict(zip(names, syms))
    res = sp.Matrix(eqs(as_map(F), as_map(Y), as_map(L), dict(zip(shocks, E)), params))
    # steady state: f(y, y, y, 0) = 0
    S = sp.symbols([f"{v}_s" for v in names])
    at_ss = {**dict(zip(F, S)), **dict(zip(Y, S)), **dict(zip(L, S)), **{e: 0 for e in E}}
    g = sp.lambdify(S, res.subs(at_ss), "numpy")
    ss = fsolve(lambda y: np.array(g(*y), dtype=float).ravel(), guess, xtol=1e-13)
    point = {**dict(zip(F, ss)), **dict(zip(Y, ss)), **dict(zip(L, ss)), **{e: 0 for e in E}}
    num = lambda M: np.array(M.subs(point), dtype=float)
    A, B, C, D = (num(res.jacobian(x)) for x in (F, Y, L, E))
    # companion pencil  N [y_t; y_{t+1}] = M [y_{t-1}; y_t]
    Z, I = np.zeros((n, n)), np.eye(n)
    M = np.block([[Z, I], [-C, -B]])
    N = np.block([[I, Z], [Z, A]])
    S_, T_, alpha, beta, Q_, Zm = ordqz(M, N, sort=lambda a, b: np.abs(a) < (1 + 1e-6) * np.abs(b), output="real")
    with np.errstate(divide="ignore", invalid="ignore"):
        mod = np.where(np.abs(beta) < 1e-12, np.inf, np.abs(alpha) / np.abs(beta))
    nstable = int(np.sum(mod < 1 + 1e-6))
    out = {"ss": ss, "roots": np.sort(mod), "nstable": nstable, "n": n}
    if nstable == n:
        P = Zm[n:, :n] @ np.linalg.inv(Zm[:n, :n])
        Qm = -np.linalg.solve(A @ P + B, D)
        W = Qm @ np.diag(np.array(sd) ** 2) @ Qm.T
        out.update(P=P, Q=Qm)
        if np.max(np.abs(np.linalg.eigvals(P))) < 1:
            Sig = solve_discrete_lyapunov(P, W)
            out.update(var=Sig, sd=np.sqrt(np.diag(Sig)))
            dec = []
            for j in range(k):
                q = Qm[:, [j]] * sd[j]
                dec.append(np.diag(solve_discrete_lyapunov(P, q @ q.T)))
            dec = np.array(dec).T
            out["vardecomp"] = 100 * dec / dec.sum(axis=1, keepdims=True)
    return out


def rbc(par):
    def eqs(f, y, l, e, p):
        return [
            y["c"] ** (-p["sigma"]) - p["beta"] * f["c"] ** (-p["sigma"])
            * (p["alpha"] * sp.exp(f["a"]) * y["k"] ** (p["alpha"] - 1) + 1 - p["delta"]),
            y["y"] - sp.exp(y["a"]) * l["k"] ** p["alpha"],
            y["k"] - ((1 - p["delta"]) * l["k"] + y["i"]),
            y["y"] - (y["c"] + y["i"]),
            y["a"] - (p["rho"] * l["a"] + e["e"]),
        ]
    return solve_model(["y", "c", "i", "k", "a"], ["e"], par, eqs, [3.0, 2.0, 1.0, 30.0, 0.0], [0.01])


def nk(par):
    def eqs(f, y, l, e, p):
        return [
            y["pi"] - (p["beta"] * f["pi"] + p["kappa"] * y["x"]),
            y["x"] - (f["x"] - (1 / p["sigma"]) * (y["i"] - f["pi"] - y["rn"])),
            y["i"] - (p["phi_pi"] * y["pi"] + p["phi_x"] * y["x"] + y["v"]),
            y["rn"] - (p["rho_n"] * l["rn"] + e["en"]),
            y["v"] - (p["rho_v"] * l["v"] + e["ev"]),
        ]
    return solve_model(["x", "pi", "i", "rn", "v"], ["en", "ev"], par, eqs, [0.0] * 5, [0.25, 0.25])


def hansen(par):
    """Hansen (1985) with divisible labour: y c i k n w r a."""
    def eqs(f, y, l, e, p):
        return [
            1 / y["c"] - p["beta"] / f["c"] * (f["r"] + 1 - p["delta"]),
            p["psi"] * y["c"] / (1 - y["n"]) - y["w"],
            y["y"] - sp.exp(y["a"]) * l["k"] ** p["alpha"] * y["n"] ** (1 - p["alpha"]),
            y["w"] - (1 - p["alpha"]) * y["y"] / y["n"],
            y["r"] - p["alpha"] * y["y"] / l["k"],
            y["k"] - ((1 - p["delta"]) * l["k"] + y["i"]),
            y["y"] - (y["c"] + y["i"]),
            y["a"] - (p["rho"] * l["a"] + e["e"]),
        ]
    return solve_model(["y", "c", "i", "k", "n", "w", "r", "a"], ["e"], par, eqs,
                       [1.0, 0.8, 0.2, 10.0, 0.3, 2.0, 0.04, 0.0], [0.007])


if __name__ == "__main__":
    RBC = dict(alpha=0.33, beta=0.99, delta=0.025, rho=0.95, sigma=1.0)
    r = rbc(RBC)
    print("RBC ss        ", r["ss"])
    print("RBC P[:, k]   ", r["P"][:, 3])
    print("RBC P[:, a]   ", r["P"][:, 4])
    print("RBC Q         ", r["Q"].ravel())
    print("RBC roots     ", r["roots"])
    print("RBC sd        ", r["sd"])
    print("RBC corr(y, c)", r["var"][0, 1] / (r["sd"][0] * r["sd"][1]))
    r2 = rbc({**RBC, "sigma": 2.0, "beta": 0.98})
    print("RBC2 ss       ", r2["ss"])
    print("RBC2 P[:, k]  ", r2["P"][:, 3])
    print("RBC2 Q        ", r2["Q"].ravel())

    NK = dict(beta=0.99, sigma=1.0, kappa=0.1275, phi_pi=1.5, phi_x=0.125, rho_n=0.9, rho_v=0.5)
    q = nk(NK)
    print("NK Q[:, en]   ", q["Q"][:, 0])
    print("NK Q[:, ev]   ", q["Q"][:, 1])
    print("NK roots      ", q["roots"])
    print("NK sd         ", q["sd"])
    print("NK vardecomp  ", q["vardecomp"][:3].ravel())
    lam = 1 / ((1 - NK["beta"] * NK["rho_v"]) * (NK["sigma"] * (1 - NK["rho_v"]) + NK["phi_x"])
               + NK["kappa"] * (NK["phi_pi"] - NK["rho_v"]))
    print("NK Gali closed form x, pi on ev:", -(1 - NK["beta"] * NK["rho_v"]) * lam, -NK["kappa"] * lam)
    for phi in (0.9, 1.0, 1.05):
        b = nk({**NK, "phi_pi": phi})
        print(f"NK phi_pi={phi}: stable roots {b['nstable']} of {b['n']} needed;",
              "Taylor principle value", NK["kappa"] * (phi - 1) + (1 - NK["beta"]) * NK["phi_x"])

    H = dict(alpha=0.36, beta=0.99, delta=0.025, rho=0.95, psi=1.75)
    h = hansen(H)
    print("Hansen ss     ", h["ss"])
    print("Hansen P[:, k]", h["P"][:, 3])
    print("Hansen Q      ", h["Q"].ravel())
    print("Hansen sd     ", h["sd"])
    print("Hansen roots  ", h["roots"])
