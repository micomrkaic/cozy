/* linalg.h — the linear-algebra dispatch seam (design entry 2).
 *
 * One kernel table, chosen at build time; the language never knows which
 * backend answered. Kernels operate on raw Cplx/double buffers only: no
 * Interp, no Value, no runtime_error — failures come back as status codes
 * and eval.c keeps ownership of marshalling, dispatch policy (e.g. the
 * Hermitian test), observable conventions (eigenpair ordering, eigenvector
 * phase), and every error message.
 *
 * Tiers (per design/DESIGN_NOTES.md entry 2):
 *   tier0 — the hand-rolled kernels inherited from Neutrino (linalg_tier0.c):
 *           always available, zero dependencies.
 *   tier1 — system BLAS/LAPACK (Accelerate/OpenBLAS/MKL), a future
 *           linalg_<backend>.c selected by `make BACKEND=<backend>`.
 *   tier2 — WASM reference kernels (tier0 rides the wasm bundle until a
 *           CLAPACK build earns its way in).
 */
#ifndef COZY_LINALG_H
#define COZY_LINALG_H

#include <stdint.h>
#include <math.h>
#include "value.h"                     /* Cplx */

/* ---- complex arithmetic, shared by the kernels and eval.c -------------- */

static inline Cplx c_add(Cplx a, Cplx b){ return (Cplx){ a.re + b.re, a.im + b.im }; }
static inline Cplx c_sub(Cplx a, Cplx b){ return (Cplx){ a.re - b.re, a.im - b.im }; }
static inline Cplx c_mul(Cplx a, Cplx b){ return (Cplx){ a.re*b.re - a.im*b.im, a.re*b.im + a.im*b.re }; }
static inline Cplx c_div(Cplx a, Cplx b){ double d = b.re*b.re + b.im*b.im;
    return (Cplx){ (a.re*b.re + a.im*b.im)/d, (a.im*b.re - a.re*b.im)/d }; }
static inline Cplx   c_conj(Cplx a)     { return (Cplx){ a.re, -a.im }; }
static inline double c_abs(Cplx a)      { return hypot(a.re, a.im); }
static inline Cplx   c_scale(double s, Cplx a) { return (Cplx){ s * a.re, s * a.im }; }
static inline Cplx c_sqrtz(Cplx z) {
    double m = hypot(z.re, z.im);
    double re = sqrt((m + z.re) * 0.5), im = sqrt((m - z.re) * 0.5);
    return (Cplx){ re, z.im < 0 ? -im : im };
}

/* ---- the kernel table --------------------------------------------------- */

typedef struct LinalgKernels {
    /* Backend identification, surfaced by the buildinfo builtin. */
    const char *name;

    /* Solve A X = B in place: A (n×n, row-major) is destroyed, B (n×m)
     * becomes X. Returns 0 on success, nonzero if A is singular. */
    int (*solve)(Cplx *A, Cplx *B, uint32_t n, uint32_t m);
    /* Real-typed fast paths (design entry 10, owner's ruling: real data on
     * real routines — faster AND more accurate: exact real arithmetic, no
     * imaginary residue to snap). NULL means "backend has no real path";
     * callers must fall back to the complex funnel. */
    int (*solve_d)(double *A, double *B, uint32_t n, uint32_t m);
    int (*det_d)(double *A, uint32_t n, double *out);
    int (*eig_sym_d)(double *A, uint32_t n, double *w, double *V);   /* row-major */
    int (*svd_d)(const double *A, uint32_t m, uint32_t n,
                 double *U, double *s, double *V);                    /* thin, row-major */
    int (*chol_d)(const double *A, uint32_t n, double *L);            /* lower, row-major */

    /* Determinant of A (n×n); A is destroyed. Result in *out (0 for a
     * singular matrix). Returns 0. n >= 1 (n == 0 is eval.c's case). */
    int (*det)(Cplx *A, uint32_t n, Cplx *out);

    /* Hermitian eigendecomposition. A (n×n, assumed Hermitian) is destroyed;
     * w receives the n real eigenvalues and V (n×n) the matching orthonormal
     * columns. Pairs come back in backend order — the caller sorts. */
    int (*eig_herm)(Cplx *A, uint32_t n, double *w, Cplx *V);

    /* General eigendecomposition. A (n×n) is preserved; w receives the n
     * eigenvalues and V (n×n) matching unit eigenvector columns. Pairs come
     * back in backend order with backend phase — the caller sorts pairs and
     * applies the phase convention (both are observable language behavior,
     * so they live above the seam). */
    int (*eig_gen)(const Cplx *A, uint32_t n, Cplx *w, Cplx *V);

    /* Thin SVD of A (m×n), any shape: A = U * diag(s) * V^H with
     * k = min(m, n); U is m×k, s length k DESCENDING (part of the
     * contract — every backend returns descending), V is n×k.
     * A is preserved. Returns 0. */
    int (*svd)(const Cplx *A, uint32_t m, uint32_t n,
               Cplx *U, double *s, Cplx *V);

    /* Cholesky factor L (lower) with L * L^H = A. A (n×n) is preserved;
     * L (n×n) is written. Returns nonzero if A is not positive definite. */
    int (*chol)(const Cplx *A, uint32_t n, Cplx *L);
} LinalgKernels;

/* The build-selected table. Exactly one linalg_<backend>.c is linked in. */
const LinalgKernels *cozy_linalg(void);

#endif
