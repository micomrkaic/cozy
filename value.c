/* value.c */
#include <math.h>
#include "value.h"
#include "chunk.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* ---- number display format (Float / Complex parts) ---------------- */
static NumFmtStyle g_fmt_style = NFMT_G;
static int         g_fmt_prec  = 6;          /* startup default == bare %g */
static bool        g_fmt_trail = false;      /* keep trailing zeros ('#'): on for explicit formats */

void value_format_set(NumFmtStyle style, int prec)
{
    if (prec < 0)  prec = 0;
    if (prec > 17) prec = 17;                /* a double carries ~17 sig digits */
    g_fmt_style = style; g_fmt_prec = prec;
    g_fmt_trail = true;                      /* an explicitly chosen format is consistent-width */
}
static void format_reset_default(void)
{
    g_fmt_style = NFMT_G; g_fmt_prec = 6; g_fmt_trail = false;   /* bare %g (golden-compatible) */
}
void value_format_get(NumFmtStyle *style, int *prec, bool *trailing)
{
    *style = g_fmt_style; *prec = g_fmt_prec; *trailing = g_fmt_trail;
}
void value_format_restore(NumFmtStyle style, int prec, bool trailing)
{
    g_fmt_style = style; g_fmt_prec = prec; g_fmt_trail = trailing;
}
bool value_format_by_name(const char *name)
{
    if (strcmp(name, "default") == 0 || strcmp(name, "reset") == 0) {
        format_reset_default();                          /* bare %g, variable width */
        return true;
    }
    struct { const char *n; NumFmtStyle s; int p; } t[] = {
        { "short",   NFMT_G, 5 },  { "long",   NFMT_G, 16 },
        { "short g", NFMT_G, 5 },  { "long g", NFMT_G, 16 },
        { "short e", NFMT_E, 4 },  { "long e", NFMT_E, 15 },
        { "short f", NFMT_F, 4 },  { "long f", NFMT_F, 14 },
    };
    for (size_t i = 0; i < sizeof t / sizeof *t; i++)
        if (strcmp(t[i].n, name) == 0) { value_format_set(t[i].s, t[i].p); return true; }
    return false;
}
const char *value_format_desc(void)
{
    static char buf[64];
    const char *style = g_fmt_style == NFMT_F ? "fixed"
                      : g_fmt_style == NFMT_E ? "scientific" : "auto";
    const char *unit  = g_fmt_style == NFMT_G ? "significant digits" : "decimals";
    snprintf(buf, sizeof buf, "%s, %d %s", style, g_fmt_prec, unit);
    return buf;
}
static void num_spec(char *spec, size_t n)
{
    char conv = g_fmt_style == NFMT_F ? 'f' : g_fmt_style == NFMT_E ? 'e' : 'g';
    snprintf(spec, n, g_fmt_trail && conv == 'g' ? "%%#.%d%c" : "%%.%d%c", g_fmt_prec, conv);
}
static void fmt_double(FILE *out, double x)
{
    if (isnan(x)) { fputs("nan", out); return; }   /* a NaN's sign bit is noise */
    char spec[8]; num_spec(spec, sizeof spec);
    fprintf(out, spec, x);
}
static int fmt_double_str(char *buf, size_t cap, double x)
{
    if (isnan(x)) return snprintf(buf, cap, "nan");
    char spec[8]; num_spec(spec, sizeof spec);
    return snprintf(buf, cap, spec, x);
}

/* ---- multi-line aligned matrix display (opt-in; REPL turns it on) ---- */
static bool g_multiline;
void value_set_multiline(bool on) { g_multiline = on; }
bool value_multiline(void)        { return g_multiline; }

/* ---- output stream indirection ------------------------------------ */
static FILE *g_out;                          /* nullptr means stdout */
FILE *vout(void)            { return g_out ? g_out : stdout; }
void  value_set_out(FILE *f) { g_out = f; }


size_t elt_size(EltType e)
{
    switch (e) {
    case ELT_STRING:  return sizeof(StrObj *);
    case ELT_INT:     return sizeof(int64_t);
    case ELT_FLOAT:   return sizeof(double);
    case ELT_DATE:    return sizeof(double);
    case ELT_COMPLEX: return sizeof(Cplx);
    case ELT_DUAL:    return sizeof(Dual);
    case ELT_HDUAL:   return sizeof(HDual);
    case ELT_BOOL:    return sizeof(unsigned char);
    }
    return 0;
}

/* ------------------------------------------------------------------ */
/* refcounting                                                         */
/* ------------------------------------------------------------------ */
static void env_free(EnvObj *e);

static void obj_free(Obj *o)
{
    switch (o->kind) {
    case VAL_STRING:
        free(((StrObj *)o)->data);
        break;
    case VAL_ARRAY: {
        ArrObj *a = (ArrObj *)o;
        if (a->elt == ELT_STRING) {
            StrObj **cells = (StrObj **)a->data;
            size_t n = (size_t)a->rows * a->cols;
            for (size_t k = 0; k < n; k++)
                if (cells[k]) value_release((Value){ .kind = VAL_STRING, .as.obj = &cells[k]->obj });
        }
        free(a->data);
        break;
    }
    case VAL_RECORD: {
        RecObj *r = (RecObj *)o;
        for (uint32_t i = 0; i < r->count; i++) value_release(r->vals[i]);
        if (r->owns_keys)
            for (uint32_t i = 0; i < r->count; i++) free((char *)r->keys[i]);
        free(r->keys); free(r->keylens); free(r->vals);
        break;
    }
    case VAL_SPARSE: {
        SpObj *s = (SpObj *)o;
        free(s->rowptr); free(s->colind); free(s->vals);
        break;
    }
    case VAL_CLOSURE: {
        CloObj *cl = (CloObj *)o;
        for (uint32_t i = 0; i < cl->nupvalues; i++) value_release(cl->upvalues[i]);
        free(cl->upvalues);
        break;
    }
    default:
        break;
    }
    free(o);
}

/* Immediacy is a property of the KIND, not the enum position: appended kinds
 * (VAL_DUAL) land numerically above VAL_STRING, so the old `kind >= VAL_STRING`
 * range test dereferenced a dual's doubles as a pointer (caught by ASan the
 * day dual arrived — the appended-kind trap, second occurrence). */
static inline bool kind_is_heap(ValueKind k)
{
    switch (k) {
    case VAL_STRING: case VAL_ARRAY: case VAL_RECORD:
    case VAL_CLOSURE: case VAL_BUILTIN: case VAL_SPARSE:
        return true;
    default:
        return false;                 /* null, bool, int, float, complex, dual */
    }
}

Value value_retain(Value v)
{
    if (kind_is_heap(v.kind) && v.as.obj) v.as.obj->rc++;
    return v;
}

void value_release(Value v)
{
    if (!kind_is_heap(v.kind) || !v.as.obj) return;
    Obj *o = v.as.obj;
    if (--o->rc == 0) obj_free(o);
}

/* closures hold an EnvObj*; route env retain/release through Obj rc */
void env_retain(EnvObj *e) { if (e) e->obj.rc++; }
void env_release(EnvObj *e)
{
    if (!e) return;
    if (--e->obj.rc == 0) env_free(e);
}
static Obj *alloc_obj(size_t size, ValueKind kind)
{
    Obj *o = calloc(1, size);
    if (!o) abort();
    o->kind = kind;
    o->rc = 1;
    return o;
}

/* ------------------------------------------------------------------ */
/* heap constructors                                                   */
/* ------------------------------------------------------------------ */
Value val_string(const char *bytes, uint32_t len)
{
    StrObj *s = (StrObj *)alloc_obj(sizeof *s, VAL_STRING);
    s->data = malloc(len + 1u);
    if (!s->data) abort();
    memcpy(s->data, bytes, len);
    s->data[len] = '\0';
    s->len = len;
    return (Value){ .kind = VAL_STRING, .as.obj = &s->obj };
}

Value val_array(EltType elt, uint32_t rows, uint32_t cols)
{
    ArrObj *a = (ArrObj *)alloc_obj(sizeof *a, VAL_ARRAY);
    a->elt = elt; a->rows = rows; a->cols = cols;
    size_t n = (size_t)rows * cols;
    a->data = n ? calloc(n, elt_size(elt)) : nullptr;
    if (n && !a->data) abort();
    return (Value){ .kind = VAL_ARRAY, .as.obj = &a->obj };
}

Value val_sparse(EltType elt, uint32_t rows, uint32_t cols, uint32_t nnz)
{
    SpObj *s = malloc(sizeof *s);
    s->obj = (Obj){ .kind = VAL_SPARSE, .rc = 1 };
    s->elt = elt; s->rows = rows; s->cols = cols; s->nnz = nnz;
    s->rowptr = calloc((size_t)rows + 1, sizeof *s->rowptr);
    s->colind = malloc((size_t)(nnz ? nnz : 1) * sizeof *s->colind);
    s->vals   = malloc((size_t)(nnz ? nnz : 1) * (elt == ELT_COMPLEX ? sizeof(Cplx) : sizeof(double)));
    return (Value){ .kind = VAL_SPARSE, .as.obj = &s->obj };
}

Value val_record(uint32_t count)
{
    RecObj *r = (RecObj *)alloc_obj(sizeof *r, VAL_RECORD);
    r->count = count;
    r->keys    = count ? calloc(count, sizeof *r->keys)    : nullptr;
    r->keylens = count ? calloc(count, sizeof *r->keylens) : nullptr;
    r->vals    = count ? calloc(count, sizeof *r->vals)    : nullptr;
    return (Value){ .kind = VAL_RECORD, .as.obj = &r->obj };
}

Value val_closure_vm(struct Chunk *proto, Value *upvalues, uint32_t nup)
{
    CloObj *c = (CloObj *)alloc_obj(sizeof *c, VAL_CLOSURE);
    c->chunk = proto; c->upvalues = upvalues; c->nupvalues = nup;
    return (Value){ .kind = VAL_CLOSURE, .as.obj = &c->obj };
}

Value val_builtin(const char *name, BuiltinFn fn, uint32_t min_arity, uint32_t max_arity)
{
    BuiltinObj *b = (BuiltinObj *)alloc_obj(sizeof *b, VAL_BUILTIN);
    b->name = name; b->fn = fn; b->min_arity = min_arity; b->max_arity = max_arity;
    return (Value){ .kind = VAL_BUILTIN, .as.obj = &b->obj };
}

/* ------------------------------------------------------------------ */
/* array element access                                                */
/* ------------------------------------------------------------------ */
/* Immortal empty string: arr_get of a never-written ELT_STRING cell returns
 * this borrowed singleton (cells are calloc'd null until arr_set). */
static StrObj *empty_str_singleton(void)
{
    static StrObj s; static char nul[1] = "";
    if (s.obj.rc == 0) { s.obj.kind = VAL_STRING; s.obj.rc = 1u << 30; s.len = 0; s.data = nul; }
    return &s;
}

Value arr_get(const ArrObj *a, size_t k)
{
    switch (a->elt) {
    case ELT_STRING: {
        StrObj *p = ((StrObj **)a->data)[k];
        if (!p) p = empty_str_singleton();
        return (Value){ .kind = VAL_STRING, .as.obj = &p->obj };   /* borrowed */
    }
    case ELT_INT:     return val_int(((const int64_t *)a->data)[k]);
    case ELT_FLOAT:   return val_float(((const double *)a->data)[k]);
    case ELT_DATE:    return val_date(((const double *)a->data)[k]);
    case ELT_COMPLEX: { Cplx c = ((const Cplx *)a->data)[k]; return val_complex(c.re, c.im); }
    case ELT_DUAL:    { Dual d = ((const Dual *)a->data)[k]; return val_dual(d.v, d.e); }
    case ELT_HDUAL:   { HDual q = ((const HDual *)a->data)[k]; return val_hdual(q.v, q.e1, q.e2, q.e12); }
    case ELT_BOOL:    return val_bool(((const unsigned char *)a->data)[k] != 0);
    }
    return val_null();
}

void arr_set(ArrObj *a, size_t k, Value v)
{
    switch (a->elt) {
    case ELT_STRING: {
        StrObj **cell = &((StrObj **)a->data)[k];
        StrObj *incoming = (v.kind == VAL_STRING) ? as_str(v) : nullptr;
        if (incoming) value_retain(v);                 /* retain before release: self-assign safe */
        if (*cell) value_release((Value){ .kind = VAL_STRING, .as.obj = &(*cell)->obj });
        *cell = incoming;
        break;
    }
    case ELT_INT:
        ((int64_t *)a->data)[k] = (v.kind == VAL_INT) ? v.as.i : (int64_t)v.as.f;
        break;
    case ELT_FLOAT:
        ((double *)a->data)[k] = (v.kind == VAL_INT) ? (double)v.as.i : v.as.f;
        break;
    case ELT_DATE:
        ((double *)a->data)[k] = v.as.f;             /* dates store their days */
        break;
    case ELT_COMPLEX: {
        Cplx c;
        if      (v.kind == VAL_INT)     c = (Cplx){ (double)v.as.i, 0.0 };
        else if (v.kind == VAL_FLOAT)   c = (Cplx){ v.as.f, 0.0 };
        else                            c = v.as.z;
        ((Cplx *)a->data)[k] = c;
        break;
    }
    case ELT_DUAL: {
        Dual d;
        if      (v.kind == VAL_INT)   d = (Dual){ (double)v.as.i, 0.0 };
        else if (v.kind == VAL_FLOAT) d = (Dual){ v.as.f, 0.0 };
        else if (v.kind == VAL_BOOL)  d = (Dual){ v.as.b ? 1.0 : 0.0, 0.0 };
        else                          d = v.as.d;    /* eval.c gates complex before here */
        ((Dual *)a->data)[k] = d;
        break;
    }
    case ELT_HDUAL: {
        HDual q;
        if      (v.kind == VAL_INT)   q = (HDual){ (double)v.as.i, 0, 0, 0 };
        else if (v.kind == VAL_FLOAT) q = (HDual){ v.as.f, 0, 0, 0 };
        else if (v.kind == VAL_BOOL)  q = (HDual){ v.as.b ? 1.0 : 0.0, 0, 0, 0 };
        else                          q = v.as.h;   /* eval.c gates dual/complex first */
        ((HDual *)a->data)[k] = q;
        break;
    }
    case ELT_BOOL:
        ((unsigned char *)a->data)[k] =
            (v.kind == VAL_BOOL) ? (v.as.b ? 1 : 0)
          : (v.kind == VAL_INT)  ? (v.as.i != 0)
          :                        (v.as.f != 0.0);
        break;
    }
}

/* ------------------------------------------------------------------ */
/* environment                                                         */
/* ------------------------------------------------------------------ */
EnvObj *env_new(EnvObj *parent)
{
    EnvObj *e = (EnvObj *)alloc_obj(sizeof *e, VAL_NULL);  /* kind unused for env */
    e->parent = parent;
    env_retain(parent);
    return e;
}

static void env_free(EnvObj *e)
{
    for (uint32_t i = 0; i < e->count; i++)
        { value_release(e->vals[i]); free((char *)e->names[i]); }
    free(e->names); free(e->namelens); free(e->vals);
    env_release(e->parent);
    free(e);
}

void env_define(EnvObj *e, const char *name, uint32_t len, Value v)
{
    /* Replace an existing binding in this frame — but never a protected
     * (standard-library) slot: shadowing those APPENDS, so `clear` can
     * remove the shadow and the original survives underneath. */
    for (uint32_t i = e->n_protected; i < e->count; i++) {
        if (e->namelens[i] == len && memcmp(e->names[i], name, len) == 0) {
            value_release(e->vals[i]);
            e->vals[i] = value_retain(v);
            return;
        }
    }
    if (e->count == e->cap) {
        e->cap = e->cap ? e->cap * 2 : 8;
        e->names    = realloc(e->names,    e->cap * sizeof *e->names);
        e->namelens = realloc(e->namelens, e->cap * sizeof *e->namelens);
        e->vals     = realloc(e->vals,     e->cap * sizeof *e->vals);
        if (!e->names || !e->namelens || !e->vals) abort();
    }
    e->names[e->count]    = strndup(name, len);   /* env OWNS names (entry 11):
                                       bindings must outlive the line's source */
    if (!e->names[e->count]) abort();
    e->namelens[e->count] = len;
    e->vals[e->count]     = value_retain(v);
    e->count++;
}

bool env_lookup(EnvObj *e, const char *name, uint32_t len, Value *out)
{
    for (; e; e = e->parent)
        for (uint32_t i = e->count; i > 0; i--)          /* newest binding wins */
            if (e->namelens[i - 1] == len && memcmp(e->names[i - 1], name, len) == 0) {
                *out = value_retain(e->vals[i - 1]);
                return true;
            }
    return false;
}

bool env_assign(EnvObj *e, const char *name, uint32_t len, Value v)
{
    for (; e; e = e->parent)
        for (uint32_t i = 0; i < e->count; i++)
            if (e->namelens[i] == len && memcmp(e->names[i], name, len) == 0) {
                value_release(e->vals[i]);
                e->vals[i] = value_retain(v);
                return true;
            }
    return false;
}

/* Release every binding and empty the frame. A global-scope closure captures
 * the very env that owns it (env -> closure -> env), a cycle refcounting alone
 * can't reclaim. Clearing the frame at teardown drops the env->closure edge so
 * the closure frees, releases its captured env, and the env then frees too.
 * count is zeroed so the later env_free does not double-release. */
void env_clear(EnvObj *e)
{
    for (uint32_t i = 0; i < e->count; i++)
        { value_release(e->vals[i]); free((char *)e->names[i]); }
    e->count = 0;
}

/* ------------------------------------------------------------------ */
/* printing                                                            */
/* ------------------------------------------------------------------ */
static void print_complex(FILE *out, double re, double im)
{
    if (im == 0.0) im = 0.0;   /* fold -0.0 (conjugation of a real element) */
    if (re == 0.0) re = 0.0;
    if (re == 0.0 && im != 0.0) { fmt_double(out, im); fputc('i', out); return; }
    fmt_double(out, re);
    fputc(im < 0 ? '-' : '+', out);
    fmt_double(out, im < 0 ? -im : im);
    fputc('i', out);
}

/* mirror of print_complex with the eps unit: 2+3eps, 3eps, 2-3eps, 2+0eps */
static void print_dual(FILE *out, double v, double e)
{
    if (e == 0.0) e = 0.0;   /* fold -0.0, exactly as complex does */
    if (v == 0.0) v = 0.0;
    if (v == 0.0 && e != 0.0) { fmt_double(out, e); fputs("eps", out); return; }
    fmt_double(out, v);
    fputc(e < 0 ? '-' : '+', out);
    fmt_double(out, e < 0 ? -e : e);
    fputs("eps", out);
}

/* v+ae1'eps1'+... always all four terms, signs folded — deterministic and
 * golden-able, mirroring print_dual */
static void print_hdual(FILE *out, HDual q)
{
    fmt_double(out, q.v == 0.0 ? 0.0 : q.v);
    double c3[3] = { q.e1, q.e2, q.e12 };
    const char *u[3] = { "eps1", "eps2", "eps12" };
    for (int i = 0; i < 3; i++) {
        double x = c3[i] == 0.0 ? 0.0 : c3[i];
        fputc(x < 0 ? '-' : '+', out);
        fmt_double(out, x < 0 ? -x : x);
        fputs(u[i], out);
    }
}

static void print_scalar(FILE *out, Value v)
{
    switch (v.kind) {
    case VAL_DATE: {
        double dd = v.as.f;
        int64_t z = (int64_t)(dd >= 0 ? dd : dd - 0.999999);  /* floor */
        double frac = dd - (double)z;
        int64_t za = z + 719468;
        int64_t era = (za >= 0 ? za : za - 146096) / 146097;
        int64_t doe = za - era * 146097;
        int64_t yoe = (doe - doe/1460 + doe/36524 - doe/146096) / 365;
        int64_t y = yoe + era * 400;
        int64_t doy = doe - (365*yoe + yoe/4 - yoe/100);
        int64_t mp = (5*doy + 2)/153;
        int64_t d = doy - (153*mp+2)/5 + 1;
        int64_t m = mp < 10 ? mp+3 : mp-9;
        if (m <= 2) y++;
        fprintf(out, "%04lld-%02lld-%02lld", (long long)y, (long long)m, (long long)d);
        if (frac > 1e-9) {
            int s = (int)(frac * 86400.0 + 0.5);
            fprintf(out, " %02d:%02d:%02d", s/3600, (s/60)%60, s%60);
        }
        break;
    }
    case VAL_NULL:    fputs("null", out); break;
    case VAL_BOOL:    fputs(v.as.b ? "true" : "false", out); break;
    case VAL_INT:     fprintf(out, "%lld", (long long)v.as.i); break;
    case VAL_FLOAT:   fmt_double(out, v.as.f); break;
    case VAL_COMPLEX: print_complex(out, v.as.z.re, v.as.z.im); break;
    case VAL_DUAL:    print_dual(out, v.as.d.v, v.as.d.e); break;
    case VAL_HDUAL:   print_hdual(out, v.as.h); break;
    case VAL_STRING:  fprintf(out, "\"%.*s\"", (int)((StrObj *)v.as.obj)->len, ((StrObj *)v.as.obj)->data); break;
    default:          break;
    }
}

const char *value_type_name(Value v)
{
    switch (v.kind) {
    case VAL_NULL:    return "Null";
    case VAL_BOOL:    return "Bool";
    case VAL_INT:     return "Int";
    case VAL_FLOAT:   return "Float";
    case VAL_DATE:    return "Date";
    case VAL_COMPLEX: return "Complex";
    case VAL_DUAL:    return "Dual";
    case VAL_HDUAL:   return "HDual";
    case VAL_STRING:  return "String";
    case VAL_ARRAY:   return "Array";
    case VAL_SPARSE:  return "Sparse";
    case VAL_RECORD:  return "Record";
    case VAL_CLOSURE: return "Closure";
    case VAL_BUILTIN: return "Builtin";
    }
    return "?";
}

/* Format one scalar element into buf (for width measurement + aligned print). */
static void scalar_str(char *buf, size_t cap, Value v)
{
    switch (v.kind) {
    case VAL_NULL:  snprintf(buf, cap, "null"); break;
    case VAL_STRING: snprintf(buf, cap, "\"%.*s\"", (int)((StrObj *)v.as.obj)->len, ((StrObj *)v.as.obj)->data); break;
    case VAL_BOOL:  snprintf(buf, cap, "%s", v.as.b ? "true" : "false"); break;
    case VAL_INT:   snprintf(buf, cap, "%lld", (long long)v.as.i); break;
    case VAL_FLOAT: fmt_double_str(buf, cap, v.as.f); break;
    case VAL_DATE: {                                   /* str(date): the display form */
        FILE *ms = tmpfile();
        if (ms) { print_scalar(ms, v); long L = ftell(ms); rewind(ms);
                  size_t got = fread(buf, 1, (size_t)L < cap - 1 ? (size_t)L : cap - 1, ms);
                  buf[got] = 0; fclose(ms); }
        else snprintf(buf, cap, "%f", v.as.f);
        break;
    }
    case VAL_COMPLEX: {
        double re = v.as.z.re, im = v.as.z.im;
        if (im == 0.0) im = 0.0;
        if (re == 0.0) re = 0.0;
        char rb[32], ib[32];
        if (re == 0.0 && im != 0.0) {
            fmt_double_str(ib, sizeof ib, im);
            snprintf(buf, cap, "%si", ib);
        } else {
            fmt_double_str(rb, sizeof rb, re);
            fmt_double_str(ib, sizeof ib, im < 0 ? -im : im);
            snprintf(buf, cap, "%s%c%si", rb, im < 0 ? '-' : '+', ib);
        }
        break;
    }
    case VAL_HDUAL: {
        HDual q = v.as.h;
        char b0[32], b1[32], b2[32], b3[32];
        fmt_double_str(b0, sizeof b0, q.v == 0.0 ? 0.0 : q.v);
        double c3[3] = { q.e1, q.e2, q.e12 }; char *bp[3] = { b1, b2, b3 };
        for (int i = 0; i < 3; i++)
            fmt_double_str(bp[i], 32, (c3[i] == 0.0 ? 0.0 : c3[i]) < 0 ? -c3[i] : c3[i]);
        char big[160];
        snprintf(big, sizeof big, "%s%c%seps1%c%seps2%c%seps12", b0,
                 q.e1 < 0 ? '-' : '+', b1, q.e2 < 0 ? '-' : '+', b2,
                 q.e12 < 0 ? '-' : '+', b3);
        size_t bl = strlen(big);                  /* cell display truncation intended */
        if (bl >= cap) bl = cap - 1;
        memcpy(buf, big, bl); buf[bl] = 0;
        break;
    }
    case VAL_DUAL: {                                   /* mirror of the complex cell */
        double dv = v.as.d.v, de = v.as.d.e;
        if (de == 0.0) de = 0.0;
        if (dv == 0.0) dv = 0.0;
        char vb[32], eb[32];
        if (dv == 0.0 && de != 0.0) {
            fmt_double_str(eb, sizeof eb, de);
            snprintf(buf, cap, "%seps", eb);
        } else {
            fmt_double_str(vb, sizeof vb, dv);
            fmt_double_str(eb, sizeof eb, de < 0 ? -de : de);
            snprintf(buf, cap, "%s%c%seps", vb, de < 0 ? '-' : '+', eb);
        }
        break;
    }
    default: snprintf(buf, cap, "?"); break;
    }
}

/* Octave-style aligned block: columns right-justified to a common width. */
static void print_matrix_aligned(FILE *out, ArrObj *a)
{
    uint32_t R = a->rows, C = a->cols;
    size_t cells = (size_t)R * C;
    char (*buf)[80] = malloc((cells ? cells : 1) * sizeof *buf);
    int w = 0;
    for (size_t k = 0; k < cells; k++) {
        scalar_str(buf[k], sizeof buf[k], arr_get(a, k));
        int l = (int)strlen(buf[k]);
        if (l > w) w = l;
    }
    for (uint32_t r = 0; r < R; r++) {
        fputs(r == 0 ? "[ " : "  ", out);
        for (uint32_t c = 0; c < C; c++) {
            if (c) fputs("  ", out);
            fprintf(out, "%*s", w, buf[(size_t)r*C + c]);
        }
        if (r + 1 < R) fputc('\n', out);
        else           fputs(" ]", out);
    }
    free(buf);
}

static void sparse_print(FILE *out, SpObj *s)
{
    fprintf(out, "sparse %ux%u, nnz = %u", s->rows, s->cols, s->nnz);
    uint32_t shown = 0, cap = 12;
    for (uint32_t i = 0; i < s->rows && shown < cap; i++)
        for (uint32_t k = s->rowptr[i]; k < s->rowptr[i+1] && shown < cap; k++, shown++) {
            fprintf(out, "\n  (%u,%u)  ", i + 1, s->colind[k] + 1);
            if (s->elt == ELT_COMPLEX) { Cplx z = ((Cplx *)s->vals)[k]; print_complex(out, z.re, z.im); }
            else print_scalar(out, (Value){ .kind = VAL_FLOAT, .as.f = ((double *)s->vals)[k] });
        }
    if (s->nnz > cap) fprintf(out, "\n  ... (%u more)", s->nnz - cap);
}

void value_print(FILE *out, Value v)
{
    if (v.kind == VAL_SPARSE) { sparse_print(out, as_sp(v)); return; }
    switch (v.kind) {
    case VAL_NULL: case VAL_BOOL: case VAL_INT: case VAL_FLOAT: case VAL_COMPLEX:
    case VAL_DUAL: case VAL_HDUAL: case VAL_DATE:
        print_scalar(out, v);
        break;
    case VAL_SPARSE: sparse_print(out, as_sp(v)); return;
    case VAL_STRING:
        fprintf(out, "\"%.*s\"", (int)as_str(v)->len, as_str(v)->data);
        break;
    case VAL_ARRAY: {
        ArrObj *a = as_arr(v);
        if (g_multiline && a->rows > 1) { print_matrix_aligned(out, a); break; }
        fputc('[', out);
        for (uint32_t r = 0; r < a->rows; r++) {
            if (r) fputs("; ", out);
            for (uint32_t c = 0; c < a->cols; c++) {
                if (c) fputs(", ", out);
                Value e = arr_get(a, (size_t)r * a->cols + c);
                print_scalar(out, e);
            }
        }
        fputc(']', out);
        break;
    }
    case VAL_RECORD: {
        RecObj *r = as_rec(v);
        bool saved = g_multiline; g_multiline = false;   /* fields stay compact/single-line */
        fputc('{', out);
        for (uint32_t i = 0; i < r->count; i++) {
            if (i) fputs(", ", out);
            fprintf(out, "%.*s = ", (int)r->keylens[i], r->keys[i]);
            value_print(out, r->vals[i]);
        }
        fputc('}', out);
        g_multiline = saved;
        break;
    }
    case VAL_CLOSURE:
        fprintf(out, "<fn/%u>", as_clo(v)->chunk->nparams);
        break;
    case VAL_BUILTIN:
        fprintf(out, "<builtin %s>", as_blt(v)->name);
        break;
    }
}
