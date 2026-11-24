/* nrt.h — runtime helpers shared between the tree-walker (eval.c)
 * and the bytecode VM (vm.c / compile.c).
 *
 * These are the Value-level operations: they take fully-evaluated operands and
 * return a fresh (+1) result. Crucially they are *non-consuming* — they never
 * release their inputs — so the VM can leave operands on the operand stack
 * until an op completes successfully. If one of these raises (runtime_error
 * longjmps), the operands are still on the stack and the VM's error handler
 * reclaims them in a single sweep. That is what erases the tree-walker's
 * error-path temp leak. */
#ifndef COZY_NRT_H
#define COZY_NRT_H

#include "lexer.h"     /* enum TokenKind */
#include "value.h"
#include "eval.h"      /* Interp */

[[noreturn]] void runtime_error(Interp *I, const char *fmt, ...);

/* numeric / array operations (non-consuming; result is +1) */
/* Two plain numbers -- every step of a scalar loop -- answered as
 * scalar_arith_k and scalar_cmp answer them, without the walk through the
 * kinds that are not numbers: Int with Int stays Int while it fits and
 * promotes to Float when it does not; anything with a Float is done in
 * doubles; division is always Float. Of the powers only the square is
 * answered (an Int squared while it fits, a Float squared), being most of
 * the powers ever written. Returns false, touching nothing, for the other
 * powers and for every other kind and operator: apply_binop then decides.
 * It is the top of apply_binop, written here so that the VM's OP_BINOP can
 * ask it without a call (0.1.39); there is one copy of the rule. */
static inline bool binop_numbers(enum TokenKind op, const Value *a, const Value *b, Value *out)
{
    if (a->kind == VAL_INT && b->kind == VAL_INT) {
        int64_t x = a->as.i, y = b->as.i, r;
        switch (op) {
        case TOK_PLUS:  *out = __builtin_add_overflow(x, y, &r) ? val_float((double)x + (double)y) : val_int(r); return true;
        case TOK_MINUS: *out = __builtin_sub_overflow(x, y, &r) ? val_float((double)x - (double)y) : val_int(r); return true;
        case TOK_STAR: case TOK_DOT_STAR:
                        *out = __builtin_mul_overflow(x, y, &r) ? val_float((double)x * (double)y) : val_int(r); return true;
        case TOK_SLASH: case TOK_DOT_SLASH: *out = val_float((double)x / (double)y); return true;
        case TOK_EQ: *out = val_bool(x == y); return true;
        case TOK_NE: *out = val_bool(x != y); return true;
        case TOK_LT: *out = val_bool(x <  y); return true;
        case TOK_LE: *out = val_bool(x <= y); return true;
        case TOK_GT: *out = val_bool(x >  y); return true;
        case TOK_GE: *out = val_bool(x >= y); return true;
        case TOK_CARET: case TOK_DOT_CARET:      /* a square that fits: what ipow_checked gives */
            if (y == 2 && !__builtin_mul_overflow(x, x, &r)) { *out = val_int(r); return true; }
            return false;
        default: return false;
        }
    }
    if ((a->kind == VAL_INT || a->kind == VAL_FLOAT) && (b->kind == VAL_INT || b->kind == VAL_FLOAT)) {
        double x = a->kind == VAL_INT ? (double)a->as.i : a->as.f;
        double y = b->kind == VAL_INT ? (double)b->as.i : b->as.f;
        switch (op) {
        case TOK_PLUS:  *out = val_float(x + y); return true;
        case TOK_MINUS: *out = val_float(x - y); return true;
        case TOK_STAR: case TOK_DOT_STAR:   *out = val_float(x * y); return true;
        case TOK_SLASH: case TOK_DOT_SLASH: *out = val_float(x / y); return true;
        case TOK_EQ: *out = val_bool(x == y); return true;
        case TOK_NE: *out = val_bool(x != y); return true;
        case TOK_LT: *out = val_bool(x <  y); return true;
        case TOK_LE: *out = val_bool(x <= y); return true;
        case TOK_GT: *out = val_bool(x >  y); return true;
        case TOK_GE: *out = val_bool(x >= y); return true;
        case TOK_CARET: case TOK_DOT_CARET:      /* a Float squared: real_pow's x * x */
            if (a->kind == VAL_FLOAT && b->kind == VAL_INT && b->as.i == 2) { *out = val_float(x * x); return true; }
            return false;
        default: return false;
        }
    }
    return false;
}
/* ---- collecting the results of a map ------------------------------------
 * map, `~>` and the indexed reductions all produce one value per element
 * and need them as an array. A Collect is that array being made:
 *
 *   - where the elements come from: an array (borrowed for the duration),
 *     or an Int range r0 : rstep : ... that is counted through and never
 *     made into an array;
 *   - the results. While every one is an Int or a Float they are written
 *     as numbers -- Ints until the first Float arrives, which turns what is
 *     there into Floats in place (what packing a mixture always did). At
 *     the first result of any other kind (complex, Bool, a string, a date,
 *     a dual, null) the numbers so far are boxed after all, and the general
 *     path -- boxed Values, packed at the end by pack_array -- carries on,
 *     so every such case is packed by the code that always packed it;
 *   - for a reduction by the builtin sum or prod over at most COLLECT_SMALL
 *     elements the numbers are kept in a buffer the owner lends (`small`)
 *     and the reducer reads them there, so nothing is allocated. Only those
 *     two, because they are known to keep nothing of their argument.
 *
 * map's own runs (eval.c) hold one on the C stack, and lend no buffer: two
 * kilobytes in every nested call was what made deep recursion through a
 * reduction overflow the C stack sooner in 0.1.43. Since 0.1.44 the VM
 * holds them too, with a buffer each, for reductions whose body it runs
 * inline (OP_RED_*). */
enum { COLLECT_SMALL = 256 };
typedef struct Collect {
    const ArrObj *arr;             /* the source array, or null: the Int range below */
    int64_t r0, rstep;
    size_t  nn, at;                /* elements; the next one to hand out */
    uint32_t rows, cols;           /* the shape the array of results has */
    BuiltinFn small_fn;            /* sum or prod, when the numbers are kept in `small` */
    bool    typed, floats;         /* results are all numbers so far; and are doubles */
    Value   out;                   /* the array they are written into (unless small_fn) */
    Value  *tmp;                   /* else: the boxed results */
    size_t  done;                  /* how many results there are */
    bool    saw_str, saw_bool, saw_other;
    int64_t *small;                /* room for COLLECT_SMALL numbers, or null */
} Collect;

/* the k-th element of the Int range s : st : ... (make_range fills its array with these) */
static inline int64_t int_range_at(int64_t s, int64_t st, uint64_t k)
{ return (int64_t)((uint64_t)s + k * (uint64_t)st); }

/* Begin. `red` is the reducer the results are meant for (null for a plain
 * map); `small` a buffer of COLLECT_SMALL numbers to use if the reduction is
 * short, or null. Both raise before anything is allocated: a bad range, a
 * source that is not an array. */
void collect_begin_range(Interp *I, Collect *c, Value red, int64_t *small,
                         int64_t start, int64_t step, int64_t stop);
void collect_begin_array(Interp *I, Collect *c, Value red, int64_t *small, Value src);
/* a result that is not a plain number, or arrives after one that was not */
void collect_take_slow(Interp *I, Collect *c, Value v);
/* End. True: *result is the reduction itself (a short sum or prod). False:
 * *result is the array of results (+1), for the caller to return or to hand
 * to the reducer. Either way the Collect holds nothing afterwards. */
bool collect_finish(Interp *I, Collect *c, Value red, Value *result);
/* give up: release whatever is held (the error path) */
void collect_abort(Collect *c);

/* the next element (lent), or false when there are no more */
static inline bool collect_next(Collect *c, Value *v)
{
    if (c->at == c->nn) return false;
    *v = c->arr ? arr_get(c->arr, c->at) : val_int(int_range_at(c->r0, c->rstep, c->at));
    c->at++;
    return true;
}
/* take one result (owned; *v is not used afterwards). May raise, with the
 * value already where collect_abort will find it. It is read through the
 * pointer, a field at a time: the value was just built by an operator as a
 * kind and a number, and copying all forty bytes of it first makes the
 * processor wait for those two small writes to land. */
static inline void collect_take(Interp *I, Collect *c, const Value *v)
{
    if (c->typed) {
        void *num = c->small_fn ? (void *)c->small : as_arr(c->out)->data;
        if (v->kind == VAL_INT) {
            if (c->floats) ((double *)num)[c->done] = (double)v->as.i;
            else           ((int64_t *)num)[c->done] = v->as.i;
            c->done++;
            return;
        }
        if (v->kind == VAL_FLOAT) {
            if (!c->floats) {                          /* the Ints so far become Floats */
                for (size_t k = 0; k < c->done; k++) ((double *)num)[k] = (double)((const int64_t *)num)[k];
                c->floats = true;
            }
            ((double *)num)[c->done] = v->as.f;
            c->done++;
            return;
        }
    }
    collect_take_slow(I, c, *v);
}

/* Calling one function many times (0.1.42). map, and through it `~>` and
 * the indexed reductions, called the function through call_value once per
 * element, and every such call was a run of the VM of its own: a jmp_buf
 * saved and restored, a setjmp, a VM taken and given back, about 100 ns
 * before the first instruction of the body. vm_call_each makes the calls in
 * ONE run: when the function returns, its result is handed to `take`, the
 * next arguments are asked of `next`, and the frame is set up again on the
 * stack that is already there.
 *   next: write this call's e->argc arguments to args[] and return true,
 *         or return false when there are no more. The arguments are LENT:
 *         the VM takes its own references.
 *   take: receives the result of the call just made and OWNS it. It may
 *         raise (after putting the value where the caller's error path
 *         will release it), exactly as code after a call_value could.
 * An error in the function, in next or in take leaves through I->jmp as an
 * error in a call_value would, so the caller's own cleanup is unchanged. A
 * callee that is a builtin is simply called in a loop. */
enum { VM_EACH_MAXARGS = 4 };
typedef struct VMEach {
    uint32_t argc;
    bool (*next)(Interp *I, struct VMEach *e, Value *args);
    void (*take)(Interp *I, struct VMEach *e, Value result);
    void *ctx;
} VMEach;
void vm_call_each(Interp *I, Value callee, VMEach *e);

Value apply_binop(Interp *I, enum TokenKind op, Value a, Value b);
Value apply_unary(Interp *I, enum TokenKind op, Value v);
Value transpose (Interp *I, Value v, bool conj);
Value make_range(Interp *I, Value start, Value stop, Value step);

/* aggregate builders (non-consuming: inputs stay owned by the caller, so on the
 * VM they remain on the operand stack and an error here is swept). Result +1. */
Value build_matrix(Interp *I, Value *ev, uint32_t nrows, const int64_t *rowcounts);
Value do_index(Interp *I, Value target, Value *idx, uint32_t argc, uint8_t colonmask);
Value do_index_set(Interp *I, Value target, Value *idx, uint32_t argc, uint8_t colonmask, Value value);

/* calls: builtins run in-place; a VM closure (CloObj.chunk != null) runs its
 * compiled proto on a nested VM frame. call_value routes VM closures here. */
Value call_value(Interp *I, Value callee, Value *args, uint32_t n);
Value vm_run_closure(Interp *I, Value callee, Value *args, uint32_t n);

/* literal decoders (used by the compiler to fold literals into constants) */
int64_t parse_int_lit  (const char *s, uint32_t len);
double  parse_float_lit(const char *s, uint32_t len);
Value   decode_string  (const char *s, uint32_t len);

#endif
