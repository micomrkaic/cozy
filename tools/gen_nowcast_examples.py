#!/usr/bin/env python3
"""The nowcast page's example data, simulated from a known model.

Writes the examples into docs/nowcast.html (between the EXAMPLES markers)
and, for scripts, aligned panels into data/:

  weekly   a weekly tracker: daily market data (a stock index and a credit
           spread, business days), weekly indicators (jobless claims, card
           spending, rail traffic), monthly ones (industrial production,
           retail sales, payrolls, a PMI, the unemployment rate) and
           quarterly GDP, with each series' release lag, so the data end on
           different days. One activity factor moves at a weekly pace; GDP
           is the quarterly average of a weekly level whose growth loads on
           it, so a weekly GDP tracker has a true path to recover.
  missing  a monthly panel with the missing-data cases a real one has: a
           survey that starts late, one suspended for six months, a series
           discontinued, one released irregularly, and a second quarterly
           series that starts late.

    python3 tools/gen_nowcast_examples.py           # write
    python3 tools/gen_nowcast_examples.py --check   # verify (make test)

Pure Python (random.Random, a fixed seed): the same bytes everywhere.
"""
import datetime as dt
import math
import random
import re
import sys

PAGE = "docs/nowcast.html"
BEGIN, END = "/* EXAMPLES-BEGIN (tools/gen_nowcast_examples.py) */", "/* EXAMPLES-END */"
ASOF = dt.date(2025, 11, 20)            # a Thursday: what is out by then


def f4(x):
    return "" if x is None else f"{x:.4f}".rstrip("0").rstrip(".")


def csv(header, rows):
    return "\n".join([",".join(header)] + [",".join(r) for r in rows]) + "\n"


# --------------------------------------------------------------------------
# the weekly tracker
# --------------------------------------------------------------------------
def weekly():
    R = random.Random(20251120)
    g = R.gauss
    start = dt.date(2017, 1, 7)          # weeks end on Saturdays
    days0 = dt.date(2017, 1, 1)
    nwk = (dt.date(2026, 1, 3) - start).days // 7 + 1     # through the last week of 2025Q4
    wend = [start + dt.timedelta(7 * k) for k in range(nwk)]
    # the activity factor, weekly AR(1), with a recession in 2020
    a, x = [], 0.0
    for k, d in enumerate(wend):
        shock = -1.6 if dt.date(2020, 3, 14) <= d <= dt.date(2020, 4, 25) else 0.0
        rebound = 0.9 if dt.date(2020, 5, 16) <= d <= dt.date(2020, 7, 4) else 0.0
        x = 0.9 * x + 0.45 * g(0, 1) + shock + rebound
        a.append(x)
    # weekly GDP: log level x 100 with growth 0.04 + 0.06 a (about 2% a year)
    lg, lv = [], 0.0
    for v in a:
        lv += 0.04 + 0.06 * v
        lg.append(lv)
    gdp_level = [100 * math.exp(v / 100) for v in lg]

    # daily data, business days: the stock index and a credit spread
    daily = []
    p, s_noise, fac = math.log(2250.0), 0.0, 0.0
    d = days0
    k = 0
    while d <= ASOF - dt.timedelta(1):
        if d.weekday() < 5 and not (d.month == 12 and d.day == 25) and not (d.month == 1 and d.day == 1):
            while k + 1 < nwk and wend[k] < d:
                k += 1
            fac = 0.8 * fac + 0.2 * a[k]
            p += 0.00035 + 0.0035 * (a[k] - (a[k - 1] if k else 0)) / 5 + 0.0095 * g(0, 1)
            s_noise = 0.97 * s_noise + 2.0 * g(0, 1)
            spread = 145 - 22 * fac + s_noise
            daily.append([d.isoformat(), f"{math.exp(p):.2f}", f"{spread:.1f}"])
        d += dt.timedelta(1)

    # weekly indicators, with their release lags (the week's Saturday + lag)
    claims, card, rail = [], [], []
    cl_n, cd_l, rl_l = 0.0, math.log(100.0), math.log(250.0)
    for k, d in enumerate(wend):
        cl_n = 0.7 * cl_n + 0.05 * g(0, 1)
        claims.append(math.exp(math.log(225) - 0.11 * a[k] + cl_n))
        cd_l += 0.0008 + 0.004 * a[k] + 0.012 * g(0, 1)
        card.append(100 * math.exp(cd_l - math.log(100.0)))
        rl_l += 0.0002 + 0.003 * a[k] + 0.02 * g(0, 1)
        rail.append(math.exp(rl_l))

    def out(d, lag):                     # released by ASOF?
        return d + dt.timedelta(lag) <= ASOF
    wrows = []
    for k, d in enumerate(wend):
        if d > ASOF:
            break
        wrows.append([d.isoformat(),
                      f"{claims[k]:.1f}" if out(d, 5) else "",
                      f"{card[k]:.2f}" if out(d, 4) else "",
                      f"{rail[k]:.1f}" if out(d, 11) else ""])

    # monthly: each a function of its weeks (a week belongs to the month of its Wednesday)
    def month_of(d):
        w = d - dt.timedelta(3)
        return (w.year, w.month)
    months = sorted({month_of(d) for d in wend})
    ip_l, rs_l, emp, u = math.log(100.0), math.log(500.0), 146000.0, 4.7
    ip_w, rs_w = [], []
    for k, d in enumerate(wend):
        ip_l += 0.0002 + 0.0022 * a[k] + 0.002 * g(0, 1)
        rs_l += 0.0009 + 0.0025 * a[k] + 0.003 * g(0, 1)
        ip_w.append(100 * math.exp(ip_l - math.log(100.0)))
        rs_w.append(math.exp(rs_l))
    mrows = []
    for (y, m) in months:
        ks = [k for k, d in enumerate(wend) if month_of(d) == (y, m)]
        am = sum(a[k] for k in ks) / len(ks)
        emp += 160 + 220 * am + 60 * g(0, 1)
        u = 0.97 * u + 0.03 * 4.2 - 0.05 * am + 0.05 * g(0, 1)
        pmi = 51 + 3.2 * am + 1.1 * g(0, 1)
        nxt = dt.date(y + (m == 12), m % 12 + 1, 1)
        ip = sum(ip_w[k] for k in ks) / len(ks)
        rs = sum(rs_w[k] for k in ks) / len(ks)
        rel = lambda lag: nxt + dt.timedelta(lag) <= ASOF     # days after the month ends
        if nxt > ASOF + dt.timedelta(40):
            continue
        mrows.append([f"{y}-{m:02d}",
                      f"{ip:.2f}" if rel(16) else "", f"{rs:.1f}" if rel(15) else "",
                      f"{emp:.0f}" if rel(5) else "", f"{pmi:.1f}" if rel(1) else "",
                      f"{u:.1f}" if rel(5) else ""])
    # quarterly GDP: the average of its weeks' levels, out a month after the quarter
    def q_of(d):
        y, m = month_of(d)
        return (y, (m - 1) // 3 + 1)
    qs = sorted({q_of(d) for d in wend})
    qrows = []
    qavg = {}
    for (y, q) in qs:
        ks = [k for k, d in enumerate(wend) if q_of(d) == (y, q)]
        lvl = sum(gdp_level[k] for k in ks) / len(ks)
        qavg[(y, q)] = lvl
        qend = dt.date(y + (q == 4), (3 * q) % 12 + 1, 1)
        if qend > ASOF + dt.timedelta(100):
            continue
        qrows.append([f"{y}Q{q}", f"{lvl:.3f}" if qend + dt.timedelta(29) <= ASOF else ""])
    files = [
        ("markets_daily.csv", csv(["date", "stock_index", "credit_spread"], daily)),
        ("indicators_weekly.csv", csv(["week_ending", "initial_claims", "card_spending", "rail_traffic"], wrows)),
        ("indicators_monthly.csv", csv(["month", "industrial_production", "retail_sales", "payrolls", "pmi", "unemployment_rate"], mrows)),
        ("gdp_quarterly.csv", csv(["quarter", "gdp"], qrows)),
    ]
    # the true weekly GDP growth, for checking the tracker (not shown on the page)
    truth = [(d.isoformat(), 0.04 + 0.06 * a[k]) for k, d in enumerate(wend) if d <= ASOF]
    q4 = 100 * (qavg[(2025, 4)] / qavg[(2025, 3)] - 1)
    return files, truth, wend, q4


# --------------------------------------------------------------------------
# missing data, monthly
# --------------------------------------------------------------------------
def missing():
    R = random.Random(7)
    g = R.gauss
    months = [(y, m) for y in range(2012, 2026) for m in range(1, 13) if (y, m) <= (2025, 10)]
    f, x = [], 0.0
    for _ in months:
        x = 0.75 * x + g(0, 1)
        f.append(x)
    rows = []
    gdp_m, inv_m = [], []
    for k, (y, m) in enumerate(months):
        v = f[k]
        ip = 0.15 + 0.9 * v + 0.6 * g(0, 1)
        rs = 0.25 + 0.7 * v + 0.7 * g(0, 1)
        emp = 0.12 + 0.5 * v + 0.4 * g(0, 1)
        conf = 98 + 4 * v + 2.5 * g(0, 1)
        hs = 0.1 + 1.1 * v + 1.2 * g(0, 1)
        ex = 0.3 + 0.6 * v + 0.9 * g(0, 1)
        bs = 0.8 * v + 0.6 * g(0, 1)
        gdp_m.append(0.18 + 0.35 * v)
        inv_m.append(0.2 + 0.9 * v)
        late = (y, m) < (2019, 1)                       # the survey starts in 2019
        gap = (2020, 4) <= (y, m) <= (2020, 9)          # suspended for six months
        gone = (y, m) > (2022, 12)                      # discontinued
        irregular = R.random() < 0.2                    # one release in five missing
        edge = (y, m) == (2025, 10)                     # the last month: not everything is out
        rows.append([f"{y}-{m:02d}-01", f4(ip), f4(None if edge else rs), f4(emp),
                     f4(None if late else conf), f4(None if gap or edge else hs),
                     f4(None if gone else ex), f4(None if irregular else bs), "", ""])
    # quarterly: averages of their months, plus noise; investment from 2016
    for k in range(2, len(months), 3):
        y, m = months[k]
        q = sum(gdp_m[k - 2:k + 1]) / 3 + 0.08 * g(0, 1)
        iv = sum(inv_m[k - 2:k + 1]) / 3 + 0.3 * g(0, 1)
        if (y, m) <= (2025, 6):
            rows[k][8] = f4(q)
            if y >= 2016:
                rows[k][9] = f4(iv)
    header = ["date", "industrial_production", "retail_sales", "employment", "consumer_confidence",
              "housing_starts", "exports", "business_survey", "gdp", "investment"]
    return [("regional_panel.csv", csv(header, rows))]


def page_block(weekly_files, missing_files):
    def js(files):
        return "[" + ",\n    ".join("{ name: " + repr(n).replace("'", '"') + ", csv: `" + c + "` }" for n, c in files) + "]"
    return (BEGIN + "\nconst EXAMPLE_FILES = {\n  weekly: " + js(weekly_files) +
            ",\n  missing: " + js(missing_files) + "\n};\n" + END)


def weekly_panel(files, wend):
    """the weekly tracker as one weekly table for scripts: daily averaged over
    each week, monthly and quarterly values in their period's last week"""
    daily = [l.split(",") for l in files[0][1].strip().split("\n")[1:]]
    wk = [l.split(",") for l in files[1][1].strip().split("\n")[1:]]
    mo = [l.split(",") for l in files[2][1].strip().split("\n")[1:]]
    qu = [l.split(",") for l in files[3][1].strip().split("\n")[1:]]
    weeks = [d for d in wend if d <= ASOF + dt.timedelta(6)]
    month_of = lambda d: ((d - dt.timedelta(3)).year, (d - dt.timedelta(3)).month)
    rows = [[d.isoformat()] + [""] * 11 for d in weeks]
    for k, d in enumerate(weeks):
        vals = [r for r in daily if d - dt.timedelta(6) <= dt.date.fromisoformat(r[0]) <= d]
        if vals:
            rows[k][1] = f"{sum(float(r[1]) for r in vals) / len(vals):.2f}"
            rows[k][2] = f"{sum(float(r[2]) for r in vals) / len(vals):.2f}"
    for r in wk:
        k = [i for i, d in enumerate(weeks) if d.isoformat() == r[0]][0]
        rows[k][3:6] = r[1:4]
    for r in mo:
        y, m = map(int, r[0].split("-"))
        ks = [i for i, d in enumerate(weeks) if month_of(d) == (y, m)]
        if ks and month_of(weeks[ks[-1]] + dt.timedelta(7)) != (y, m):
            rows[ks[-1]][6:11] = r[1:6]
    for r in qu:
        y, q = int(r[0][:4]), int(r[0][5])
        ks = [i for i, d in enumerate(weeks) if month_of(d)[0] == y and (month_of(d)[1] - 1) // 3 + 1 == q]
        nxt = weeks[ks[-1]] + dt.timedelta(7) if ks else None
        if ks and ((month_of(nxt)[1] - 1) // 3 + 1 != q or month_of(nxt)[0] != y):
            rows[ks[-1]][11] = r[1]
    header = ["date", "stock_index", "credit_spread", "initial_claims", "card_spending", "rail_traffic",
              "industrial_production", "retail_sales", "payrolls", "pmi", "unemployment_rate", "gdp"]
    return csv(header, rows)


def main():
    check = "--check" in sys.argv
    wfiles, truth, wend, q4 = weekly()
    mfiles = missing()
    block = page_block(wfiles, mfiles)
    outputs = {"data/nowcast_weekly.csv": weekly_panel(wfiles, wend),
               "data/nowcast_missing.csv": mfiles[0][1],
               "data/nowcast_weekly_truth.csv": csv(["week_ending", "gdp_growth_weekly"], [[d, f"{v:.5f}"] for d, v in truth])}
    page = open(PAGE).read()
    if BEGIN not in page:
        sys.exit(f"{PAGE}: no {BEGIN} marker")
    new_page = re.sub(re.escape(BEGIN) + ".*?" + re.escape(END), lambda _: block, page, flags=re.S)
    stale = []
    if new_page != page:
        stale.append(PAGE)
    for p, text in outputs.items():
        try:
            if open(p).read() != text:
                stale.append(p)
        except FileNotFoundError:
            stale.append(p)
    if check:
        if stale:
            sys.exit("nowcast examples: STALE " + ", ".join(stale) + " — run tools/gen_nowcast_examples.py")
        print("nowcast examples: page and data/ current")
        return
    open(PAGE, "w").write(new_page)
    for p, text in outputs.items():
        open(p, "w").write(text)
    kb = sum(len(c) for _, c in wfiles + mfiles) / 1024
    print(f"weekly example: true 2025Q4 GDP growth {q4:.3f}% q/q (the nowcast's target)")
    print(f"nowcast examples: {len(wfiles) + len(mfiles)} files ({kb:.0f} KB) into {PAGE}; " + ", ".join(outputs))


if __name__ == "__main__":
    main()
