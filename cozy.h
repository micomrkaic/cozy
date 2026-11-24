/* cozy.h — Cozy as a library: one interpreter session inside a C program.
 *
 * The program hands Cozy source as text and reads results back as text or,
 * for numbers, directly as doubles. A complete use:
 *
 *     #include "cozy.h"
 *     cozy_open();
 *     cozy_eval("load(\"dsge\")");
 *     cozy_eval("let s = dsge.solve(dsge.model(dsge.nk())); let P = s.P;");
 *     if (!cozy_ok()) { fputs(cozy_output(), stderr); return 1; }
 *     int rows, cols;
 *     const double *P = cozy_matrix("P", &rows, &cols);     // row-major
 *
 * Build against it with `make lib` (build/libcozy.a) and link -lm. There is
 * one session per process, and it is not thread-safe: call from one thread.
 * A package can also be carried inside the program: cozy_eval() accepts a
 * whole file's text, so load() and the packages directory are optional. */
#ifndef COZY_H
#define COZY_H
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Start the session (builtins loaded, nothing else). Call once, first. */
void cozy_open(void);

/* Run source — a line, several statements, or a whole file's text — in the
 * session; let-bindings persist. Returns what the source printed, errors
 * included, exactly as the REPL would show it (empty if it printed nothing).
 * The text belongs to the library and is valid until the next cozy_eval. */
const char *cozy_eval(const char *source);

/* Did the last cozy_eval finish without a parse or runtime error? */
bool cozy_ok(void);

/* The text the last cozy_eval returned. */
const char *cozy_output(void);

/* Read the global `name` as numbers. A numeric array gives its elements in
 * row-major order with *rows and *cols set; a numeric scalar gives one
 * element, 1 x 1. Returns NULL (and 0 x 0) if there is no such global or it
 * is not real-valued numeric. The buffer belongs to the library and is
 * valid until the next cozy_matrix call for the SAME slot — four reads may
 * be held at once, in rotation — so copy what must outlive that. */
const double *cozy_matrix(const char *name, int *rows, int *cols);

/* Read the global `name` as one number. False if absent or not a real scalar. */
bool cozy_scalar(const char *name, double *value);

/* Read the global `name` as a string; NULL if absent or not a string. Valid
 * until the next cozy_eval that rebinds or clears the name. */
const char *cozy_string(const char *name);

/* The interpreter's version, e.g. "0.1.38". */
const char *cozy_version(void);

#ifdef __cplusplus
}
#endif
#endif
