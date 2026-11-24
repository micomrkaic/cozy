/* kalman.c — the simultaneous Kalman smoother (design entry 31; see kalman.h).
 *
 * Four passes over one block-banded SPD system J X = h:
 *
 *   1. assemble J (lower blocks only) and h from the prior, the transitions
 *      and the observed rows of each y_t;
 *   2. factor J = L L' by banded block Cholesky — no fill outside the band;
 *   3. solve L z = h, L' x = z;
 *   4. (want_cov) selected inversion: the blocks of Sigma = J^{-1} inside
 *      the band, by the Takahashi recurrence  Sigma L = L^{-T}.
 *
 * Storage: block (t, t-k) of J, L or Sigma lives at blk(t, k), k = 0..b.
 * Cost O(T b^2 n^3) time and O(T b n^2) memory; the observation side costs
 * O(m n^2 (p+1)^2) per distinct missing-data pattern (patterns are cached),
 * so many series fold into small n x n blocks before anything else runs. */
#include <math.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "kalman.h"

#define LOG2PI 1.8378770664093454836

/* ---- small dense kernels, row-major --------------------------------- */

/* C += alpha * op(A) * op(B); op(A) is M x K, op(B) is K x N. A is stored
 * M x K (or K x M when tA), B is stored K x N (or N x K when tB). */
static void gemm(uint32_t M, uint32_t N, uint32_t K, double alpha,
                 const double *A, bool tA, const double *B, bool tB, double *C)
{
    for (uint32_t i = 0; i < M; i++)
        for (uint32_t k = 0; k < K; k++) {
            double a = tA ? A[(size_t)k * M + i] : A[(size_t)i * K + k];
            if (a == 0.0) continue;
            a *= alpha;
            double *c = C + (size_t)i * N;
            if (tB) for (uint32_t j = 0; j < N; j++) c[j] += a * B[(size_t)j * K + k];
            else  { const double *b = B + (size_t)k * N;
                    for (uint32_t j = 0; j < N; j++) c[j] += a * b[j]; }
        }
}

/* in-place lower Cholesky of the n x n SPD A (upper triangle zeroed);
 * returns 0, or -1 if A is not positive definite */
static int chol_lower(double *A, uint32_t n)
{
    for (uint32_t j = 0; j < n; j++) {
        double s = A[(size_t)j * n + j];
        for (uint32_t k = 0; k < j; k++) s -= A[(size_t)j * n + k] * A[(size_t)j * n + k];
        if (!(s > 0.0) || !isfinite(s)) return -1;
        double l = sqrt(s);
        A[(size_t)j * n + j] = l;
        for (uint32_t i = j + 1; i < n; i++) {
            double v = A[(size_t)i * n + j];
            for (uint32_t k = 0; k < j; k++) v -= A[(size_t)i * n + k] * A[(size_t)j * n + k];
            A[(size_t)i * n + j] = v / l;
        }
        for (uint32_t i = 0; i < j; i++) A[(size_t)i * n + j] = 0.0;
    }
    return 0;
}

/* Li = L^{-1} for lower-triangular L (Li lower) */
static void tri_inv_lower(const double *L, double *Li, uint32_t n)
{
    memset(Li, 0, (size_t)n * n * sizeof *Li);
    for (uint32_t j = 0; j < n; j++) {
        Li[(size_t)j * n + j] = 1.0 / L[(size_t)j * n + j];
        for (uint32_t i = j + 1; i < n; i++) {
            double s = 0.0;
            for (uint32_t k = j; k < i; k++) s += L[(size_t)i * n + k] * Li[(size_t)k * n + j];
            Li[(size_t)i * n + j] = -s / L[(size_t)i * n + i];
        }
    }
}

/* triangular solves with the lower L (n x n), each over the rows of an
 * r x n block S, in place — more accurate than multiplying by L^{-1} when
 * a diagonal block is badly scaled */
static void rsolve_LT(double *S, uint32_t r, const double *L, uint32_t n)    /* S <- S L^{-T} */
{
    for (uint32_t q = 0; q < r; q++) {
        double *x = S + (size_t)q * n;                 /* L x = s */
        for (uint32_t i = 0; i < n; i++) {
            double v = x[i];
            for (uint32_t k = 0; k < i; k++) v -= L[(size_t)i * n + k] * x[k];
            x[i] = v / L[(size_t)i * n + i];
        }
    }
}
static void rsolve_L(double *S, uint32_t r, const double *L, uint32_t n)     /* S <- S L^{-1} */
{
    for (uint32_t q = 0; q < r; q++) {
        double *x = S + (size_t)q * n;                 /* L' x = s */
        for (uint32_t i = n; i-- > 0; ) {
            double v = x[i];
            for (uint32_t k = i + 1; k < n; k++) v -= L[(size_t)k * n + i] * x[k];
            x[i] = v / L[(size_t)i * n + i];
        }
    }
}

static double logdet_chol(const double *L, uint32_t n)
{
    double s = 0.0;
    for (uint32_t i = 0; i < n; i++) s += log(L[(size_t)i * n + i]);
    return 2.0 * s;
}

/* inverse and log-determinant of an SPD matrix via Cholesky; -1 if not PD */
static int spd_inv(const double *A, uint32_t n, double *Ai, double *logdet, double *work)
{
    memcpy(work, A, (size_t)n * n * sizeof *work);
    if (chol_lower(work, n) != 0) return -1;
    *logdet = logdet_chol(work, n);
    double *Li = work + (size_t)n * n;
    tri_inv_lower(work, Li, n);
    memset(Ai, 0, (size_t)n * n * sizeof *Ai);
    gemm(n, n, n, 1.0, Li, true, Li, false, Ai);          /* L^{-T} L^{-1} */
    return 0;
}

static bool all_finite(const double *v, size_t cnt)
{
    for (size_t k = 0; k < cnt; k++) if (!isfinite(v[k])) return false;
    return true;
}

/* ---- the missing-data pattern cache ---------------------------------- */

#define KS_NPAT 16
typedef struct {
    uint8_t  *mask;           /* m bytes: 1 = row enters                     */
    uint32_t  mo;             /* rows that enter                             */
    uint32_t *idx;            /* their indices                               */
    double   *W;              /* R_oo^{-1}: mo x mo (full R) or mo values (diagonal) */
    double   *G;             /* Hs_o' W Hs_o: np x np                       */
    double    logdetR;        /* log det R_oo                                */
    uint64_t  used;           /* last use, for eviction                      */
    bool      valid;
} Pat;

typedef struct {
    const KsModel *M;
    uint32_t n, m, T, p, b, np;
    double *J;                /* (T+1)(b+1) blocks of n x n                  */
    double *h;                /* n (T+1)                                     */
    double *Li;               /* (T+1) blocks: inverse of L's diagonal block */
    double *S;                /* Sigma band, same layout as J (want_cov)     */
    Pat     pat[KS_NPAT];
    uint64_t clock;
    double  *work;            /* scratch                                     */
    size_t   work_len;
} Ks;

static inline double *blk(const Ks *k, double *base, uint32_t t, uint32_t lag)
{ return base + ((size_t)t * (k->b + 1) + lag) * k->n * k->n; }

static int fail(KsResult *out, const char *fmt, ...) __attribute__((format(printf, 2, 3)));
static int fail(KsResult *out, const char *fmt, ...)
{
    va_list ap; va_start(ap, fmt);
    vsnprintf(out->msg, sizeof out->msg, fmt, ap);
    va_end(ap);
    return -1;
}

static void ks_release(Ks *k)
{
    free(k->J); free(k->h); free(k->Li); free(k->S); free(k->work);
    for (int i = 0; i < KS_NPAT; i++) { free(k->pat[i].mask); free(k->pat[i].idx); free(k->pat[i].W); free(k->pat[i].G); }
}

void ks_free(KsResult *out)
{
    free(out->x); free(out->P);
    for (int i = 0; i < KS_MAXLAG; i++) free(out->C[i]);
    out->x = out->P = nullptr;
    for (int i = 0; i < KS_MAXLAG; i++) out->C[i] = nullptr;
}

/* Hs_o' * diag-or-full W * Hs_o into G (np x np), for the rows idx[0..mo) */
static void form_G(const Ks *k, const uint32_t *idx, uint32_t mo, const double *W, bool diagW,
                   double *G, double *tmp /* mo x np */)
{
    const double *H = k->M->H;
    uint32_t np = k->np;
    memset(G, 0, (size_t)np * np * sizeof *G);
    if (diagW) {
        for (uint32_t r = 0; r < mo; r++) {
            const double *hr = H + (size_t)idx[r] * np;
            double w = W[r];
            for (uint32_t a = 0; a < np; a++) {
                double ha = hr[a] * w;
                if (ha == 0.0) continue;
                double *g = G + (size_t)a * np;
                for (uint32_t c = 0; c < np; c++) g[c] += ha * hr[c];
            }
        }
        return;
    }
    /* tmp = W * Hs_o, then G = Hs_o' * tmp */
    memset(tmp, 0, (size_t)mo * np * sizeof *tmp);
    for (uint32_t r = 0; r < mo; r++)
        for (uint32_t s = 0; s < mo; s++) {
            double w = W[(size_t)r * mo + s];
            if (w == 0.0) continue;
            const double *hs = H + (size_t)idx[s] * np;
            for (uint32_t c = 0; c < np; c++) tmp[(size_t)r * np + c] += w * hs[c];
        }
    for (uint32_t r = 0; r < mo; r++) {
        const double *hr = H + (size_t)idx[r] * np;
        for (uint32_t a = 0; a < np; a++) {
            double ha = hr[a];
            if (ha == 0.0) continue;
            double *g = G + (size_t)a * np;
            const double *tr = tmp + (size_t)r * np;
            for (uint32_t c = 0; c < np; c++) g[c] += ha * tr[c];
        }
    }
}

/* look up (or build) the cached entry for this mask; nullptr + msg on a
 * non-PD R_oo */
static Pat *pattern(Ks *k, const uint8_t *mask, uint32_t mo, uint32_t t, KsResult *out)
{
    const KsModel *M = k->M;
    uint32_t m = k->m;
    Pat *victim = &k->pat[0];
    for (int i = 0; i < KS_NPAT; i++) {
        Pat *P = &k->pat[i];
        if (P->valid && P->mo == mo && memcmp(P->mask, mask, m) == 0) { P->used = ++k->clock; return P; }
        if (!P->valid) { victim = P; break; }
        if (P->used < victim->used) victim = P;
    }
    Pat *P = victim;
    if (!P->mask) {
        P->mask = malloc(m);
        P->idx  = malloc((size_t)m * sizeof *P->idx);
        P->W    = malloc((size_t)(M->R_diag ? m : (size_t)m * m) * sizeof *P->W);
        P->G    = malloc((size_t)k->np * k->np * sizeof *P->G);
        if (!P->mask || !P->idx || !P->W || !P->G) { fail(out, "out of memory"); return nullptr; }
    }
    memcpy(P->mask, mask, m);
    P->mo = mo;
    for (uint32_t i = 0, r = 0; i < m; i++) if (mask[i]) P->idx[r++] = i;
    if (M->R_diag) {
        P->logdetR = 0.0;
        for (uint32_t r = 0; r < mo; r++) { double v = M->R[P->idx[r]]; P->W[r] = 1.0 / v; P->logdetR += log(v); }
    } else {
        double *Roo = k->work, *wk = k->work + (size_t)m * m;
        for (uint32_t r = 0; r < mo; r++)
            for (uint32_t s = 0; s < mo; s++) Roo[(size_t)r * mo + s] = M->R[(size_t)P->idx[r] * m + P->idx[s]];
        if (spd_inv(Roo, mo, P->W, &P->logdetR, wk) != 0) {
            P->valid = false;
            fail(out, "R is not positive definite on the rows observed at t = %u", t);
            return nullptr;
        }
    }
    form_G(k, P->idx, mo, P->W, M->R_diag, P->G, k->work);
    P->valid = true;
    P->used = ++k->clock;
    return P;
}

/* add alpha * G (np x np, the lagged-states Gram) into J at period t */
static void add_G(Ks *k, uint32_t t, const double *G, double alpha)
{
    uint32_t n = k->n, np = k->np;
    for (uint32_t a = 0; a <= k->p; a++)
        for (uint32_t c = a; c <= k->p; c++) {
            if (c > t) break;                       /* x_{t-c} does not exist */
            double *B = blk(k, k->J, t - a, c - a);  /* block (t-a, t-c)       */
            for (uint32_t i = 0; i < n; i++)
                for (uint32_t j = 0; j < n; j++)
                    B[(size_t)i * n + j] += alpha * G[(size_t)(a * n + i) * np + c * n + j];
        }
}

int ks_smooth(const KsModel *M, KsResult *out)
{
    memset(out, 0, sizeof *out);
    uint32_t n = M->n, m = M->m, T = M->T, p = M->p;
    if (n == 0 || m == 0 || T == 0) return fail(out, "need at least one state, one series and one period");
    if (p > KS_MAXLAG) return fail(out, "at most %d measurement lags", KS_MAXLAG);
    if (M->wy_full && !M->R_diag) return fail(out, "per-series weights (wy as m x T) need a diagonal R");

    Ks K = { .M = M, .n = n, .m = m, .T = T, .p = p, .b = p > 1 ? p : 1, .np = n * (p + 1) };
    uint32_t b = K.b;
    size_t nn = (size_t)n * n, nblk = (size_t)(T + 1) * (b + 1);
    size_t mm = (size_t)m * m;
    size_t wl = 4 * nn + 3 * mm + (size_t)m * K.np + (size_t)K.np * K.np + 4 * (size_t)n + 2 * (size_t)m + 64;
    K.work_len = wl;
    K.J  = calloc(nblk * nn, sizeof *K.J);
    K.h  = calloc((size_t)(T + 1) * n, sizeof *K.h);
    K.Li = calloc((size_t)(T + 1) * nn, sizeof *K.Li);
    K.work = calloc(wl, sizeof *K.work);
    uint8_t  *mask   = calloc(m, 1);
    uint32_t *rowlag = calloc(m, sizeof *rowlag);
    double   *yt     = calloc(m, sizeof *yt);
    double   *u      = calloc(m, sizeof *u);
    double   *Qi = calloc(nn, sizeof *Qi), *QiF = calloc(nn, sizeof *QiF), *FtQiF = calloc(nn, sizeof *FtQiF);
    double   *G  = calloc((size_t)K.np * K.np, sizeof *G);
    int rc = -1;
#define BAIL(...) do { fail(out, __VA_ARGS__); goto done; } while (0)
    if (!K.J || !K.h || !K.Li || !K.work || !mask || !rowlag || !yt || !u || !Qi || !QiF || !FtQiF || !G)
        BAIL("out of memory (T = %u, n = %u)", T, n);

    /* ---- validate the parameters ---- */
    if (!all_finite(M->F, nn) || !all_finite(M->Q, nn)) BAIL("F and Q must be finite");
    if (!all_finite(M->H, (size_t)m * K.np)) BAIL("H must be finite");
    if (!all_finite(M->R, M->R_diag ? m : mm)) BAIL("R must be finite");
    if (M->R_diag) for (uint32_t i = 0; i < m; i++) if (!(M->R[i] > 0.0)) BAIL("R must be positive (diagonal entry %u is %g)", i + 1, M->R[i]);
    if (M->c && !all_finite(M->c, (size_t)n * M->c_cols)) BAIL("c must be finite");
    if (M->d && !all_finite(M->d, (size_t)m * M->d_cols)) BAIL("d must be finite");
    if (M->wy) { size_t cnt = M->wy_full ? (size_t)m * T : T;
                 for (size_t q = 0; q < cnt; q++) if (!(M->wy[q] > 0.0) || !isfinite(M->wy[q])) BAIL("weights wy must be positive and finite"); }
    if (M->wx) for (uint32_t q = 0; q < T; q++) if (!(M->wx[q] > 0.0) || !isfinite(M->wx[q])) BAIL("weights wx must be positive and finite");
    for (size_t q = 0; q < (size_t)m * T; q++) if (isinf(M->Y[q])) BAIL("Y has an infinite entry (use NaN for missing)");

    double Kq = 0.0;          /* the constant of the completed square        */
    double ld = 0.0;          /* log-determinant terms of the joint density  */
    uint64_t nobs = 0;

    /* ---- prior ---- */
    if (M->P0) {
        if (!all_finite(M->P0, nn) || !all_finite(M->a0, n)) BAIL("a0 and P0 must be finite");
        double *P0i = K.work + 2 * nn, ldP0;
        if (spd_inv(M->P0, n, P0i, &ldP0, K.work) != 0) BAIL("P0 is not positive definite (omit it for a diffuse prior)");
        double *J00 = blk(&K, K.J, 0, 0);
        for (size_t q = 0; q < nn; q++) J00[q] += P0i[q];
        for (uint32_t i = 0; i < n; i++) {
            double s = 0.0;
            for (uint32_t j = 0; j < n; j++) s += P0i[(size_t)i * n + j] * M->a0[j];
            K.h[i] += s;
            Kq += M->a0[i] * s;
        }
        ld += ldP0;
    }

    /* ---- transitions ---- */
    double ldQ;
    if (spd_inv(M->Q, n, Qi, &ldQ, K.work) != 0)
        BAIL("Q is not positive definite — a lagged state belongs in the measurement "
             "lags of H, not in a companion block with zero variance");
    gemm(n, n, n, 1.0, Qi, false, M->F, false, QiF);
    gemm(n, n, n, 1.0, M->F, true, QiF, false, FtQiF);
    for (uint32_t t = 1; t <= T; t++) {
        double s = M->wx ? M->wx[t - 1] : 1.0;
        double *Jt = blk(&K, K.J, t, 0), *Jp = blk(&K, K.J, t - 1, 0), *Jo = blk(&K, K.J, t, 1);
        for (size_t q = 0; q < nn; q++) { Jt[q] += s * Qi[q]; Jp[q] += s * FtQiF[q]; Jo[q] -= s * QiF[q]; }
        if (M->c) {
            const uint32_t cc = M->c_cols == 1 ? 0 : t - 1;
            double *ct = K.work, *qc = K.work + n;
            for (uint32_t i = 0; i < n; i++) ct[i] = M->c[(size_t)i * M->c_cols + cc];
            for (uint32_t i = 0; i < n; i++) { double v = 0; for (uint32_t j = 0; j < n; j++) v += Qi[(size_t)i * n + j] * ct[j]; qc[i] = s * v; }
            for (uint32_t i = 0; i < n; i++) {
                K.h[(size_t)t * n + i] += qc[i];
                double v = 0; for (uint32_t j = 0; j < n; j++) v += M->F[(size_t)j * n + i] * qc[j];
                K.h[(size_t)(t - 1) * n + i] -= v;
                Kq += ct[i] * qc[i];
            }
        }
        ld += ldQ - n * log(s);
    }

    /* ---- observations ---- */
    for (uint32_t i = 0; i < m; i++) {
        uint32_t lag = 0;
        for (uint32_t c = 0; c < K.np; c++) if (M->H[(size_t)i * K.np + c] != 0.0) lag = c / n;
        rowlag[i] = lag;
    }
    for (uint32_t t = 1; t <= T; t++) {
        uint32_t mo = 0;
        for (uint32_t i = 0; i < m; i++) {
            double y = M->Y[(size_t)i * T + (t - 1)];
            mask[i] = !isnan(y) && rowlag[i] <= t;
            if (mask[i]) {
                double dv = M->d ? M->d[(size_t)i * M->d_cols + (M->d_cols == 1 ? 0 : t - 1)] : 0.0;
                yt[mo++] = y - dv;
            }
        }
        if (mo == 0) continue;
        nobs += mo;
        const uint32_t *idx; double ws = 1.0;
        if (!M->wy_full) {
            Pat *P = pattern(&K, mask, mo, t, out);
            if (!P) goto done;
            if (M->wy) ws = M->wy[t - 1];
            add_G(&K, t, P->G, ws);
            idx = P->idx;
            if (M->R_diag) for (uint32_t r = 0; r < mo; r++) u[r] = ws * P->W[r] * yt[r];
            else for (uint32_t r = 0; r < mo; r++) {
                double v = 0; for (uint32_t s2 = 0; s2 < mo; s2++) v += P->W[(size_t)r * mo + s2] * yt[s2];
                u[r] = ws * v;
            }
            ld += P->logdetR - mo * log(ws);
        } else {
            uint32_t *list = (uint32_t *)(K.work + 4 * nn);       /* m slots */
            double   *wv   = K.work + 4 * nn + m;                 /* m slots */
            for (uint32_t i = 0, r = 0; i < m; i++) if (mask[i]) {
                double w = M->wy[(size_t)i * T + (t - 1)];
                list[r] = i; wv[r] = w / M->R[i];
                ld += log(M->R[i]) - log(w);
                u[r] = wv[r] * yt[r];
                r++;
            }
            form_G(&K, list, mo, wv, true, G, nullptr);
            add_G(&K, t, G, 1.0);
            idx = list;
        }
        for (uint32_t r = 0; r < mo; r++) Kq += yt[r] * u[r];
        /* h_{t-a} += H_a(o)' u */
        for (uint32_t r = 0; r < mo; r++) {
            const double *hr = M->H + (size_t)idx[r] * K.np;
            for (uint32_t a = 0; a <= p && a <= t; a++)
                for (uint32_t i = 0; i < n; i++) K.h[(size_t)(t - a) * n + i] += hr[a * n + i] * u[r];
        }
    }

    /* ---- banded block Cholesky, in place: J(t, k) becomes L(t, t-k) ---- */
    double *Sb = K.work, *Lt = K.work + nn;
    for (uint32_t t = 0; t <= T; t++) {
        uint32_t kmax = t < b ? t : b;
        for (uint32_t k = kmax; k >= 1; k--) {
            uint32_t j = t - k;
            double *Ltj = blk(&K, K.J, t, k);
            memcpy(Sb, Ltj, nn * sizeof *Sb);
            /* S -= sum_{i} L(t,i) L(j,i)',  i from t-kmax .. j-1 (inside both bands) */
            for (uint32_t i = t - kmax; i < j; i++)
                gemm(n, n, n, -1.0, blk(&K, K.J, t, t - i), false, blk(&K, K.J, j, j - i), true, Sb);
            rsolve_LT(Sb, n, blk(&K, K.J, j, 0), n);                            /* S L_jj^{-T} */
            memcpy(Ltj, Sb, nn * sizeof *Ltj);
        }
        double *D = blk(&K, K.J, t, 0);
        for (uint32_t k = 1; k <= kmax; k++) {
            double *A = blk(&K, K.J, t, k);
            gemm(n, n, n, -1.0, A, false, A, true, D);
        }
        for (uint32_t i = 0; i < n; i++) for (uint32_t j = i + 1; j < n; j++) {   /* symmetrize */
            double v = 0.5 * (D[(size_t)i * n + j] + D[(size_t)j * n + i]);
            D[(size_t)i * n + j] = D[(size_t)j * n + i] = v;
        }
        if (chol_lower(D, n) != 0)
            BAIL("the posterior precision is singular at x_%u: a state that neither the "
                 "data nor the prior pins down%s", t, M->P0 ? "" : " (a diffuse prior needs "
                 "enough early observations; give a0 and P0)");
        tri_inv_lower(D, K.Li + (size_t)t * nn, n);
        out->logdetJ += logdet_chol(D, n);
    }

    /* ---- solve ---- */
    out->x = malloc((size_t)n * (T + 1) * sizeof *out->x);
    double *z = malloc((size_t)n * (T + 1) * sizeof *z);
    if (!out->x || !z) { free(z); BAIL("out of memory"); }
    double *r = Lt;
    for (uint32_t t = 0; t <= T; t++) {
        memcpy(r, K.h + (size_t)t * n, n * sizeof *r);
        uint32_t kmax = t < b ? t : b;
        for (uint32_t k = 1; k <= kmax; k++)
            gemm(n, 1, n, -1.0, blk(&K, K.J, t, k), false, z + (size_t)(t - k) * n, false, r);
        rsolve_LT(r, 1, blk(&K, K.J, t, 0), n);                                /* L_tt^{-1} r */
        memcpy(z + (size_t)t * n, r, n * sizeof *r);
    }
    double *xx = malloc((size_t)n * (T + 1) * sizeof *xx);
    if (!xx) { free(z); BAIL("out of memory"); }
    for (uint32_t tt = T + 1; tt-- > 0; ) {
        memcpy(r, z + (size_t)tt * n, n * sizeof *r);
        for (uint32_t k = 1; k <= b && tt + k <= T; k++)
            gemm(n, 1, n, -1.0, blk(&K, K.J, tt + k, k), true, xx + (size_t)(tt + k) * n, false, r);
        rsolve_L(r, 1, blk(&K, K.J, tt, 0), n);                                /* L_tt^{-T} r */
        memcpy(xx + (size_t)tt * n, r, n * sizeof *r);
    }
    free(z);
    double hx = 0.0;
    for (size_t q = 0; q < (size_t)n * (T + 1); q++) hx += K.h[q] * xx[q];
    for (uint32_t t = 0; t <= T; t++)
        for (uint32_t i = 0; i < n; i++) out->x[(size_t)i * (T + 1) + t] = xx[(size_t)t * n + i];
    free(xx);
    Kq -= hx;
    out->loglik = -0.5 * (Kq + (double)nobs * LOG2PI + ld + out->logdetJ);
    out->nobs = nobs;
    out->n = n; out->T = T; out->b = b;

    /* ---- selected inversion: Sigma(i, j) = (delta_ij L_jj^{-T} - sum_k Sigma(i,k) L(k,j)) L_jj^{-1} ---- */
    if (M->want_cov) {
        K.S = calloc(nblk * nn, sizeof *K.S);
        if (!K.S) BAIL("out of memory");
        double *A = K.work, *Bm = K.work + nn, *Tmp = K.work + 2 * nn;
        for (uint32_t jj = T + 1; jj-- > 0; ) {
            uint32_t kend = jj + b < T ? jj + b : T;
            const double *Lij = K.Li + (size_t)jj * nn, *Ljj = blk(&K, K.J, jj, 0);
            for (uint32_t i = jj + 1; i <= kend; i++) {
                memset(A, 0, nn * sizeof *A);
                for (uint32_t k = jj + 1; k <= kend; k++) {
                    const double *Lkj = blk(&K, K.J, k, k - jj);
                    if (i >= k) gemm(n, n, n, 1.0, blk(&K, K.S, i, i - k), false, Lkj, false, A);
                    else        gemm(n, n, n, 1.0, blk(&K, K.S, k, k - i), true,  Lkj, false, A);
                }
                double *Sij = blk(&K, K.S, i, i - jj);
                rsolve_L(A, n, Ljj, n);                                        /* A L_jj^{-1} */
                for (size_t q = 0; q < nn; q++) Sij[q] = -A[q];
            }
            /* Sigma(j,j) = (L^{-T} - sum_k Sigma(k,j)' L(k,j)) L^{-1} */
            memset(Bm, 0, nn * sizeof *Bm);
            for (uint32_t i = 0; i < n; i++) for (uint32_t j = 0; j < n; j++) Bm[(size_t)i * n + j] = Lij[(size_t)j * n + i];
            for (uint32_t k = jj + 1; k <= kend; k++)
                gemm(n, n, n, -1.0, blk(&K, K.S, k, k - jj), true, blk(&K, K.J, k, k - jj), false, Bm);
            rsolve_L(Bm, n, Ljj, n);
            memcpy(Tmp, Bm, nn * sizeof *Tmp);
            double *Sjj = blk(&K, K.S, jj, 0);
            for (uint32_t i = 0; i < n; i++) for (uint32_t j = 0; j < n; j++)
                Sjj[(size_t)i * n + j] = 0.5 * (Tmp[(size_t)i * n + j] + Tmp[(size_t)j * n + i]);
        }
        size_t W = (size_t)n * (T + 1);       /* row length of the wide outputs */
        out->P = malloc((size_t)n * W * sizeof *out->P);
        if (!out->P) BAIL("out of memory");
        for (uint32_t k = 1; k <= b; k++) {
            out->C[k - 1] = malloc((size_t)n * W * sizeof *out->C[k - 1]);
            if (!out->C[k - 1]) BAIL("out of memory");
        }
        for (uint32_t t = 0; t <= T; t++)
            for (uint32_t k = 0; k <= b; k++) {
                double *dst = k == 0 ? out->P : out->C[k - 1];
                const double *src = t >= k ? blk(&K, K.S, t, k) : nullptr;
                for (uint32_t i = 0; i < n; i++)
                    for (uint32_t j = 0; j < n; j++)
                        dst[(size_t)i * W + (size_t)t * n + j] = src ? src[(size_t)i * n + j] : NAN;
            }
    }
    rc = 0;
done:
#undef BAIL
    if (rc != 0) ks_free(out);
    ks_release(&K);
    free(mask); free(rowlag); free(yt); free(u); free(Qi); free(QiF); free(FtQiF); free(G);
    return rc;
}
