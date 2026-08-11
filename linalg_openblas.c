/* linalg_openblas.c — the tier-1 linear-algebra backend (design entry 2).
 *
 * The same LinalgKernels table as tier0, answered by LAPACK (OpenBLAS):
 * solve -> zgesv, det -> zgetrf, eig_herm -> zheev, eig_gen -> zgeev,
 * svd -> zgesvd, chol -> zpotrf. Selected by `make BACKEND=openblas`;
 * eval.c neither knows nor cares — that is the seam's whole contract.
 *
 * LAPACK is column-major; the contracts are row-major. Marshalling is done
 * here, in the backend, with explicit O(n^2) transposition — negligible
 * beside the O(n^3) kernels and worth the clarity. Cozy's Cplx (two
 * doubles, re then im) is layout-compatible with Fortran doublecomplex,
 * so buffers pass as double* directly.
 *
 * Observable conventions (eigenpair ordering, eigenvector phase) remain
 * eval.c's law: this file returns pairs in LAPACK's order with LAPACK's
 * phases, exactly as tier0 returns its own. The conformance suite runs
 * identically over both — the backend-equivalence harness in action.
 */
#include <stdlib.h>
#include <string.h>
#include "linalg.h"

/* Fortran LAPACK entry points (complex double). */
extern void zgesv_(const int *n, const int *nrhs, double *a, const int *lda,
                   int *ipiv, double *b, const int *ldb, int *info);
extern void zgetrf_(const int *m, const int *n, double *a, const int *lda,
                    int *ipiv, int *info);
extern void zheev_(const char *jobz, const char *uplo, const int *n, double *a,
                   const int *lda, double *w, double *work, const int *lwork,
                   double *rwork, int *info);
extern void zgeev_(const char *jobvl, const char *jobvr, const int *n,
                   double *a, const int *lda, double *w, double *vl,
                   const int *ldvl, double *vr, const int *ldvr, double *work,
                   const int *lwork, double *rwork, int *info);
extern void zgesvd_(const char *jobu, const char *jobvt, const int *m,
                    const int *n, double *a, const int *lda, double *s,
                    double *u, const int *ldu, double *vt, const int *ldvt,
                    double *work, const int *lwork, double *rwork, int *info);
extern void zpotrf_(const char *uplo, const int *n, double *a, const int *lda,
                    int *info);

/* row-major (r x c) -> new column-major buffer */
static Cplx *to_cm(const Cplx *rm, uint32_t r, uint32_t c)
{
    Cplx *cm = malloc((size_t)(r && c ? (size_t)r * c : 1) * sizeof *cm);
    for (uint32_t i = 0; i < r; i++)
        for (uint32_t j = 0; j < c; j++)
            cm[(size_t)j * r + i] = rm[(size_t)i * c + j];
    return cm;
}
static void from_cm(Cplx *rm, const Cplx *cm, uint32_t r, uint32_t c)
{
    for (uint32_t i = 0; i < r; i++)
        for (uint32_t j = 0; j < c; j++)
            rm[(size_t)i * c + j] = cm[(size_t)j * r + i];
}

static int ob_solve(Cplx *A, Cplx *B, uint32_t n, uint32_t m)
{
    if (n == 0) return 0;
    int N = (int)n, M = (int)m, info = 0;
    Cplx *a = to_cm(A, n, n);
    Cplx *b = to_cm(B, n, m);
    int *ipiv = malloc((size_t)n * sizeof *ipiv);
    zgesv_(&N, &M, (double *)a, &N, ipiv, (double *)b, &N, &info);
    if (info == 0) from_cm(B, b, n, m);
    free(a); free(b); free(ipiv);
    return info != 0;
}

extern void dgesv_(const int *n, const int *nrhs, double *a, const int *lda,
                   int *ipiv, double *b, const int *ldb, int *info);
extern void dgetrf_(const int *m, const int *n, double *a, const int *lda,
                    int *ipiv, int *info);
static double *to_cm_d(const double *A, uint32_t r, uint32_t c)
{
    double *o = malloc((size_t)r * c * sizeof *o);
    if (!o && r && c) abort();
    for (uint32_t i = 0; i < r; i++)
        for (uint32_t j = 0; j < c; j++) o[(size_t)j*r+i] = A[(size_t)i*c+j];
    return o;
}
static void from_cm_d(double *A, const double *cm, uint32_t r, uint32_t c)
{
    for (uint32_t i = 0; i < r; i++)
        for (uint32_t j = 0; j < c; j++) A[(size_t)i*c+j] = cm[(size_t)j*r+i];
}
static int ob_solve_d(double *A, double *B, uint32_t n, uint32_t m)
{
    if (n == 0) return 0;
    int N = (int)n, M = (int)m, info = 0;
    double *a = to_cm_d(A, n, n);
    double *b = to_cm_d(B, n, m);
    int *ipiv = malloc((size_t)n * sizeof *ipiv);
    dgesv_(&N, &M, a, &N, ipiv, b, &N, &info);
    if (info == 0) from_cm_d(B, b, n, m);
    free(a); free(b); free(ipiv);
    return info != 0;
}
static int ob_det_d(double *A, uint32_t n, double *out)
{
    if (n == 0) { *out = 1.0; return 0; }
    int N = (int)n, info = 0;
    int *ipiv = malloc((size_t)n * sizeof *ipiv);
    dgetrf_(&N, &N, A, &N, ipiv, &info);   /* row-major read as A^T: same det */
    if (info > 0) { *out = 0.0; free(ipiv); return 0; }
    double det = 1.0;
    for (uint32_t k = 0; k < n; k++) {
        det *= A[(size_t)k * n + k];
        if (ipiv[k] != (int)k + 1) det = -det;
    }
    free(ipiv);
    *out = det;
    return 0;
}

static int ob_det(Cplx *A, uint32_t n, Cplx *out)
{
    if (n == 0) { *out = (Cplx){ 1, 0 }; return 0; }
    int N = (int)n, info = 0;
    int *ipiv = malloc((size_t)n * sizeof *ipiv);
    /* det(A) == det(A^T): the row-major buffer read as column-major is A^T,
     * so zgetrf can take it unmarshalled. */
    zgetrf_(&N, &N, (double *)A, &N, ipiv, &info);
    if (info > 0) { *out = (Cplx){ 0, 0 }; free(ipiv); return 0; }
    Cplx det = { 1, 0 };
    for (uint32_t k = 0; k < n; k++) {
        Cplx d = A[(size_t)k * n + k];
        det = (Cplx){ det.re * d.re - det.im * d.im, det.re * d.im + det.im * d.re };
        if (ipiv[k] != (int)k + 1) { det.re = -det.re; det.im = -det.im; }
    }
    free(ipiv);
    *out = det;
    return 0;
}

static int ob_eig_herm(Cplx *A, uint32_t n, double *w, Cplx *V)
{
    if (n == 0) return 0;
    int N = (int)n, info = 0, lwork = -1;
    Cplx *a = to_cm(A, n, n);
    double *rwork = malloc((size_t)(3 * n) * sizeof *rwork);
    Cplx wkq;
    zheev_("V", "L", &N, (double *)a, &N, w, (double *)&wkq, &lwork, rwork, &info);
    lwork = (int)wkq.re;
    Cplx *work = malloc((size_t)(lwork > 0 ? lwork : 1) * sizeof *work);
    zheev_("V", "L", &N, (double *)a, &N, w, (double *)work, &lwork, rwork, &info);
    from_cm(V, a, n, n);                      /* columns are the eigenvectors */
    free(a); free(rwork); free(work);
    return info != 0;
}

static int ob_eig_gen(const Cplx *A0, uint32_t n, Cplx *w, Cplx *V)
{
    if (n == 0) return 0;
    int N = (int)n, info = 0, lwork = -1;
    Cplx *a = to_cm(A0, n, n);
    size_t nn2 = (size_t)n * n;
    Cplx *vr = malloc((nn2 ? nn2 : 1) * sizeof *vr);
    double *rwork = malloc((size_t)(2 * n) * sizeof *rwork);
    Cplx wkq;
    zgeev_("N", "V", &N, (double *)a, &N, (double *)w, NULL, &N,
           (double *)vr, &N, (double *)&wkq, &lwork, rwork, &info);
    lwork = (int)wkq.re;
    Cplx *work = malloc((size_t)(lwork > 0 ? lwork : 1) * sizeof *work);
    zgeev_("N", "V", &N, (double *)a, &N, (double *)w, NULL, &N,
           (double *)vr, &N, (double *)work, &lwork, rwork, &info);
    from_cm(V, vr, n, n);
    free(a); free(vr); free(rwork); free(work);
    return info != 0;
}

static int ob_svd(const Cplx *A, uint32_t m, uint32_t n,
                  Cplx *U, double *s, Cplx *V)
{
    uint32_t k = m < n ? m : n;
    if (k == 0) return 0;
    int M = (int)m, N = (int)n, K = (int)k, info = 0, lwork = -1;
    Cplx *a = to_cm(A, m, n);
    Cplx *u = malloc((size_t)m * k * sizeof *u);
    Cplx *vt = malloc((size_t)k * n * sizeof *vt);
    double *rwork = malloc((size_t)(5 * (size_t)k) * sizeof *rwork);
    Cplx wkq;
    zgesvd_("S", "S", &M, &N, (double *)a, &M, s, (double *)u, &M,
            (double *)vt, &K, (double *)&wkq, &lwork, rwork, &info);
    lwork = (int)wkq.re;
    Cplx *work = malloc((size_t)(lwork > 0 ? lwork : 1) * sizeof *work);
    zgesvd_("S", "S", &M, &N, (double *)a, &M, s, (double *)u, &M,
            (double *)vt, &K, (double *)work, &lwork, rwork, &info);
    from_cm(U, u, m, k);                          /* U: m x k */
    for (uint32_t j = 0; j < n; j++)              /* V = (V^H)^H: n x k */
        for (uint32_t c = 0; c < k; c++) {
            Cplx z = vt[(size_t)j * k + c];       /* VT is k x n, col-major */
            V[(size_t)j * k + c] = (Cplx){ z.re, -z.im };
        }
    free(a); free(u); free(vt); free(rwork); free(work);
    return info != 0;
}

static int ob_chol(const Cplx *A, uint32_t n, Cplx *L)
{
    if (n == 0) return 0;
    int N = (int)n, info = 0;
    Cplx *a = to_cm(A, n, n);
    zpotrf_("L", &N, (double *)a, &N, &info);
    if (info != 0) { free(a); return 1; }         /* not positive definite */
    memset(L, 0, (size_t)n * n * sizeof *L);
    for (uint32_t i = 0; i < n; i++)              /* lower triangle only */
        for (uint32_t j = 0; j <= i; j++)
            L[(size_t)i * n + j] = a[(size_t)j * n + i];
    free(a);
    return 0;
}

/* The same source serves any library exporting the Fortran LAPACK symbols:
 * OpenBLAS on Linux, Accelerate on macOS (LP64 interface: 32-bit ints,
 * matching the externs above). The Makefile sets the reported name. */
#ifndef COZY_LAPACK_NAME
#define COZY_LAPACK_NAME "openblas"
#endif
extern void dsyev_(const char *jobz, const char *uplo, const int *n, double *a,
                   const int *lda, double *w, double *work, const int *lwork, int *info);
extern void dgesvd_(const char *jobu, const char *jobvt, const int *m, const int *n,
                    double *a, const int *lda, double *s, double *u, const int *ldu,
                    double *vt, const int *ldvt, double *work, const int *lwork, int *info);
extern void dpotrf_(const char *uplo, const int *n, double *a, const int *lda, int *info);

static int ob_eig_sym_d(double *A, uint32_t n, double *w, double *V)
{
    if (n == 0) return 0;
    int N = (int)n, info = 0, lwork = -1;
    double *a = to_cm_d(A, n, n);
    double wkq;
    dsyev_("V", "L", &N, a, &N, w, &wkq, &lwork, &info);
    lwork = (int)wkq;
    double *work = malloc((size_t)(lwork > 0 ? lwork : 1) * sizeof *work);
    dsyev_("V", "L", &N, a, &N, w, work, &lwork, &info);
    from_cm_d(V, a, n, n);
    free(a); free(work);
    return info != 0;
}
static int ob_svd_d(const double *A, uint32_t m, uint32_t n,
                    double *U, double *s, double *V)
{
    if (m == 0 || n == 0) return 0;
    int M = (int)m, N = (int)n, K = m < n ? (int)m : (int)n, info = 0, lwork = -1;
    double *a = to_cm_d(A, m, n);
    double *u = malloc((size_t)m * K * sizeof *u);
    double *vt = malloc((size_t)K * n * sizeof *vt);
    double wkq;
    dgesvd_("S", "S", &M, &N, a, &M, s, u, &M, vt, &K, &wkq, &lwork, &info);
    lwork = (int)wkq;
    double *work = malloc((size_t)(lwork > 0 ? lwork : 1) * sizeof *work);
    dgesvd_("S", "S", &M, &N, a, &M, s, u, &M, vt, &K, work, &lwork, &info);
    if (info == 0) {
        from_cm_d(U, u, m, (uint32_t)K);
        for (uint32_t i = 0; i < n; i++)                 /* V = VTᵀ, row-major n×K */
            for (int j = 0; j < K; j++) V[(size_t)i*K + j] = vt[j + (size_t)K*i];
    }
    free(a); free(u); free(vt); free(work);
    return info != 0;
}
static int ob_chol_d(const double *A, uint32_t n, double *L)
{
    if (n == 0) return 0;
    int N = (int)n, info = 0;
    double *a = to_cm_d(A, n, n);          /* symmetric: transpose immaterial */
    dpotrf_("L", &N, a, &N, &info);
    if (info == 0)
        for (uint32_t i = 0; i < n; i++)
            for (uint32_t j = 0; j < n; j++)
                L[(size_t)i*n + j] = j <= i ? a[i + (size_t)j*n] : 0.0;
    free(a);
    return info != 0;
}

static const LinalgKernels openblas = {
    .name     = COZY_LAPACK_NAME,
    .solve    = ob_solve,
    .solve_d  = ob_solve_d,
    .det_d    = ob_det_d,
    .eig_sym_d = ob_eig_sym_d,
    .svd_d    = ob_svd_d,
    .chol_d   = ob_chol_d,
    .det      = ob_det,
    .eig_herm = ob_eig_herm,
    .eig_gen  = ob_eig_gen,
    .svd      = ob_svd,
    .chol     = ob_chol,
};

const LinalgKernels *cozy_linalg(void) { return &openblas; }
