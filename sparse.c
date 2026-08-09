/* sparse.c — CSR core (design entry 1). See sparse.h for the contract. */
#include <stdlib.h>
#include <string.h>
#include "sparse.h"

static inline Cplx spv(const SpObj *s, uint32_t k)
{
    if (s->elt == ELT_COMPLEX) return ((const Cplx *)s->vals)[k];
    return (Cplx){ ((const double *)s->vals)[k], 0.0 };
}
static inline void put(SpObj *s, uint32_t k, Cplx z)
{
    if (s->elt == ELT_COMPLEX) ((Cplx *)s->vals)[k] = z;
    else ((double *)s->vals)[k] = z.re;
}
static inline bool iszero(Cplx z) { return z.re == 0.0 && z.im == 0.0; }

/* ---- from triplets: sort (row, col), sum duplicates, drop zeros --------- */

typedef struct { uint32_t r, c; Cplx v; } Trip;

static int trip_cmp(const void *pa, const void *pb)
{
    const Trip *a = pa, *b = pb;
    if (a->r != b->r) return a->r < b->r ? -1 : 1;
    if (a->c != b->c) return a->c < b->c ? -1 : 1;
    return 0;
}

Value sp_from_triplets(EltType elt, uint32_t rows, uint32_t cols,
                       uint32_t cnt, const uint32_t *ri, const uint32_t *ci,
                       const void *vv)
{
    Trip *t = malloc((size_t)(cnt ? cnt : 1) * sizeof *t);
    for (uint32_t k = 0; k < cnt; k++) {
        t[k].r = ri[k]; t[k].c = ci[k];
        t[k].v = elt == ELT_COMPLEX ? ((const Cplx *)vv)[k]
                                    : (Cplx){ ((const double *)vv)[k], 0.0 };
    }
    qsort(t, cnt, sizeof *t, trip_cmp);
    uint32_t m = 0;                                   /* coalesce + drop zeros */
    for (uint32_t k = 0; k < cnt; ) {
        Cplx acc = t[k].v; uint32_t r = t[k].r, c = t[k].c; k++;
        while (k < cnt && t[k].r == r && t[k].c == c) {
            acc.re += t[k].v.re; acc.im += t[k].v.im; k++;
        }
        if (!iszero(acc)) { t[m].r = r; t[m].c = c; t[m].v = acc; m++; }
    }
    Value out = val_sparse(elt, rows, cols, m);
    SpObj *s = as_sp(out);
    for (uint32_t k = 0; k < m; k++) {
        s->rowptr[t[k].r + 1]++;
        s->colind[k] = t[k].c;
        put(s, k, t[k].v);
    }
    for (uint32_t i = 0; i < rows; i++) s->rowptr[i + 1] += s->rowptr[i];
    free(t);
    return out;
}

Value sp_from_dense(const ArrObj *a)
{
    bool cx = a->elt == ELT_COMPLEX;
    uint32_t nnz = 0;
    size_t n = (size_t)a->rows * a->cols;
    for (size_t k = 0; k < n; k++) {
        Cplx z = cx ? ((const Cplx *)a->data)[k]
                    : (Cplx){ a->elt == ELT_INT ? (double)((const int64_t *)a->data)[k]
                                                : ((const double *)a->data)[k], 0.0 };
        if (!iszero(z)) nnz++;
    }
    Value out = val_sparse(cx ? ELT_COMPLEX : ELT_FLOAT, a->rows, a->cols, nnz);
    SpObj *s = as_sp(out);
    uint32_t m = 0;
    for (uint32_t i = 0; i < a->rows; i++) {
        for (uint32_t j = 0; j < a->cols; j++) {
            size_t k = (size_t)i * a->cols + j;
            Cplx z = cx ? ((const Cplx *)a->data)[k]
                        : (Cplx){ a->elt == ELT_INT ? (double)((const int64_t *)a->data)[k]
                                                    : ((const double *)a->data)[k], 0.0 };
            if (!iszero(z)) { s->colind[m] = j; put(s, m, z); m++; }
        }
        s->rowptr[i + 1] = m;
    }
    return out;
}

Value sp_to_dense(const SpObj *s)
{
    bool cx = s->elt == ELT_COMPLEX;
    Value out = val_array(cx ? ELT_COMPLEX : ELT_FLOAT, s->rows, s->cols);
    ArrObj *a = as_arr(out);
    memset(a->data, 0, (size_t)s->rows * s->cols * (cx ? sizeof(Cplx) : sizeof(double)));
    for (uint32_t i = 0; i < s->rows; i++)
        for (uint32_t k = s->rowptr[i]; k < s->rowptr[i + 1]; k++) {
            size_t at = (size_t)i * s->cols + s->colind[k];
            if (cx) ((Cplx *)a->data)[at] = spv(s, k);
            else    ((double *)a->data)[at] = spv(s, k).re;
        }
    return out;
}

Value sp_eye(uint32_t n)
{
    Value out = val_sparse(ELT_FLOAT, n, n, n);
    SpObj *s = as_sp(out);
    for (uint32_t i = 0; i < n; i++) {
        s->rowptr[i + 1] = i + 1;
        s->colind[i] = i;
        ((double *)s->vals)[i] = 1.0;
    }
    return out;
}

/* ---- merges (dims already checked by the caller) ------------------------ */

Value sp_add(const SpObj *a, const SpObj *b, double sign)
{
    EltType elt = (a->elt == ELT_COMPLEX || b->elt == ELT_COMPLEX) ? ELT_COMPLEX : ELT_FLOAT;
    uint32_t cap = a->nnz + b->nnz;
    uint32_t *ci = malloc((size_t)(cap ? cap : 1) * sizeof *ci);
    Cplx    *cv = malloc((size_t)(cap ? cap : 1) * sizeof *cv);
    uint32_t *rp = calloc((size_t)a->rows + 1, sizeof *rp);
    uint32_t m = 0;
    for (uint32_t i = 0; i < a->rows; i++) {
        uint32_t ka = a->rowptr[i], ea = a->rowptr[i + 1];
        uint32_t kb = b->rowptr[i], eb = b->rowptr[i + 1];
        while (ka < ea || kb < eb) {
            uint32_t ca = ka < ea ? a->colind[ka] : UINT32_MAX;
            uint32_t cb = kb < eb ? b->colind[kb] : UINT32_MAX;
            Cplx z; uint32_t c;
            if (ca < cb)      { z = spv(a, ka); c = ca; ka++; }
            else if (cb < ca) { Cplx w = spv(b, kb); z = (Cplx){ sign * w.re, sign * w.im }; c = cb; kb++; }
            else { Cplx u = spv(a, ka), w = spv(b, kb);
                   z = (Cplx){ u.re + sign * w.re, u.im + sign * w.im }; c = ca; ka++; kb++; }
            if (!iszero(z)) { ci[m] = c; cv[m] = z; m++; }
        }
        rp[i + 1] = m;
    }
    Value out = val_sparse(elt, a->rows, a->cols, m);
    SpObj *s = as_sp(out);
    memcpy(s->rowptr, rp, ((size_t)a->rows + 1) * sizeof *rp);
    memcpy(s->colind, ci, (size_t)(m ? m : 1) * sizeof *ci);
    for (uint32_t k = 0; k < m; k++) put(s, k, cv[k]);
    free(ci); free(cv); free(rp);
    return out;
}

Value sp_hadamard(const SpObj *a, const SpObj *b)
{
    EltType elt = (a->elt == ELT_COMPLEX || b->elt == ELT_COMPLEX) ? ELT_COMPLEX : ELT_FLOAT;
    uint32_t cap = a->nnz < b->nnz ? a->nnz : b->nnz;
    uint32_t *ci = malloc((size_t)(cap ? cap : 1) * sizeof *ci);
    Cplx    *cv = malloc((size_t)(cap ? cap : 1) * sizeof *cv);
    uint32_t *rp = calloc((size_t)a->rows + 1, sizeof *rp);
    uint32_t m = 0;
    for (uint32_t i = 0; i < a->rows; i++) {
        uint32_t ka = a->rowptr[i], ea = a->rowptr[i + 1];
        uint32_t kb = b->rowptr[i], eb = b->rowptr[i + 1];
        while (ka < ea && kb < eb) {
            uint32_t ca = a->colind[ka], cb = b->colind[kb];
            if (ca < cb) ka++;
            else if (cb < ca) kb++;
            else {
                Cplx u = spv(a, ka), w = spv(b, kb);
                Cplx z = { u.re * w.re - u.im * w.im, u.re * w.im + u.im * w.re };
                if (!iszero(z)) { ci[m] = ca; cv[m] = z; m++; }
                ka++; kb++;
            }
        }
        rp[i + 1] = m;
    }
    Value out = val_sparse(elt, a->rows, a->cols, m);
    SpObj *s = as_sp(out);
    memcpy(s->rowptr, rp, ((size_t)a->rows + 1) * sizeof *rp);
    memcpy(s->colind, ci, (size_t)(m ? m : 1) * sizeof *ci);
    for (uint32_t k = 0; k < m; k++) put(s, k, cv[k]);
    free(ci); free(cv); free(rp);
    return out;
}

Value sp_scale(const SpObj *a, Cplx k, bool k_real)
{
    EltType elt = (!k_real || a->elt == ELT_COMPLEX) ? ELT_COMPLEX : ELT_FLOAT;
    Value out = val_sparse(elt, a->rows, a->cols, a->nnz);
    SpObj *s = as_sp(out);
    memcpy(s->rowptr, a->rowptr, ((size_t)a->rows + 1) * sizeof *s->rowptr);
    memcpy(s->colind, a->colind, (size_t)(a->nnz ? a->nnz : 1) * sizeof *s->colind);
    for (uint32_t j = 0; j < a->nnz; j++) {
        Cplx u = spv(a, j);
        put(s, j, (Cplx){ u.re * k.re - u.im * k.im, u.re * k.im + u.im * k.re });
    }
    return out;
}

Value sp_neg(const SpObj *a) { return sp_scale(a, (Cplx){ -1.0, 0.0 }, true); }

Value sp_transpose(const SpObj *a, bool conj)
{
    Value out = val_sparse(a->elt, a->cols, a->rows, a->nnz);
    SpObj *s = as_sp(out);
    uint32_t *cnt = calloc((size_t)a->cols + 1, sizeof *cnt);
    for (uint32_t k = 0; k < a->nnz; k++) cnt[a->colind[k] + 1]++;
    for (uint32_t j = 0; j < a->cols; j++) cnt[j + 1] += cnt[j];
    memcpy(s->rowptr, cnt, ((size_t)a->cols + 1) * sizeof *cnt);
    for (uint32_t i = 0; i < a->rows; i++)
        for (uint32_t k = a->rowptr[i]; k < a->rowptr[i + 1]; k++) {
            uint32_t j = a->colind[k], at = cnt[j]++;
            s->colind[at] = i;
            Cplx z = spv(a, k);
            if (conj) z.im = -z.im;
            put(s, at, z);
        }
    free(cnt);
    return out;
}

Value sp_matvec(const SpObj *a, const ArrObj *v)
{
    bool cx = a->elt == ELT_COMPLEX || v->elt == ELT_COMPLEX;
    Value out = val_array(cx ? ELT_COMPLEX : ELT_FLOAT, a->rows, 1);
    ArrObj *o = as_arr(out);
    for (uint32_t i = 0; i < a->rows; i++) {
        Cplx acc = { 0, 0 };
        for (uint32_t k = a->rowptr[i]; k < a->rowptr[i + 1]; k++) {
            uint32_t j = a->colind[k];
            Cplx x = v->elt == ELT_COMPLEX ? ((const Cplx *)v->data)[j]
                   : (Cplx){ v->elt == ELT_INT ? (double)((const int64_t *)v->data)[j]
                                               : ((const double *)v->data)[j], 0.0 };
            Cplx u = spv(a, k);
            acc.re += u.re * x.re - u.im * x.im;
            acc.im += u.re * x.im + u.im * x.re;
        }
        if (cx) ((Cplx *)o->data)[i] = acc;
        else    ((double *)o->data)[i] = acc.re;
    }
    return out;
}

Cplx sp_get(const SpObj *a, uint32_t i, uint32_t j)
{
    uint32_t lo = a->rowptr[i], hi = a->rowptr[i + 1];
    while (lo < hi) {                                 /* binary search the row */
        uint32_t mid = lo + (hi - lo) / 2;
        if (a->colind[mid] < j) lo = mid + 1;
        else if (a->colind[mid] > j) hi = mid;
        else return spv(a, mid);
    }
    return (Cplx){ 0, 0 };
}
