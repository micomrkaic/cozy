# docs/ — the browser playground (GitHub Pages)

This folder is served by GitHub Pages at
`https://micomrkaic.github.io/cozy/`.

- `index.html` — the terminal page (custom minimal REPL, brand palette).
- `cozy.js` — the whole interpreter as a single self-contained file: the
  WebAssembly binary is embedded as base64 (`-sSINGLE_FILE=1`), so there is no
  separate `.wasm` to fetch and no MIME/CORS/path concerns. The three books
  and all twelve packages ride inside it.
- `logo.png` — the hearth (also the REPL banner).
- `.nojekyll` — tells Pages to serve the files as-is (skip Jekyll).

## Enabling Pages (one time)

Repo -> Settings -> Pages -> Build and deployment ->
Source: "Deploy from a branch" -> Branch: `main`, folder: `/docs` -> Save.
Live in a minute or two. (The default root folder renders README.md as the
site — the /docs folder is where index.html lives; this bit us at 0.0.20.)

## The workbench (native engine)

`cozy --workbench` serves this same page at http://localhost:8765 with
evals routed through the NATIVE interpreter — Accelerate/OpenBLAS speed,
your real filesystem and workspace. The page detects the local engine and
says so in the terminal; without it (GitHub Pages, or double-clicking
index.html), the embedded wasm engine answers instead. Panes: Plots,
Editor, Docs, Workspace (live who), Packages (one-click loads). Scope
fence: the server binds 127.0.0.1 only — a loopback tool, not a network
service.

## Rebuilding cozy.js after interpreter changes

With a current emsdk (`emcc` on PATH), from the repo root: `make wasm`.
On stock Ubuntu, `make wasm-ubuntu` carries the distro-emscripten shims
(full recipe in PLAYBOOK.md). Commit and push; Pages redeploys docs/
automatically.
