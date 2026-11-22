#define _POSIX_C_SOURCE 200809L
/* embed.c — Cozy as a library (see cozy.h): a persistent session driven by
 * strings, for C programs that want the language as their numerical engine.
 * The native twin of wasm_api.c, which serves the browser page: the same
 * loop as vmtest.c, output captured into a buffer the caller reads. What it
 * adds is the way back for numbers — cozy_matrix() hands a global's
 * elements over as doubles, so a host never parses printed digits. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "arena.h"
#include "parser.h"
#include "value.h"
#include "eval.h"
#include "vm.h"
#include "version.h"
#include "cozy.h"

/* vmtest.c's Keep, once more: accepted (arena, source) pairs stay alive for
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
static char   *g_out;          /* what the last eval printed; owned here */
static size_t  g_out_len;
static bool    g_ok = true;

#ifdef __EMSCRIPTEN__
/* eval.c's pause() calls this in a wasm build; an embedded session has no
 * page to wait on, so it returns at once (as wasm_test_stub.c does). */
void nu_wasm_pause(const char *msg, uint32_t mlen) { (void)msg; (void)mlen; }
#endif

void cozy_open(void)
{
    if (globals) return;                 /* one session; opening twice is harmless */
    interp_init(&I);
    globals = globals_new();
}

const char *cozy_version(void) { return COZY_VERSION; }
bool cozy_ok(void) { return g_ok; }
const char *cozy_output(void) { return g_out ? g_out : ""; }

const char *cozy_eval(const char *source)
{
    if (!globals) cozy_open();
    free(g_out); g_out = NULL; g_out_len = 0; g_ok = true;
    FILE *cap = open_memstream(&g_out, &g_out_len);
    if (!cap) { g_ok = false; return "internal: out of memory\n"; }
    value_set_out(cap);

    char *src = strdup(source ? source : "");
    Arena *a = arena_new();
    Parser p;
    parser_init(&p, src, a);
    AstNode *prog = parser_parse(&p);
    if (p.had_error) {
        fprintf(cap, "  parse error at %u:%u: %s\n", p.err_tok.line, p.err_tok.col, p.err_msg);
        g_ok = false;
        arena_free(a); free(src);
    } else {
        Value r = vm_eval_program(&I, prog, globals, /*echo=*/true);
        if (I.had_error) {
            fprintf(cap, "  error at %u:%u: %s\n", I.cur_line, I.cur_col, I.err);
            g_ok = false;
        }
        value_release(r);
        if (I.line_borrows_src) keep_push(&keep, a, src);
        else { arena_free(a); free(src); }
    }
    fclose(cap);                         /* finalizes g_out */
    value_set_out(NULL);
    return g_out ? g_out : "";
}

/* ---- the way back for numbers ---------------------------------------- */
#define SLOTS 4                          /* results a host may hold at once */
static double *slot[SLOTS];
static size_t  slot_cap[SLOTS];
static int     slot_next;

const double *cozy_matrix(const char *name, int *rows, int *cols)
{
    int r = 0, c = 0;
    const double *out = NULL;
    Value v;
    if (globals && name && env_lookup(globals, name, (uint32_t)strlen(name), &v)) {
        size_t n = 0;
        if (v.kind == VAL_INT || v.kind == VAL_FLOAT || v.kind == VAL_BOOL) { r = c = 1; n = 1; }
        else if (v.kind == VAL_ARRAY) {
            ArrObj *arr = as_arr(v);
            if (arr->elt == ELT_INT || arr->elt == ELT_FLOAT || arr->elt == ELT_BOOL) {
                r = (int)arr->rows; c = (int)arr->cols; n = (size_t)arr->rows * arr->cols;
            }
        }
        if (r && c) {
            int k = slot_next; slot_next = (slot_next + 1) % SLOTS;
            if (n > slot_cap[k]) {
                double *grown = realloc(slot[k], n * sizeof(double));
                if (!grown) { value_release(v); if (rows) *rows = 0; if (cols) *cols = 0; return NULL; }
                slot[k] = grown; slot_cap[k] = n;
            }
            double *d = slot[k];
            if (v.kind == VAL_INT) d[0] = (double)v.as.i;
            else if (v.kind == VAL_FLOAT) d[0] = v.as.f;
            else if (v.kind == VAL_BOOL) d[0] = v.as.b ? 1.0 : 0.0;
            else {
                ArrObj *arr = as_arr(v);
                for (size_t i = 0; i < n; i++)
                    d[i] = arr->elt == ELT_FLOAT ? ((double *)arr->data)[i]
                         : arr->elt == ELT_INT   ? (double)((int64_t *)arr->data)[i]
                         :                         (((const unsigned char *)arr->data)[i] ? 1.0 : 0.0);
            }
            out = d;
        }
        value_release(v);
    }
    if (rows) *rows = r;
    if (cols) *cols = c;
    return out;
}

bool cozy_scalar(const char *name, double *value)
{
    Value v;
    if (!globals || !name || !env_lookup(globals, name, (uint32_t)strlen(name), &v)) return false;
    bool ok = true;
    if (v.kind == VAL_INT) *value = (double)v.as.i;
    else if (v.kind == VAL_FLOAT) *value = v.as.f;
    else if (v.kind == VAL_BOOL) *value = v.as.b ? 1.0 : 0.0;
    else ok = false;
    value_release(v);
    return ok;
}

const char *cozy_string(const char *name)
{
    Value v;
    if (!globals || !name || !env_lookup(globals, name, (uint32_t)strlen(name), &v)) return NULL;
    const char *s = v.kind == VAL_STRING ? as_str(v)->data : NULL;
    value_release(v);                    /* the global still holds its own reference */
    return s;
}
