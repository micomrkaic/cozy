# isocline in Cozy

The line editor of the native REPL (and, from the browser stage on, of the
WebAssembly one): [isocline](https://github.com/daanx/isocline) by Daan
Leijen, MIT licence (LICENSE here), vendored at commit 8d6dc1e (April 2026).
It replaced GNU readline in 0.1.50 so that one editor, compiled into every
build, behaves the same everywhere.

Built as one translation unit (`src/isocline.c` includes the rest) with
`-Wno-unused-function` and without `-Werror`: it is not our code.

## Local changes (grep COZY)

- `ic_last_cancelled()`: whether the last `ic_readline` ended with Ctrl-C.
  Upstream returns an empty string for both Ctrl-C and an empty Enter; the
  REPL needs the difference (Ctrl-C abandons a multi-line entry, an empty
  Enter continues it). `src/env.h` (the flag), `src/editline.c` (set on
  Ctrl-C/Ctrl-G), `src/isocline.c` and `include/isocline.h` (reset and
  query).

- A line feed (Ctrl-J, `\n`) is Enter when multi-line editing is off, as
  in readline; upstream ignores it then. Some terminals and pasted text
  send it, and so do scripts that drive the REPL. `src/editline.c`.

- Raw mode is entered and left with `TCSADRAIN`, not `TCSAFLUSH`
  (`src/tty.c`, `tty_start_raw` / `tty_end_raw`). The flush discarded
  whatever was typed but not yet read: a command typed ahead while a
  computation ran was lost when the prompt came back (readline keeps it),
  and a Ctrl-C typed just after Enter could vanish as the editor let go of
  the terminal — which is how the macOS CI lost one (0.1.50).

- Keys typed while isocline waits for a terminal's reply to a query are
  kept, not eaten (`src/tty.c`, `tty_read_esc_response`), and the macOS-only
  query for the 16-color palette is skipped (`src/term.c`): at the first
  prompt it waited up to 400 ms for an answer and swallowed what was typed
  in that time — macOS CI's pager test lost the "m" of "manual". Cozy
  paints nothing that needs the palette.

- **The browser build** (`#if defined(__EMSCRIPTEN__)`, from 0.1.51): keys
  come from Cozy's `cozy_term_readbyte` (wasm_api.c), which suspends until
  the page delivers one; there is no termios, so raw mode is a flag and no
  signal handlers are installed; "is it a terminal" is `cozy_term_active()`;
  the size is the page's (`cozy_term_cols/rows`). `src/tty.c`, `src/term.c`.

To update: copy upstream's `src`, `include` and `LICENSE` over these, and
re-apply the changes above (each is marked with a `COZY` comment).
