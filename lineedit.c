#define _GNU_SOURCE
/* lineedit.c — the line editor behind the REPL and input(); see lineedit.h.
 *
 * isocline does the editing: Emacs keys, history with Ctrl-R search,
 * completion with a menu, brace matching, Unicode. What Cozy decides here:
 *   - completion: names (keywords and the session's globals); file names
 *     inside a double-quoted string, so load("tests/da<TAB> works;
 *   - history: whole entries, the REPL's choice — a multi-line entry is one
 *     history item, a continuation line is not one on its own; so isocline's
 *     automatic history is undone after each line and the REPL adds;
 *   - look: the prompt exactly as given (isocline would add "> " and paint
 *     it green), no hints or inline help, no color when NO_COLOR is set. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include "lineedit.h"
#include "isocline.h"
#ifdef __EMSCRIPTEN__
extern int cozy_term_active(void);   /* the browser's terminal (wasm_api.c) */
#endif

static EnvObj *g_env;           /* the live globals, for name completion */
static bool    g_tty;           /* stdin is a terminal: edit lines */

bool le_interactive(void) { return g_tty; }

/* ---- completion ------------------------------------------------------ */

/* identifier characters: ASCII letters, digits, _ — and any multi-byte
 * UTF-8 sequence, since Cozy identifiers may be Unicode */
static bool is_ident(const char *s, long len)
{
    if (len > 1) return true;
    char c = s[0];
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_';
}

static void complete_names(ic_completion_env_t *cenv, const char *word)
{
    static const char *kw[] = { "let", "fn", "if", "then", "else", "elseif", "end", "for", "while",
                                "do", "in", "break", "continue", "return", "true", "false", "null" };
    size_t wl = strlen(word);
    for (size_t i = 0; i < sizeof kw / sizeof *kw; i++)
        if (strncmp(kw[i], word, wl) == 0 && !ic_add_completion(cenv, kw[i])) return;
    if (!g_env) return;
    char buf[512];
    for (uint32_t i = 0; i < g_env->count; i++) {
        uint32_t nl = g_env->namelens[i];
        if (nl < wl || nl >= sizeof buf || strncmp(g_env->names[i], word, wl) != 0) continue;
        /* a name may be bound twice (the library's and the user's): offer it once */
        bool dup = false;
        for (uint32_t j = i + 1; j < g_env->count && !dup; j++)
            dup = g_env->namelens[j] == nl && memcmp(g_env->names[j], g_env->names[i], nl) == 0;
        if (dup) continue;
        memcpy(buf, g_env->names[i], nl); buf[nl] = '\0';
        if (!ic_add_completion(cenv, buf)) return;
    }
}

static void completer(ic_completion_env_t *cenv, const char *prefix)
{
    /* inside a string — an odd number of unescaped quotes before the
     * cursor — complete file names; otherwise names */
    int quotes = 0;
    for (const char *p = prefix; *p; p++) {
        if (*p == '\\' && p[1]) { p++; continue; }
        if (*p == '"') quotes++;
    }
    if (quotes % 2 == 1) {
        const char *start = strrchr(prefix, '"') + 1;   /* the path typed so far */
        ic_complete_filename(cenv, start, '/', ".", NULL);
        return;
    }
    ic_complete_word(cenv, prefix, &complete_names, &is_ident);
}

/* ---- setup ----------------------------------------------------------- */

void le_init(const char *histpath, EnvObj *globals)
{
    g_env = globals;
#ifdef __EMSCRIPTEN__
    g_tty = cozy_term_active() != 0;            /* the page's xterm.js */
#else
    g_tty = isatty(fileno(stdin)) != 0;
#endif
    if (!g_tty) return;                         /* pipes never touch the editor */
    ic_set_prompt_marker("", "");               /* our prompts are complete */
    ic_style_def("ic-prompt", "");              /* and plain, as before */
    ic_enable_multiline(false);                 /* the parser decides continuation */
    ic_enable_hint(false);
    ic_enable_inline_help(false);
    ic_enable_brace_insertion(false);
    ic_enable_brace_matching(true);
    if (getenv("NO_COLOR")) ic_enable_color(false);
    ic_set_default_completer(&completer, nullptr);
    if (histpath) ic_set_history(histpath, 1000);
}

/* ---- reading --------------------------------------------------------- */

static char *read_plain(const char *prompt)
{
    fputs(prompt, stdout); fflush(stdout);
    char *buf = nullptr; size_t cap = 0;
    ssize_t n = getline(&buf, &cap, stdin);
    if (n < 0) { free(buf); return nullptr; }
    if (n && buf[n - 1] == '\n') buf[--n] = '\0';
    if (n && buf[n - 1] == '\r') buf[--n] = '\0';
    return buf;
}

/* isocline prints a prompt as markup ("[b]bold[/]"); Cozy's prompts —
 * input()'s question, pause()'s message, the demo's "[ Enter for the next
 * problem ]" — are plain text, so [ and \ are escaped */
static char *plain_prompt(const char *prompt)
{
    size_t n = strlen(prompt), k = 0;
    char *e = malloc(2 * n + 1);
    if (!e) return nullptr;
    for (size_t i = 0; i < n; i++) {
        if (prompt[i] == '[' || prompt[i] == '\\') e[k++] = '\\';
        e[k++] = prompt[i];
    }
    e[k] = '\0';
    return e;
}

/* one edited line; isocline's own history entry for it removed */
static char *read_edited(const char *prompt, bool *cancelled)
{
    fflush(stdout); fflush(stderr);
    char *pe = plain_prompt(prompt);
    char *r = ic_readline(pe ? pe : prompt);
    free(pe);
    if (cancelled) *cancelled = ic_last_cancelled();
    if (!r) return nullptr;
    if (strlen(r) > 1) ic_history_remove_last();  /* isocline keeps only entries of 2+ bytes */
    char *out = strdup(r);
    free(r);                                     /* isocline allocates with malloc */
    return out;
}

char *le_readline(const char *prompt, bool *cancelled)
{
    if (cancelled) *cancelled = false;
    return g_tty ? read_edited(prompt, cancelled) : read_plain(prompt);
}

char *le_readline_nohist(const char *prompt)
{
    if (!g_tty) {                                /* input() from a pipe: no prompt echo
                                                    beyond what it printed before */
        fputs(prompt, stdout); fflush(stdout);
        char *buf = nullptr; size_t cap = 0;
        ssize_t n = getline(&buf, &cap, stdin);
        if (n < 0) { free(buf); return strdup(""); }
        while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = '\0';
        return buf;
    }
    bool c = false;
    char *r = read_edited(prompt, &c);
    if (c) { free(r); return nullptr; }          /* Ctrl-C: interrupt */
    return r ? r : strdup("");
}

bool le_pause(const char *msg)
{
    if (!g_tty) {                                /* a pipe: wait for a line */
        fputs(msg, stdout); fflush(stdout);
        int ch;
        while ((ch = getchar()) != EOF && ch != '\n') { }
        return true;
    }
    bool c = false;
    char *r = read_edited(msg, &c);
    free(r);
    return !c;
}

void le_history_add(const char *entry)
{
    if (!g_tty || !entry) return;
    size_t n = strlen(entry);
    while (n && (entry[n - 1] == '\n' || entry[n - 1] == '\r')) n--;
    if (n == 0) return;
    char *e = strndup(entry, n);
    if (e) { ic_history_add(e); free(e); }
}
