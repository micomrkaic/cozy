/* linalg_openblas.c — the tier-1 linear-algebra backend (design entry 2).
 *
 * The same LinalgKernels table as tier0, answered by LAPACK (OpenBLAS):
 * solve -> zgesv, det -> zgetrf, eig_herm -> zheevd, eig_gen -> zgeev,
 * svd -> zgesdd, chol -> zpotrf. Selected by `make BACKEND=openblas`;
 * eval.c neither knows nor cares — that is the seam's whole contract.
 *
 * The symmetric eigenproblem and the SVD use LAPACK's divide-and-conquer
 * drivers (dsyevd/zheevd, dgesdd/zgesdd) since 0.1.38. They compute the
 * same decompositions as the QR-iteration drivers used before (dsyev,
 * dgesvd), to the same backward error, and about ten times faster once
 * vectors are wanted and the matrix is a few hundred wide: 0.07 s against
 * 0.8 s for a symmetric eig at n = 1000, 0.3 s against 1.9 s for an svd.
 * They are also what NumPy calls. (Below n = 25 the divide-and-conquer
 * routines hand the reduced problem to the same QR iteration.)
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
extern void zheevd_(const char *jobz, const char *uplo, const int *n, double *a,
                    const int *lda, double *w, double *work, const int *lwork,
                    double *rwork, const int *lrwork, int *iwork, const int *liwork, int *info);
extern void zgeev_(const char *jobvl, const char *jobvr, const int *n,
                   double *a, const int *lda, double *w, double *vl,
                   const int *ldvl, double *vr, const int *ldvr, double *work,
                   const int *lwork, double *rwork, int *info);
extern void zgesdd_(const char *jobz, const int *m, const int *n, double *a,
                    const int *lda, double *s, double *u, const int *ldu,
                    double *vt, const int *ldvt, double *work, const int *lwork,
                    double *rwork, int *iwork, int *info);
extern void zpotrf_(const char *uplo, const int *n, double *a, const int *lda,
                    int *info);

/* Small problems run on one thread (OpenBLAS only, 0.1.57). OpenBLAS
 * threads LU from 100x100 up (m*n >= 10000), and every threaded call wakes
 * the whole worker pool: on a laptop with many cores and deep idle states
 * that cost 5-50 ms per call at the REPL, for a 100x100 inverse that takes
 * 0.3 ms on one core. Below OB_SMALL_N the call drops to one thread and
 * the cleanup attribute restores the count on every return path. gemm is
 * left alone: OpenBLAS sizes its own threading there. Accelerate (the
 * other user of this file) manages its own threads and has no such call. */
#ifdef COZY_OPENBLAS
extern void openblas_set_num_threads(int);
extern int  openblas_get_num_threads(void);
#define OB_SMALL_N 160
static int ob_single(void)
{
    int t = openblas_get_num_threads();
    if (t <= 1) return 0;
    openblas_set_num_threads(1);
    return t;
}
static void ob_restore(int *t) { if (*t > 0) openblas_set_num_threads(*t); }
#define OB_SMALL(n) __attribute__((cleanup(ob_restore))) int ob_saved_ = ((n) < OB_SMALL_N ? ob_single() : 0); (void)ob_saved_
#else
#define OB_SMALL(n) ((void)0)
#endif

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
    OB_SMALL(n);
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
    OB_SMALL(n);
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
    OB_SMALL(n);
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
    OB_SMALL(n);
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
    OB_SMALL(n);
    if (n == 0) return 0;
    int N = (int)n, info = 0, lwork = -1, lrwork = -1, liwork = -1;
    Cplx *a = to_cm(A, n, n);
    Cplx wkq; double rwq; int iwq;
    zheevd_("V", "L", &N, (double *)a, &N, w, (double *)&wkq, &lwork, &rwq, &lrwork, &iwq, &liwork, &info);
    lwork = (int)wkq.re; lrwork = (int)rwq; liwork = iwq;
    Cplx *work = malloc((size_t)(lwork > 0 ? lwork : 1) * sizeof *work);
    double *rwork = malloc((size_t)(lrwork > 0 ? lrwork : 1) * sizeof *rwork);
    int *iwork = malloc((size_t)(liwork > 0 ? liwork : 1) * sizeof *iwork);
    zheevd_("V", "L", &N, (double *)a, &N, w, (double *)work, &lwork, rwork, &lrwork, iwork, &liwork, &info);
    from_cm(V, a, n, n);                      /* columns are the eigenvectors */
    free(a); free(rwork); free(work); free(iwork);
    return info != 0;
}

static int ob_eig_gen(const Cplx *A0, uint32_t n, Cplx *w, Cplx *V)
{
    OB_SMALL(n);
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
    OB_SMALL((m > n ? m : n));
    uint32_t k = m < n ? m : n;
    if (k == 0) return 0;
    int M = (int)m, N = (int)n, K = (int)k, info = 0, lwork = -1;
    Cplx *a = to_cm(A, m, n);
    Cplx *u = malloc((size_t)m * k * sizeof *u);
    Cplx *vt = malloc((size_t)k * n * sizeof *vt);
    /* rwork for jobz = S: the larger of what LAPACK 3.7 and its predecessors ask for */
    size_t mn = k, mx = m > n ? m : n;
    size_t lrw_new = 5 * mn * mn + 5 * mn, lrw_alt = 2 * mx * mn + 2 * mn * mn + mn, lrw_old = 5 * mn * mn + 7 * mn;
    size_t lrw = lrw_new > lrw_alt ? lrw_new : lrw_alt;
    if (lrw_old > lrw) lrw = lrw_old;
    double *rwork = malloc(lrw * sizeof *rwork);
    int *iwork = malloc(8 * mn * sizeof *iwork);
    Cplx wkq;
    zgesdd_("S", &M, &N, (double *)a, &M, s, (double *)u, &M,
            (double *)vt, &K, (double *)&wkq, &lwork, rwork, iwork, &info);
    lwork = (int)wkq.re;
    Cplx *work = malloc((size_t)(lwork > 0 ? lwork : 1) * sizeof *work);
    zgesdd_("S", &M, &N, (double *)a, &M, s, (double *)u, &M,
            (double *)vt, &K, (double *)work, &lwork, rwork, iwork, &info);
    free(iwork);
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
    OB_SMALL(n);
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
extern void dsyevd_(const char *jobz, const char *uplo, const int *n, double *a,
                    const int *lda, double *w, double *work, const int *lwork,
                    int *iwork, const int *liwork, int *info);
extern void dgesdd_(const char *jobz, const int *m, const int *n,
                    double *a, const int *lda, double *s, double *u, const int *ldu,
                    double *vt, const int *ldvt, double *work, const int *lwork,
                    int *iwork, int *info);
extern void dpotrf_(const char *uplo, const int *n, double *a, const int *lda, int *info);

static int ob_eig_sym_d(double *A, uint32_t n, double *w, double *V)
{
    OB_SMALL(n);
    if (n == 0) return 0;
    int N = (int)n, info = 0, lwork = -1, liwork = -1;
    double *a = to_cm_d(A, n, n);
    double wkq; int iwq;
    dsyevd_("V", "L", &N, a, &N, w, &wkq, &lwork, &iwq, &liwork, &info);
    lwork = (int)wkq; liwork = iwq;
    double *work = malloc((size_t)(lwork > 0 ? lwork : 1) * sizeof *work);
    int *iwork = malloc((size_t)(liwork > 0 ? liwork : 1) * sizeof *iwork);
    dsyevd_("V", "L", &N, a, &N, w, work, &lwork, iwork, &liwork, &info);
    from_cm_d(V, a, n, n);
    free(a); free(work); free(iwork);
    return info != 0;
}
static int ob_svd_d(const double *A, uint32_t m, uint32_t n,
                    double *U, double *s, double *V)
{
    OB_SMALL((m > n ? m : n));
    if (m == 0 || n == 0) return 0;
    int M = (int)m, N = (int)n, K = m < n ? (int)m : (int)n, info = 0, lwork = -1;
    double *a = to_cm_d(A, m, n);
    double *u = malloc((size_t)m * K * sizeof *u);
    double *vt = malloc((size_t)K * n * sizeof *vt);
    double wkq;
    int *iwork = malloc(8 * (size_t)K * sizeof *iwork);
    dgesdd_("S", &M, &N, a, &M, s, u, &M, vt, &K, &wkq, &lwork, iwork, &info);
    lwork = (int)wkq;
    double *work = malloc((size_t)(lwork > 0 ? lwork : 1) * sizeof *work);
    dgesdd_("S", &M, &N, a, &M, s, u, &M, vt, &K, work, &lwork, iwork, &info);
    free(iwork);
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
    OB_SMALL(n);
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

extern void dgemm_(const char *ta, const char *tb, const int *m, const int *n,
                   const int *k, const double *alpha, const double *a, const int *lda,
                   const double *b, const int *ldb, const double *beta, double *c, const int *ldc);
extern void zgemm_(const char *ta, const char *tb, const int *m, const int *n,
                   const int *k, const void *alpha, const void *a, const int *lda,
                   const void *b, const int *ldb, const void *beta, void *c, const int *ldc);
/* Row-major without copies: C_rm = A_rm * B_rm is C^T = B^T * A^T in
 * column-major, and a row-major array IS its transpose read column-major. */
static int ob_gemm_d(const double *A, const double *B, double *C,
                     uint32_t m, uint32_t k, uint32_t n)
{
    if (!m || !n) return 0;
    int M = (int)n, N = (int)m, K = (int)k, lda = (int)n, ldb = (int)(k ? k : 1), ldc = (int)n;
    double one = 1.0, zero = 0.0;
    if (!k) { for (size_t q = 0; q < (size_t)m * n; q++) C[q] = 0.0; return 0; }
    dgemm_("N", "N", &M, &N, &K, &one, B, &lda, A, &ldb, &zero, C, &ldc);
    return 0;
}
static int ob_gemm_z(const Cplx *A, const Cplx *B, Cplx *C,
                     uint32_t m, uint32_t k, uint32_t n)
{
    if (!m || !n) return 0;
    int M = (int)n, N = (int)m, K = (int)k, lda = (int)n, ldb = (int)(k ? k : 1), ldc = (int)n;
    Cplx one = { 1.0, 0.0 }, zero = { 0.0, 0.0 };
    if (!k) { for (size_t q = 0; q < (size_t)m * n; q++) C[q] = (Cplx){ 0, 0 }; return 0; }
    zgemm_("N", "N", &M, &N, &K, &one, B, &lda, A, &ldb, &zero, C, &ldc);
    return 0;
}

extern void dgeev_(const char *jl, const char *jr, const int *n, double *a,
                   const int *lda, double *wr, double *wi, double *vl, const int *ldvl,
                   double *vr, const int *ldvr, double *work, const int *lwork, int *info);

/* dgeev's paired-column format, unpacked: for a conjugate pair
 * (wi[j] > 0), columns j and j+1 of VR hold Re(v) and Im(v); the pair's
 * eigenvectors are v and conj(v). Real eigenvalues use one real column. */
static int ob_eig_gen_d(const double *A, uint32_t n, Cplx *w, Cplx *V)
{
    OB_SMALL(n);
    if (n == 0) return 0;
    int N = (int)n, info = 0, lwork = -1;
    double *a = to_cm_d(A, n, n);
    double *wr = malloc(n * sizeof *wr), *wi = malloc(n * sizeof *wi);
    double *vr = malloc((size_t)n * n * sizeof *vr);
    double wkq;
    dgeev_("N", "V", &N, a, &N, wr, wi, NULL, &N, vr, &N, &wkq, &lwork, &info);
    lwork = (int)wkq;
    double *work = malloc((size_t)(lwork > 0 ? lwork : 1) * sizeof *work);
    dgeev_("N", "V", &N, a, &N, wr, wi, NULL, &N, vr, &N, work, &lwork, &info);
    if (info == 0) {
        for (uint32_t j = 0; j < n; j++) w[j] = (Cplx){ wr[j], wi[j] };
        for (uint32_t j = 0; j < n; ) {
            if (wi[j] > 0.0 && j + 1 < n) {              /* conjugate pair */
                for (uint32_t i = 0; i < n; i++) {
                    double re = vr[i + (size_t)N * j], im = vr[i + (size_t)N * (j + 1)];
                    V[(size_t)i * n + j]     = (Cplx){ re,  im };
                    V[(size_t)i * n + j + 1] = (Cplx){ re, -im };
                }
                j += 2;
            } else {
                for (uint32_t i = 0; i < n; i++)
                    V[(size_t)i * n + j] = (Cplx){ vr[i + (size_t)N * j], 0.0 };
                j += 1;
            }
        }
    }
    free(a); free(wr); free(wi); free(vr); free(work);
    return info != 0;
}

/* schur -> dgees / zgees, eigenvalues in the order LAPACK finds them (no
 * sorting: the select function is never called with sort = "N"). */
extern void dgees_(const char *jobvs, const char *sort, void *select, const int *n,
                   double *a, const int *lda, int *sdim, double *wr, double *wi,
                   double *vs, const int *ldvs, double *work, const int *lwork,
                   int *bwork, int *info);
extern void zgees_(const char *jobvs, const char *sort, void *select, const int *n,
                   double *a, const int *lda, int *sdim, double *w,
                   double *vs, const int *ldvs, double *work, const int *lwork,
                   double *rwork, int *bwork, int *info);

static int ob_schur_d(const double *A, uint32_t n, double *T, double *U)
{
    OB_SMALL(n);
    if (n == 0) return 0;
    int N = (int)n, info = 0, lwork = -1, sdim = 0;
    double *a = to_cm_d(A, n, n);
    double *wr = malloc(n * sizeof *wr), *wi = malloc(n * sizeof *wi);
    double *vs = malloc((size_t)n * n * sizeof *vs);
    double wkq;
    dgees_("V", "N", NULL, &N, a, &N, &sdim, wr, wi, vs, &N, &wkq, &lwork, NULL, &info);
    lwork = (int)wkq;
    double *work = malloc((size_t)(lwork > 0 ? lwork : 1) * sizeof *work);
    dgees_("V", "N", NULL, &N, a, &N, &sdim, wr, wi, vs, &N, work, &lwork, NULL, &info);
    if (info == 0) {
        from_cm_d(T, a, n, n); from_cm_d(U, vs, n, n);
        for (uint32_t i = 2; i < n; i++)                 /* dgees leaves the part below the */
            for (uint32_t j = 0; j + 1 < i; j++) T[(size_t)i * n + j] = 0.0;   /* subdiagonal untouched */
    }
    free(a); free(wr); free(wi); free(vs); free(work);
    return info != 0;
}

static int ob_schur(const Cplx *A, uint32_t n, Cplx *T, Cplx *U)
{
    OB_SMALL(n);
    if (n == 0) return 0;
    int N = (int)n, info = 0, lwork = -1, sdim = 0;
    Cplx *a = to_cm(A, n, n);
    Cplx *w = malloc(n * sizeof *w), *vs = malloc((size_t)n * n * sizeof *vs);
    double *rwork = malloc(n * sizeof *rwork);
    Cplx wkq;
    zgees_("V", "N", NULL, &N, (double *)a, &N, &sdim, (double *)w, (double *)vs, &N,
           (double *)&wkq, &lwork, rwork, NULL, &info);
    lwork = (int)wkq.re;
    Cplx *work = malloc((size_t)(lwork > 0 ? lwork : 1) * sizeof *work);
    zgees_("V", "N", NULL, &N, (double *)a, &N, &sdim, (double *)w, (double *)vs, &N,
           (double *)work, &lwork, rwork, NULL, &info);
    if (info == 0) {
        from_cm(T, a, n, n); from_cm(U, vs, n, n);
        for (uint32_t i = 1; i < n; i++)
            for (uint32_t j = 0; j < i; j++) T[(size_t)i * n + j] = (Cplx){ 0, 0 };
    }
    free(a); free(w); free(vs); free(rwork); free(work);
    return info != 0;
}

/* ordschur -> dtrsen / ztrsen (job "N": reorder only; compq "V": update Q).
 * A Fortran LOGICAL is an int here, as everywhere this file meets one. */
extern void dtrsen_(const char *job, const char *compq, const int *select, const int *n,
                    double *t, const int *ldt, double *q, const int *ldq, double *wr, double *wi,
                    int *m, double *s, double *sep, double *work, const int *lwork,
                    int *iwork, const int *liwork, int *info);
extern void ztrsen_(const char *job, const char *compq, const int *select, const int *n,
                    double *t, const int *ldt, double *q, const int *ldq, double *w,
                    int *m, double *s, double *sep, double *work, const int *lwork, int *info);

static int ob_ordschur_d(double *T, double *U, uint32_t n, const unsigned char *sel, uint32_t *k)
{
    OB_SMALL(n);
    *k = 0;
    if (n == 0) return 0;
    int N = (int)n, info = 0, m = 0, lwork = N > 1 ? N : 1, liwork = 1, iwork[1];
    int *select = malloc(n * sizeof *select);
    for (uint32_t i = 0; i < n; i++) select[i] = sel[i] != 0;
    double *t = to_cm_d(T, n, n), *q = to_cm_d(U, n, n);
    double *wr = malloc(n * sizeof *wr), *wi = malloc(n * sizeof *wi), *work = malloc((size_t)lwork * sizeof *work);
    double s = 0.0, sep = 0.0;
    dtrsen_("N", "V", select, &N, t, &N, q, &N, wr, wi, &m, &s, &sep, work, &lwork, iwork, &liwork, &info);
    from_cm_d(T, t, n, n); from_cm_d(U, q, n, n);        /* valid even when the reordering stopped early */
    for (uint32_t i = 2; i < n; i++)
        for (uint32_t j = 0; j + 1 < i; j++) T[(size_t)i * n + j] = 0.0;
    *k = (uint32_t)m;
    free(select); free(t); free(q); free(wr); free(wi); free(work);
    return info != 0;
}

static int ob_ordschur(Cplx *T, Cplx *U, uint32_t n, const unsigned char *sel, uint32_t *k)
{
    OB_SMALL(n);
    *k = 0;
    if (n == 0) return 0;
    int N = (int)n, info = 0, m = 0, lwork = 1;
    int *select = malloc(n * sizeof *select);
    for (uint32_t i = 0; i < n; i++) select[i] = sel[i] != 0;
    Cplx *t = to_cm(T, n, n), *q = to_cm(U, n, n);
    Cplx *w = malloc(n * sizeof *w), work[1];
    double s = 0.0, sep = 0.0;
    ztrsen_("N", "V", select, &N, (double *)t, &N, (double *)q, &N, (double *)w, &m, &s, &sep,
            (double *)work, &lwork, &info);
    from_cm(T, t, n, n); from_cm(U, q, n, n);
    for (uint32_t i = 1; i < n; i++)
        for (uint32_t j = 0; j < i; j++) T[(size_t)i * n + j] = (Cplx){ 0, 0 };
    *k = (uint32_t)m;
    free(select); free(t); free(q); free(w);
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
    .gemm_d   = ob_gemm_d,
    .gemm_z   = ob_gemm_z,
    .eig_gen_d = ob_eig_gen_d,
    .det      = ob_det,
    .eig_herm = ob_eig_herm,
    .eig_gen  = ob_eig_gen,
    .schur    = ob_schur,
    .schur_d  = ob_schur_d,
    .ordschur = ob_ordschur,
    .ordschur_d = ob_ordschur_d,
    .svd      = ob_svd,
    .chol     = ob_chol,
};

const LinalgKernels *cozy_linalg(void) { return &openblas; }
