/* sparse.h — CSR operations for the sparse value kind (design entry 1).
 *
 * Pure math on SpObj/ArrObj: no Interp, no runtime_error — eval.c checks
 * conformability and ownership of every message BEFORE calling, and owns
 * the promotion gates (zero-preserving ops arrive here; zero-breaking ops
 * are refused above with teaching errors). All constructors return +1.
 * elt promotion: float ∘ float -> float, anything with complex -> complex.
 */
#ifndef COZY_SPARSE_H
#define COZY_SPARSE_H

#include "value.h"

/* Build from 0-based triplets; duplicates are summed, zeros dropped. */
Value sp_from_triplets(EltType elt, uint32_t rows, uint32_t cols,
                       uint32_t cnt, const uint32_t *ri, const uint32_t *ci,
                       const void *vv);
Value sp_from_dense(const ArrObj *a);        /* elt FLOAT/INT/COMPLEX dense */
Value sp_to_dense(const SpObj *s);
Value sp_eye(uint32_t n);

Value sp_add(const SpObj *a, const SpObj *b, double sign);   /* a + sign*b */
Value sp_hadamard(const SpObj *a, const SpObj *b);           /* a .* b     */
Value sp_scale(const SpObj *a, Cplx k, bool k_real);         /* k != 0     */
Value sp_neg(const SpObj *a);
Value sp_transpose(const SpObj *a, bool conj);
Value sp_matvec(const SpObj *a, const ArrObj *v);            /* dense col out */
Cplx  sp_get(const SpObj *a, uint32_t i, uint32_t j);        /* 0-based    */

#endif
