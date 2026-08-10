/* linalg_tier0.c — the tier-0 linear-algebra backend (design entry 2).
 *
 * The hand-rolled kernels inherited from Neutrino, moved verbatim from
 * eval.c behind the LinalgKernels table: pivoted Gaussian elimination
 * (solve, det), cyclic complex Jacobi (eig_herm), Hessenberg + shifted
 * complex QR + inverse iteration (eig_gen), one-sided Jacobi (svd), and
 * classical Cholesky (chol). Zero dependencies beyond libm — `make` on a
 * bare machine works forever; this file is also the wasm bundle's backend
 * (tier 2) until a CLAPACK build earns its way in.
 *
 * Contract discipline (see linalg.h): raw buffers in, status codes out.
 * Nothing here raises, prints, touches Values, or decides observable
 * conventions — pair ordering and eigenvector phase are eval.c's law.
 */
#include <stdlib.h>
#include <string.h>
#include "linalg.h"

/* ---- shared elimination core ------------------------------------------- */

/* Solve A x = b in place (A,b modified, x left in b); false if singular. */
static bool csolve_inplace(Cplx *A, uint32_t N, Cplx *b)
{
    for (uint32_t k = 0; k < N; k++) {
        uint32_t p = k; double best = c_abs(A[(size_t)k*N+k]);
        for (uint32_t i = k+1; i < N; i++) { double m = c_abs(A[(size_t)i*N+k]); if (m > best) { best = m; p = i; } }
        if (best < 1e-300) return false;
        if (p != k) {
            for (uint32_t j = 0; j < N; j++) { Cplx t = A[(size_t)k*N+j]; A[(size_t)k*N+j] = A[(size_t)p*N+j]; A[(size_t)p*N+j] = t; }
            Cplx t = b[k]; b[k] = b[p]; b[p] = t;
        }
        Cplx akk = A[(size_t)k*N+k];
        for (uint32_t i = k+1; i < N; i++) {
            Cplx f = c_div(A[(size_t)i*N+k], akk);
            for (uint32_t j = k+1; j < N; j++) A[(size_t)i*N+j] = c_sub(A[(size_t)i*N+j], c_mul(f, A[(size_t)k*N+j]));
            b[i] = c_sub(b[i], c_mul(f, b[k]));
        }
    }
    for (int64_t i = (int64_t)N-1; i >= 0; i--) {
        Cplx s = b[i];
        for (uint32_t j = (uint32_t)i+1; j < N; j++) s = c_sub(s, c_mul(A[(size_t)i*N+j], b[j]));
        b[i] = c_div(s, A[(size_t)i*N+i]);
    }
    return true;
}

/* ---- solve: A X = B, Gaussian elimination with partial pivoting --------- */

static int t0_solve(Cplx *LU, Cplx *X, uint32_t n, uint32_t m)
{
    for (uint32_t k = 0; k < n; k++) {
        uint32_t piv = k;
        double best = hypot(LU[(size_t)k*n+k].re, LU[(size_t)k*n+k].im);
        for (uint32_t i = k+1; i < n; i++) {
            double mag = hypot(LU[(size_t)i*n+k].re, LU[(size_t)i*n+k].im);
            if (mag > best) { best = mag; piv = i; }
        }
        if (best == 0.0) return 1;                            /* singular */
        if (piv != k) {
            for (uint32_t j = 0; j < n; j++) { Cplx t = LU[(size_t)k*n+j]; LU[(size_t)k*n+j] = LU[(size_t)piv*n+j]; LU[(size_t)piv*n+j] = t; }
            for (uint32_t j = 0; j < m; j++) { Cplx t = X[(size_t)k*m+j];  X[(size_t)k*m+j]  = X[(size_t)piv*m+j];  X[(size_t)piv*m+j]  = t; }
        }
        Cplx akk = LU[(size_t)k*n+k];
        for (uint32_t i = k+1; i < n; i++) {
            Cplx f = c_div(LU[(size_t)i*n+k], akk);
            for (uint32_t j = k; j < n; j++) LU[(size_t)i*n+j] = c_sub(LU[(size_t)i*n+j], c_mul(f, LU[(size_t)k*n+j]));
            for (uint32_t j = 0; j < m; j++) X[(size_t)i*m+j]  = c_sub(X[(size_t)i*m+j],  c_mul(f, X[(size_t)k*m+j]));
        }
    }
    for (uint32_t c = 0; c < m; c++)
        for (int64_t ii = (int64_t)n - 1; ii >= 0; ii--) {
            uint32_t i = (uint32_t)ii;
            Cplx s = X[(size_t)i*m+c];
            for (uint32_t j = i+1; j < n; j++) s = c_sub(s, c_mul(LU[(size_t)i*n+j], X[(size_t)j*m+c]));
            X[(size_t)i*m+c] = c_div(s, LU[(size_t)i*n+i]);
        }
    return 0;
}

/* ---- det: pivoted LU, product of the diagonal --------------------------- */

static int t0_det(Cplx *M, uint32_t N, Cplx *out)
{
    Cplx det = { 1.0, 0.0 };
    int sign = 1;
    for (uint32_t k = 0; k < N; k++) {
        uint32_t p = k; double best = hypot(M[(size_t)k*N+k].re, M[(size_t)k*N+k].im);
        for (uint32_t i = k+1; i < N; i++) {
            double mg = hypot(M[(size_t)i*N+k].re, M[(size_t)i*N+k].im);
            if (mg > best) { best = mg; p = i; }
        }
        if (best == 0.0) { *out = (Cplx){ 0.0, 0.0 }; return 0; }
        if (p != k) { for (uint32_t j = 0; j < N; j++) { Cplx t = M[(size_t)k*N+j]; M[(size_t)k*N+j] = M[(size_t)p*N+j]; M[(size_t)p*N+j] = t; } sign = -sign; }
        det = c_mul(det, M[(size_t)k*N+k]);
        for (uint32_t i = k+1; i < N; i++) {
            Cplx f = c_div(M[(size_t)i*N+k], M[(size_t)k*N+k]);
            for (uint32_t j = k+1; j < N; j++) M[(size_t)i*N+j] = c_sub(M[(size_t)i*N+j], c_mul(f, M[(size_t)k*N+j]));
        }
    }
    if (sign < 0) det = (Cplx){ -det.re, -det.im };
    *out = det;
    return 0;
}

/* ---- rotations ----------------------------------------------------------

 * complex Jacobi rotation that diagonalizes the Hermitian 2x2
 * [[app, apq],[apq*, aqq]]; yields a real cosine c and complex sine sp
 * with |c|^2 + |sp|^2 = 1. */
static void jacobi_rot(double app, double aqq, Cplx apq, double *c_out, Cplx *sp_out)
{
    double a = c_abs(apq);
    if (a == 0.0) { *c_out = 1.0; *sp_out = (Cplx){0,0}; return; }
    double tau = (aqq - app) / (2.0 * a);
    double t = (tau >= 0 ? 1.0 : -1.0) / (fabs(tau) + sqrt(tau*tau + 1.0));
    double cc = 1.0 / sqrt(t*t + 1.0), s = t * cc;
    *c_out = cc;
    *sp_out = c_scale(s / a, apq);          /* s * e^{i arg(apq)} */
}

/* Complex Givens rotation: (c real, s) such that [[c,s],[-conj(s),c]] [a;b] = [r;0]. */
static void c_givens(Cplx a, Cplx b, double *c, Cplx *s)
{
    double ab = c_abs(b);
    if (ab == 0.0) { *c = 1.0; *s = (Cplx){0,0}; return; }
    double aa = c_abs(a);
    if (aa == 0.0) { *c = 0.0; *s = c_scale(1.0/ab, c_conj(b)); return; }
    double t = hypot(aa, ab);
    *c = aa / t;
    *s = c_scale(1.0/(aa*t), c_mul(a, c_conj(b)));      /* (a/|a|)*conj(b)/t */
}

/* ---- eig_herm: cyclic complex Jacobi, accumulating vectors in V --------- */

static int t0_eig_herm(Cplx *A, uint32_t N, double *w, Cplx *V)
{
    memset(V, 0, (size_t)(N ? N*N : 1) * sizeof *V);
    for (uint32_t i = 0; i < N; i++) V[(size_t)i*N+i] = (Cplx){1,0};
    for (int sweep = 0; sweep < 100; sweep++) {
        double off = 0;
        for (uint32_t i = 0; i < N; i++) for (uint32_t j = i+1; j < N; j++) { double a = c_abs(A[(size_t)i*N+j]); off += a*a; }
        if (off < 1e-30) break;
        for (uint32_t p = 0; p < N; p++)
            for (uint32_t q = p+1; q < N; q++) {
                Cplx apq = A[(size_t)p*N+q];
                if (c_abs(apq) < 1e-300) continue;
                double app = A[(size_t)p*N+p].re, aqq = A[(size_t)q*N+q].re;
                double cc; Cplx sp; jacobi_rot(app, aqq, apq, &cc, &sp);
                for (uint32_t k = 0; k < N; k++) {     /* A <- A R */
                    Cplx kp = A[(size_t)k*N+p], kq = A[(size_t)k*N+q];
                    A[(size_t)k*N+p] = c_sub(c_scale(cc, kp), c_mul(c_conj(sp), kq));
                    A[(size_t)k*N+q] = c_add(c_mul(sp, kp), c_scale(cc, kq));
                }
                for (uint32_t k = 0; k < N; k++) {     /* A <- R^H A */
                    Cplx pk = A[(size_t)p*N+k], qk = A[(size_t)q*N+k];
                    A[(size_t)p*N+k] = c_sub(c_scale(cc, pk), c_mul(sp, qk));
                    A[(size_t)q*N+k] = c_add(c_mul(c_conj(sp), pk), c_scale(cc, qk));
                }
                for (uint32_t k = 0; k < N; k++) {     /* V <- V R (eigenvectors) */
                    Cplx kp = V[(size_t)k*N+p], kq = V[(size_t)k*N+q];
                    V[(size_t)k*N+p] = c_sub(c_scale(cc, kp), c_mul(c_conj(sp), kq));
                    V[(size_t)k*N+q] = c_add(c_mul(sp, kp), c_scale(cc, kq));
                }
            }
    }
    for (uint32_t i = 0; i < N; i++) w[i] = A[(size_t)i*N+i].re;
    return 0;
}

/* ---- eig_gen: Hessenberg + shifted complex QR, inverse iteration -------- */

/* Reduce A to upper Hessenberg form in place by Householder similarity. */
static void hessenberg(Cplx *A, uint32_t N)
{
    Cplx *v = malloc((N ? N : 1) * sizeof *v);
    for (uint32_t k = 0; k + 2 < N; k++) {
        double nrm = 0; for (uint32_t i = k+1; i < N; i++) { double a = c_abs(A[(size_t)i*N+k]); nrm += a*a; }
        nrm = sqrt(nrm);
        if (nrm == 0.0) continue;
        Cplx xk = A[(size_t)(k+1)*N+k]; double axk = c_abs(xk);
        Cplx alpha = axk > 0 ? c_scale(-nrm/axk, xk) : (Cplx){ -nrm, 0 };
        for (uint32_t i = 0; i < N; i++) v[i] = (Cplx){0,0};
        for (uint32_t i = k+1; i < N; i++) v[i] = A[(size_t)i*N+k];
        v[k+1] = c_sub(v[k+1], alpha);
        double vn2 = 0; for (uint32_t i = k+1; i < N; i++) { double a = c_abs(v[i]); vn2 += a*a; }
        if (vn2 == 0.0) continue;
        double beta = 2.0 / vn2;
        for (uint32_t j = 0; j < N; j++) {                 /* A <- (I - beta v v^H) A */
            Cplx w = {0,0}; for (uint32_t i = k+1; i < N; i++) w = c_add(w, c_mul(c_conj(v[i]), A[(size_t)i*N+j]));
            w = c_scale(beta, w);
            for (uint32_t i = k+1; i < N; i++) A[(size_t)i*N+j] = c_sub(A[(size_t)i*N+j], c_mul(v[i], w));
        }
        for (uint32_t i = 0; i < N; i++) {                 /* A <- A (I - beta v v^H) */
            Cplx w = {0,0}; for (uint32_t j = k+1; j < N; j++) w = c_add(w, c_mul(A[(size_t)i*N+j], v[j]));
            w = c_scale(beta, w);
            for (uint32_t j = k+1; j < N; j++) A[(size_t)i*N+j] = c_sub(A[(size_t)i*N+j], c_mul(w, c_conj(v[j])));
        }
    }
    free(v);
}

/* Eigenvalues of a (Hessenberg) matrix H by shifted complex QR; writes N eigenvalues to eig. */
static void qr_eig(Cplx *H, uint32_t N, Cplx *eig)
{
    hessenberg(H, N);
    double *cs = malloc((N ? N : 1) * sizeof *cs);
    Cplx   *sn = malloc((N ? N : 1) * sizeof *sn);
    uint32_t hi = N;
    int iters = 0;
    while (hi > 0) {
        uint32_t lo = hi - 1;                               /* find bottom block [lo, hi) */
        while (lo > 0) {
            double s = c_abs(H[(size_t)(lo-1)*N+(lo-1)]) + c_abs(H[(size_t)lo*N+lo]);
            if (s == 0.0) s = 1.0;
            if (c_abs(H[(size_t)lo*N+(lo-1)]) <= 1e-15 * s) break;
            lo--;
        }
        if (lo == hi - 1) { eig[hi-1] = H[(size_t)(hi-1)*N+(hi-1)]; hi--; iters = 0; continue; }
        if (++iters > 300) {                                /* give up: accept the diagonal */
            for (uint32_t i = lo; i < hi; i++) eig[i] = H[(size_t)i*N+i];
            hi = lo; iters = 0; continue;
        }
        Cplx a = H[(size_t)(hi-2)*N+(hi-2)], b = H[(size_t)(hi-2)*N+(hi-1)];
        Cplx cc2 = H[(size_t)(hi-1)*N+(hi-2)], d = H[(size_t)(hi-1)*N+(hi-1)];
        Cplx tr = c_add(a, d), det = c_sub(c_mul(a, d), c_mul(b, cc2));
        Cplx disc = c_sqrtz(c_sub(c_mul(tr, tr), c_scale(4.0, det)));
        Cplx mu1 = c_scale(0.5, c_add(tr, disc)), mu2 = c_scale(0.5, c_sub(tr, disc));
        Cplx mu = c_abs(c_sub(mu1, d)) < c_abs(c_sub(mu2, d)) ? mu1 : mu2;
        for (uint32_t i = lo; i < hi; i++) H[(size_t)i*N+i] = c_sub(H[(size_t)i*N+i], mu);   /* H - muI */
        for (uint32_t i = lo; i + 1 < hi; i++) {            /* QR: zero subdiagonals -> R */
            double c; Cplx s; c_givens(H[(size_t)i*N+i], H[(size_t)(i+1)*N+i], &c, &s);
            cs[i] = c; sn[i] = s;
            for (uint32_t j = i; j < hi; j++) {
                Cplx hi_ = H[(size_t)i*N+j], hj_ = H[(size_t)(i+1)*N+j];
                H[(size_t)i*N+j]     = c_add(c_scale(c, hi_), c_mul(s, hj_));
                H[(size_t)(i+1)*N+j] = c_sub(c_scale(c, hj_), c_mul(c_conj(s), hi_));
            }
        }
        for (uint32_t i = lo; i + 1 < hi; i++) {            /* H <- R Q (postmultiply by G^H) */
            double c = cs[i]; Cplx s = sn[i];
            uint32_t rmax = i + 2 < hi ? i + 2 : hi;
            for (uint32_t r = lo; r < rmax; r++) {
                Cplx mi = H[(size_t)r*N+i], mj = H[(size_t)r*N+(i+1)];
                H[(size_t)r*N+i]     = c_add(c_scale(c, mi), c_mul(c_conj(s), mj));
                H[(size_t)r*N+(i+1)] = c_sub(c_scale(c, mj), c_mul(s, mi));
            }
        }
        for (uint32_t i = lo; i < hi; i++) H[(size_t)i*N+i] = c_add(H[(size_t)i*N+i], mu);   /* + muI */
    }
    free(cs); free(sn);
}

static int t0_eig_gen(const Cplx *A0, uint32_t N, Cplx *w, Cplx *V)
{
    size_t cells = (size_t)(N ? N*N : 1);
    Cplx *H = malloc(cells * sizeof *H);                     /* consumed by qr_eig */
    Cplx *M = malloc(cells * sizeof *M);
    Cplx *x = malloc((N ? N : 1) * sizeof *x);
    memcpy(H, A0, cells * sizeof *H);
    qr_eig(H, N, w);

    for (uint32_t j = 0; j < N; j++) {                       /* one vector per value */
        Cplx lam = w[j];
        double shift = 1e-8 * (1.0 + c_abs(lam));            /* perturb off the exact eigenvalue */
        Cplx mu = { lam.re + shift, lam.im + shift };
        for (uint32_t i = 0; i < N; i++) x[i] = (Cplx){ 1.0 / (i + 1.0), 0 };   /* start vector */
        for (int it = 0; it < 6; it++) {
            memcpy(M, A0, (size_t)N*N * sizeof *M);
            for (uint32_t i = 0; i < N; i++) M[(size_t)i*N+i] = c_sub(M[(size_t)i*N+i], mu);
            if (!csolve_inplace(M, N, x)) break;             /* x <- (A - mu I)^{-1} x */
            double nrm = 0; for (uint32_t i = 0; i < N; i++) { double a = c_abs(x[i]); nrm += a*a; }
            nrm = sqrt(nrm);
            if (nrm == 0) break;
            for (uint32_t i = 0; i < N; i++) x[i] = c_scale(1.0/nrm, x[i]);
        }
        for (uint32_t i = 0; i < N; i++) V[(size_t)i*N+j] = x[i];
    }
    free(H); free(M); free(x);
    return 0;
}

/* ---- svd: thin SVD via one-sided Jacobi (S descending) ------------------
 * Works on the tall orientation; for m<n it factors A^H and swaps U,V. */

static int t0_svd(const Cplx *Araw, uint32_t m, uint32_t nc,
                  Cplx *Uout, double *sout, Cplx *Vout)
{
    bool tr = m < nc;
    uint32_t M = tr ? nc : m, N = tr ? m : nc;               /* work tall: M >= N */
    Cplx *U = malloc((size_t)(M ? M : 1) * (N ? N : 1) * sizeof *U);
    if (!tr) for (uint32_t i = 0; i < M; i++) for (uint32_t j = 0; j < N; j++) U[(size_t)i*N+j] = Araw[(size_t)i*nc+j];
    else     for (uint32_t i = 0; i < M; i++) for (uint32_t j = 0; j < N; j++) U[(size_t)i*N+j] = c_conj(Araw[(size_t)j*nc+i]);
    Cplx *V = calloc((size_t)(N ? N*N : 1), sizeof *V);
    for (uint32_t i = 0; i < N; i++) V[(size_t)i*N+i] = (Cplx){1,0};

    for (int sweep = 0; sweep < 60; sweep++) {
        double off = 0;
        for (uint32_t p = 0; p < N; p++)
            for (uint32_t q = p+1; q < N; q++) {
                double app = 0, aqq = 0; Cplx apq = {0,0};
                for (uint32_t k = 0; k < M; k++) {
                    Cplx up = U[(size_t)k*N+p], uq = U[(size_t)k*N+q];
                    app += up.re*up.re + up.im*up.im;
                    aqq += uq.re*uq.re + uq.im*uq.im;
                    apq = c_add(apq, c_mul(c_conj(up), uq));
                }
                double a = c_abs(apq); off += a*a;
                if (a < 1e-300) continue;
                double cc; Cplx sp; jacobi_rot(app, aqq, apq, &cc, &sp);
                for (uint32_t k = 0; k < M; k++) {
                    Cplx kp = U[(size_t)k*N+p], kq = U[(size_t)k*N+q];
                    U[(size_t)k*N+p] = c_sub(c_scale(cc, kp), c_mul(c_conj(sp), kq));
                    U[(size_t)k*N+q] = c_add(c_mul(sp, kp), c_scale(cc, kq));
                }
                for (uint32_t k = 0; k < N; k++) {
                    Cplx kp = V[(size_t)k*N+p], kq = V[(size_t)k*N+q];
                    V[(size_t)k*N+p] = c_sub(c_scale(cc, kp), c_mul(c_conj(sp), kq));
                    V[(size_t)k*N+q] = c_add(c_mul(sp, kp), c_scale(cc, kq));
                }
            }
        if (off < 1e-28) break;
    }
    double *sv = malloc((N ? N : 1) * sizeof *sv);
    for (uint32_t j = 0; j < N; j++) {
        double s = 0; for (uint32_t k = 0; k < M; k++) { Cplx u = U[(size_t)k*N+j]; s += u.re*u.re + u.im*u.im; }
        sv[j] = sqrt(s);
        if (sv[j] > 0) for (uint32_t k = 0; k < M; k++) U[(size_t)k*N+j] = c_div(U[(size_t)k*N+j], (Cplx){ sv[j], 0 });
    }
    uint32_t *ord = malloc((N ? N : 1) * sizeof *ord);
    for (uint32_t i = 0; i < N; i++) ord[i] = i;
    for (uint32_t i = 1; i < N; i++) { uint32_t o = ord[i]; double x = sv[o]; uint32_t j = i; while (j > 0 && sv[ord[j-1]] < x) { ord[j] = ord[j-1]; j--; } ord[j] = o; }

    /* Deliver in contract orientation: U_out m×k, s_out k descending, V_out n×k
     * (k = min(m, nc)). In the transposed case the working U/V swap roles. */
    Cplx *Ud = tr ? Vout : Uout;                             /* destination for working U (M×N) */
    Cplx *Vd = tr ? Uout : Vout;                             /* destination for working V (N×N) */
    for (uint32_t jj = 0; jj < N; jj++) {
        uint32_t src = ord[jj]; sout[jj] = sv[src];
        for (uint32_t k = 0; k < M; k++) Ud[(size_t)k*N+jj] = U[(size_t)k*N+src];
        for (uint32_t k = 0; k < N; k++) Vd[(size_t)k*N+jj] = V[(size_t)k*N+src];
    }
    free(U); free(V); free(sv); free(ord);
    return 0;
}

/* ---- chol: classical Cholesky, lower L with L L^H = A ------------------- */

static int t0_chol(const Cplx *A, uint32_t N, Cplx *L)
{
    memset(L, 0, (size_t)(N ? N*N : 1) * sizeof *L);
    for (uint32_t j = 0; j < N; j++) {
        double d = A[(size_t)j*N+j].re;
        for (uint32_t k = 0; k < j; k++) { double a = c_abs(L[(size_t)j*N+k]); d -= a*a; }
        if (d <= 0.0) return 1;                              /* not positive definite */
        double Ljj = sqrt(d);
        L[(size_t)j*N+j] = (Cplx){ Ljj, 0 };
        for (uint32_t i = j+1; i < N; i++) {
            Cplx s = A[(size_t)i*N+j];
            for (uint32_t k = 0; k < j; k++) s = c_sub(s, c_mul(L[(size_t)i*N+k], c_conj(L[(size_t)j*N+k])));
            L[(size_t)i*N+j] = c_div(s, (Cplx){ Ljj, 0 });
        }
    }
    return 0;
}

/* ---- the table ---------------------------------------------------------- */

static int t0_solve_d(double *A, double *B, uint32_t n, uint32_t m)
{
    Cplx *a = malloc((size_t)n * n * sizeof *a);
    Cplx *b = malloc((size_t)n * m * sizeof *b);
    if ((!a && n) || (!b && n && m)) abort();
    for (size_t k = 0; k < (size_t)n * n; k++) a[k] = (Cplx){ A[k], 0 };
    for (size_t k = 0; k < (size_t)n * m; k++) b[k] = (Cplx){ B[k], 0 };
    int rc = t0_solve(a, b, n, m);
    if (rc == 0) for (size_t k = 0; k < (size_t)n * m; k++) B[k] = b[k].re;
    free(a); free(b);
    return rc;
}
static int t0_det_d(double *A, uint32_t n, double *out)
{
    Cplx *a = malloc((size_t)n * n * sizeof *a);
    if (!a && n) abort();
    for (size_t k = 0; k < (size_t)n * n; k++) a[k] = (Cplx){ A[k], 0 };
    Cplx d;
    int rc = t0_det(a, n, &d);
    free(a);
    *out = d.re;
    return rc;
}

static const LinalgKernels tier0 = {
    .name     = "tier0",
    .solve    = t0_solve,
    .solve_d  = t0_solve_d,
    .det_d    = t0_det_d,
    .det      = t0_det,
    .eig_herm = t0_eig_herm,
    .eig_gen  = t0_eig_gen,
    .svd      = t0_svd,
    .chol     = t0_chol,
};

const LinalgKernels *cozy_linalg(void) { return &tier0; }
