#!/usr/bin/env python3
"""ref_dsge2.py — Dynare's second-order solution of the models tests/78_dsge2.test pins.

    python3 tests/ref_dsge2.py            print the numbers the test pins
    python3 tests/ref_dsge2.py DIR        also write DIR/NAME.json for each model

Not part of `make test` (it needs Octave and Dynare 6: apt install dynare).
It is the independent reference for dsge.solve2 and dsge.mean2: the same
models in Dynare's language, solved with stoch_simul(order = 2).

  rbc     the stochastic growth model of dsge.rbc()
  twoshk  Collard's growth model with labour and two shocks (Dynare's
          example1.mod, with the shocks independent)
  twocorr the same model with correlated shocks of different sizes, for
          the covariance that dg_second and dg_mean2 take
  hours   a growth model with a labour choice, wage and rental rate

The JSON holds, with rows in the order the variables are declared and the
state variables in the order they are declared: ghx, ghu, ghxx, ghxu, ghuu
(Kronecker columns reordered to match), ghs2, the second-order mean, and
a pruned simulation of 40 periods for the shocks sd * sin(1.3 t + j).
"""
import json
import pathlib
import subprocess
import sys
import tempfile

MODELS = {
    "rbc": r"""
var y c i k a;
varexo e;
parameters alpha beta delta rho sigma;
alpha = 0.33; beta = 0.99; delta = 0.025; rho = 0.95; sigma = 1;
model;
c^(-sigma) = beta * c(+1)^(-sigma) * (alpha * exp(a(+1)) * k^(alpha - 1) + 1 - delta);
y = exp(a) * k(-1)^alpha;
k = (1 - delta) * k(-1) + i;
y = c + i;
a = rho * a(-1) + e;
end;
initval; y = 3; c = 2; i = 1; k = 30; a = 0; end;
shocks; var e; stderr 0.01; end;
""",
    "twoshk": r"""
var y c k a h b;
varexo e u;
parameters beta rho alpha delta theta psi tau;
alpha = 0.36; rho = 0.95; tau = 0.025; beta = 0.99; delta = 0.025; psi = 0; theta = 2.95;
model;
c*theta*h^(1+psi) = (1-alpha)*y;
k = beta*(((exp(b)*c)/(exp(b(+1))*c(+1)))*(exp(b(+1))*alpha*y(+1)+(1-delta)*k));
y = exp(a)*(k(-1)^alpha)*(h^(1-alpha));
k = exp(b)*(y-c)+(1-delta)*k(-1);
a = rho*a(-1)+tau*b(-1) + e;
b = tau*a(-1)+rho*b(-1) + u;
end;
initval; y = 1.08068253095672; c = 0.80359242014163; h = 0.29175631001732; k = 11.08360443260358; a = 0; b = 0; end;
shocks; var e; stderr 0.009; var u; stderr 0.009; end;
""",
    "twocorr": r"""
var y c k a h b;
varexo e u;
parameters beta rho alpha delta theta psi tau;
alpha = 0.36; rho = 0.95; tau = 0.025; beta = 0.99; delta = 0.025; psi = 0; theta = 2.95;
model;
c*theta*h^(1+psi) = (1-alpha)*y;
k = beta*(((exp(b)*c)/(exp(b(+1))*c(+1)))*(exp(b(+1))*alpha*y(+1)+(1-delta)*k));
y = exp(a)*(k(-1)^alpha)*(h^(1-alpha));
k = exp(b)*(y-c)+(1-delta)*k(-1);
a = rho*a(-1)+tau*b(-1) + e;
b = tau*a(-1)+rho*b(-1) + u;
end;
initval; y = 1.08068253095672; c = 0.80359242014163; h = 0.29175631001732; k = 11.08360443260358; a = 0; b = 0; end;
shocks; var e; stderr 0.009; var u; stderr 0.012; corr e, u = 0.4; end;
""",
    "hours": r"""
var y c i n k w r a;
varexo e;
parameters alpha beta delta rho sigma phi psi;
alpha = 0.33; beta = 0.99; delta = 0.025; rho = 0.95; sigma = 2; phi = 1; psi = 8;
model;
c^(-sigma) = beta * c(+1)^(-sigma) * (r(+1) + 1 - delta);
psi * n^phi * c^sigma = w;
y = exp(a) * k(-1)^alpha * n^(1 - alpha);
w = (1 - alpha) * y / n;
r = alpha * y / k(-1);
k = (1 - delta) * k(-1) + i;
y = c + i;
a = rho * a(-1) + e;
end;
initval; y = 1; c = 0.75; i = 0.25; n = 0.33; k = 10; w = 2; r = 0.035; a = 0; end;
shocks; var e; stderr 0.007; end;
""",
}

DUMP = r"""
dr = oo_.dr; n = M_.endo_nbr; k = M_.exo_nbr;
back(dr.order_var) = 1:n;
st_dr = dr.order_var(M_.nstatic + (1:M_.nspred));     % the states, in Dynare's order
st = sort(st_dr);                                     % ... and in declaration order
[~, pos] = ismember(st, st_dr);                       % where each declared state sits in Dynare's order
nx = numel(st);
% Kronecker columns (a, b) in declaration order -> Dynare's column index
ixx = zeros(1, nx*nx); ixu = zeros(1, nx*k);
for a = 1:nx
  for b = 1:nx, ixx((a-1)*nx + b) = (pos(a)-1)*nx + pos(b); end
  for j = 1:k,  ixu((a-1)*k + j)  = (pos(a)-1)*k + j; end
end
r = struct();
r.dynare = dynare_version();
r.endo = M_.endo_names';
r.states = M_.endo_names(st)';
r.ys = dr.ys';
r.ghx = dr.ghx(back, pos);
r.ghu = dr.ghu(back, :);
r.ghxx = dr.ghxx(back, ixx);
r.ghxu = dr.ghxu(back, ixu);
r.ghuu = dr.ghuu(back, :);
r.ghs2 = dr.ghs2(back)';
r.mean = oo_.mean';
% a pruned simulation from the steady state for a fixed, reproducible shock path
T = 40; ex = zeros(T, k);
for t = 1:T, for j = 1:k, ex(t, j) = sqrt(M_.Sigma_e(j, j)) * sin(1.3 * t + j); end, end
options_.pruning = true;
ysim = simult_(M_, options_, dr.ys, dr, ex, 2);
r.sim = ysim(:, 2:end)';
fid = fopen('out.json', 'w'); fputs(fid, jsonencode(r)); fclose(fid);
"""


def run(name, text):
    with tempfile.TemporaryDirectory() as tmp:
        tmp = pathlib.Path(tmp)
        (tmp / f"{name}.mod").write_text(text + "\nsteady(tolf=1e-13, tolx=1e-13);\nstoch_simul(order = 2, irf = 0, nograph, noprint);\n")
        out = tmp / "out.json"
        (tmp / "dump_second_order.m").write_text(DUMP)
        cmd = f"dynare {name} nolog; dump_second_order"
        p = subprocess.run(["octave", "--no-gui", "--quiet", "--eval", cmd], cwd=tmp, capture_output=True, text=True)
        if p.returncode != 0 or not out.exists():
            sys.stderr.write(p.stdout[-2000:] + p.stderr[-2000:])
            raise SystemExit(f"{name}: Dynare failed")
        return json.loads(out.read_text())


def flat(x):
    return [v for row in x for v in row] if x and isinstance(x[0], list) else list(x)


def main():
    outdir = pathlib.Path(sys.argv[1]) if len(sys.argv) > 1 else None
    for name, text in MODELS.items():
        r = run(name, text)
        if outdir:
            (outdir / f"{name}.json").write_text(json.dumps(r))
        states = r["states"] if isinstance(r["states"], list) else [r["states"]]
        print(f"== {name}: Dynare {r['dynare']}; variables {' '.join(r['endo'])}; states {' '.join(states)}")
        for key in ("ghxx", "ghxu", "ghuu"):
            m = r[key]
            rows = m if isinstance(m[0], list) else [[v] for v in m]
            for nm, row in zip(r["endo"], rows):
                print(f"   {key}[{nm}]:", " ".join(f"{v:.12g}" for v in row))
        print("   ghs2:", " ".join(f"{v:.12g}" for v in r["ghs2"]))
        print("   mean:", " ".join(f"{v:.12g}" for v in r["mean"]))


if __name__ == "__main__":
    main()
