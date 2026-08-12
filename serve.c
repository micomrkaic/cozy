#define _POSIX_C_SOURCE 200809L
#ifdef __APPLE__
#define _DARWIN_C_SOURCE 1   /* re-expose Apple extensions (the QoS API) that
    strict POSIX hides — the playbook's own Darwin rule, applied this time */
#endif
/* serve.c — cozy --workbench: the workbench's native engine (design entry 9).
 * A deliberately tiny localhost HTTP server: serves docs/ (the same page
 * GitHub Pages serves), answers POST /eval by running the line through THIS
 * process's interpreter — Accelerate/OpenBLAS speed, real filesystem, your
 * actual workspace — and lists plot_*.svg for the plot pane. The page detects
 * this backend and routes evals here; without it, the embedded wasm engine
 * answers instead. Scope fence, documented: 127.0.0.1 only, one connection at
 * a time, no TLS, no auth — a loopback tool, not a network service. */
#include <arpa/inet.h>
#include <dirent.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#ifdef __APPLE__
#include <pthread/qos.h>
#endif
#include <unistd.h>
#ifndef INADDR_LOOPBACK
#define INADDR_LOOPBACK ((uint32_t)0x7f000001)   /* Darwin under strict
    _POSIX_C_SOURCE hides BSD-heritage symbols; the loopback address is
    eternal, so define it ourselves (owner's Mac deploy caught this) */
#endif
#include "arena.h"
#include "version.h"
#include "linalg.h"
#include "parser.h"
#include "eval.h"
#include "vm.h"

typedef struct { Arena **arenas; char **srcs; size_t count, cap; } SvKeep;
static SvKeep skeep;
static void skeep_push(Arena *a, char *src)
{
    if (skeep.count == skeep.cap) {
        skeep.cap = skeep.cap ? skeep.cap * 2 : 16;
        skeep.arenas = realloc(skeep.arenas, skeep.cap * sizeof *skeep.arenas);
        skeep.srcs   = realloc(skeep.srcs,   skeep.cap * sizeof *skeep.srcs);
        if (!skeep.arenas || !skeep.srcs) abort();
    }
    skeep.arenas[skeep.count] = a; skeep.srcs[skeep.count] = src; skeep.count++;
}

static const char *mime_of(const char *path)
{
    const char *dot = strrchr(path, '.');
    if (!dot) return "application/octet-stream";
    if (!strcmp(dot, ".html")) return "text/html; charset=utf-8";
    if (!strcmp(dot, ".js"))   return "text/javascript";
    if (!strcmp(dot, ".css"))  return "text/css";
    if (!strcmp(dot, ".svg"))  return "image/svg+xml";
    if (!strcmp(dot, ".png"))  return "image/png";
    if (!strcmp(dot, ".md"))   return "text/markdown; charset=utf-8";
    return "application/octet-stream";
}

static void send_all(int fd, const char *p, size_t n)
{
    while (n) { ssize_t w = write(fd, p, n); if (w <= 0) return; p += w; n -= (size_t)w; }
}
static void respond(int fd, const char *status, const char *ctype, const char *body, size_t blen)
{
    char hdr[256];
    int hl = snprintf(hdr, sizeof hdr,
                      "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                      "Cache-Control: no-store\r\nConnection: close\r\n\r\n",
                      status, ctype, blen);
    send_all(fd, hdr, (size_t)hl);
    send_all(fd, body, blen);
}

static void serve_file(int fd, const char *urlpath)
{
    /* map "/" -> docs/index.html; "/x" -> docs/x; "/plot_N.svg" -> ./plot_N.svg */
    char path[512];
    if (strstr(urlpath, "..") || strlen(urlpath) > 200)
        { respond(fd, "400 Bad Request", "text/plain", "no\n", 3); return; }
    if (!strcmp(urlpath, "/")) snprintf(path, sizeof path, "docs/index.html");
    else if (!strncmp(urlpath, "/plot_", 6)) snprintf(path, sizeof path, "%.220s", urlpath + 1);
    else snprintf(path, sizeof path, "docs%.220s", urlpath);
    FILE *f = fopen(path, "rb");
    if (!f) { respond(fd, "404 Not Found", "text/plain", "not found\n", 10); return; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n ? (size_t)n : 1);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) { fclose(f); free(buf);
        respond(fd, "500 Internal Server Error", "text/plain", "io\n", 3); return; }
    fclose(f);
    respond(fd, "200 OK", mime_of(path), buf, (size_t)n);
    free(buf);
}

static void list_plots(int fd)
{
    char body[4096]; size_t bl = 0;
    DIR *d = opendir(".");
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) && bl < sizeof body - 64)
            if (!strncmp(e->d_name, "plot_", 5) && strstr(e->d_name, ".svg"))
                bl += (size_t)snprintf(body + bl, sizeof body - bl, "%s\n", e->d_name);
        closedir(d);
    }
    respond(fd, "200 OK", "text/plain", body, bl);
}

/* Frontend command sugar, mirroring repl.c's match_command family so the
 * workbench terminal speaks the same commands as the REPL (owner's catch:
 * `pretty on` parsed as an error here while working in the terminal).
 * Returns true if the line was a command (reply already written to cap). */
static bool serve_command(FILE *cap, const char *line)
{
    const char *q = line;
    while (*q == ' ' || *q == '\t') q++;
    if (!strncmp(q, "pretty", 6) && (q[6] == '\0' || q[6] == ' ')) {
        const char *arg = q + 6; while (*arg == ' ') arg++;
        size_t al = strcspn(arg, " \t\r\n");
        if (!strncmp(arg, "on", al) && al == 2)       value_set_multiline(true);
        else if (!strncmp(arg, "off", al) && al == 3) value_set_multiline(false);
        else { fprintf(cap, "pretty is %s\n", value_multiline() ? "on" : "off"); return true; }
        return true;
    }
    if (!strncmp(q, "manual", 6) && (q[6] == '\0' || q[6] == ' ')) {
        fprintf(cap, "the manual lives in the Docs tab up top\n"); return true;
    }
    if (!strncmp(q, "more", 4) && (q[4] == '\0' || q[4] == ' ')) {
        fprintf(cap, "more is the terminal pager; the browser scrolls\n"); return true;
    }
    return false;
}

static void do_eval(int fd, Interp *I, EnvObj *globals, char *line)
{
    char *out = NULL; size_t outlen = 0;
    FILE *cap = open_memstream(&out, &outlen);
    value_set_out(cap);
    if (serve_command(cap, line)) {                 /* frontend commands first */
        fclose(cap); value_set_out(stdout);
        respond(fd, "200 OK", "text/plain", out ? out : "", outlen);
        free(out);
        return;
    }
    char *src = strdup(line);
    Arena *a = arena_new();
    Parser p;
    parser_init(&p, src, a);
    AstNode *prog = parser_parse(&p);
    if (p.had_error) {
        fprintf(cap, "  parse error at %u:%u: %s\n", p.err_tok.line, p.err_tok.col, p.err_msg);
        arena_free(a); free(src);
    } else {
        Value r = vm_eval_program(I, prog, globals, /*echo=*/true);
        if (I->had_error)
            fprintf(cap, "  error at %u:%u: %s\n", I->cur_line, I->cur_col, I->err);
        value_release(r);
        if (I->line_borrows_src) skeep_push(a, src);
        else { arena_free(a); free(src); }
    }
    fclose(cap);
    value_set_out(NULL);
    respond(fd, "200 OK", "text/plain; charset=utf-8", out ? out : "", outlen);
    free(out);
}

#ifdef __APPLE__
#include <dispatch/dispatch.h>
struct EvReqFwd { int fd; Interp *I; EnvObj *g; char *body; };
static void serve_eval_dispatch(void *p)
{
    struct EvReqFwd *e = p;
    do_eval(e->fd, e->I, e->g, e->body);
}
#endif

int cozy_workbench(int port)
{
    extern bool cozy_stdin_ok;
    cozy_stdin_ok = false;      /* HTTP evals must never block on our stdin */
#ifdef __APPLE__
    /* A socket-blocked daemon reads as background work to the macOS
     * scheduler and gets relegated to efficiency cores — the owner
     * measured inv(1000) at 0.042s in the workbench vs 0.028s in the
     * terminal, SAME binary. Declare interactive QoS so evals run on
     * performance cores like the REPL does. */
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
    signal(SIGPIPE, SIG_IGN);
    setenv("COZY_PLOT_TERM", "svg", 1);          /* plots land as plot_N.svg for the pane */
    Interp I; interp_init(&I);
    EnvObj *globals = globals_new();

    int s = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   /* 127.0.0.1 ONLY */
    addr.sin_port = htons((uint16_t)port);
    if (bind(s, (struct sockaddr *)&addr, sizeof addr) != 0 || listen(s, 4) != 0) {
        fprintf(stderr, "workbench: cannot bind 127.0.0.1:%d\n", port); return 1;
    }
    printf("cozy workbench: http://localhost:%d  (native engine v%s | %s backend; Ctrl-C stops)\n",
           port, COZY_VERSION, cozy_linalg()->name);
    for (;;) {
        int c = accept(s, NULL, NULL);
        if (c < 0) continue;
        char req[65536];
        ssize_t n = read(c, req, sizeof req - 1);
        if (n <= 0) { close(c); continue; }
        req[n] = 0;
        if (!strncmp(req, "GET /native-ping", 16)) {
            char pb[128];
            int pl = snprintf(pb, sizeof pb, "cozy %s %s\n",
                              COZY_VERSION, cozy_linalg()->name);
            respond(c, "200 OK", "text/plain", pb, (size_t)pl);
        }
        else if (!strncmp(req, "GET /plots", 10))
            list_plots(c);
        else if (!strncmp(req, "POST /eval", 10)) {
            char *body = strstr(req, "\r\n\r\n");
#ifdef __APPLE__
            /* Enter the eval through libdispatch at stated QoS: Accelerate
             * parallelizes via GCD, and pool QoS follows the DISPATCH
             * context — a bare pthread's QoS attribute demonstrably did
             * not reach it (owner's measurement, 0.0.47: still 1.5x).
             * dispatch_sync_f is plain C, no blocks runtime. */
            struct EvReqFwd er = { c, &I, globals, body ? body + 4 : "" };
            dispatch_sync_f(dispatch_get_global_queue(QOS_CLASS_USER_INTERACTIVE, 0),
                            &er, serve_eval_dispatch);
#else
            do_eval(c, &I, globals, body ? body + 4 : "");
#endif
        } else if (!strncmp(req, "GET ", 4)) {
            char *sp = strchr(req + 4, ' ');
            if (sp) *sp = 0;
            serve_file(c, req + 4);
        } else respond(c, "400 Bad Request", "text/plain", "no\n", 3);
        close(c);
    }
}
