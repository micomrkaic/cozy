#!/usr/bin/env python3
"""ref_occbin.py — Dynare's OccBin on the two models tests/77_occbin.test pins.

    python3 tests/ref_occbin.py            print the numbers the test pins
    python3 tests/ref_occbin.py DIR        also write DIR/zlb.json, DIR/irr.json

Not part of `make test` (it needs Octave and Dynare 6: apt install dynare).
It is the independent reference for dsge.occbin, as ref_dsge.py is for
dsge.solve: the same two models, written in Dynare's language with its
occbin_constraints block, the same surprise shocks, solved by Dynare's
own implementation of Guerrieri and Iacoviello's piecewise-linear method.

  zlb  the three-equation New Keynesian model with a floor on the policy
       rate (linear; the regimes differ in the rule)
  irr  a growth model in which investment cannot fall below a fraction of
       its steady-state level (nonlinear; a Lagrange multiplier is zero in
       one regime and the constraint holds in the other)
"""
import json
import pathlib
import subprocess
import sys
import tempfile

ZLB = r"""
var x pi i inot rn;
varexo en;
parameters beta sigma kappa phi_pi phi_x rho_n ibar;
beta = 0.99; sigma = 1; kappa = 0.1275; phi_pi = 1.5; phi_x = 0.125; rho_n = 0.8; ibar = 1.0;
model(linear);
pi = beta*pi(+1) + kappa*x;
x = x(+1) - (1/sigma)*(i - pi(+1) - rn);
inot = phi_pi*pi + phi_x*x;
[name='rate', relax='zlb']
i = inot;
[name='rate', bind='zlb']
i = -ibar;
rn = rho_n*rn(-1) + en;
end;
occbin_constraints;
name 'zlb'; bind inot < -ibar; relax inot > -ibar;
end;
shocks; var en; stderr 1; end;
shocks(surprise);
var en;
periods 1, 2, 12;
values -3, -1, 2;
end;
occbin_setup;
occbin_solver(simul_periods=40, simul_check_ahead_periods=100);
"""

IRR = r"""
var A C Invest K Lambda;
varexo epsilon;
parameters alpha delta beta sigma rho phi;
beta = 0.96; alpha = 0.33; delta = 0.10; sigma = 2; rho = 0.9; phi = 0.975;
model;
-C^(-sigma) + beta*C(+1)^(-sigma)*(1-delta+alpha*exp(A(+1))*K^(alpha-1)) = -Lambda + beta*(1-delta)*Lambda(+1);
C + K - (1-delta)*K(-1) = exp(A)*K(-1)^alpha;
Invest = K - (1-delta)*K(-1);
[name='investment', relax='IRR']
Lambda = 0;
[name='investment', bind='IRR']
log(Invest) - log(phi*steady_state(Invest)) = 0;
A = rho*A(-1) + epsilon;
end;
occbin_constraints;
name 'IRR'; bind log(Invest)-log(steady_state(Invest)) < log(phi); relax Lambda < 0;
end;
steady_state_model;
K = ((1/beta-1+delta)/alpha)^(1/(alpha-1));
C = -delta*K + K^alpha;
Invest = delta*K;
Lambda = 0;
A = 0;
end;
shocks; var epsilon; stderr 0.015; end;
steady;
shocks(surprise);
var epsilon;
periods 1:5, 10, 30, 31:36;
values -0.01, -0.02, 0.02, -0.015;
end;
occbin_setup;
occbin_solver(simul_periods=60, simul_check_ahead_periods=200);
"""

DUMP = r"""
s = oo_.occbin.simul;
r = struct();
r.dynare = dynare_version();
r.endo = M_.endo_names';
r.ys = s.ys';
r.piecewise = s.piecewise;
r.linear = s.linear;
r.binding = arrayfun(@(h) h.regime(1), s.regime_history);
r.error_flag = s.error_flag;
fid = fopen('%s', 'w'); fputs(fid, jsonencode(r)); fclose(fid);
"""


def run(name, text):
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        (tmp / f"{name}.mod").write_text(text)
        out = tmp / "out.json"
        cmd = f"dynare {name} nolog nograph; " + (DUMP % out).replace("\n", " ")
        p = subprocess.run(["octave", "--no-gui", "--quiet", "--eval", cmd], cwd=tmp, capture_output=True, text=True)
        if p.returncode != 0 or not out.exists():
            sys.stderr.write(p.stdout[-2000:] + p.stderr[-2000:])
            raise SystemExit(f"{name}: Dynare failed")
        return json.loads(out.read_text())


def main():
    outdir = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else None
    for name, text in (("zlb", ZLB), ("irr", IRR)):
        r = run(name, text)
        if outdir:
            (outdir / f"{name}.json").write_text(json.dumps(r))
        print(f"== {name}: Dynare {r['dynare']}, error_flag {r['error_flag']}")
        print("   variables:", " ".join(r["endo"]))
        print("   steady state:", " ".join(f"{v:.12g}" for v in r["ys"]))
        b = r["binding"]
        print("   binding periods:", "".join(str(int(v)) for v in b))
        for t in (0, 1, 4, 9, 30, 35):
            if t < len(r["piecewise"]):
                print(f"   t={t + 1:2d}:", " ".join(f"{v:.12g}" for v in r["piecewise"][t]))


if __name__ == "__main__":
    main()
