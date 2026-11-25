// The playground (docs/index.html) in a real browser — Playwright + Chromium;
// skips politely without them. Since 0.1.51 the page is xterm.js running the
// native REPL (repl.c and the isocline editor, compiled to WebAssembly, in a
// Web Worker), so this types at it key by key and reads the screen through
// xterm's own buffer — the browser's counterpart of tests/run_lineedit.py,
// which types at the native REPL on a pty.
//   - the REPL: banner, evaluation, editing keys, Tab completion, multi-line
//     entries, history (up arrow, and across a reload), Ctrl-C at the prompt
//     and in a running loop, Ctrl-D ending the session and a key starting
//     another;
//   - what read the keyboard in the old page's special cases: input(),
//     pause() and demo() with its pauses, packeur, the manual's pager;
//   - the panes: Plots, Workspace (from the REPL's notice: no commands of
//     the page's own), Packages, Docs, the editor's Run;
//   - nothing lost to the browser console, no page errors.
// Run from the repository root: node tests/run_playground_page.js
let chromium;
try { chromium = require("playwright").chromium; }
catch (e) { console.log("playground: playwright not installed, skipping"); process.exit(0); }
const fs = require("fs"), path = require("path"), http = require("http");

const ROOT = process.cwd();
const fail = m => { console.error("playground FAIL:", m); process.exit(1); };
const TYPES = { ".js": "text/javascript", ".css": "text/css", ".md": "text/plain", ".svg": "image/svg+xml" };
const server = http.createServer((req, res) => {
  const f = path.join(ROOT, "docs", decodeURIComponent(req.url.split("?")[0]));
  if (!f.startsWith(path.join(ROOT, "docs")) || !fs.existsSync(f) || fs.statSync(f).isDirectory()) { res.writeHead(404); res.end(); return; }
  res.writeHead(200, { "Content-Type": TYPES[path.extname(f)] || "text/html" });
  fs.createReadStream(f).pipe(res);
});

(async () => {
  await new Promise(r => server.listen(0, "127.0.0.1", r));
  let browser;
  try { browser = await chromium.launch(); }
  catch (e) { console.log("playground: no browser for playwright, skipping"); server.close(); process.exit(0); }
  const url = `http://127.0.0.1:${server.address().port}/index.html`;
  const checks = [];
  const check = (name, ok, detail) => { if (!ok) fail(name + (detail ? ": " + detail : "")); checks.push(name); };
  try {
    const ctx = await browser.newContext({ viewport: { width: 1300, height: 900 } });
    const p = await ctx.newPage();
    const errors = [];
    p.on("pageerror", e => errors.push(e.message));
    p.on("console", m => { if (m.type() === "error") errors.push(m.text()); });
    p.on("dialog", d => { errors.push("modal dialog: " + d.message()); d.dismiss(); });
    await p.goto(url);
    const screen = () => p.evaluate(() => {
      const b = window.__term.buffer.active, out = [];
      for (let i = 0; i < b.length; i++) out.push(b.getLine(i).translateToString(true));
      return out.join("\n").replace(/\s+$/, "");
    });
    const lastLine = async () => (await screen()).split("\n").pop();
    const settle = async (pred, what, ms = 15000) => {
      const end = Date.now() + ms;
      while (Date.now() < end) { if (await pred()) return; await p.waitForTimeout(40); }
      fail("timed out waiting for " + what + "\n--- screen tail ---\n" + (await screen()).split("\n").slice(-12).join("\n"));
    };
    const atPrompt = () => settle(async () => /cozy> $/.test(await lastLine() + " ") || (await lastLine()) === "cozy>", "the prompt");
    const mark = async () => (await screen()).length;
    const since = async m => (await screen()).slice(m);
    const enter = async (s) => { await p.keyboard.type(s); await p.keyboard.press("Enter"); };
    const run = async (s, want, name) => {
      const m = await mark(); await enter(s);
      await settle(async () => (await since(m)).includes(want), name + " (wanted " + JSON.stringify(want) + ")");
      await atPrompt(); checks.push(name);
    };

    await settle(() => p.evaluate(() => !!window.__term && window.__term.buffer.active.length > 8), "the REPL to start", 90000);
    await atPrompt();
    const banner = await screen();
    check("the native banner", /a heavier numerical language, warmly held/.test(banner) && /Ctrl-D to exit/.test(banner), banner.slice(0, 900));
    await p.click("#term");
    await run("1 + 2", "\n3", "evaluate a line");
    await p.keyboard.type("2 * 3"); await p.keyboard.press("Home"); await p.keyboard.type("10 + ");
    let m = await mark(); await p.keyboard.press("Enter");
    await settle(async () => (await since(m)).includes("\n16"), "Home, then insert"); await atPrompt(); checks.push("editing keys");
    await enter("let v = [1,");
    await settle(async () => (await lastLine()).includes("...>"), "the continuation prompt");
    await run("2]", "[1, 2]", "a multi-line entry");
    m = await mark(); await p.keyboard.type("sqr"); await p.keyboard.press("Tab"); await p.keyboard.type("(81)"); await p.keyboard.press("Enter");
    await settle(async () => (await since(m)).includes("\n9"), "Tab completion"); await atPrompt(); checks.push("Tab completion");
    m = await mark(); await p.keyboard.press("ArrowUp"); await p.keyboard.press("Enter");
    await settle(async () => (await since(m)).includes("\n9"), "the up arrow"); await atPrompt(); checks.push("history: up arrow");
    // Ctrl-C at the prompt abandons the entry; in a loop it interrupts
    await enter("let zz = [1,"); await settle(async () => (await lastLine()).includes("...>"), "a continuation");
    await p.keyboard.press("Control+c"); await atPrompt();
    await run("zz", "undefined name 'zz'", "Ctrl-C at the prompt abandons the entry");
    m = await mark(); await enter("while true do 1 end"); await p.waitForTimeout(600);
    await p.keyboard.press("Control+c");
    await settle(async () => (await since(m)).includes("^C"), "Ctrl-C in a running loop"); await atPrompt();
    await run("40 + 2", "\n42", "Ctrl-C interrupts a running loop, and the session goes on");
    // input() and pause(), at the terminal
    m = await mark(); await enter('let who_are_you = input("name? ")');
    await settle(async () => (await lastLine()).includes("name?"), "input()'s question");
    await enter("Ada"); await atPrompt();
    await run("who_are_you", '"Ada"', "input() answered at the terminal");
    m = await mark(); await enter('pause("waiting"); 7');
    await settle(async () => (await lastLine()).includes("waiting"), "pause()");
    await p.keyboard.press("Enter");
    await settle(async () => (await since(m)).includes("\n7"), "pause() released by Enter"); await atPrompt(); checks.push("pause()");
    // the demo: the menu, a section, its pauses, the menu again, q
    await run('load("demo")', "demo15", "load the demo");
    m = await mark(); await enter("demo()");
    await settle(async () => (await lastLine()).includes("section (number"), "the demo's section prompt");
    check("the demo's menu is on screen", /demo5\s+5\. Matrices/.test(await since(m)));
    await enter("demo5");
    for (let k = 2; k <= 4; k++) {
      await settle(async () => (await since(m)).includes("Enter for the next problem"), "a demo pause");
      const m2 = await mark(); await p.keyboard.press("Enter");
      await settle(async () => (await since(m2)).includes("Problem 5." + k), "Problem 5." + k);
    }
    await settle(async () => (await lastLine()).includes("section (number"), "the menu again");
    await enter("q"); await atPrompt();
    const shown = await since(m);
    check("demo(): every problem of section 5 on screen", ["5.1", "5.2", "5.3", "5.4"].every(t => shown.includes("Problem " + t)));
    // the manual, in the built-in pager
    m = await mark(); await enter("manual");
    await settle(async () => (await since(m)).includes("-- more --"), "the manual's pager");
    check("the manual is rendered", /Getting started/.test(await since(m)));
    await p.keyboard.press("q"); await atPrompt(); checks.push("the pager quits on q");
    // packeur in the browser: Esc cancels
    m = await mark(); await enter("packeur");
    await settle(async () => (await screen()).includes("space toggles, Enter applies"), "packeur's screen");
    await p.keyboard.press("q");
    await settle(async () => (await screen()).includes("packeur: cancelled"), "packeur cancelled"); await atPrompt(); checks.push("packeur");
    // panes: plots, workspace, packages, docs, the editor
    await run("plot(1:10, (1:10) .^ 2)", "plot written", "plot");
    await settle(async () => (await p.$$eval("#plots svg", s => s.length)) > 0, "the Plots pane");
    await p.click("#tab-ws");
    await settle(async () => (await p.textContent("#ws-pre")).includes("who_are_you"), "the Workspace pane follows the session");
    await p.click("#tab-pkgs");
    await settle(async () => (await p.$$("#pkgs input")).length > 10, "the Packages pane");
    m = await mark(); await p.click("#pkgs label:has-text('packages/dist.cz') input");
    await settle(async () => (await since(m)).includes('load("packages/dist.cz")'), "a ticked package typed at the prompt"); await atPrompt();
    await settle(async () => p.$eval("#pkgs label:has-text('packages/dist.cz') input", c => c.checked), "the tick following the workspace");
    await p.click("#tab-docs");
    await settle(async () => (await p.textContent("#docview")).includes("Getting started"), "the Docs pane");
    await p.click("#tab-editor"); await p.fill("#edtext", "let cube = fn x -> x^3\ncube(4)");
    m = await mark(); await p.click("#ed-run");
    await settle(async () => (await since(m)).includes("\n64"), "the editor's Run"); await atPrompt(); checks.push("the editor's Run types the buffer");
    // Ctrl-D ends the session; a key starts another, which remembers the history
    await p.click("#term");
    m = await mark(); await p.keyboard.press("Control+d");
    await settle(async () => (await since(m)).includes("session ended"), "Ctrl-D ends the session");
    await p.keyboard.press("x");
    await settle(async () => (await screen()).split("a heavier numerical language, warmly held").length > 2, "a new session");
    await atPrompt(); checks.push("Ctrl-D, then a key, a new session");
    // the history survives a reload
    await p.reload();
    await settle(() => p.evaluate(() => !!window.__term && window.__term.buffer.active.length > 8), "the REPL after a reload", 90000);
    await atPrompt(); await p.click("#term");
    m = await mark(); await p.keyboard.press("ArrowUp");
    await settle(async () => (await lastLine()).includes("cube(4)"), "the history after a reload"); checks.push("history across a reload");
    if (errors.length) fail("page errors: " + errors.join("; "));
    console.log(`playground: the native REPL in xterm.js — ${checks.length} checks (editing, completion, history, ` +
                "Ctrl-C at the prompt and in a loop, input, pause, demo, pager, packeur, panes, Ctrl-D, reload)");
  } finally { await browser.close(); server.close(); }
})().catch(e => fail(e.stack || e));
