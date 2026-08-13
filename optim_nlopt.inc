/* optim_nlopt.inc — included by eval.c under COZY_NLOPT (entry 14, steps 1-2).
 *
 * nlmin(f, x0, opts) drives NLopt with EXACT derivatives: gradients come
 * from dual-number passes through the user's closure — no finite
 * differences anywhere. opts record: {alg = "slsqp"|"lbfgs"|"bobyla"|
 * "cobyla", eq = fn, ineq = fn, lb, ub, xtol, maxeval}.
 *
 * Callback discipline (LESSONS §2, designed in the docket entry): a
 * runtime error inside the user's objective must not longjmp through
 * NLopt's C frames. Each trampoline saves I->jmp, installs its own,
 * and on unwind flags the context, force-stops NLopt, and returns NaN;
 * nlmin re-raises after nlopt_optimize returns.
 */
#include <nlopt.h>

typedef struct {
    Interp  *I;
    Value    f;          /* objective or constraint closure */
    unsigned n;          /* parameter count */
    unsigned m;          /* constraint count (mconstraint) */
    nlopt_opt opt;
    bool     failed;     /* user code raised; error text is in I */
} NlCtx;

/* one pass: call closure with x (dual-seeded in coordinate `seed`, or
 * plain floats when seed < 0); returns the (value, derivative) of result
 * component `comp` (scalars: comp 0). On user error: ctx->failed. */
static bool nl_pass(NlCtx *c, const double *x, int seed,
                    double *val, double *der, uint32_t comp)
{
    Interp *I = c->I;
    jmp_buf saved;
    memcpy(saved, I->jmp, sizeof(jmp_buf));
    if (setjmp(I->jmp) != 0) {                 /* user objective raised */
        memcpy(I->jmp, saved, sizeof(jmp_buf));
        c->failed = true;
        if (c->opt) nlopt_force_stop(c->opt);
        return false;
    }
    Value xa = val_array(seed >= 0 ? ELT_DUAL : ELT_FLOAT, c->n, 1);
    ArrObj *a = as_arr(xa);
    if (seed >= 0) {
        Dual *d = (Dual *)a->data;
        for (unsigned k = 0; k < c->n; k++)
            d[k] = (Dual){ x[k], k == (unsigned)seed ? 1.0 : 0.0 };
    } else {
        double *dd = (double *)a->data;
        for (unsigned k = 0; k < c->n; k++) dd[k] = x[k];
    }
    Value r = call_value(I, c->f, &xa, 1);
    memcpy(I->jmp, saved, sizeof(jmp_buf));
    value_release(xa);
    Value comp_v = r;
    bool rel = false;
    if (is_array(r)) { comp_v = value_retain(arr_get(as_arr(r), comp)); rel = true; }
    double v, e;
    if (comp_v.kind == VAL_DUAL)      { v = comp_v.as.d.v; e = comp_v.as.d.e; }
    else if (comp_v.kind == VAL_FLOAT){ v = comp_v.as.f;   e = 0.0; }
    else if (comp_v.kind == VAL_INT)  { v = (double)comp_v.as.i; e = 0.0; }
    else { v = e = 0.0 / 0.0; }
    if (rel) value_release(comp_v);
    value_release(r);
    *val = v; if (der) *der = e;
    return true;
}

static double nl_objective(unsigned n, const double *x, double *grad, void *data)
{
    NlCtx *c = data; (void)n;
    double v = 0.0 / 0.0, e;
    if (grad) {
        for (unsigned j = 0; j < c->n; j++) {
            if (!nl_pass(c, x, (int)j, &v, &e, 0)) return v;
            grad[j] = e;
        }
    } else if (!nl_pass(c, x, -1, &v, NULL, 0)) return v;
    return v;
}

/* vector constraint: result[i] = h_i(x); jac is m x n row-major */
static void nl_mconstraint(unsigned m, double *result, unsigned n,
                           const double *x, double *jac, void *data)
{
    NlCtx *c = data; (void)n;
    double v, e;
    if (jac) {
        for (unsigned j = 0; j < c->n; j++)
            for (unsigned i = 0; i < m; i++) {
                if (!nl_pass(c, x, (int)j, &v, &e, i)) return;
                jac[(size_t)i * c->n + j] = e;
                if (j == 0) result[i] = v;
            }
    } else
        for (unsigned i = 0; i < m; i++)
            if (!nl_pass(c, x, -1, &result[i], NULL, i)) return;
}

/* count a constraint closure's output length by one probe at x0 */
static unsigned nl_probe_m(NlCtx *c, const double *x0)
{
    jmp_buf saved; Interp *I = c->I;
    memcpy(saved, I->jmp, sizeof(jmp_buf));
    if (setjmp(I->jmp) != 0) {
        memcpy(I->jmp, saved, sizeof(jmp_buf));
        c->failed = true; return 0;
    }
    Value xa = val_array(ELT_FLOAT, c->n, 1);
    double *dd = (double *)as_arr(xa)->data;
    for (unsigned k = 0; k < c->n; k++) dd[k] = x0[k];
    Value r = call_value(I, c->f, &xa, 1);
    memcpy(I->jmp, saved, sizeof(jmp_buf));
    value_release(xa);
    unsigned m = is_array(r) ? (unsigned)((size_t)as_arr(r)->rows * as_arr(r)->cols) : 1;
    value_release(r);
    return m;
}

static Value bi_nlmin(Interp *I, Value *args, uint32_t n)
{
    if (args[0].kind != VAL_CLOSURE && args[0].kind != VAL_BUILTIN)
        runtime_error(I, "nlmin: expected a function");
    if (!is_array(args[1]) ||
        (as_arr(args[1])->elt != ELT_FLOAT && as_arr(args[1])->elt != ELT_INT))
        runtime_error(I, "nlmin: x0 must be a numeric vector");
    ArrObj *x0a = as_arr(args[1]);
    unsigned N = (unsigned)((size_t)x0a->rows * x0a->cols);
    RecObj *opts = (n == 3 && args[2].kind == VAL_RECORD) ? as_rec(args[2]) : NULL;

    const char *alg = "slsqp";
    Value av = opts ? rec_field(opts, "alg") : val_null();
    if (av.kind == VAL_STRING) alg = as_str(av)->data;
    nlopt_algorithm A = NLOPT_LD_SLSQP;
    if (!strcmp(alg, "lbfgs"))  A = NLOPT_LD_LBFGS;
    else if (!strcmp(alg, "bobyqa")) A = NLOPT_LN_BOBYQA;
    else if (!strcmp(alg, "cobyla")) A = NLOPT_LN_COBYLA;
    else if (strcmp(alg, "slsqp"))
        runtime_error(I, "nlmin: unknown alg '%s' (slsqp, lbfgs, bobyqa, cobyla)", alg);

    nlopt_opt opt = nlopt_create(A, N);
    NlCtx fctx = { I, args[0], N, 0, opt, false };
    nlopt_set_min_objective(opt, nl_objective, &fctx);

    double xtol = 1e-8; long maxeval = 20000;
    if (opts) {
        Value tv = rec_field(opts, "xtol");
        if (tv.kind == VAL_FLOAT) xtol = tv.as.f;
        Value mv = rec_field(opts, "maxeval");
        if (mv.kind == VAL_INT) maxeval = (long)mv.as.i;
        Value lb = rec_field(opts, "lb"), ub = rec_field(opts, "ub");
        if (is_array(lb)) {
            double *b = malloc(N * sizeof *b);
            for (unsigned k = 0; k < N; k++) b[k] = as_double(arr_get(as_arr(lb), k));
            nlopt_set_lower_bounds(opt, b); free(b);
        }
        if (is_array(ub)) {
            double *b = malloc(N * sizeof *b);
            for (unsigned k = 0; k < N; k++) b[k] = as_double(arr_get(as_arr(ub), k));
            nlopt_set_upper_bounds(opt, b); free(b);
        }
    }
    nlopt_set_xtol_rel(opt, xtol);
    nlopt_set_maxeval(opt, (int)maxeval);

    double *x = malloc((N ? N : 1) * sizeof *x);
    if (!x) abort();
    for (unsigned k = 0; k < N; k++) x[k] = as_double(arr_get(x0a, k));

    NlCtx ectx = { I, val_null(), N, 0, opt, false };
    NlCtx ictx = { I, val_null(), N, 0, opt, false };
    double *tol = NULL;
    if (opts) {
        Value eq = rec_field(opts, "eq");
        if (eq.kind == VAL_CLOSURE || eq.kind == VAL_BUILTIN) {
            ectx.f = eq;
            ectx.m = nl_probe_m(&ectx, x);
            if (!ectx.failed && ectx.m) {
                tol = calloc(ectx.m, sizeof *tol);
                for (unsigned i = 0; i < ectx.m; i++) tol[i] = 1e-8;
                nlopt_add_equality_mconstraint(opt, ectx.m, nl_mconstraint, &ectx, tol);
            }
        }
        Value iq = rec_field(opts, "ineq");
        if (iq.kind == VAL_CLOSURE || iq.kind == VAL_BUILTIN) {
            ictx.f = iq;
            ictx.m = nl_probe_m(&ictx, x);
            if (!ictx.failed && ictx.m) {
                double *tol2 = calloc(ictx.m, sizeof *tol2);
                for (unsigned i = 0; i < ictx.m; i++) tol2[i] = 1e-8;
                nlopt_add_inequality_mconstraint(opt, ictx.m, nl_mconstraint, &ictx, tol2);
                free(tol2);
            }
        }
    }

    double fmin = 0.0;
    nlopt_result rc = (fctx.failed || ectx.failed || ictx.failed)
                      ? NLOPT_FORCED_STOP : nlopt_optimize(opt, x, &fmin);
    int evals = nlopt_get_numevals(opt);
    bool user_err = fctx.failed || ectx.failed || ictx.failed;
    free(tol);
    nlopt_destroy(opt);

    if (user_err) {
        free(x);
        longjmp(I->jmp, 1);                    /* re-raise the user's error */
    }

    Value xv = val_array(ELT_FLOAT, x0a->rows, x0a->cols);
    memcpy(as_arr(xv)->data, x, N * sizeof(double));
    free(x);
    Value out = record4("x", xv, "fx", val_float(fmin),
                        "iters", val_int(evals),
                        "converged", val_bool(rc > 0 && rc != NLOPT_MAXEVAL_REACHED));
    return out;
}
