// The nowcasting page, end to end in a real browser (Playwright + Chromium;
// skips politely without them). For docs/nowcast.html served over HTTP and
// for docs/nowcast-standalone.html opened from disk:
//   - the engine starts in its Web Worker and loads the nowcast package;
//   - a CSV goes in through the file input (dates in three formats, a
//     quarterly series in another file, a header that needs renaming);
//   - estimation converges and the cards show a nowcast;
//   - the news decomposition adds up to the change in the nowcast;
//   - the script and data the page offers for download, run by the native
//     interpreter, give the page's nowcast to 1e-9.
// Run from the repository root: node tests/run_nowcast_page.js
const SKIP = process.env.COZY_REQUIRE_BROWSER ? ", and COZY_REQUIRE_BROWSER is set: FAIL" : ", skipping";
let chromium;
try { chromium = require("playwright").chromium; }
catch (e) { console.log("nowcast page: playwright not installed" + SKIP); process.exit(process.env.COZY_REQUIRE_BROWSER ? 1 : 0); }
const fs = require("fs"), path = require("path"), http = require("http"), os = require("os");
const { execFileSync } = require("child_process");

const ROOT = process.cwd();
const tmp = fs.mkdtempSync(path.join(os.tmpdir(), "nowcast-page-"));
const fail = m => { console.error("nowcast page FAIL:", m); process.exit(1); };

// test data: two files — monthly indicators with mixed date styles, quarterly GDP by quarter label
const Y = fs.readFileSync("tests/data/app2_nowcast.csv", "utf8").trim().split("\n").map(r => r.split(","));
const T = Y[0].length;
const mon = ["date,Industrial Production,Retail sales,employment,Hours (worked),PMI"];
for (let t = 0; t < T; t++) {
  const y = 2015 + Math.floor(t / 12), m = t % 12 + 1;
  const d = t % 3 === 0 ? `${y}-${String(m).padStart(2, "0")}-15` : t % 3 === 1 ? `${y}M${String(m).padStart(2, "0")}` : `${["Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"][m - 1]} ${y}`;
  mon.push([d, ...Y.slice(0, 5).map(r => r[t])].join(","));
}
const qrt = ["observation_date,GDP"];
for (let t = 2; t < T; t += 3) if (Y[5][t] !== "") qrt.push(`${2015 + Math.floor(t / 12)}Q${Math.floor((t % 12) / 3) + 1},${Y[5][t]}`);
fs.writeFileSync(path.join(tmp, "monthly.csv"), mon.join("\n") + "\n");
fs.writeFileSync(path.join(tmp, "gdp.csv"), qrt.join("\n") + "\n");

const server = http.createServer((req, res) => {
  const f = path.join(ROOT, "docs", decodeURIComponent(req.url.split("?")[0]));
  if (!f.startsWith(path.join(ROOT, "docs")) || !fs.existsSync(f)) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { "Content-Type": f.endsWith(".js") ? "text/javascript" : "text/html" });
  fs.createReadStream(f).pipe(res);
});

async function exercise(browser, url, label) {
  const page = await browser.newPage();
  const errors = [];
  page.on("pageerror", e => errors.push(e.message));
  await page.goto(url);
  await page.waitForFunction(() => document.getElementById("engine").textContent === "engine ready", null, { timeout: 90000 })
    .catch(() => fail(label + ": the engine did not start (" + errors.join("; ") + ")"));
  // dates and names, straight from the page's own parsers
  const parsed = await page.evaluate(() => {
    const A = window.NowcastApp, d = s => { const r = A.parseDate(s); return r ? [r.ym, r.q] : null; };
    return { a: d("2024-03-31"), b: d("2024-03"), c: d("2024M03"), e: d("Mar 2024"), f: d("2024Q1"), g: d("Q1 2024"), h: d("31.03.2024"),
             n1: A.cozyName("GDP Growth (%)", 0), n2: A.cozyName("2nd series", 3) };
  });
  const mar = 2024 * 12 + 2, q1 = 2024 * 12;
  for (const k of ["a", "b", "c", "e", "h"]) if (!parsed[k] || parsed[k][0] !== mar) fail(label + ": date " + k + " parsed as " + JSON.stringify(parsed[k]));
  if (parsed.f[0] !== q1 || !parsed.f[1] || parsed.g[0] !== q1) fail(label + ": quarter labels");
  if (parsed.n1 !== "gdp_growth" || parsed.n2 !== "c2nd_series") fail(label + ": readtable's name rule");
  // the two files through the file input
  await page.setInputFiles("#file", [path.join(tmp, "monthly.csv"), path.join(tmp, "gdp.csv")]);
  await page.waitForFunction(() => document.querySelectorAll("#series tbody tr").length === 6, null, { timeout: 20000 })
    .catch(async () => fail(label + ": import — " + await page.textContent("#datamsg")));
  const names = await page.$$eval("#series tbody tr td:nth-child(2)", tds => tds.map(t => t.textContent.trim()));
  if (names.join(" ") !== "industrial_production retail_sales employment hours_worked pmi gdp Q") fail(label + ": series names " + names.join(" "));
  await page.selectOption("#agg", "mean");
  await page.click("#run");
  await page.waitForFunction(() => document.querySelector("#cards .nc") || document.querySelector("#modelmsg .err"), null, { timeout: 90000 });
  const err = await page.$("#modelmsg .err");
  if (err) fail(label + ": estimation — " + await err.textContent());
  const st = await page.evaluate(() => {
    const F = window.NowcastApp.S.fit, P = F.P, i = P.names.indexOf("gdp"), t = P.endQ - P.lo;
    return { conv: F.conv, iters: F.iters, value: F.yv.data[i * F.yv.cols + t], sd: F.ys.data[i * F.ys.cols + t],
             script: window.NowcastApp.S.lastScript, csv: window.NowcastApp.cleanCSV(P), cards: document.querySelectorAll("#cards .nc").length };
  });
  if (!st.conv || st.cards !== 2 || !(st.sd > 0)) fail(label + ": fit " + JSON.stringify({ conv: st.conv, cards: st.cards, sd: st.sd }));
  // news over two held-back months: the parts add up
  await page.click("#newsrun");
  await page.waitForFunction(() => !document.getElementById("newsout").classList.contains("hidden") || document.querySelector("#newsmsg .err"), null, { timeout: 60000 });
  const nw = await page.evaluate(() => {
    const N = window.NowcastApp.S.news; if (!N) return null;
    const s = N.items.reduce((a, it) => a + it.value + it.rev, 0);
    return { gap: Math.abs(N.oldv + s - N.newv), move: Math.abs(N.newv - N.oldv) };
  });
  if (!nw || nw.gap > 1e-10 || !(nw.move > 0)) fail(label + ": news " + JSON.stringify(nw));
  if (errors.length) fail(label + ": page errors: " + errors.join("; "));
  await page.close();
  return st;
}

(async () => {
  await new Promise(r => server.listen(0, "127.0.0.1", r));
  const port = server.address().port;
  let browser;
  try { browser = await chromium.launch(); }
  catch (e) { console.log("nowcast page: no browser for playwright" + SKIP); server.close(); process.exit(process.env.COZY_REQUIRE_BROWSER ? 1 : 0); }
  try {
    const a = await exercise(browser, `http://127.0.0.1:${port}/nowcast.html`, "served page");
    const b = await exercise(browser, "file://" + path.join(ROOT, "docs/nowcast-standalone.html"), "standalone file");
    if (Math.abs(a.value - b.value) > 1e-12) fail("served and standalone pages disagree: " + a.value + " vs " + b.value);
    // the downloads, run natively
    fs.writeFileSync(path.join(tmp, "nowcast_data.csv"), a.csv);
    fs.writeFileSync(path.join(tmp, "nowcast_script.cz"), a.script + 'print("{:.17g} {:.17g}", n.value, n.sd)\n');
    const out = execFileSync(path.join(ROOT, "cozy"), ["nowcast_script.cz"], { cwd: tmp, encoding: "utf8" }).trim().split("\n").pop();
    const [v, s] = out.split(" ").map(Number);
    if (!(Math.abs(v - a.value) < 1e-9 && Math.abs(s - a.sd) < 1e-9))
      fail(`the exported script gives ${v} (sd ${s}), the page ${a.value} (sd ${a.sd})`);
    console.log(`nowcast page: served and standalone pages run (EM ${a.iters} iterations); news adds up; ` +
                `the exported script reproduces the nowcast (${v.toFixed(6)}, |diff| ${Math.abs(v - a.value).toExponential(1)})`);
  } finally {
    await browser.close(); server.close();
    fs.rmSync(tmp, { recursive: true, force: true });
  }
})().catch(e => fail(e.stack || e));
