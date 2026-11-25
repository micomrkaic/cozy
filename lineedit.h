/* lineedit.h — the line editor behind the REPL and input() (0.1.50).
 *
 * One editor for every build: isocline (third_party/isocline), so native
 * and, from the browser stage on, WebAssembly sessions edit, complete and
 * remember history the same way. When stdin is not a terminal (a pipe, a
 * file, the test suite) there is no editing: the prompt is printed and a
 * line read plainly, as before. */
#ifndef COZY_LINEEDIT_H
#define COZY_LINEEDIT_H
#include <stdbool.h>
#include "value.h"

/* Set up the editor: history file (may be NULL), the global frame whose
 * names complete. Call once, before le_readline. */
void  le_init(const char *histpath, EnvObj *globals);

/* Read one line with prompt. Returns a malloc'd string without the newline
 * (caller frees), or NULL at end of input (Ctrl-D on an empty line). When
 * the line was abandoned with Ctrl-C, returns "" and sets *cancelled. */
char *le_readline(const char *prompt, bool *cancelled);

/* input(): the same editing, but the answer is not kept in the history.
 * Returns null when the question was abandoned with Ctrl-C (the caller
 * interrupts), "" at end of input. */
char *le_readline_nohist(const char *prompt);

/* pause(): show msg, wait for Enter. False when Ctrl-C was pressed instead. */
bool  le_pause(const char *msg);

/* Remember an entry (a whole multi-line entry, or a command). */
void  le_history_add(const char *entry);

/* Is the session interactive (stdin a terminal, so lines are edited)? */
bool  le_interactive(void);

#endif
