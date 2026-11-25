#define _GNU_SOURCE
#ifdef __APPLE__
#define _DARWIN_C_SOURCE 1   /* re-expose Apple extensions (the QoS API, openpty) */
#endif
/* serve.c — cozy --workbench: the playground page on the native engine.
 *
 * Since 0.1.51 the playground (docs/index.html) is the REPL itself on
 * xterm.js; in the browser it runs as WebAssembly. Here (0.1.52, design
 * entry 34, stage 3) the page gets the NATIVE REPL instead: this server
 * forks one — repl_run(), the very code ./cozy runs — onto a pseudo-
 * terminal, and relays it. Keys the page sends are written to the pty, the
 * REPL's bytes go back to the page, the page's size becomes the pty's size,
 * and Ctrl-C is a real SIGINT through the line discipline. One page, two
 * engines, the same REPL; Accelerate/OpenBLAS speed and your real files.
 *
 *   GET  /native-ping        "cozy <version> <backend> <token>" (same-origin only)
 *   GET  /pty/out?from=N     the session's output from byte N (long poll)
 *   POST /pty/in             keys
 *   POST /pty/size?cols=C&rows=R
 *   POST /pty/restart        a new session after Ctrl-D ended the last
 *   GET  /plots              plot_*.svg in the working directory
 *   POST /upload?name=X      a file into the working directory
 *   GET  /packages-list      the package files
 *   GET  /doc/NAME.md        the books
 *   GET  /x                  docs/x (the page and its assets), /plot_N.svg
 *
 * Scope fence: 127.0.0.1 only, Host must be localhost, and every endpoint
 * that reaches the REPL or the disk wants the session token, which only a
 * page served from here can read (no CORS headers): a web page elsewhere
 * cannot drive this REPL — it runs !commands. No TLS; a loopback tool. */
#include <arpa/inet.h>
#include <dirent.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>                       /* strncasecmp */
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>
#ifdef __APPLE__
#include <pthread/qos.h>
#include <util.h>
#else
#include <pty.h>
#include <sys/prctl.h>
#endif
#ifndef INADDR_LOOPBACK
#define INADDR_LOOPBACK ((uint32_t)0x7f000001)
#endif
#include "version.h"
#include "linalg.h"
#include "eval.h"
#include "repl.h"

/* ---- HTTP, the small part ----------------------------------------------- */

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
    while (n) {
        ssize_t w = write(fd, p, n);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return;
        p += w; n -= (size_t)w;
    }
}

static void respond_x(int fd, const char *status, const char *ctype, const char *body, size_t blen,
                      const char *extra)
{
    char hdr[512];
    int hl = snprintf(hdr, sizeof hdr,
                      "HTTP/1.1 %s\r\nContent-Type: %s\r\nContent-Length: %zu\r\n"
                      "Cache-Control: no-store\r\nConnection: close\r\n%s\r\n",
                      status, ctype, blen, extra ? extra : "");
    send_all(fd, hdr, (size_t)hl);
    send_all(fd, body, blen);
}
static void respond(int fd, const char *status, const char *ctype, const char *body, size_t blen)
{ respond_x(fd, status, ctype, body, blen, nullptr); }

static void send_file(int fd, FILE *f, const char *path)
{
    if (!f) { respond(fd, "404 Not Found", "text/plain", "not found\n", 10); return; }
    fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET);
    char *buf = malloc((size_t)n ? (size_t)n : 1);
    if (!buf || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fclose(f); free(buf);
        respond(fd, "500 Internal Server Error", "text/plain", "io\n", 3); return;
    }
    fclose(f);
    respond(fd, "200 OK", mime_of(path), buf, (size_t)n);
    free(buf);
}

static void serve_file(int fd, const char *urlpath)
{
    /* "/" -> docs/index.html; "/x" -> docs/x; "/plot_N.svg" -> ./plot_N.svg.
     * docs/ resolves like any asset: cwd (the dev tree), beside the binary,
     * then the installed share dir; plots are the session's, in the cwd. */
    char path[512];
    if (strstr(urlpath, "..") || strlen(urlpath) > 200)
        { respond(fd, "400 Bad Request", "text/plain", "no\n", 3); return; }
    if (!strcmp(urlpath, "/")) snprintf(path, sizeof path, "docs/index.html");
    else if (!strncmp(urlpath, "/plot_", 6)) snprintf(path, sizeof path, "%.220s", urlpath + 1);
    else snprintf(path, sizeof path, "docs%.220s", urlpath);
    send_file(fd, strncmp(path, "docs/", 5) == 0 ? fopen_asset(path) : fopen(path, "rb"), path);
}

static void list_plots(int fd)
{
    char body[8192]; size_t bl = 0;
    DIR *d = opendir(".");
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) && bl < sizeof body - 300)
            if (!strncmp(e->d_name, "plot_", 5) && strstr(e->d_name, ".svg"))
                bl += (size_t)snprintf(body + bl, sizeof body - bl, "%.250s\n", e->d_name);
        closedir(d);
    }
    respond(fd, "200 OK", "text/plain", body, bl);
}

static void list_packages(int fd)
{
    char body[8192]; size_t bl = 0, dl;
    char dir[1200];
    DIR *d = opendir("packages");
    if (!d && exe_dir()[0]) {
        snprintf(dir, sizeof dir, "%.600s/packages", exe_dir());
        if (!(d = opendir(dir))) { snprintf(dir, sizeof dir, "%.500s/../share/cozy/packages", exe_dir()); d = opendir(dir); }
    }
    if (d) {
        struct dirent *e;
        while ((e = readdir(d)) && bl < sizeof body - 300)
            if ((dl = strlen(e->d_name)) > 3 && !strcmp(e->d_name + dl - 3, ".cz"))
                bl += (size_t)snprintf(body + bl, sizeof body - bl, "%.250s\n", e->d_name);
        closedir(d);
    }
    respond(fd, "200 OK", "text/plain", body, bl);
}

/* a query parameter's value (into out), or false */
static bool query(const char *url, const char *key, char *out, size_t cap)
{
    const char *q = strchr(url, '?');
    size_t kl = strlen(key);
    while (q) {
        q++;
        if (!strncmp(q, key, kl) && q[kl] == '=') {
            const char *v = q + kl + 1;
            size_t n = strcspn(v, "& \r\n");
            if (n >= cap) n = cap - 1;
            memcpy(out, v, n); out[n] = 0;
            return true;
        }
        q = strchr(q, '&');
    }
    return false;
}

/* a header's value (case-insensitive name), or null; points into req */
static const char *header(const char *req, const char *name, size_t *len)
{
    size_t nl = strlen(name);
    for (const char *p = strstr(req, "\r\n"); p && p[2] && !(p[2] == '\r' && p[3] == '\n'); p = strstr(p + 2, "\r\n")) {
        const char *h = p + 2;
        if (!strncasecmp(h, name, nl) && h[nl] == ':') {
            const char *v = h + nl + 1;
            while (*v == ' ') v++;
            *len = strcspn(v, "\r\n");
            return v;
        }
    }
    return nullptr;
}

/* ---- the session: the native REPL on a pseudo-terminal -------------------- */

static int    g_master = -1;          /* the pty's master side */
static pid_t  g_child = -1;
static bool   g_ended;                /* the REPL left (Ctrl-D); a key restarts */
static unsigned short g_cols = 80, g_rows = 24;
static char  *g_log;                  /* the session's output, from byte g_base */
static size_t g_len, g_cap, g_base;
#define LOG_MAX (4u << 20)            /* keep the last 4 MB: enough to redraw the screen */
static char   g_token[33];

static void log_append(const char *p, size_t n)
{
    if (g_len + n > g_cap) {
        size_t cap = g_cap ? g_cap : 65536;
        while (cap < g_len + n) cap *= 2;
        g_log = realloc(g_log, cap);
        if (!g_log) abort();
        g_cap = cap;
    }
    memcpy(g_log + g_len, p, n);
    g_len += n;
    if (g_len > LOG_MAX) {                            /* drop the oldest half */
        size_t drop = g_len - LOG_MAX / 2;
        memmove(g_log, g_log + drop, g_len - drop);
        g_len -= drop; g_base += drop;
    }
}

static void session_start(void)
{
    struct winsize ws = { .ws_row = g_rows, .ws_col = g_cols };
    fflush(stdout); fflush(stderr);
    pid_t pid = forkpty(&g_master, nullptr, nullptr, &ws);
    if (pid < 0) { perror("workbench: forkpty"); g_master = -1; return; }
    if (pid == 0) {                                   /* the REPL: plain ./cozy, on the pty */
#ifdef __linux__
        prctl(PR_SET_PDEATHSIG, SIGKILL);             /* gone with the server, however it ends
                                                         (elsewhere the pty's hangup ends it) */
#endif
        signal(SIGPIPE, SIG_DFL);
        setenv("TERM", "xterm-256color", 1);          /* what the page's xterm.js is */
        setenv("COZY_PLOT_TERM", "svg", 1);           /* plots as plot_N.svg, for the pane */
        setenv("COZY_WORKBENCH", "1", 1);             /* the notice before each prompt */
        setenv("COZY_PAGER", "builtin", 1);           /* the pager the browser has, too */
        _exit(repl_run());
    }
    g_child = pid;
    g_ended = false;
    fcntl(g_master, F_SETFL, fcntl(g_master, F_GETFL) | O_NONBLOCK);
}

static void session_resize(unsigned short cols, unsigned short rows)
{
    g_cols = cols; g_rows = rows;
    if (g_master >= 0) {
        struct winsize ws = { .ws_row = rows, .ws_col = cols };
        ioctl(g_master, TIOCSWINSZ, &ws);             /* the REPL gets SIGWINCH */
    }
}

static void session_reap(void)
{
    if (g_child > 0 && waitpid(g_child, nullptr, WNOHANG) == g_child) g_child = -1;
}

/* ---- long polls waiting for output ---------------------------------------- */

typedef struct { int fd; size_t from; time_t since; } Waiter;
static Waiter g_wait[32];
static int    g_nwait;

static void reply_out(int fd, size_t from)
{
    if (from < g_base) from = g_base;
    if (from > g_base + g_len) from = g_base + g_len;
    char extra[96];
    snprintf(extra, sizeof extra, "X-Next: %zu\r\nX-Ended: %d\r\n", g_base + g_len, g_ended ? 1 : 0);
    respond_x(fd, "200 OK", "application/octet-stream", g_log + (from - g_base),
              g_base + g_len - from, extra);
}

static void wake_waiters(bool all)
{
    time_t now = time(nullptr);
    for (int i = 0; i < g_nwait; ) {
        Waiter *w = &g_wait[i];
        if (all || g_base + g_len > w->from || g_ended || now - w->since >= 20) {
            reply_out(w->fd, w->from);
            close(w->fd);
            g_wait[i] = g_wait[--g_nwait];
        } else i++;
    }
}

/* ---- one request -------------------------------------------------------- */

static bool authorized(const char *req)
{
    size_t l;
    const char *h = header(req, "Host", &l);         /* no DNS rebinding */
    if (!h || !(( l >= 9 && !strncmp(h, "127.0.0.1", 9)) || (l >= 9 && !strncmp(h, "localhost", 9))))
        return false;
    const char *t = header(req, "X-Cozy-Token", &l);
    return t && l == 32 && !memcmp(t, g_token, 32);
}

/* read a whole request: headers, then Content-Length bytes of body */
static char *read_request(int c, size_t *blen, char **body)
{
    size_t cap = 65536, len = 0;
    char *req = malloc(cap + 1);
    if (!req) return nullptr;
    char *hend = nullptr;
    size_t want = 0;
    for (;;) {
        if (len == cap) {
            if (cap >= (64u << 20)) { free(req); return nullptr; }   /* 64 MB: enough for an upload */
            cap *= 2; char *r = realloc(req, cap + 1); if (!r) { free(req); return nullptr; } req = r;
        }
        ssize_t n = read(c, req + len, cap - len);
        if (n < 0 && errno == EINTR) continue;
        if (n <= 0) break;
        len += (size_t)n; req[len] = 0;
        if (!hend && (hend = strstr(req, "\r\n\r\n"))) {
            size_t l; const char *cl = header(req, "Content-Length", &l);
            want = (size_t)(hend - req) + 4 + (cl ? strtoul(cl, nullptr, 10) : 0);
        }
        if (hend && len >= want) break;
    }
    if (!hend) { free(req); return nullptr; }
    req[len] = 0;
    *body = req + ((size_t)(strstr(req, "\r\n\r\n") - req) + 4);
    *blen = len - (size_t)(*body - req);
    return req;
}

static void handle(int c)
{
    size_t blen = 0; char *body = nullptr;
    char *req = read_request(c, &blen, &body);
    if (!req) { close(c); return; }
    char url[400] = "";
    sscanf(req, "%*s %399s", url);
    bool get = !strncmp(req, "GET ", 4), post = !strncmp(req, "POST ", 5);
    bool keep_open = false;

    if (get && !strcmp(url, "/native-ping")) {
        char pb[160];
        int pl = snprintf(pb, sizeof pb, "cozy %s %s %s\n", COZY_VERSION, cozy_linalg()->name, g_token);
        respond(c, "200 OK", "text/plain", pb, (size_t)pl);
    }
    else if (!strncmp(url, "/pty/", 5) || !strncmp(url, "/upload", 7)) {
        if (!authorized(req)) respond(c, "403 Forbidden", "text/plain", "token\n", 6);
        else if (get && !strncmp(url, "/pty/out", 8)) {
            char v[32] = "0";
            query(url, "from", v, sizeof v);
            size_t from = strtoull(v, nullptr, 10);
            if (g_master < 0 && !g_ended && g_child < 0) session_start();
            if (g_base + g_len > from || g_ended || g_nwait >= (int)(sizeof g_wait / sizeof *g_wait))
                reply_out(c, from);
            else {                                    /* park until there is output */
                g_wait[g_nwait++] = (Waiter){ c, from, time(nullptr) };
                keep_open = true;
            }
        }
        else if (post && !strcmp(url, "/pty/in")) {
            if (g_master >= 0) send_all(g_master, body, blen);
            respond(c, "204 No Content", "text/plain", "", 0);
        }
        else if (post && !strncmp(url, "/pty/size", 9)) {
            char cs[16], rs[16];
            if (query(url, "cols", cs, sizeof cs) && query(url, "rows", rs, sizeof rs)) {
                int cols = atoi(cs), rows = atoi(rs);
                if (cols >= 10 && cols <= 1000 && rows >= 3 && rows <= 500)
                    session_resize((unsigned short)cols, (unsigned short)rows);
            }
            respond(c, "204 No Content", "text/plain", "", 0);
        }
        else if (post && !strcmp(url, "/pty/restart")) {
            if (g_master < 0 || g_ended) {
                if (g_master >= 0) { close(g_master); g_master = -1; }
                session_reap();
                session_start();
            }
            respond(c, "204 No Content", "text/plain", "", 0);
        }
        else if (post && !strncmp(url, "/upload", 7)) {
            char name[260];
            const char *base = name;
            if (!query(url, "name", name, sizeof name) || !*name) {
                respond(c, "400 Bad Request", "text/plain", "name\n", 5);
            } else {
                const char *sl = strrchr(name, '/'); if (sl) base = sl + 1;
                if (!*base || *base == '.') respond(c, "400 Bad Request", "text/plain", "name\n", 5);
                else {
                    FILE *f = fopen(base, "wb");
                    bool ok = f && fwrite(body, 1, blen, f) == blen;
                    if (f) fclose(f);
                    if (ok) respond(c, "204 No Content", "text/plain", "", 0);
                    else respond(c, "500 Internal Server Error", "text/plain", "write\n", 6);
                }
            }
        }
        else respond(c, "400 Bad Request", "text/plain", "no\n", 3);
    }
    else if (get && !strcmp(url, "/plots")) list_plots(c);
    else if (get && !strcmp(url, "/packages-list")) list_packages(c);
    else if (get && !strncmp(url, "/doc/", 5)) {
        static const char *books[] = { "MANUAL.md", "PACKAGES.md", "BOOK.md", "CHANGELOG.md",
                                       "LESSONS.md", "DESIGN_NOTES.md" };
        FILE *f = nullptr;
        for (size_t i = 0; i < sizeof books / sizeof *books; i++)
            if (!strcmp(url + 5, books[i])) f = fopen_asset(books[i]);
        send_file(c, f, url);
    }
    else if (get) {
        char *qm = strchr(url, '?'); if (qm) *qm = 0;
        serve_file(c, url);
    }
    else respond(c, "400 Bad Request", "text/plain", "no\n", 3);
    if (!keep_open) close(c);
    free(req);
}

/* ---- the loop ------------------------------------------------------------- */

static void make_token(void)
{
    unsigned char r[16] = {0};
    FILE *u = fopen("/dev/urandom", "rb");
    if (!u || fread(r, 1, sizeof r, u) != sizeof r) {   /* no urandom: time and pid, weak but local */
        unsigned long s = (unsigned long)time(nullptr) ^ ((unsigned long)getpid() << 16);
        for (int i = 0; i < 16; i++) { s = s * 6364136223846793005ul + 1442695040888963407ul; r[i] = (unsigned char)(s >> 56); }
    }
    if (u) fclose(u);
    for (int i = 0; i < 16; i++) snprintf(g_token + 2 * i, 3, "%02x", r[i]);
}

int cozy_workbench(int port)
{
#ifdef __APPLE__
    /* A socket-blocked daemon reads as background work to the macOS
     * scheduler; declare interactive QoS (the REPL forked below inherits
     * it), so Accelerate runs on performance cores as in the terminal. */
    pthread_set_qos_class_self_np(QOS_CLASS_USER_INTERACTIVE, 0);
#endif
    signal(SIGPIPE, SIG_IGN);
    make_token();

    int s = socket(AF_INET, SOCK_STREAM, 0);
    int yes = 1; setsockopt(s, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    struct sockaddr_in addr = {0};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);   /* 127.0.0.1 ONLY */
    addr.sin_port = htons((uint16_t)port);
    if (bind(s, (struct sockaddr *)&addr, sizeof addr) != 0 || listen(s, 16) != 0) {
        fprintf(stderr, "workbench: cannot bind 127.0.0.1:%d\n", port); return 1;
    }
    printf("cozy workbench: http://localhost:%d  (the native REPL v%s | %s backend, in your browser; Ctrl-C stops)\n",
           port, COZY_VERSION, cozy_linalg()->name);
    fflush(stdout);
    for (;;) {
        struct pollfd pf[2] = { { .fd = s, .events = POLLIN }, { .fd = g_master, .events = POLLIN } };
        int np = g_master >= 0 ? 2 : 1;
        int r = poll(pf, (nfds_t)np, 1000);
        if (r < 0 && errno != EINTR) break;
        if (np == 2 && (pf[1].revents & (POLLIN | POLLHUP | POLLERR))) {
            char buf[65536];
            ssize_t n = read(g_master, buf, sizeof buf);
            if (n > 0) log_append(buf, (size_t)n);
            else if (n == 0 || (errno != EAGAIN && errno != EINTR)) {   /* the REPL left */
                close(g_master); g_master = -1;
                session_reap();
                g_ended = true;
            }
            wake_waiters(false);
        }
        if (pf[0].revents & POLLIN) {
            int c = accept(s, nullptr, nullptr);
            if (c >= 0) handle(c);
        }
        session_reap();
        wake_waiters(false);                          /* time the long polls out */
    }
    return 0;
}
