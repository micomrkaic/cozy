#define _GNU_SOURCE
/* repl.c — Cozy interactive REPL.
 *
 * Lifetime note: the evaluator stores environment names as non-owning slices
 * into the source and closures as raw pointers into the parse arena. So every
 * ACCEPTED entry's (arena, source) pair must outlive the session — a closure
 * defined at the prompt keeps referencing both. We therefore retain each
 * accepted entry and free the lot only at exit. Rejected/incomplete entries
 * are discarded immediately. (Aborted runtime errors leak their in-flight
 * temporaries via longjmp — bounded per error, the one rough edge here.)
 *
 * Completeness: the parser swallows newlines inside (), [], and if..end, so an
 * unterminated construct fails at EOF. Thus "error whose token is EOF" means
 * "incomplete, read another line"; any other error is a real syntax error. */
#include "repl.h"
#include "lexer.h"
#include "parser.h"
#include "arena.h"
#include "ast.h"
#include "value.h"
#include <time.h>
#include "version.h"
#include "linalg.h"
#include "eval.h"
#include "vm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <setjmp.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <termios.h>

#include "lineedit.h"                       /* isocline: editing, history, completion */
#ifdef __EMSCRIPTEN__
extern int cozy_term_readbyte(long timeout_ms);   /* the browser's terminal (wasm_api.c) */
extern int cozy_term_rows(void);
#endif

/* Called before each new prompt (not a continuation): the browser REPL
 * sends the page its workspace from here, so the side panes follow the
 * session without running commands of their own. Null natively. */
void (*repl_prompt_hook)(Interp *I) = nullptr;

/* The page's notice, in band: before each prompt the workspace listing
 * travels as a private OSC sequence, ESC ] 5379 ; <base64> BEL, which the
 * page's xterm.js catches (a terminal ignores an OSC it does not know).
 * The same bytes whether the REPL is native on the workbench's pty or
 * WebAssembly in the page: one handler, both engines. */
void repl_osc_notice(Interp *I)
{
    extern void cozy_who_text(Interp *, FILE *);
    char *buf = nullptr; size_t sz = 0;
    FILE *m = open_memstream(&buf, &sz);
    if (!m) return;
    cozy_who_text(I, m);
    fclose(m);
    static const char b64[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    fputs("\033]5379;", stdout);
    for (size_t i = 0; i < sz; i += 3) {
        unsigned v = (unsigned char)buf[i] << 16;
        if (i + 1 < sz) v |= (unsigned char)buf[i + 1] << 8;
        if (i + 2 < sz) v |= (unsigned char)buf[i + 2];
        putchar(b64[(v >> 18) & 63]); putchar(b64[(v >> 12) & 63]);
        putchar(i + 1 < sz ? b64[(v >> 6) & 63] : '=');
        putchar(i + 2 < sz ? b64[v & 63] : '=');
    }
    fputs("\007", stdout);
    fflush(stdout);
    free(buf);
}

/* is output a terminal? natively isatty; in the browser, the page's */
static bool out_is_tty(void)
{
#ifdef __EMSCRIPTEN__
    return le_interactive();
#else
    return isatty(fileno(stdout)) != 0;
#endif
}

/* ------------------------------------------------------------------ */
/* session retention: accepted (arena, source) pairs live until exit   */
/* ------------------------------------------------------------------ */
typedef struct { Arena **arenas; char **srcs; size_t count, cap; } Keep;

static void keep_push(Keep *k, Arena *a, char *src)
{
    if (k->count == k->cap) {
        k->cap = k->cap ? k->cap * 2 : 16;
        k->arenas = realloc(k->arenas, k->cap * sizeof *k->arenas);
        k->srcs   = realloc(k->srcs,   k->cap * sizeof *k->srcs);
        if (!k->arenas || !k->srcs) abort();
    }
    k->arenas[k->count] = a;
    k->srcs[k->count]   = src;
    k->count++;
}

static void keep_free_all(Keep *k)
{
    for (size_t i = 0; i < k->count; i++) { arena_free(k->arenas[i]); free(k->srcs[i]); }
    free(k->arenas); free(k->srcs);
}

/* line input: lineedit.c (isocline) — completion, history, editing */

static char *append_line(char *acc, size_t *len, const char *line)
{
    size_t ll = strlen(line);
    acc = realloc(acc, *len + ll + 2);
    if (!acc) abort();
    memcpy(acc + *len, line, ll);
    *len += ll;
    acc[(*len)++] = '\n';
    acc[*len] = '\0';
    return acc;
}

static bool is_blank(const char *s)
{
    for (; *s; s++) if (*s != ' ' && *s != '\t' && *s != '\n' && *s != '\r') return false;
    return true;
}

/* ------------------------------------------------------------------ */
/* diagnostics                                                         */
/* ------------------------------------------------------------------ */
static void print_diag(const char *src, uint32_t line, uint32_t col, const char *msg)
{
    fprintf(stderr, "  error at %u:%u: %s\n", line, col, msg ? msg : "error");
    const char *p = src;
    for (uint32_t ln = 1; ln < line && *p; p++) if (*p == '\n') ln++;
    const char *eol = p;
    while (*eol && *eol != '\n') eol++;
    fprintf(stderr, "    %.*s\n", (int)(eol - p), p);
    fprintf(stderr, "    %*s^\n", col ? (int)col - 1 : 0, "");
}

/* ------------------------------------------------------------------ */
/* SIGINT: cancel the current (possibly multi-line) entry, don't exit  */
/* ------------------------------------------------------------------ */
static sigjmp_buf g_sigint_jmp;
static void on_sigint(int sig) { (void)sig; siglongjmp(g_sigint_jmp, 1); }

/* ------------------------------------------------------------------ */
/* output paging ('more on' captures an entry's output and pages it)   */
/* ------------------------------------------------------------------ */
static bool g_more;              /* paging enabled? (default off) */

/* Open the user's pager for writing ($PAGER, else less). What we page is
 * colored, and less prints color escapes as literal ESC[1;36m noise unless
 * it is told -R — which a bare PAGER=less, the commonest setting there is,
 * does not say. So -R is appended to $LESS for the pager we start (the last
 * option wins; other pagers do not read the variable). Once per session. */
static FILE *open_pager(void)
{
    static bool less_told;
    if (!less_told) {
        less_told = true;
        const char *cur = getenv("LESS");
        char buf[1024];
        snprintf(buf, sizeof buf, "%s%s-R", cur ? cur : "", cur && *cur ? " " : "");
        setenv("LESS", buf, 1);
    }
    const char *pager = getenv("PAGER");
    if (!pager || !*pager) pager = "less";
    return popen(pager, "w");
}

/* A small pager for where there is no less: the browser, and the native
 * workbench (COZY_PAGER=builtin), so the page shows the same thing whichever
 * engine answers. Space or f pages, Enter or j steps a line, q (or Ctrl-C,
 * Esc) quits. */
static int pager_key(void)
{
#ifdef __EMSCRIPTEN__
    return cozy_term_readbyte(-1);
#else
    struct termios oldt, raw;
    bool tty = isatty(0) && tcgetattr(0, &oldt) == 0;
    if (tty) { raw = oldt; raw.c_lflag &= (tcflag_t)~(ICANON | ECHO); raw.c_cc[VMIN] = 1; raw.c_cc[VTIME] = 0;
               tcsetattr(0, TCSANOW, &raw); }
    unsigned char c = 0;
    ssize_t n = read(0, &c, 1);
    if (tty) tcsetattr(0, TCSANOW, &oldt);
    return n == 1 ? c : -1;
#endif
}
static int pager_rows(void)
{
#ifdef __EMSCRIPTEN__
    return cozy_term_rows();
#else
    struct winsize ws;
    return ioctl(fileno(stdout), TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0 ? ws.ws_row : 24;
#endif
}
static bool pager_builtin_wanted(void)
{
#ifdef __EMSCRIPTEN__
    return true;
#else
    const char *p = getenv("COZY_PAGER");
    return p && !strcmp(p, "builtin");
#endif
}
static void page_builtin(const char *buf, size_t sz)
{
    int rows = pager_rows();
    if (rows < 4) rows = 24;
    int page = rows - 1, shown = 0;
    size_t pos = 0;
    while (pos < sz) {
        const char *nl = memchr(buf + pos, '\n', sz - pos);
        size_t end = nl ? (size_t)(nl - buf) + 1 : sz;
        fwrite(buf + pos, 1, end - pos, stdout);
        pos = end; shown++;
        if (shown >= page && pos < sz) {
            fputs("\033[7m-- more --  space: page  Enter: line  q: quit\033[0m", stdout);
            fflush(stdout);
            int k = pager_key();
            fputs("\r\033[K", stdout);
            if (k < 0 || k == 'q' || k == 'Q' || k == 3 || k == 27) break;
            shown = (k == '\r' || k == '\n' || k == 'j') ? page - 1 : 0;
        }
    }
    fflush(stdout);
}

static void page_output(const char *buf, size_t sz)
{
    if (!buf || sz == 0) return;
    if (pager_builtin_wanted()) { page_builtin(buf, sz); return; }
    size_t lines = 0;
    for (size_t i = 0; i < sz; i++) if (buf[i] == '\n') lines++;
    int rows = 24;
    struct winsize ws;
    if (ioctl(fileno(stdout), TIOCGWINSZ, &ws) == 0 && ws.ws_row > 0) rows = ws.ws_row;
    if ((int)lines < rows) { fwrite(buf, 1, sz, stdout); return; }
    FILE *p = open_pager();
    if (!p) { fwrite(buf, 1, sz, stdout); return; }
    fwrite(buf, 1, sz, p);
    pclose(p);
}

/* If `line` is the command `word` (followed by end or whitespace, not '('),
 * return a pointer to its trimmed argument (empty string if none); else null. */
static const char *match_command(const char *line, const char *word)
{
    while (*line == ' ' || *line == '\t') line++;
    size_t wl = strlen(word);
    if (strncmp(line, word, wl) != 0) return nullptr;
    const char *after = line + wl;
    if (*after != '\0' && *after != ' ' && *after != '\t') return nullptr;  /* e.g. 'format(' */
    while (*after == ' ' || *after == '\t') after++;
    return after;
}

/* ------------------------------------------------------------------ */
static void print_banner(bool color)
{
    /* Cozy in figlet's nancyj — the Cooper Black of ASCII fonts: fat,
     * round, warm, unmistakably 1970s. One word, simply (owner's call,
     * 0.0.21). */
    if (color) {
        fputs("  \033[38;2;255;176;96m a88888b.                            \033[0m\n", stdout);
        fputs("  \033[38;2;255;176;96md8'   `88                            \033[0m\n", stdout);
        fputs("  \033[38;2;255;176;96m88        .d8888b. d888888b dP    dP \033[0m\n", stdout);
        fputs("  \033[38;2;255;176;96m88        88'  `88    .d8P' 88    88 \033[0m\n", stdout);
        fputs("  \033[38;2;255;176;96mY8.   .88 88.  .88  .Y8P    88.  .88 \033[0m\n", stdout);
        fputs("  \033[38;2;255;176;96m Y88888P' `88888P' d888888P `8888P88 \033[0m\n", stdout);
        fputs("  \033[38;2;255;176;96m                                 .88 \033[0m\n", stdout);
        fputs("  \033[38;2;255;176;96m                             d8888P  \033[0m\n", stdout);
        fputs("\n  \033[38;2;120;132;150ma heavier numerical language, warmly held\033[0m\n", stdout);
        char now[32]; time_t t = time(NULL);
        strftime(now, sizeof now, "%Y-%m-%d %H:%M:%S", localtime(&t));
        printf("  \033[38;2;120;132;150mv%s · %s backend · built %s · session %s\033[0m\n",
               COZY_VERSION, cozy_linalg()->name, COZY_BUILT, now);
    } else {
        fputs("   a88888b.                            \n", stdout);
        fputs("  d8'   `88                            \n", stdout);
        fputs("  88        .d8888b. d888888b dP    dP \n", stdout);
        fputs("  88        88'  `88    .d8P' 88    88 \n", stdout);
        fputs("  Y8.   .88 88.  .88  .Y8P    88.  .88 \n", stdout);
        fputs("   Y88888P' `88888P' d888888P `8888P88 \n", stdout);
        fputs("                                   .88 \n", stdout);
        fputs("                               d8888P  \n", stdout);
        fputs("\n  a heavier numerical language, warmly held\n", stdout);
        char now[32]; time_t t = time(NULL);
        strftime(now, sizeof now, "%Y-%m-%d %H:%M:%S", localtime(&t));
        printf("  v%s · %s backend · built %s · session %s\n",
               COZY_VERSION, cozy_linalg()->name, COZY_BUILT, now);
    }
}

/* ------------------------------------------------------------------ */
/* ---- markdown -> ANSI: a renderer for Cozy's own docs ------------- */
/* Handles the subset our documents use: #/##/### headers, ``` fences,
 * `inline code`, **bold**, bullets, --- rules, tables (passed through).
 * Colors only when writing to a terminal-bound pager. */
static void md_span(FILE *o, const char *p, bool color)
{
    while (*p) {
        if (p[0] == '\\' && p[1] == '|') {   /* markdown table escape: \| is a literal pipe */
            fputc('|', o);
            p += 2; continue;
        }
        if (*p == '[') {                       /* [text](url) -> text */
            const char *rb = strchr(p + 1, ']');
            if (rb && rb[1] == '(') {
                const char *cp = strchr(rb + 2, ')');
                if (cp) {
                    fwrite(p + 1, 1, (size_t)(rb - p - 1), o);
                    p = cp + 1; continue;
                }
            }
        }
        if (*p == '`') {
            const char *e = strchr(p + 1, '`');
            if (e) {
                if (color) fputs("\033[36m", o);
                for (const char *q = p + 1; q < e; q++) {   /* \| stays a pipe here too */
                    if (q[0] == '\\' && q + 1 < e && q[1] == '|') { fputc('|', o); q++; }
                    else fputc(*q, o);
                }
                if (color) fputs("\033[0m", o);
                p = e + 1; continue;
            }
        }
        if (p[0] == '*' && p[1] == '*') {
            const char *e = strstr(p + 2, "**");
            if (e) {
                if (color) fputs("\033[1m", o);
                fwrite(p + 2, 1, (size_t)(e - p - 2), o);
                if (color) fputs("\033[0m", o);
                p = e + 2; continue;
            }
        }
        fputc(*p++, o);
    }
}

static void md_render(FILE *in, FILE *o, bool color)
{
    char *line = nullptr; size_t cap = 0; ssize_t got;
    bool in_code = false;
    while ((got = getline(&line, &cap, in)) != -1) {
        if (got && line[got - 1] == '\n') line[got - 1] = '\0';
        if (strncmp(line, "```", 3) == 0) {
            in_code = !in_code;
            continue;                               /* drop the fence itself */
        }
        if (in_code) {
            if (color) fputs("    \033[32m", o); else fputs("    ", o);
            fputs(line, o);
            if (color) fputs("\033[0m", o);
            fputc('\n', o);
            continue;
        }
        if (line[0] == '#') {
            int n = 0; while (line[n] == '#') n++;
            const char *t = line + n; while (*t == ' ') t++;
            if (color) fputs(n == 1 ? "\033[1;34m" : "\033[1;36m", o);
            if (n >= 3) fputs("  ", o);
            fputs(t, o);
            if (color) fputs("\033[0m", o);
            fputc('\n', o);
            continue;
        }
        if (strncmp(line, "---", 3) == 0 && line[3] == '\0') {
            if (color) fputs("\033[2m", o);
            for (int i = 0; i < 64; i++) fputs("\u2500", o);
            if (color) fputs("\033[0m", o);
            fputc('\n', o);
            continue;
        }
        if (line[0] == '-' && line[1] == ' ') {
            fputs(color ? "  \033[33m\u2022\033[0m " : "  \u2022 ", o);
            md_span(o, line + 2, color);
            fputc('\n', o);
            continue;
        }
        if (line[0] == '|' && color) fputs("\033[2m", o);   /* tables: pass, dimmed pipes stay readable */
        md_span(o, line, color && line[0] != '|');
        if (line[0] == '|' && color) fputs("\033[0m", o);
        fputc('\n', o);
    }
    free(line);
}

int repl_run(void)
{
    Interp I;
    interp_init(&I);
    EnvObj *globals = globals_new();
    Keep keep = {0};
    value_set_multiline(true);                   /* aligned matrices in the interactive shell */

    char histpath[4096];
    const char *home = getenv("HOME");
    if (home) snprintf(histpath, sizeof histpath, "%s/.cozy_history", home);
    else      snprintf(histpath, sizeof histpath, ".cozy_history");

    le_init(histpath, globals);
    extern char *(*cozy_input_hook)(const char *);
    extern bool (*cozy_pause_hook)(const char *);
    if (getenv("COZY_WORKBENCH") && !repl_prompt_hook) repl_prompt_hook = repl_osc_notice;   /* serve.c's pty */
    if (le_interactive()) {                     /* input() and pause() edit like the prompt */
        cozy_input_hook = le_readline_nohist;
        cozy_pause_hook = le_pause;
    }
    if (le_interactive()) {
        print_banner(out_is_tty() && !getenv("NO_COLOR"));
        fputs("  type  help  for a tour  \u00b7  Ctrl-D to exit  \u00b7  Ctrl-C cancels  \u00b7  Ctrl-R searches history\n\n", stdout);
    }

    struct sigaction sa; memset(&sa, 0, sizeof sa);
    sa.sa_handler = on_sigint;
    sigaction(SIGINT, &sa, nullptr);

    static char  *acc;
    static size_t acclen;
    static bool   cont;
    acc = nullptr; acclen = 0; cont = false;

    for (;;) {
        if (sigsetjmp(g_sigint_jmp, 1)) {       /* landed here on Ctrl-C during evaluation */
            fputc('\n', stdout);
            free(acc); acc = nullptr; acclen = 0; cont = false;
        }

        if (!cont && repl_prompt_hook) repl_prompt_hook(&I);
        bool cancelled = false;
        char *line = le_readline(cont ? "     ...> " : "cozy> ", &cancelled);
        if (line && cancelled) {                /* Ctrl-C at the prompt: abandon the entry */
            free(line);
            free(acc); acc = nullptr; acclen = 0; cont = false;
            continue;
        }
        if (!line) {                            /* EOF / Ctrl-D */
            if (cont) {                         /* cancel partial entry, back to prompt */
                fputc('\n', stdout);
                free(acc); acc = nullptr; acclen = 0; cont = false;
                continue;
            }
            fputc('\n', stdout);
            break;
        }

        if (!cont) {                            /* '!cmd' / 'more' / 'format' commands */
            const char *q = line;
            while (*q == ' ' || *q == '\t') q++;
            const char *arg;
            if (*q == '!') {
                q++;
#ifdef __EMSCRIPTEN__
                fputs("!: there is no shell in the browser (ls, cd and pwd are builtins)\n", stderr);
#else
                if (*q) { fflush(stdout); fflush(stderr); int rc = system(q); (void)rc; }
                else fputs("usage: !<shell command>\n", stderr);
#endif
                le_history_add(line);
                free(line);
                continue;
            }
            if ((arg = match_command(q, "more"))) {
                if (!strcmp(arg, "on") || !strcmp(arg, "1"))       g_more = true;
                else if (!strcmp(arg, "off") || !strcmp(arg, "0")) g_more = false;
                else if (*arg == '\0') printf("more is %s\n", g_more ? "on" : "off");
                else fputs("usage: more on | off\n", stderr);
                le_history_add(line);
                free(line);
                continue;
            }
            if ((arg = match_command(q, "manual"))) {
                /* manual [packages|changelog|lessons|design|readme] — render
                 * the document as ANSI-formatted text and page it. Files are
                 * found in the cwd, /usr/local/share/cozy, or beside the
                 * binary (../share/cozy after make install); set
                 * COZY_MANUAL to override the main manual's path. */
                static const struct { const char *name, *file; } docs[] = {
                    { "",          "MANUAL.md" },     { "packages", "PACKAGES.md" },
                    { "book",      "BOOK.md" },
                    { "changelog", "CHANGELOG.md" },  { "lessons",  "LESSONS.md" },
                    { "design",    "DESIGN_NOTES.md" },{ "readme",   "README.md" },
                };
                const char *file = nullptr;
                while (*arg == ' ') arg++;
                for (size_t di = 0; di < sizeof docs / sizeof *docs; di++)
                    if (strcmp(arg, docs[di].name) == 0) { file = docs[di].file; break; }
                if (!file) {
                    fputs("manual: unknown document; try one of: manual, manual book, manual packages, "
                          "manual changelog, manual lessons, manual design, manual readme\n", stderr);
                    goto manual_done;
                }
                char path[1100]; FILE *mf = nullptr;
                const char *envp = getenv("COZY_MANUAL");
                if (*arg == '\0' && envp && *envp && (mf = fopen(envp, "rb"))) { /* override */ }
                if (!mf && (mf = fopen(file, "rb"))) { }
                if (!mf) {
                    snprintf(path, sizeof path, "/usr/local/share/cozy/%s", file);
                    mf = fopen(path, "rb");
                }
                if (!mf) mf = fopen_asset(file);          /* beside the binary, wherever we run */
                if (!mf) {
                    fprintf(stderr, "manual: %s not found (run from the repo root, "
                            "make install, or set COZY_MANUAL for the main manual)\n", file);
                    goto manual_done;
                }
                bool tty = out_is_tty();
                if (tty && pager_builtin_wanted()) {   /* no less: render, then the built-in pager */
                    char *mb = nullptr; size_t ms = 0;
                    FILE *mm = open_memstream(&mb, &ms);
                    if (mm) { md_render(mf, mm, true); fclose(mm); page_builtin(mb, ms); free(mb); }
                    fclose(mf);
                    goto manual_done;
                }
                if (tty) {
                    FILE *pp = open_pager();
                    if (pp) {
                        md_render(mf, pp, true);
                        pclose(pp);
                    } else {
                        md_render(mf, stdout, true);
                    }
                } else {
                    md_render(mf, stdout, false);      /* piped: formatted, no colors */
                }
                fclose(mf);
                manual_done: ;
                le_history_add(line);
                free(line);
                continue;
            }
            if ((arg = match_command(q, "pretty"))) {
                if (!strcmp(arg, "on") || !strcmp(arg, "1"))       value_set_multiline(true);
                else if (!strcmp(arg, "off") || !strcmp(arg, "0")) value_set_multiline(false);
                else if (*arg == '\0') printf("pretty is %s\n", value_multiline() ? "on" : "off");
                else fputs("usage: pretty on | off\n", stderr);
                le_history_add(line);
                free(line);
                continue;
            }
            if ((arg = match_command(q, "format"))) {
                if (*arg == '\0') printf("format: %s\n", value_format_desc());
                else if (!value_format_by_name(arg))
                    fprintf(stderr, "format: unknown mode '%s' (try short, long, short e, long e, default)\n", arg);
                le_history_add(line);
                free(line);
                continue;
            }
        }

        acc = append_line(acc, &acclen, line);
        free(line);

        if (is_blank(acc)) {
            /* the demo's stepping gesture: when a section is mid-tour
             * (globals hold demo_cursec > 0), a bare Enter advances one
             * problem — the same mapping the workbench page applies, so
             * "[ Enter for the next problem ]" is true on both surfaces */
            Value dcv;
            bool stepping = env_lookup(I.globals, "demo_cursec", 11, &dcv) &&
                            dcv.kind == VAL_INT && dcv.as.i > 0;
            if (stepping) {
                value_release(dcv);
                free(acc);
                acc = strdup("next"); acclen = 4;
            } else { free(acc); acc = nullptr; acclen = 0; cont = false; continue; }
        }

        bool synthetic = acc && strcmp(acc, "next") == 0 && acclen == 4;   /* the demo step, not typed */
        Arena *a = arena_new();
        Parser p;
        parser_init(&p, acc, a);
        AstNode *prog = parser_parse(&p);

        if (p.had_error) {
            if (p.err_tok.kind == TOK_EOF) {    /* unterminated -> read another line */
                arena_free(a);
                cont = true;
                continue;
            }
            print_diag(acc, p.err_tok.line, p.err_tok.col, p.err_msg);
            arena_free(a);
            le_history_add(acc);
            free(acc); acc = nullptr; acclen = 0; cont = false;
            continue;
        }

        if (!synthetic) le_history_add(acc);
        char  *cap = nullptr; size_t capsz = 0; FILE *ms = nullptr;
        bool   paging = g_more && out_is_tty();
        if (paging) {
            ms = open_memstream(&cap, &capsz);
            if (ms) value_set_out(ms); else paging = false;
        }

        Value r = vm_eval_program(&I, prog, globals, /*echo=*/true);
        if (I.had_error && strncmp(I.err, "interrupted", 11) == 0 && (I.err[11] == '\0' || I.err[11] == '\n'))
            fputc('\n', stdout);                /* Ctrl-C at input()/pause(), or in the browser:
                                                    the same newline SIGINT gives natively */
        else if (I.had_error)
            print_diag(acc, I.cur_line, I.cur_col, I.err);
        value_release(r);

        if (paging) {
            value_set_out(nullptr);
            fclose(ms);
            page_output(cap, capsz);
            free(cap);
        }

        if (I.line_borrows_src)                  /* entry 11: retain only borrowers */
            keep_push(&keep, a, acc);
        else { arena_free(a); free(acc); }
        acc = nullptr; acclen = 0; cont = false;
    }

    /* the history file is kept current after each line (isocline) */
    free(acc);                                   /* any unfinished entry */
    env_clear(globals);                          /* break global closure/env cycles */
    env_release(globals);
    vm_pool_drain();
    vm_session_end();                            /* free retained chunks before arenas */
    keep_free_all(&keep);                        /* closures are gone; safe to free arenas/srcs */
    return 0;
}
