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

/* ---- isolating eigenvalues by permutation -------------------------------
 * The permuting half of LAPACK's balancing (dgebal). If a row is zero off
 * the diagonal, its diagonal entry is an eigenvalue and the row can go to
 * the bottom; likewise a column, to the left. Repeating this on what is
 * left gives a reordering in which the matrix is
 *
 *     [ T1  X   Y  ]
 *     [ 0   B   Z  ]     T1, T2 upper triangular
 *     [ 0   0   T2 ]
 *
 * so the eigenvalues on T1 and T2 are read off exactly and the QR iteration
 * only ever works on B: the zeros below T1's and T2's diagonals make those
 * blocks deflate at once. Without this a triangular matrix written the
 * "wrong" way round (lower instead of upper) goes through the full
 * iteration, and if it is far from normal its eigenvalues come back with
 * errors many orders above rounding. nz is the n x n pattern of nonzeros;
 * perm[k] receives the original index that goes to position k. */
static void isolate_perm(const unsigned char *nz, uint32_t n, uint32_t *perm)
{
    for (uint32_t i = 0; i < n; i++) perm[i] = i;
    uint32_t lo = 0, hi = n;
    for (bool moved = true; moved && hi - lo > 1; ) {
        moved = false;
        for (uint32_t j = hi; j-- > lo; ) {                 /* a row with nothing off the diagonal */
            bool alone = true;
            for (uint32_t k = lo; k < hi && alone; k++)
                if (k != j && nz[(size_t)perm[j] * n + perm[k]]) alone = false;
            if (alone) { uint32_t t = perm[j]; perm[j] = perm[hi - 1]; perm[hi - 1] = t; hi--; moved = true; break; }
        }
        if (moved) continue;
        for (uint32_t j = lo; j < hi; j++) {                /* a column with nothing off the diagonal */
            bool alone = true;
            for (uint32_t k = lo; k < hi && alone; k++)
                if (k != j && nz[(size_t)perm[k] * n + perm[j]]) alone = false;
            if (alone) { uint32_t t = perm[j]; perm[j] = perm[lo]; perm[lo] = t; lo++; moved = true; break; }
        }
    }
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

/* Eigenvalues of a (Hessenberg) matrix H by shifted complex QR; writes N
 * eigenvalues to eig. Returns false if a block did not converge. */
static bool qr_eig(Cplx *H, uint32_t N, Cplx *eig)
{
    hessenberg(H, N);
    double *cs = malloc((N ? N : 1) * sizeof *cs);
    Cplx   *sn = malloc((N ? N : 1) * sizeof *sn);
    uint32_t hi = N;
    int iters = 0;
    bool converged = true;
    while (hi > 0) {
        uint32_t lo = hi - 1;                               /* find bottom block [lo, hi) */
        while (lo > 0) {
            double s = c_abs(H[(size_t)(lo-1)*N+(lo-1)]) + c_abs(H[(size_t)lo*N+lo]);
            if (s == 0.0) s = 1.0;
            if (c_abs(H[(size_t)lo*N+(lo-1)]) <= 1e-15 * s) break;
            lo--;
        }
        if (lo == hi - 1) { eig[hi-1] = H[(size_t)(hi-1)*N+(hi-1)]; hi--; iters = 0; continue; }
        if (++iters > 300) {                                /* give up, and say so: the diagonal */
            for (uint32_t i = lo; i < hi; i++) eig[i] = H[(size_t)i*N+i];   /* is not the spectrum */
            hi = lo; iters = 0; converged = false; continue;
        }
        Cplx a = H[(size_t)(hi-2)*N+(hi-2)], b = H[(size_t)(hi-2)*N+(hi-1)];
        Cplx cc2 = H[(size_t)(hi-1)*N+(hi-2)], d = H[(size_t)(hi-1)*N+(hi-1)];
        Cplx tr = c_add(a, d), det = c_sub(c_mul(a, d), c_mul(b, cc2));
        Cplx disc = c_sqrtz(c_sub(c_mul(tr, tr), c_scale(4.0, det)));
        Cplx mu1 = c_scale(0.5, c_add(tr, disc)), mu2 = c_scale(0.5, c_sub(tr, disc));
        Cplx mu = c_abs(c_sub(mu1, d)) < c_abs(c_sub(mu2, d)) ? mu1 : mu2;
        /* An exceptional shift every tenth step. With the usual shift the
         * step can be a fixed point: a cyclic shift matrix has a zero shift
         * and is mapped to itself, and the loop used to run out and return
         * its all-zero diagonal as the eigenvalues. */
        if (iters % 10 == 0)
            mu = (Cplx){ d.re + c_abs(cc2) + (hi - lo > 2 ? c_abs(H[(size_t)(hi-2)*N+(hi-3)]) : 0.0), d.im };
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
    return converged;
}

static int t0_eig_gen(const Cplx *A0, uint32_t N, Cplx *w, Cplx *V)
{
    size_t cells = (size_t)(N ? N*N : 1);
    Cplx *H = malloc(cells * sizeof *H);                     /* consumed by qr_eig */
    Cplx *M = malloc(cells * sizeof *M);
    Cplx *x = malloc((N ? N : 1) * sizeof *x);
    {                                                        /* H = P^T A0 P, eigenvalues isolated */
        unsigned char *nz = calloc(cells, 1);
        uint32_t *perm = malloc((N ? N : 1) * sizeof *perm);
        for (size_t q = 0; q < (size_t)N * N; q++) nz[q] = A0[q].re != 0.0 || A0[q].im != 0.0;
        isolate_perm(nz, N, perm);
        for (uint32_t i = 0; i < N; i++)
            for (uint32_t j = 0; j < N; j++) H[(size_t)i * N + j] = A0[(size_t)perm[i] * N + perm[j]];
        free(nz); free(perm);
    }
    if (!qr_eig(H, N, w)) { free(H); free(M); free(x); return 1; }

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

/* ---- schur: Hessenberg reduction and shifted QR, transformations kept ---- */

/* The real Schur form. A = U T U^T with U orthogonal and T quasi-upper-
 * triangular: 1x1 blocks for real eigenvalues, 2x2 blocks for complex-
 * conjugate pairs. Everything is real arithmetic: a real matrix with complex
 * eigenvalues has no real triangular form, and the 2x2 blocks are what stands
 * in for it.
 *
 * Three stages, all accumulated into U: Householder reduction to Hessenberg
 * form; Francis's implicit double-shift QR iteration (Golub and Van Loan,
 * Matrix Computations, alg. 7.5.1), which chases a 3x3 bulge down the active
 * block until a subdiagonal entry is negligible and the block splits; and a
 * rotation of each 2x2 block that splits it if its eigenvalues are real, or
 * else puts it in LAPACK's standard form (equal diagonal entries,
 * off-diagonal entries of opposite sign). */

#define RS(i, j) T[(size_t)(i) * n + (j)]
#define RU(i, j) U[(size_t)(i) * n + (j)]

/* A plane rotation G = [cs -sn; sn cs] in the (i, i+1) plane, applied as a
 * similarity: T <- G^T T G, U <- U G. */
static void rs_rotate(double *T, double *U, uint32_t n, uint32_t i, double cs, double sn)
{
    for (uint32_t j = 0; j < n; j++) {                      /* rows i, i+1 */
        double a = RS(i, j), b = RS(i + 1, j);
        RS(i, j) = cs * a + sn * b; RS(i + 1, j) = cs * b - sn * a;
    }
    for (uint32_t r = 0; r < n; r++) {                      /* columns i, i+1 */
        double a = RS(r, i), b = RS(r, i + 1);
        RS(r, i) = cs * a + sn * b; RS(r, i + 1) = cs * b - sn * a;
        a = RU(r, i); b = RU(r, i + 1);
        RU(r, i) = cs * a + sn * b; RU(r, i + 1) = cs * b - sn * a;
    }
}

/* The 2x2 block at (l, l): triangular if its eigenvalues are real, standard
 * form if they are complex. */
static void rs_standardize(double *T, double *U, uint32_t n, uint32_t l)
{
    double a = RS(l, l), b = RS(l, l + 1), c = RS(l + 1, l), d = RS(l + 1, l + 1);
    if (c == 0.0) return;
    double p = 0.5 * (a - d), bcmax = fmax(fabs(b), fabs(c));
    double bcmis = fmin(fabs(b), fabs(c)) * (b < 0 ? -1.0 : 1.0) * (c < 0 ? -1.0 : 1.0);
    double scale = fmax(fabs(p), bcmax);
    double z = p / scale * p + bcmax / scale * bcmis;       /* (p^2 + bc) / scale, without overflow */
    if (z >= 0.0) {
        /* real eigenvalues: rotate an eigenvector into the first position */
        double root = sqrt(scale) * sqrt(z);
        double zz = p + (p < 0 ? -root : root);             /* eigenvalue d + zz, no cancellation */
        double r = hypot(zz, c);
        rs_rotate(T, U, n, l, zz / r, c / r);
        RS(l + 1, l) = 0.0;
        return;
    }
    /* complex pair: the rotation that makes the diagonal entries equal */
    if (a == d) return;                                     /* they already are */
    double sigma = b + c, tau = hypot(sigma, a - d);
    double cs = sqrt(0.5 * (1.0 + fabs(sigma) / tau));
    double sn = -(p / (tau * cs)) * (sigma < 0 ? -1.0 : 1.0);
    rs_rotate(T, U, n, l, cs, sn);
    double mean = 0.5 * (RS(l, l) + RS(l + 1, l + 1));
    RS(l, l) = mean; RS(l + 1, l + 1) = mean;
    b = RS(l, l + 1); c = RS(l + 1, l);
    if (c != 0.0 && b * c >= 0.0) {
        /* rounding made the pair real after all (a double eigenvalue): the
         * block [mean b; c mean] has the eigenvector (sqrt|b|, +-sqrt|c|) */
        double sb = sqrt(fabs(b)), sc = c < 0 ? -sqrt(fabs(c)) : sqrt(fabs(c));
        double r = hypot(sb, sc);
        rs_rotate(T, U, n, l, sb / r, sc / r);
        RS(l + 1, l) = 0.0;
    }
}

/* v, beta with (I - beta v v^T) x = (alpha, 0, ...); returns false for x = 0.
 * Only the direction of x matters to the reflector, so x is scaled by its
 * largest entry first: its squares then neither overflow nor underflow. */
static bool rs_house(const double *x, uint32_t m, double *v, double *beta)
{
    double nrm = 0.0, big = 0.0;
    for (uint32_t i = 0; i < m; i++) big = fmax(big, fabs(x[i]));
    if (big == 0.0) return false;
    for (uint32_t i = 0; i < m; i++) { v[i] = x[i] / big; nrm += v[i] * v[i]; }
    nrm = sqrt(nrm);
    v[0] -= v[0] < 0 ? nrm : -nrm;
    double vv = 0.0;
    for (uint32_t i = 0; i < m; i++) vv += v[i] * v[i];
    *beta = 2.0 / vv;
    return true;
}

static int t0_schur_d(const double *A, uint32_t n, double *T, double *U)
{
    memcpy(T, A, (size_t)n * n * sizeof *T);
    for (uint32_t i = 0; i < n; i++) for (uint32_t j = 0; j < n; j++) RU(i, j) = (i == j);
    if (n < 2) return 0;
    /* work on P^T A P with its eigenvalues isolated; U's rows go back at the end */
    unsigned char *nz = malloc((size_t)n * n);
    uint32_t *perm = malloc(n * sizeof *perm);
    for (size_t q = 0; q < (size_t)n * n; q++) nz[q] = A[q] != 0.0;
    isolate_perm(nz, n, perm);
    for (uint32_t i = 0; i < n; i++) for (uint32_t j = 0; j < n; j++) RS(i, j) = A[(size_t)perm[i] * n + perm[j]];
    free(nz);
    /* the iteration multiplies entries pairwise: a matrix of extreme size is
     * brought to size one first and T scaled back at the end */
    double amax = 0.0, unit = 1.0;
    for (size_t q = 0; q < (size_t)n * n; q++) amax = fmax(amax, fabs(T[q]));
    if (amax > 0.0 && (amax < 1e-100 || amax > 1e100)) {
        unit = amax;
        for (size_t q = 0; q < (size_t)n * n; q++) T[q] /= unit;
    }
    double *v = malloc(n * sizeof *v), *x = malloc(n * sizeof *x);

    /* stage 1: Hessenberg form */
    for (uint32_t k = 0; k + 2 < n; k++) {
        uint32_t m = n - k - 1;
        double beta;
        for (uint32_t i = 0; i < m; i++) x[i] = RS(k + 1 + i, k);
        if (!rs_house(x, m, v, &beta)) continue;
        for (uint32_t j = 0; j < n; j++) {                  /* T <- P T */
            double s = 0.0;
            for (uint32_t i = 0; i < m; i++) s += v[i] * RS(k + 1 + i, j);
            s *= beta;
            for (uint32_t i = 0; i < m; i++) RS(k + 1 + i, j) -= s * v[i];
        }
        for (uint32_t r = 0; r < n; r++) {                  /* T <- T P, U <- U P */
            double s = 0.0, t = 0.0;
            for (uint32_t i = 0; i < m; i++) { s += RS(r, k + 1 + i) * v[i]; t += RU(r, k + 1 + i) * v[i]; }
            s *= beta; t *= beta;
            for (uint32_t i = 0; i < m; i++) { RS(r, k + 1 + i) -= s * v[i]; RU(r, k + 1 + i) -= t * v[i]; }
        }
        for (uint32_t i = k + 2; i < n; i++) RS(i, k) = 0.0;
    }
    free(v); free(x);

    /* stage 2: QR iteration on the active block [l, hi] */
    double norm = 0.0;
    for (size_t q = 0; q < (size_t)n * n; q++) norm = fmax(norm, fabs(T[q]));
    const double eps = 2.220446049250313e-16;
    uint32_t hi = n - 1;
    int iter = 0, rc = 0;
    while (hi >= 1) {
        uint32_t l = hi;                                    /* the block ends where a subdiagonal vanishes */
        while (l > 0) {
            double s = fabs(RS(l - 1, l - 1)) + fabs(RS(l, l));
            if (s == 0.0) s = norm;
            if (fabs(RS(l, l - 1)) <= eps * s) { RS(l, l - 1) = 0.0; break; }
            l--;
        }
        if (l == hi) { hi--; iter = 0; continue; }          /* a 1x1 block has split off */
        if (l + 1 == hi) {                                   /* a 2x2 block has split off */
            rs_standardize(T, U, n, l);
            if (hi < 2) break;
            hi -= 2; iter = 0; continue;
        }
        if (++iter > 60) { rc = 1; break; }

        /* the shifts are the eigenvalues of the trailing 2x2; s is their sum, t their product */
        double s = RS(hi - 1, hi - 1) + RS(hi, hi);
        double t = RS(hi - 1, hi - 1) * RS(hi, hi) - RS(hi - 1, hi) * RS(hi, hi - 1);
        if (iter == 10 || iter == 20 || iter == 30 || iter == 40 || iter == 50) {
            /* an exceptional shift, to break a cycle (EISPACK's) */
            double w = fabs(RS(hi, hi - 1)) + fabs(RS(hi - 1, hi - 2));
            s = 1.5 * w; t = w * w;
        }
        /* first column of (T - aI)(T - bI) */
        double x = RS(l, l) * RS(l, l) + RS(l, l + 1) * RS(l + 1, l) - s * RS(l, l) + t;
        double y = RS(l + 1, l) * (RS(l, l) + RS(l + 1, l + 1) - s);
        double z = RS(l + 1, l) * RS(l + 2, l + 1);
        for (uint32_t k = l; k + 2 <= hi; k++) {            /* chase the bulge */
            double xv[3] = { x, y, z }, hv[3], beta;
            if (rs_house(xv, 3, hv, &beta)) {
                uint32_t c0 = k > l ? k - 1 : l;
                for (uint32_t j = c0; j < n; j++) {
                    double sm = beta * (hv[0] * RS(k, j) + hv[1] * RS(k + 1, j) + hv[2] * RS(k + 2, j));
                    RS(k, j) -= sm * hv[0]; RS(k + 1, j) -= sm * hv[1]; RS(k + 2, j) -= sm * hv[2];
                }
                uint32_t rmax = k + 3 < hi ? k + 3 : hi;
                for (uint32_t r = 0; r <= rmax; r++) {
                    double sm = beta * (RS(r, k) * hv[0] + RS(r, k + 1) * hv[1] + RS(r, k + 2) * hv[2]);
                    RS(r, k) -= sm * hv[0]; RS(r, k + 1) -= sm * hv[1]; RS(r, k + 2) -= sm * hv[2];
                }
                for (uint32_t r = 0; r < n; r++) {
                    double sm = beta * (RU(r, k) * hv[0] + RU(r, k + 1) * hv[1] + RU(r, k + 2) * hv[2]);
                    RU(r, k) -= sm * hv[0]; RU(r, k + 1) -= sm * hv[1]; RU(r, k + 2) -= sm * hv[2];
                }
            }
            if (k > l) { RS(k + 1, k - 1) = 0.0; RS(k + 2, k - 1) = 0.0; }
            x = RS(k + 1, k); y = RS(k + 2, k);
            if (k + 3 <= hi) z = RS(k + 3, k);
        }
        {                                                    /* the last reflector is 2x2 */
            double xv[2] = { x, y }, hv[2], beta;
            if (rs_house(xv, 2, hv, &beta)) {
                for (uint32_t j = hi - 2; j < n; j++) {
                    double sm = beta * (hv[0] * RS(hi - 1, j) + hv[1] * RS(hi, j));
                    RS(hi - 1, j) -= sm * hv[0]; RS(hi, j) -= sm * hv[1];
                }
                for (uint32_t r = 0; r <= hi; r++) {
                    double sm = beta * (RS(r, hi - 1) * hv[0] + RS(r, hi) * hv[1]);
                    RS(r, hi - 1) -= sm * hv[0]; RS(r, hi) -= sm * hv[1];
                }
                for (uint32_t r = 0; r < n; r++) {
                    double sm = beta * (RU(r, hi - 1) * hv[0] + RU(r, hi) * hv[1]);
                    RU(r, hi - 1) -= sm * hv[0]; RU(r, hi) -= sm * hv[1];
                }
            }
            RS(hi, hi - 2) = 0.0;
        }
    }
    if (unit != 1.0) for (size_t q = 0; q < (size_t)n * n; q++) T[q] *= unit;
    {                                                        /* A = (P U) T (P U)^T */
        double *W = malloc((size_t)n * n * sizeof *W);
        memcpy(W, U, (size_t)n * n * sizeof *W);
        for (uint32_t i = 0; i < n; i++) memcpy(&RU(perm[i], 0), &W[(size_t)i * n], n * sizeof *W);
        free(W);
    }
    free(perm);
    return rc;
}
#undef RS
#undef RU

/* The complex Schur form: A = U T U^H, U unitary, T upper triangular.
 * Householder reduction to Hessenberg form, then QR steps with Wilkinson's
 * shift on the active block; each rotation is also applied to the rest of
 * its rows and columns and to U, so that T stays similar to A. */
static int t0_schur(const Cplx *A, uint32_t n, Cplx *T, Cplx *U)
{
#define ZS(i, j) T[(size_t)(i) * n + (j)]
#define ZU(i, j) U[(size_t)(i) * n + (j)]
    memcpy(T, A, (size_t)n * n * sizeof *T);
    for (uint32_t i = 0; i < n; i++) for (uint32_t j = 0; j < n; j++) ZU(i, j) = (Cplx){ i == j, 0 };
    if (n < 2) return 0;
    unsigned char *nz = malloc((size_t)n * n);               /* as in the real kernel */
    uint32_t *perm = malloc(n * sizeof *perm);
    for (size_t q = 0; q < (size_t)n * n; q++) nz[q] = A[q].re != 0.0 || A[q].im != 0.0;
    isolate_perm(nz, n, perm);
    for (uint32_t i = 0; i < n; i++) for (uint32_t j = 0; j < n; j++) ZS(i, j) = A[(size_t)perm[i] * n + perm[j]];
    free(nz);
    double amax = 0.0, unit = 1.0;                           /* as in the real kernel */
    for (size_t q = 0; q < (size_t)n * n; q++) amax = fmax(amax, fmax(fabs(T[q].re), fabs(T[q].im)));
    if (amax > 0.0 && (amax < 1e-100 || amax > 1e100)) {
        unit = amax;
        for (size_t q = 0; q < (size_t)n * n; q++) T[q] = c_scale(1.0 / unit, T[q]);
    }
    Cplx *v = malloc(n * sizeof *v), *sn = malloc(n * sizeof *sn);
    double *cs = malloc(n * sizeof *cs);

    for (uint32_t k = 0; k + 2 < n; k++) {                   /* Hessenberg form */
        double nrm = 0.0;
        for (uint32_t i = k + 1; i < n; i++) { double a = c_abs(ZS(i, k)); nrm += a * a; }
        nrm = sqrt(nrm);
        if (nrm == 0.0) continue;
        Cplx xk = ZS(k + 1, k); double axk = c_abs(xk);
        Cplx alpha = axk > 0 ? c_scale(-nrm / axk, xk) : (Cplx){ -nrm, 0 };
        for (uint32_t i = k + 1; i < n; i++) v[i] = ZS(i, k);
        v[k + 1] = c_sub(v[k + 1], alpha);
        double vn2 = 0.0;
        for (uint32_t i = k + 1; i < n; i++) { double a = c_abs(v[i]); vn2 += a * a; }
        if (vn2 == 0.0) continue;
        double beta = 2.0 / vn2;
        for (uint32_t j = 0; j < n; j++) {                   /* T <- P T, P = I - beta v v^H */
            Cplx w = { 0, 0 };
            for (uint32_t i = k + 1; i < n; i++) w = c_add(w, c_mul(c_conj(v[i]), ZS(i, j)));
            w = c_scale(beta, w);
            for (uint32_t i = k + 1; i < n; i++) ZS(i, j) = c_sub(ZS(i, j), c_mul(v[i], w));
        }
        for (uint32_t r = 0; r < n; r++) {                   /* T <- T P, U <- U P */
            Cplx w = { 0, 0 }, u = { 0, 0 };
            for (uint32_t j = k + 1; j < n; j++) { w = c_add(w, c_mul(ZS(r, j), v[j])); u = c_add(u, c_mul(ZU(r, j), v[j])); }
            w = c_scale(beta, w); u = c_scale(beta, u);
            for (uint32_t j = k + 1; j < n; j++) {
                ZS(r, j) = c_sub(ZS(r, j), c_mul(w, c_conj(v[j])));
                ZU(r, j) = c_sub(ZU(r, j), c_mul(u, c_conj(v[j])));
            }
        }
        for (uint32_t i = k + 2; i < n; i++) ZS(i, k) = (Cplx){ 0, 0 };
    }

    double norm = 0.0;
    for (size_t q = 0; q < (size_t)n * n; q++) norm = fmax(norm, c_abs(T[q]));
    const double eps = 2.220446049250313e-16;
    uint32_t hi = n - 1;
    int iter = 0, rc = 0;
    while (hi >= 1) {
        uint32_t l = hi;
        while (l > 0) {
            double s = c_abs(ZS(l - 1, l - 1)) + c_abs(ZS(l, l));
            if (s == 0.0) s = norm;
            if (c_abs(ZS(l, l - 1)) <= eps * s) { ZS(l, l - 1) = (Cplx){ 0, 0 }; break; }
            l--;
        }
        if (l == hi) { hi--; iter = 0; continue; }
        if (++iter > 60) { rc = 1; break; }
        /* Wilkinson's shift: the eigenvalue of the trailing 2x2 nearer its last entry */
        Cplx a = ZS(hi - 1, hi - 1), b = ZS(hi - 1, hi), c = ZS(hi, hi - 1), d = ZS(hi, hi);
        Cplx tr = c_add(a, d), det = c_sub(c_mul(a, d), c_mul(b, c));
        Cplx disc = c_sqrtz(c_sub(c_mul(tr, tr), c_scale(4.0, det)));
        Cplx mu1 = c_scale(0.5, c_add(tr, disc)), mu2 = c_scale(0.5, c_sub(tr, disc));
        Cplx mu = c_abs(c_sub(mu1, d)) < c_abs(c_sub(mu2, d)) ? mu1 : mu2;
        if (iter % 10 == 0)                                  /* an exceptional shift, to break a cycle */
            mu = (Cplx){ d.re + c_abs(c) + (hi >= 2 ? c_abs(ZS(hi - 1, hi - 2)) : 0.0), d.im };
        for (uint32_t i = l; i <= hi; i++) ZS(i, i) = c_sub(ZS(i, i), mu);
        for (uint32_t i = l; i < hi; i++) {                  /* R = Q^H (T - mu I): rows i, i+1 */
            double cc; Cplx ss; c_givens(ZS(i, i), ZS(i + 1, i), &cc, &ss);
            cs[i] = cc; sn[i] = ss;
            for (uint32_t j = i; j < n; j++) {
                Cplx p = ZS(i, j), q = ZS(i + 1, j);
                ZS(i, j)     = c_add(c_scale(cc, p), c_mul(ss, q));
                ZS(i + 1, j) = c_sub(c_scale(cc, q), c_mul(c_conj(ss), p));
            }
            ZS(i + 1, i) = (Cplx){ 0, 0 };
        }
        for (uint32_t i = l; i < hi; i++) {                  /* R Q: columns i, i+1; U <- U Q */
            double cc = cs[i]; Cplx ss = sn[i];
            uint32_t rmax = i + 1;                           /* R is triangular: rows 0 .. i+1 */
            for (uint32_t r = 0; r <= rmax; r++) {
                Cplx p = ZS(r, i), q = ZS(r, i + 1);
                ZS(r, i)     = c_add(c_scale(cc, p), c_mul(c_conj(ss), q));
                ZS(r, i + 1) = c_sub(c_scale(cc, q), c_mul(ss, p));
            }
            for (uint32_t r = 0; r < n; r++) {
                Cplx p = ZU(r, i), q = ZU(r, i + 1);
                ZU(r, i)     = c_add(c_scale(cc, p), c_mul(c_conj(ss), q));
                ZU(r, i + 1) = c_sub(c_scale(cc, q), c_mul(ss, p));
            }
        }
        for (uint32_t i = l; i <= hi; i++) ZS(i, i) = c_add(ZS(i, i), mu);
    }
    if (unit != 1.0) for (size_t q = 0; q < (size_t)n * n; q++) T[q] = c_scale(unit, T[q]);
    {
        Cplx *W = malloc((size_t)n * n * sizeof *W);
        memcpy(W, U, (size_t)n * n * sizeof *W);
        for (uint32_t i = 0; i < n; i++) memcpy(&ZU(perm[i], 0), &W[(size_t)i * n], n * sizeof *W);
        free(W);
    }
    free(v); free(sn); free(cs); free(perm);
    return rc;
#undef ZS
#undef ZU
}

/* ---- ordschur: reordering a Schur form ------------------------------------
 * The eigenvalues marked in sel are moved to the top-left of T, keeping
 * their order among themselves (and the others theirs), by exchanging
 * neighbouring diagonal blocks one pair at a time; U follows, so U T U^H is
 * unchanged. *k receives how many eigenvalues ended up in front. Returns
 * nonzero if two blocks that had to pass each other have eigenvalues too
 * close for the exchange to be done stably (T and U are then left as a
 * valid, partly reordered Schur form). */

#define RS(i, j) T[(size_t)(i) * n + (j)]
#define RU(i, j) U[(size_t)(i) * n + (j)]

/* Exchange the block of order p at (j, j) with the block of order q below
 * it (p, q in {1, 2}) in a real Schur form. LAPACK's dlaexc, by the direct
 * route: the columns of [-X; I], with X solving T11 X - X T22 = T12, span
 * the invariant subspace of T22's eigenvalues inside the two blocks; an
 * orthogonal Q with that span in its first q columns puts T22's eigenvalues
 * first. The exchange is tried on a copy of the two blocks and only applied
 * to T and U if what should be zero afterwards is zero to rounding. */
static bool rs_swap(double *T, double *U, uint32_t n, uint32_t j, uint32_t p, uint32_t q)
{
    if (p == 1 && q == 1) {
        double a = RS(j, j), b = RS(j, j + 1), c = RS(j + 1, j + 1);
        double r = hypot(b, c - a);
        if (r == 0.0) return true;                           /* equal and uncoupled: already exchanged */
        rs_rotate(T, U, n, j, b / r, (c - a) / r);           /* first column: the eigenvector of c */
        RS(j + 1, j) = 0.0; RS(j, j) = c; RS(j + 1, j + 1) = a;
        return true;
    }
    uint32_t m = p + q;
    double D[16], dmax = 0.0;
    for (uint32_t r = 0; r < m; r++) for (uint32_t c = 0; c < m; c++) {
        D[r * 4 + c] = RS(j + r, j + c); dmax = fmax(dmax, fabs(D[r * 4 + c]));
    }
    /* T11 X - X T22 = T12 as a linear system in the p*q entries of X */
    uint32_t nx = p * q;
    double K[16] = { 0 }, x[4];
    for (uint32_t r = 0; r < p; r++) for (uint32_t c = 0; c < q; c++) {
        uint32_t row = r * q + c;
        for (uint32_t i = 0; i < p; i++) K[row * 4 + i * q + c] += D[r * 4 + i];                 /* T11[r][i] X[i][c] */
        for (uint32_t i = 0; i < q; i++) K[row * 4 + r * q + i] -= D[(p + i) * 4 + (p + c)];     /* X[r][i] T22[i][c] */
        x[row] = D[r * 4 + (p + c)];
    }
    uint32_t perm[4] = { 0, 1, 2, 3 };
    for (uint32_t s = 0; s < nx; s++) {                      /* elimination with complete pivoting */
        uint32_t pr = s, pc = s; double big = 0.0;
        for (uint32_t r = s; r < nx; r++) for (uint32_t c = s; c < nx; c++)
            if (fabs(K[r * 4 + c]) > big) { big = fabs(K[r * 4 + c]); pr = r; pc = c; }
        if (big <= 2.220446049250313e-16 * dmax) return false;      /* the blocks share an eigenvalue, near enough */
        for (uint32_t c = 0; c < nx; c++) { double t = K[s * 4 + c]; K[s * 4 + c] = K[pr * 4 + c]; K[pr * 4 + c] = t; }
        { double t = x[s]; x[s] = x[pr]; x[pr] = t; }
        for (uint32_t r = 0; r < nx; r++) { double t = K[r * 4 + s]; K[r * 4 + s] = K[r * 4 + pc]; K[r * 4 + pc] = t; }
        { uint32_t t = perm[s]; perm[s] = perm[pc]; perm[pc] = t; }
        for (uint32_t r = s + 1; r < nx; r++) {
            double f = K[r * 4 + s] / K[s * 4 + s];
            for (uint32_t c = s; c < nx; c++) K[r * 4 + c] -= f * K[s * 4 + c];
            x[r] -= f * x[s];
        }
    }
    double X[4];
    for (uint32_t s = nx; s-- > 0; ) {
        double v = x[s];
        for (uint32_t c = s + 1; c < nx; c++) v -= K[s * 4 + c] * x[c];
        x[s] = v / K[s * 4 + s];
    }
    for (uint32_t s = 0; s < nx; s++) X[perm[s]] = x[s];

    /* M = [-X; I] (m x q); Householder reflectors with Q^T M = [R; 0] */
    double M[8], hv[2][4], hb[2];
    bool have[2] = { false, false };
    for (uint32_t r = 0; r < p; r++) for (uint32_t c = 0; c < q; c++) M[r * 2 + c] = -X[r * q + c];
    for (uint32_t r = 0; r < q; r++) for (uint32_t c = 0; c < q; c++) M[(p + r) * 2 + c] = (r == c);
    for (uint32_t h = 0; h < q; h++) {
        double col[4];
        for (uint32_t r = h; r < m; r++) col[r - h] = M[r * 2 + h];
        have[h] = rs_house(col, m - h, hv[h], &hb[h]);
        if (!have[h]) continue;
        for (uint32_t c = h; c < q; c++) {
            double s = 0.0;
            for (uint32_t r = h; r < m; r++) s += hv[h][r - h] * M[r * 2 + c];
            s *= hb[h];
            for (uint32_t r = h; r < m; r++) M[r * 2 + c] -= s * hv[h][r - h];
        }
    }
    /* the trial, on the copy: Q^T D Q must have a zero p x q block at the bottom left */
    for (uint32_t h = 0; h < q; h++) {
        if (!have[h]) continue;
        for (uint32_t c = 0; c < m; c++) {
            double s = 0.0;
            for (uint32_t r = h; r < m; r++) s += hv[h][r - h] * D[r * 4 + c];
            s *= hb[h];
            for (uint32_t r = h; r < m; r++) D[r * 4 + c] -= s * hv[h][r - h];
        }
        for (uint32_t r = 0; r < m; r++) {
            double s = 0.0;
            for (uint32_t c = h; c < m; c++) s += D[r * 4 + c] * hv[h][c - h];
            s *= hb[h];
            for (uint32_t c = h; c < m; c++) D[r * 4 + c] -= s * hv[h][c - h];
        }
    }
    double left = 0.0;
    for (uint32_t r = q; r < m; r++) for (uint32_t c = 0; c < q; c++) left = fmax(left, fabs(D[r * 4 + c]));
    if (left > 10.0 * 2.220446049250313e-16 * dmax) return false;

    /* accepted: the same reflectors on T and U */
    for (uint32_t h = 0; h < q; h++) {
        if (!have[h]) continue;
        uint32_t r0 = j + h, len = m - h;
        for (uint32_t c = j; c < n; c++) {
            double s = 0.0;
            for (uint32_t i = 0; i < len; i++) s += hv[h][i] * RS(r0 + i, c);
            s *= hb[h];
            for (uint32_t i = 0; i < len; i++) RS(r0 + i, c) -= s * hv[h][i];
        }
        for (uint32_t r = 0; r < j + m; r++) {
            double s = 0.0;
            for (uint32_t i = 0; i < len; i++) s += RS(r, r0 + i) * hv[h][i];
            s *= hb[h];
            for (uint32_t i = 0; i < len; i++) RS(r, r0 + i) -= s * hv[h][i];
        }
        for (uint32_t r = 0; r < n; r++) {
            double s = 0.0;
            for (uint32_t i = 0; i < len; i++) s += RU(r, r0 + i) * hv[h][i];
            s *= hb[h];
            for (uint32_t i = 0; i < len; i++) RU(r, r0 + i) -= s * hv[h][i];
        }
    }
    for (uint32_t r = q; r < m; r++) for (uint32_t c = 0; c < q; c++) RS(j + r, j + c) = 0.0;
    if (q == 2) rs_standardize(T, U, n, j);
    if (p == 2) rs_standardize(T, U, n, j + q);
    return true;
}

static int t0_ordschur_d(double *T, double *U, uint32_t n, const unsigned char *sel, uint32_t *k)
{
    unsigned char *mark = malloc(n ? n : 1);
    uint32_t count = 0;
    for (uint32_t i = 0; i < n; ) {                          /* a 2x2 block goes if either half is marked */
        bool two = i + 1 < n && RS(i + 1, i) != 0.0;
        bool on = sel[i] || (two && sel[i + 1]);
        mark[i] = on; if (two) mark[i + 1] = on;
        count += on ? (two ? 2 : 1) : 0;
        i += two ? 2 : 1;
    }
    *k = count;
    int rc = 0;
    for (bool moved = true; moved && rc == 0; ) {            /* a marked block rises past an unmarked one */
        moved = false;
        for (uint32_t i = 0; i < n; ) {
            uint32_t p = (i + 1 < n && RS(i + 1, i) != 0.0) ? 2 : 1;
            uint32_t j = i + p;
            if (j >= n) break;
            uint32_t q = (j + 1 < n && RS(j + 1, j) != 0.0) ? 2 : 1;
            if (!mark[i] && mark[j]) {
                if (!rs_swap(T, U, n, i, p, q)) { rc = 1; break; }
                for (uint32_t t = 0; t < q; t++) mark[i + t] = 1;
                for (uint32_t t = 0; t < p; t++) mark[i + q + t] = 0;
                moved = true;
                i += q;                                      /* go on from the block that came down */
            } else i = j;
        }
    }
    free(mark);
    return rc;
}
#undef RS
#undef RU

static int t0_ordschur(Cplx *T, Cplx *U, uint32_t n, const unsigned char *sel, uint32_t *k)
{
#define ZS(i, j) T[(size_t)(i) * n + (j)]
#define ZU(i, j) U[(size_t)(i) * n + (j)]
    unsigned char *mark = malloc(n ? n : 1);
    uint32_t count = 0;
    for (uint32_t i = 0; i < n; i++) { mark[i] = sel[i] != 0; count += mark[i]; }
    *k = count;
    for (bool moved = true; moved; ) {
        moved = false;
        for (uint32_t j = 0; j + 1 < n; j++) {
            if (mark[j] || !mark[j + 1]) continue;
            /* [a b; 0 c] -> [c *; 0 a] by the unitary G whose first column is the eigenvector of c */
            Cplx a = ZS(j, j), b = ZS(j, j + 1), c = ZS(j + 1, j + 1), d = c_sub(c, a);
            double r = hypot(c_abs(b), c_abs(d));
            if (r > 0.0) {
                Cplx g11 = c_scale(1.0 / r, b), g21 = c_scale(1.0 / r, d);
                Cplx g12 = c_scale(-1.0, c_conj(g21)), g22 = c_conj(g11);
                for (uint32_t col = j; col < n; col++) {     /* rows: G^H T */
                    Cplx x = ZS(j, col), y = ZS(j + 1, col);
                    ZS(j, col)     = c_add(c_mul(c_conj(g11), x), c_mul(c_conj(g21), y));
                    ZS(j + 1, col) = c_add(c_mul(c_conj(g12), x), c_mul(c_conj(g22), y));
                }
                for (uint32_t row = 0; row <= j + 1; row++) { /* columns: T G */
                    Cplx x = ZS(row, j), y = ZS(row, j + 1);
                    ZS(row, j)     = c_add(c_mul(x, g11), c_mul(y, g21));
                    ZS(row, j + 1) = c_add(c_mul(x, g12), c_mul(y, g22));
                }
                for (uint32_t row = 0; row < n; row++) {
                    Cplx x = ZU(row, j), y = ZU(row, j + 1);
                    ZU(row, j)     = c_add(c_mul(x, g11), c_mul(y, g21));
                    ZU(row, j + 1) = c_add(c_mul(x, g12), c_mul(y, g22));
                }
                ZS(j + 1, j) = (Cplx){ 0, 0 }; ZS(j, j) = c; ZS(j + 1, j + 1) = a;
            }
            mark[j] = 1; mark[j + 1] = 0;
            moved = true;
        }
    }
    free(mark);
    return 0;
#undef ZS
#undef ZU
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

/* Real twins of t0_solve/t0_det (ported from Neutrino v2.30.x): the same
 * partial-pivot elimination in plain doubles with restrict row pointers.
 * Pivoting via fabs == hypot(x, 0) and real division == Smith's c_div on
 * zero-imag inputs, so results are BIT-IDENTICAL to the old promotion
 * shims — no golden recaptures. Upstream measured 2.0x on 1000x1000
 * (bandwidth-bound); the win belongs to tier0 native and, above all,
 * to the browser build, which has no other backend. */
static int t0_solve_d(double *LU, double *X, uint32_t n, uint32_t m)
{
    for (uint32_t k = 0; k < n; k++) {
        uint32_t piv = k;
        double best = fabs(LU[(size_t)k*n+k]);
        for (uint32_t i = k+1; i < n; i++) {
            double mag = fabs(LU[(size_t)i*n+k]);
            if (mag > best) { best = mag; piv = i; }
        }
        if (best == 0.0) return 1;                            /* singular */
        if (piv != k) {
            for (uint32_t j = 0; j < n; j++) { double t = LU[(size_t)k*n+j]; LU[(size_t)k*n+j] = LU[(size_t)piv*n+j]; LU[(size_t)piv*n+j] = t; }
            for (uint32_t j = 0; j < m; j++) { double t = X[(size_t)k*m+j];  X[(size_t)k*m+j]  = X[(size_t)piv*m+j];  X[(size_t)piv*m+j]  = t; }
        }
        double akk = LU[(size_t)k*n+k];
        for (uint32_t i = k+1; i < n; i++) {
            double f = LU[(size_t)i*n+k] / akk;
            double *restrict ri = &LU[(size_t)i*n];
            const double *restrict rk = &LU[(size_t)k*n];
            for (uint32_t j = k; j < n; j++) ri[j] -= f * rk[j];
            double *restrict xi = &X[(size_t)i*m];
            const double *restrict xk = &X[(size_t)k*m];
            for (uint32_t j = 0; j < m; j++) xi[j] -= f * xk[j];
        }
    }
    for (uint32_t c = 0; c < m; c++)
        for (int64_t ii = (int64_t)n - 1; ii >= 0; ii--) {
            uint32_t i = (uint32_t)ii;
            double s = X[(size_t)i*m+c];
            for (uint32_t j = i+1; j < n; j++) s -= LU[(size_t)i*n+j] * X[(size_t)j*m+c];
            X[(size_t)i*m+c] = s / LU[(size_t)i*n+i];
        }
    return 0;
}
static int t0_det_d(double *M, uint32_t n, double *out)
{
    double det = 1.0;
    int sign = 1;
    for (uint32_t k = 0; k < n; k++) {
        uint32_t p = k; double best = fabs(M[(size_t)k*n+k]);
        for (uint32_t i = k+1; i < n; i++) {
            double mg = fabs(M[(size_t)i*n+k]);
            if (mg > best) { best = mg; p = i; }
        }
        if (best == 0.0) { *out = 0.0; return 0; }
        if (p != k) { for (uint32_t j = 0; j < n; j++) { double t = M[(size_t)k*n+j]; M[(size_t)k*n+j] = M[(size_t)p*n+j]; M[(size_t)p*n+j] = t; } sign = -sign; }
        det *= M[(size_t)k*n+k];
        for (uint32_t i = k+1; i < n; i++) {
            double f = M[(size_t)i*n+k] / M[(size_t)k*n+k];
            double *restrict ri = &M[(size_t)i*n];
            const double *restrict rk = &M[(size_t)k*n];
            for (uint32_t j = k+1; j < n; j++) ri[j] -= f * rk[j];
        }
    }
    *out = sign < 0 ? -det : det;
    return 0;
}
/* Flat float matmul (Neutrino v2.31.0's 68x kernel): i-k-j order so the
 * inner loop streams C[i,:] += a_ik * B[k,:]; memset-zero C first.
 * 0.0 + p == p, so results match the boxed generic loop exactly. */
static int t0_gemm_d(const double *A, const double *B, double *C,
                     uint32_t m, uint32_t k, uint32_t n)
{
    memset(C, 0, (size_t)m * n * sizeof *C);
    for (uint32_t i = 0; i < m; i++) {
        double *restrict ci = &C[(size_t)i*n];
        for (uint32_t p = 0; p < k; p++) {
            double a = A[(size_t)i*k+p];
            const double *restrict bp = &B[(size_t)p*n];
            for (uint32_t j = 0; j < n; j++) ci[j] += a * bp[j];
        }
    }
    return 0;
}

static int t0_eig_sym_d(double *A, uint32_t n, double *w, double *V)
{
    size_t c = (size_t)n * n;
    Cplx *a = calloc(c ? c : 1, sizeof *a), *v = calloc(c ? c : 1, sizeof *v);
    if (!a || !v) abort();
    for (size_t k = 0; k < c; k++) a[k] = (Cplx){ A[k], 0 };
    int rc = t0_eig_herm(a, n, w, v);
    if (rc == 0) for (size_t k = 0; k < c; k++) V[k] = v[k].re;
    free(a); free(v);
    return rc;
}
static int t0_svd_d(const double *A, uint32_t m, uint32_t n,
                    double *U, double *s, double *V)
{
    uint32_t k = m < n ? m : n;
    size_t ca = (size_t)m * n, cu = (size_t)m * k, cv = (size_t)n * k;
    Cplx *a = calloc(ca ? ca : 1, sizeof *a);
    Cplx *u = malloc((cu ? cu : 1) * sizeof *u);
    Cplx *v = malloc((cv ? cv : 1) * sizeof *v);
    if (!a || !u || !v) abort();
    for (size_t q = 0; q < ca; q++) a[q] = (Cplx){ A[q], 0 };
    int rc = t0_svd(a, m, n, u, s, v);
    if (rc == 0) {
        for (size_t q = 0; q < cu; q++) U[q] = u[q].re;
        for (size_t q = 0; q < cv; q++) V[q] = v[q].re;
    }
    free(a); free(u); free(v);
    return rc;
}
static int t0_chol_d(const double *A, uint32_t n, double *L)
{
    size_t c = (size_t)n * n;
    Cplx *a = calloc(c ? c : 1, sizeof *a), *l = calloc(c ? c : 1, sizeof *l);
    if (!a || !l) abort();
    for (size_t k = 0; k < c; k++) a[k] = (Cplx){ A[k], 0 };
    int rc = t0_chol(a, n, l);
    if (rc == 0) for (size_t k = 0; k < c; k++) L[k] = l[k].re;
    free(a); free(l);
    return rc;
}

static const LinalgKernels tier0 = {
    .name     = "tier0",
    .solve    = t0_solve,
    .solve_d  = t0_solve_d,
    .det_d    = t0_det_d,
    .gemm_d   = t0_gemm_d,
    .eig_sym_d = t0_eig_sym_d,
    .svd_d    = t0_svd_d,
    .chol_d   = t0_chol_d,
    .det      = t0_det,
    .eig_herm = t0_eig_herm,
    .eig_gen  = t0_eig_gen,
    .schur    = t0_schur,
    .schur_d  = t0_schur_d,
    .ordschur = t0_ordschur,
    .ordschur_d = t0_ordschur_d,
    .svd      = t0_svd,
    .chol     = t0_chol,
};

const LinalgKernels *cozy_linalg(void) { return &tier0; }
