#define _POSIX_C_SOURCE 200809L
/* wasm_api.c — Cozy in the browser: a persistent session exposed as
 * string-in / string-out calls. Mirrors vmtest.c's loop, but output (echo,
 * print, help, errors) is captured into a buffer returned to JavaScript. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <emscripten/emscripten.h>
#include "arena.h"
#include "parser.h"
#include "value.h"
#include "eval.h"
#include "vm.h"
#include "version.h"

/* vmtest.c's Keep, duplicated: accepted (arena, source) pairs stay alive for
 * the session because identifiers point into their source text. */
typedef struct { Arena **arenas; char **srcs; size_t len, cap; } Keep;
static void keep_push(Keep *k, Arena *a, char *s)
{
    if (k->len == k->cap) {
        k->cap = k->cap ? k->cap * 2 : 16;
        k->arenas = realloc(k->arenas, k->cap * sizeof *k->arenas);
        k->srcs   = realloc(k->srcs,   k->cap * sizeof *k->srcs);
        if (!k->arenas || !k->srcs) abort();
    }
    k->arenas[k->len] = a; k->srcs[k->len] = s; k->len++;
}

static Interp  I;
static EnvObj *globals;
static Keep    keep;
static char   *g_buf;      /* last result, owned here, freed on next call */
static size_t  g_cap_len;  /* memstream size, updated on fflush */
static FILE   *g_cap;      /* live capture stream during cozy_eval (pause streams from it) */
static size_t  g_streamed; /* bytes already streamed to the page mid-eval */

/* Real pause in the browser (called from bi_pause under __EMSCRIPTEN__).
 * Output accumulates in the memstream and normally reaches the page only
 * when cozy_eval returns — so a blocking pause must first flush and stream
 * the pending text (or the user waits at a blank screen, the v2.25.2
 * lesson), then yield to the event loop via Asyncify until the page's
 * Enter latch fires. Stale pages without the latch skip the wait. */
void nu_wasm_pause(const char *msg, uint32_t mlen)
{
    if (!g_cap) return;
    fwrite(msg, 1, mlen, g_cap); fputc('\n', g_cap);
    fflush(g_cap);             /* POSIX: buffer and size now current */
    EM_ASM({
        var t = UTF8ToString($0);
        if (globalThis.__cozyStream) { globalThis.__cozyStream(t);
                                 globalThis.__cozyPauseWaiting = 1; globalThis.__cozyPauseDone = 0; }
        else                   { globalThis.__cozyPauseDone = 1; }
    }, g_buf + g_streamed);
    g_streamed = g_cap_len;
    while (!EM_ASM_INT({ return (globalThis.__cozyPauseDone | 0); }))
        emscripten_sleep(60);
    EM_ASM({ globalThis.__cozyPauseWaiting = 0; });
}

/* Numbers in and out without text (the nowcasting page's data path,
 * 0.1.47): bind the global `name` to a rows x cols Float matrix copied from
 * row-major doubles, and read a real numeric global back as doubles. A
 * dataset of 200 series x 360 months is 72,000 numbers — as source text,
 * a megabyte to format and parse. */
EMSCRIPTEN_KEEPALIVE
int cozy_set_matrix(const char *name, const double *data, int rows, int cols)
{
    if (!name || rows < 0 || cols < 0) return -1;
    Value v = val_array(ELT_FLOAT, (uint32_t)rows, (uint32_t)cols);
    size_t cnt = (size_t)rows * (size_t)cols;
    if (cnt) memcpy(as_arr(v)->data, data, cnt * sizeof(double));
    uint32_t len = (uint32_t)strlen(name);
    if (!env_assign(globals, name, len, v)) env_define(globals, name, len, v);
    value_release(v);
    return 0;
}

static double *g_mat;      /* cozy_get_matrix's buffer, valid until the next call */
static int     g_dims[2];
/* The global `name` as row-major doubles (Int and Bool convert; a real
 * scalar is 1 x 1), with its shape at cozy_get_dims(); NULL if absent or
 * not real numeric. */
EMSCRIPTEN_KEEPALIVE
const double *cozy_get_matrix(const char *name)
{
    free(g_mat); g_mat = NULL; g_dims[0] = g_dims[1] = 0;
    Value v;
    if (!name || !env_lookup(globals, name, (uint32_t)strlen(name), &v)) return NULL;
    if (v.kind == VAL_INT || v.kind == VAL_FLOAT || v.kind == VAL_BOOL) {
        g_mat = malloc(sizeof *g_mat);
        g_mat[0] = v.kind == VAL_INT ? (double)v.as.i : v.kind == VAL_FLOAT ? v.as.f : (v.as.b ? 1.0 : 0.0);
        g_dims[0] = g_dims[1] = 1;
    } else if (v.kind == VAL_ARRAY) {
        ArrObj *a = as_arr(v);
        if (a->elt == ELT_FLOAT || a->elt == ELT_INT || a->elt == ELT_BOOL) {
            size_t cnt = (size_t)a->rows * a->cols;
            g_mat = malloc((cnt ? cnt : 1) * sizeof *g_mat);
            for (size_t k = 0; k < cnt; k++) {
                Value e = arr_get(a, k);
                g_mat[k] = e.kind == VAL_INT ? (double)e.as.i : e.kind == VAL_FLOAT ? e.as.f : (e.as.b ? 1.0 : 0.0);
            }
            g_dims[0] = (int)a->rows; g_dims[1] = (int)a->cols;
        }
    }
    value_release(v);
    return g_mat;
}
EMSCRIPTEN_KEEPALIVE
const int *cozy_get_dims(void) { return g_dims; }

EMSCRIPTEN_KEEPALIVE
const char *cozy_version(void) { return COZY_VERSION " (wasm, built " COZY_BUILT ")"; }

/* input() in the browser (0.1.49): the question is asked in the page's own
 * terminal, not in a modal window.prompt. The pending output — the demo's
 * menu, say — and the prompt are streamed first, so the reader sees what is
 * being asked; then, as for pause, Asyncify yields until the page delivers
 * the typed line (globalThis.__cozyInputReady / __cozyInputValue). A page
 * without the terminal hook (__cozyInputSupported unset) gets the old
 * window.prompt, and a worker without one gets "". Returns malloc'd text. */
char *nu_wasm_input(const char *prompt, uint32_t plen)
{
    int term = g_cap && EM_ASM_INT({ return (globalThis.__cozyInputSupported && globalThis.__cozyStream) ? 1 : 0; });
    if (term) {
        fwrite(prompt, 1, plen, g_cap);
        fflush(g_cap);
        EM_ASM({
            globalThis.__cozyInputReady = 0; globalThis.__cozyInputValue = "";
            globalThis.__cozyStream(UTF8ToString($0));
            globalThis.__cozyInputWaiting = 1;
        }, g_buf + g_streamed);
        g_streamed = g_cap_len;
        while (!EM_ASM_INT({ return (globalThis.__cozyInputReady | 0); }))
            emscripten_sleep(40);
    }
    char cp[256];
    snprintf(cp, sizeof cp, "%.*s", (int)plen, prompt);
    return (char *)(intptr_t)EM_ASM_INT({
        var r;
        if ($1) { r = globalThis.__cozyInputValue; globalThis.__cozyInputWaiting = 0; globalThis.__cozyInputReady = 0; }
        else r = (typeof window !== "undefined" && window.prompt) ? window.prompt(UTF8ToString($0)) : "";
        if (r === null || r === undefined) r = "";
        r = String(r);
        var len = lengthBytesUTF8(r) + 1;
        var b = _malloc(len);
        stringToUTF8(r, b, len);
        return b;
    }, cp, term);
}

static int g_running;      /* an eval is in progress (perhaps suspended at pause/input) */

EMSCRIPTEN_KEEPALIVE
void cozy_init(void)
{
    interp_init(&I);
    globals = globals_new();
    value_set_multiline(true);    /* aligned multi-line matrices, as the native REPL defaults */
}

/* REPL-level commands (handled in repl.c natively, which the wasm build
 * bypasses). Skip leading blanks; return the argument tail, or nullptr if the
 * line is not this command. Mirrors repl.c's match_command. */
static const char *wasm_command(const char *line, const char *word)
{
    while (*line == ' ' || *line == '\t') line++;
    size_t wl = strlen(word);
    if (strncmp(line, word, wl) != 0) return NULL;
    const char *after = line + wl;
    if (*after != '\0' && *after != ' ' && *after != '\t') return NULL;
    while (*after == ' ' || *after == '\t') after++;
    return after;
}

EMSCRIPTEN_KEEPALIVE
const char *cozy_eval(const char *line)
{
    /* A page that calls again while an eval waits at pause or input would
     * tear down that eval's output stream (0.1.49: the demo's sections then
     * went to the browser console and its pauses stopped waiting). Refuse;
     * pages call with ccall(..., {async: true}) and wait their turn. */
    if (g_running) return "  busy: the previous command is still running (waiting for Enter or input)\n";
    g_running = 1;
    free(g_buf); g_buf = NULL;
    g_cap_len = 0; g_streamed = 0;
    FILE *cap = open_memstream(&g_buf, &g_cap_len);
    g_cap = cap;
    if (!cap) { g_running = 0; return "internal: out of memory\n"; }
    value_set_out(cap);

    const char *arg;
    if ((arg = wasm_command(line ? line : "", "pretty"))) {   /* REPL command, not a builtin */
        if (!strcmp(arg, "on") || !strcmp(arg, "1"))       value_set_multiline(true);
        else if (!strcmp(arg, "off") || !strcmp(arg, "0")) value_set_multiline(false);
        else if (*arg == '\0') fprintf(cap, "pretty is %s\n", value_multiline() ? "on" : "off");
        else fprintf(cap, "usage: pretty on | off\n");
        fclose(cap); g_cap = NULL;
        value_set_out(NULL);
        g_running = 0;
        return g_buf ? g_buf : "";
    }

    char *src = strdup(line ? line : "");
    Arena *a = arena_new();
    Parser p;
    parser_init(&p, src, a);
    AstNode *prog = parser_parse(&p);
    if (p.had_error) {
        fprintf(cap, "  parse error at %u:%u: %s\n", p.err_tok.line, p.err_tok.col, p.err_msg);
        arena_free(a); free(src);
    } else {
        Value r = vm_eval_program(&I, prog, globals, /*echo=*/true);
        if (I.had_error)
            fprintf(cap, "  error at %u:%u: %s\n", I.cur_line, I.cur_col, I.err);
        value_release(r);
        if (I.line_borrows_src) keep_push(&keep, a, src);
        else { arena_free(a); free(src); }
    }
    fclose(cap); g_cap = NULL; /* finalizes g_buf */
    value_set_out(NULL);
    g_running = 0;
    return (g_buf ? g_buf + g_streamed : "");   /* page gets the unstreamed tail */
}


/* ==================================================================== */
/* The browser terminal (0.1.51): the native REPL — repl.c, the isocline */
/* editor, the same commands — run in a Web Worker on the page's xterm.js. */
/* ==================================================================== */
/* The page posts keys to the worker, which appends their bytes to
 * globalThis.__cozyKeys; output leaves through the Emscripten FS stdout and
 * stderr devices (the page's worker sets them up with FS.init) and is handed
 * to the page by globalThis.__cozyFlush. Reading a key suspends (Asyncify)
 * until one arrives — the pause() mechanism, now the only way in. */
#include "repl.h"

static int g_term;                 /* the REPL is attached to the page's terminal */

EMSCRIPTEN_KEEPALIVE int cozy_term_active(void) { return g_term; }

int cozy_term_cols(void)
{
    int c = EM_ASM_INT({ return globalThis.__cozyCols | 0; });
    return c > 0 ? c : 80;
}
int cozy_term_rows(void)
{
    int r = EM_ASM_INT({ return globalThis.__cozyRows | 0; });
    return r > 0 ? r : 24;
}

static void term_flush(void)
{
    fflush(stdout); fflush(stderr);
    EM_ASM({ if (globalThis.__cozyFlush) globalThis.__cozyFlush(); });
}

/* a byte the page sent, waiting for one when there is none: timeout_ms < 0
 * waits for good, 0 only looks, > 0 waits that long (escape sequences) */
int cozy_term_readbyte(long timeout_ms)
{
    double start = emscripten_get_now();
    term_flush();
    EM_ASM({ globalThis.__cozyReading = 1; });
    int b = -1;
    for (;;) {
        b = EM_ASM_INT({ var q = globalThis.__cozyKeys; return (q && q.length) ? q.shift() : -1; });
        if (b >= 0 || timeout_ms == 0) break;
        double waited = emscripten_get_now() - start;
        if (timeout_ms > 0 && waited >= (double)timeout_ms) break;
        emscripten_sleep(timeout_ms > 0 && timeout_ms < 10 ? (unsigned)timeout_ms : 10);
    }
    EM_ASM({ globalThis.__cozyReading = 0; });
    return b;
}

/* Ctrl-C while something runs: the VM asks here (vm.c); about every 40 ms
 * this yields to the worker's event loop, which may have set the flag */
static int interrupt_poll(void)
{
    static double last;
    double now = emscripten_get_now();
    if (now - last < 40.0) return 0;
    last = now;
    term_flush();
    emscripten_sleep(0);
    return EM_ASM_INT({ var f = globalThis.__cozyInterrupt | 0; globalThis.__cozyInterrupt = 0; return f; });
}

/* before each new prompt: the workspace, for the page's side panes */
static void prompt_notice(Interp *I)
{
    extern void cozy_who_text(Interp *, FILE *);
    char *buf = NULL; size_t sz = 0;
    FILE *m = open_memstream(&buf, &sz);
    if (m) { cozy_who_text(I, m); fclose(m); }
    term_flush();
    EM_ASM({ if (globalThis.__cozyNotice) globalThis.__cozyNotice(UTF8ToString($0)); }, buf ? buf : "");
    free(buf);
}

/* the REPL itself; returns when the session ends (Ctrl-D). Call with
 * ccall(..., {async: true}): it suspends while it waits for keys. */
EMSCRIPTEN_KEEPALIVE int cozy_repl(void)
{
    extern int (*cozy_interrupt_poll)(void);
    extern void (*repl_prompt_hook)(Interp *);
    setenv("TERM", "xterm-256color", 1);          /* what xterm.js emulates */
    g_term = 1;
    cozy_interrupt_poll = interrupt_poll;
    repl_prompt_hook = prompt_notice;
    int rc = repl_run();
    term_flush();
    cozy_interrupt_poll = NULL;
    repl_prompt_hook = NULL;
    g_term = 0;
    return rc;
}
