/* kalman.h — the simultaneous Kalman smoother as one banded linear system
 * (design entry 31).
 *
 * The model, for t = 1..T:
 *
 *     x_t = F x_{t-1} + c_t + w_t,                 w_t ~ N(0, Q / wx_t)
 *     y_t = H_0 x_t + H_1 x_{t-1} + ... + H_p x_{t-p} + d_t + v_t,
 *                                                  v_t ~ N(0, R / wy_t)
 *     x_0 ~ N(a0, P0)          (or diffuse: P0 == NULL, the prior block dropped)
 *
 * The smoothed path is the solution of J X = h, where J — the posterior
 * precision of X = (x_0, ..., x_T) — is symmetric positive definite and
 * block-banded with block bandwidth b = max(1, p). Lags in the measurement
 * equation (a quarterly aggregate of a monthly state, say) widen the band;
 * they need no companion states and no near-singular Q.
 *
 * NaN in Y is a missing observation: only the observed rows of y_t enter.
 * A row of y_t that loads on a state before x_0 (H_k nonzero with k > t)
 * is dropped as unusable, the same as missing.
 *
 * Pure math: no Interp, no runtime_error. Everything is real double,
 * row-major. On failure ks_smooth returns nonzero with a message in
 * out->msg and nothing to free; on success the caller owns out->x, out->P,
 * out->C[k] and frees them with ks_free. */
#ifndef COZY_KALMAN_H
#define COZY_KALMAN_H

#include <stdint.h>
#include <stdbool.h>

#define KS_MAXLAG 400         /* largest p accepted: a quarter of days, by Mariano-Murasawa weights (2 x 92 - 1 = 183), with room */

typedef struct {
    uint32_t n, m, T, p;      /* states, series, periods, measurement lags */
    const double *Y;          /* m x T; NaN = missing                       */
    const double *F, *Q;      /* n x n                                      */
    const double *H;          /* m x n(p+1): [H_0, H_1, ..., H_p]           */
    const double *R;          /* m x m, or m values when R_diag             */
    bool R_diag;
    const double *a0;         /* n (ignored when diffuse)                   */
    const double *P0;         /* n x n, or NULL: diffuse                    */
    const double *c; uint32_t c_cols;   /* NULL, or n x 1 / n x T           */
    const double *d; uint32_t d_cols;   /* NULL, or m x 1 / m x T           */
    const double *wy; bool wy_full;     /* NULL, T values, or m x T (R_diag only) */
    const double *wx;                   /* NULL, or T values                */
    bool want_cov;            /* compute P and cross blocks (selected inverse) */
    bool want_stats;          /* EM sufficient statistics (implies the selected inverse) */
    bool want_fit;            /* fitted signal yhat (and ysd when want_cov) */
} KsModel;

typedef struct {
    uint32_t n, T, b;         /* b = block bandwidth = max(1, p)            */
    double *x;                /* n x (T+1): column t is x_t                 */
    double *P;                /* n x n(T+1): block t is Cov(x_t | Y)        */
    double *C[KS_MAXLAG];     /* C[k-1]: n x n(T+1), block t is Cov(x_t, x_{t-k} | Y)
                                 for t >= k, NaN for t < k; k = 1..b        */
    double loglik;            /* marginal log p(Y) (diffuse: the prior term dropped) */
    double logdetJ;
    uint64_t nobs;            /* observations that entered                  */
    /* want_stats — with s_t = (x_t, x_{t-1}, ..., x_{t-p}), np = n(p+1), and
       y~ = y - d; blocks of s_t that would reach before x_0 count as zero */
    double *S00, *S10, *S11;  /* n x n: sums over t = 1..T of E[x_{t-1}x_{t-1}'],
                                 E[x_t x_{t-1}'], E[x_t x_t']                */
    double *Syy;              /* m: sum over the periods series i entered of y~^2 */
    double *Sys;              /* m x np: row i, sum of y~_it E[s_t]'        */
    double *Sss;              /* np x (np m): block i, sum of E[s_t s_t']   */
    double *nT;               /* m: periods series i entered                */
    /* want_fit */
    double *yhat;             /* m x T: E[H s_t + d | Y] (NaN where a row reaches before x_0) */
    double *ysd;              /* m x T: sd of H s_t | Y, without the measurement noise (want_cov) */
    char msg[256];
} KsResult;

int  ks_smooth(const KsModel *M, KsResult *out);
void ks_free(KsResult *out);

#endif
