// The playground (docs/index.html) in a real browser — Playwright + Chromium;
// skips politely without them. What the jsdom wiring test (run_page.js)
// cannot see: the engine suspending at pause() and input() (Asyncify) and
// the page waiting for it.
//   - demo(): the menu is in the terminal when the section prompt appears;
//     "demo5" typed there plays section 5, Enter steps its problems, all of
//     them land in the terminal, the menu comes back, q quits;
//   - the stepping tour: demo5, then Enter for each next problem;
//   - input() answered in the terminal, no modal dialog;
//   - the page's own commands (the workspace refresh) wait for a
//     suspended command instead of running into it.
// 0.1.49: before, the first pause returned control to the page, the page ran
// its workspace refresh into the waiting demo, and the rest of the section
// went to the browser console while the section prompt kept coming back.
// Run from the repository root: node tests/run_playground_page.js
let chromium;
try { chromium = require("playwright").chromium; }
catch (e) { console.log("playground: playwright not installed, skipping"); process.exit(0); }
const fs = require("fs"), path = require("path"), http = require("http");

const ROOT = process.cwd();
const fail = m => { console.error("playground FAIL:", m); process.exit(1); };
const server = http.createServer((req, res) => {
  const f = path.join(ROOT, "docs", decodeURIComponent(req.url.split("?")[0]));
  if (!f.startsWith(path.join(ROOT, "docs")) || !fs.existsSync(f) || fs.statSync(f).isDirectory()) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { "Content-Type": f.endsWith(".js") ? "text/javascript" : f.endsWith(".md") ? "text/plain" : "text/html" });
  fs.createReadStream(f).pipe(res);
});

(async () => {
  await new Promise(r => server.listen(0, "127.0.0.1", r));
  let browser;
  try { browser = await chromium.launch(); }
  catch (e) { console.log("playground: no browser for playwright, skipping"); server.close(); process.exit(0); }
  try {
    const p = await browser.newPage();
    let dialogs = 0; const leaked = [];
    p.on("dialog", d => { dialogs++; d.accept("q"); });
    p.on("console", m => { if (/Problem|cozy>/.test(m.text())) leaked.push(m.text()); });
    p.on("pageerror", e => fail("page error: " + e.message));
    await p.goto(`http://127.0.0.1:${server.address().port}/index.html`);
    await p.waitForSelector(".inputline input", { state: "visible", timeout: 90000 });
    const term = () => p.evaluate(() => document.getElementById("term").innerText);
    const type = async s => { await p.fill(".inputline input", s); await p.press(".inputline input", "Enter"); };
    const state = () => p.evaluate(() => ({ pause: window.__cozyPauseWaiting | 0, input: window.__cozyInputWaiting | 0 }));
    const settle = async (pred, what) => {
      for (let i = 0; i < 200; i++) { if (await pred()) return; await p.waitForTimeout(50); }
      fail("timed out waiting for " + what);
    };

    await type('load("demo")');
    await settle(async () => (await term()).includes("demo15"), "the demo package");

    // --- demo(): the menu loop ---
    let mark = (await term()).length;
    await type("demo()");
    await settle(async () => (await state()).input === 1, "the section prompt");
    let seen = (await term()).slice(mark);
    if (!/demo5\s+5\. Matrices/.test(seen)) fail("the menu is not in the terminal at the section prompt");
    if (!/section \(number/.test(seen.trim().split("\n").pop())) fail("the section prompt is not the last line");
    await type("demo5");                                   // what the menu itself suggests
    await settle(async () => !(await state()).input, "the answer to be taken");
    let steps = 0;
    for (let i = 0; i < 400; i++) {
      const st = await state();
      if (st.input) break;
      if (st.pause) { steps++; await p.keyboard.press("Enter"); await p.waitForTimeout(60); }
      else await p.waitForTimeout(50);
    }
    seen = (await term()).slice(mark);
    for (const t of ["Problem 5.1", "Problem 5.2", "Problem 5.3", "Problem 5.4"])
      if (!seen.includes(t)) fail(t + " is missing from the terminal:\n" + seen.slice(-1500));
    if (steps < 3) fail("only " + steps + " pauses waited for Enter");
    if ((seen.match(/demo5\s+5\. Matrices/g) || []).length !== 2) fail("the menu did not come back after the section");
    // a command typed while demo() waits at its prompt is an answer, not a command;
    // q ends the tour, and then commands run again
    await type("q");
    await settle(async () => !(await state()).input, "demo() to end");
    await type("40 + 2");
    await settle(async () => (await term()).trim().split("\n").pop() === "42", "a command after the tour");

    // --- the stepping tour: demo5, then a bare Enter for each next problem ---
    mark = (await term()).length;
    await type("demo5");
    await settle(async () => (await term()).slice(mark).includes("Problem 5.1"), "demo5");
    for (let k = 2; k <= 4; k++) {
      await p.press(".inputline input", "Enter");
      await settle(async () => (await term()).slice(mark).includes("Problem 5." + k), "Problem 5." + k + " on Enter");
    }

    // --- input() asked in the terminal; a command typed meanwhile waits its turn ---
    mark = (await term()).length;
    await type('let who_are_you = input("name? "); "hello, " + who_are_you');
    await settle(async () => (await state()).input === 1, "input()");
    if (!(await term()).slice(mark).trim().endsWith("name?")) fail("the input() prompt is not in the terminal");
    await type("Ada");
    await settle(async () => (await term()).includes('"hello, Ada"'), "the answer to input()");
    mark = (await term()).length;
    await type('pause("wait"); 1');
    await settle(async () => (await state()).pause === 1, "pause()");
    await p.keyboard.press("Enter");                      // releases the pause
    await settle(async () => (await term()).trim().split("\n").pop() === "1", "the pause to release");
    await type("2 + 2");
    await settle(async () => (await term()).trim().split("\n").pop() === "4", "a command after the pause");

    if (dialogs) fail(dialogs + " modal dialog(s) appeared");
    if (leaked.length) fail(leaked.length + " line(s) of output went to the browser console");
    console.log(`playground: demo() menu, sections and prompt in the terminal (demo5 at the prompt; ${steps} pauses); ` +
                "the Enter-stepping tour; input() and pause() in the terminal");
  } finally { await browser.close(); server.close(); }
})().catch(e => fail(e.stack || e));
