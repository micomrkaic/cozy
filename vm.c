/* vm.c — Cozy bytecode VM (call-frame stack, lexical addressing).
 *
 * One operand stack and one CallFrame stack per vm_run invocation, with a single
 * setjmp at the top. Every local lives in a stack slot, so a direct call (OP_CALL
 * on a compiled closure) pushes a frame over the callee and its arguments already
 * on the stack — no per-call environment, malloc, setjmp, or C recursion. Slot 0
 * of a frame is the closure itself (the self-slot, for direct recursion); slots
 * 1..nparams are the arguments; above them are chunk->nlocals slots for the
 * function's loop variables, `let`s and pipes (0.1.39), and above those the
 * temporaries. A top-level statement's frame has only the locals. Free variables
 * are value-snapshot upvalues carried on the closure, so a closure never points
 * back at the scope that created it. Globals remain name-keyed in the session's
 * environment, preserving late binding for recursion and mutual recursion.
 *
 * A local slot outside its scope is EMPTY: a null that carries a mark no
 * program value has (slot_empty). Scopes end by emptying their slots
 * (OP_CLEAR_LOCALS), which releases the values when a dropped environment
 * used to; OP_GET_LOCAL_OR / OP_SET_LOCAL_OR test the mark for the one kind
 * of `let` that may be skipped. Stores into a slot wash a null of whatever
 * its payload is, so the mark cannot be forged.
 *
 * Builtins that invoke a closure (e.g. map) go through call_value ->
 * vm_run_closure, which nests a fresh vm_run (saving/restoring I->jmp); that is
 * the only place C recursion happens, and it is shallow.
 *
 * Error handling stays total: on runtime_error the handler sweeps the whole
 * operand stack (one contiguous live-set across all frames, locals included).
 * The discipline that makes the sweep
 * complete: a fallible op PEEKs its operands (they stay on the stack), calls a
 * non-consuming runtime helper, and only on success pops them and pushes a result.
 */
#include "vm.h"
#include "pkg_hints.h"
#include "chunk.h"
#include "compile.h"
#include "nrt.h"
#include <stdlib.h>
#include <string.h>
#include <setjmp.h>

typedef struct {
    Chunk   *chunk;
    uint32_t ip;
    uint32_t slot_base;  /* slot 0 = closure (self); slots 1.. = params; then locals */
    uint32_t temp_base;  /* first temporary: slot_base + self + params + locals      */
    uint32_t mark_base;  /* marks stack height at frame entry (restored on return)   */
    CloObj  *closure;    /* the executing closure (for upvalues); null at top level  */
} CallFrame;

/* an inline reduction under way: its Collect, and the room it lends it for
 * the numbers of a short one */
struct RedSlot { Collect c; int64_t small[COLLECT_SMALL]; };

typedef struct {
    Value     *stack;  uint32_t sp, cap;
    CallFrame *frames; uint32_t nframe, fcap;
    EnvObj    *globals;  /* the session's names: every variable that is not a slot */
    int64_t   *end_dims; uint32_t end_sp, end_cap;  /* 'end' context: 2 dims per frame */
    uint32_t  *marks;  uint32_t nmark, mcap;        /* loop-body sp bases (break/continue reset) */
    struct RedSlot *reds; uint32_t nred, cred;      /* inline reductions under way (OP_RED_*)    */
    void      *prev;   /* enclosing VM (nested vm_run), for backtraces */
} VM;

#define VM_MAX_FRAMES 200000   /* direct recursion: frame-stack depth   */
#ifdef __EMSCRIPTEN__
/* In WebAssembly a nested run is paid for on the JavaScript engine's own
 * call stack, which no address in here measures. V8 (node, Chrome) gives up
 * between 300 and 400 levels with a RangeError that ends the session; the
 * limit is set well inside that. */
#define VM_MAX_DEPTH  200      /* builtin->closure nesting: the engine's stack */
#else
#define VM_MAX_DEPTH  1800     /* builtin->closure nesting: C-stack depth */
#endif
static int vm_depth = 0;

/* The count above was meant to keep nested runs off the end of the C stack,
 * and did not: a function that recurses through map or a reduction puts
 * some five kilobytes on it per level (two jump buffers, the frames of the
 * builtin and of vm_run), and 1790 levels of that is more than the eight
 * megabytes a process usually has. It was a segmentation fault, not an
 * error (every version to 0.1.43; in the browser, with five megabytes,
 * sooner). So the stack itself is measured: how far below the outermost
 * run this one is. Past the budget it is the error the count gives. The
 * budget is three quarters of the stack the process was given, where that
 * can be asked (the rest is for whatever the deepest run goes on to call);
 * define COZY_STACK_BUDGET, in bytes, to say otherwise -- an embedding
 * that runs the interpreter on a thread with a small stack should. */
#if !defined(COZY_STACK_BUDGET) && !defined(__EMSCRIPTEN__) && (defined(__unix__) || defined(__APPLE__))
#  include <sys/resource.h>
#  define COZY_ASK_STACK 1
#endif
static uintptr_t vm_stack_top;                          /* where the outermost run's frame is */
static size_t vm_stack_budget(void)
{
    static size_t budget = 0;
    if (budget) return budget;
#if defined(COZY_STACK_BUDGET)
    budget = (size_t)(COZY_STACK_BUDGET);
#elif defined(__EMSCRIPTEN__)
    budget = (size_t)3 << 20;                           /* of emscripten's 5 MB */
#elif defined(COZY_ASK_STACK)
    struct rlimit rl;
    budget = (size_t)6 << 20;                           /* of the usual 8 MB, if it cannot be asked */
    if (getrlimit(RLIMIT_STACK, &rl) == 0) {
        if (rl.rlim_cur == RLIM_INFINITY) budget = (size_t)48 << 20;
        else budget = (size_t)(rl.rlim_cur - rl.rlim_cur / 4);
    }
    if (budget < ((size_t)256 << 10)) budget = (size_t)256 << 10;
#else
    budget = (size_t)512 << 10;                         /* of a 1 MB stack */
#endif
    return budget;
}

/* Chunks that define closures are retained for the session (an escaping
 * closure's proto lives inside its statement chunk). Freed by vm_session_end. */
static Chunk  **g_kept;
static uint32_t g_nkept, g_ckept;

static void keep_chunk(Chunk *c)
{
    if (g_nkept == g_ckept) {
        g_ckept = g_ckept ? g_ckept * 2 : 16;
        g_kept = realloc(g_kept, g_ckept * sizeof *g_kept);
        if (!g_kept) abort();
    }
    g_kept[g_nkept++] = c;
}

void vm_session_end(void)
{
    for (uint32_t i = 0; i < g_nkept; i++) { chunk_free(g_kept[i]); free(g_kept[i]); }
    free(g_kept); g_kept = nullptr; g_nkept = g_ckept = 0;
}

static void stack_grow(VM *st)
{
    st->cap = st->cap ? st->cap * 2 : 256;
    st->stack = realloc(st->stack, st->cap * sizeof *st->stack);
    if (!st->stack) abort();
}

/* An empty local slot: a null no program can make. */
#define SLOT_EMPTY_MARK INT64_C(0x454d5054594c4f43)
static inline Value slot_empty(void) { return (Value){ .kind = VAL_NULL, .as.i = SLOT_EMPTY_MARK }; }
static inline bool  slot_is_empty(Value v) { return v.kind == VAL_NULL && v.as.i == SLOT_EMPTY_MARK; }
/* Most of what a scalar loop moves is a number, which nobody counts: the VM
 * asks the kind itself before calling value.c (0.1.39). Same rule, same
 * function (kind_is_heap); only the call is saved. */
static inline Value vm_retain(Value v)
{
    if (kind_is_heap(v.kind) && v.as.obj) v.as.obj->rc++;
    return v;
}
static inline void vm_release(Value v) { if (kind_is_heap(v.kind)) value_release(v); }
/* what a slot takes: the value retained, a null washed of its payload */
static inline void slot_put(Value *slot, const Value *v)
{
    *slot = *v;                                   /* one whole copy (see the note on copies) */
    if (kind_is_heap(v->kind)) { if (v->as.obj) v->as.obj->rc++; }
    else if (v->kind == VAL_NULL) slot->as.i = 0;
}

static bool vm_is_num(Value v)
{ return v.kind == VAL_INT || v.kind == VAL_FLOAT || v.kind == VAL_COMPLEX; }

static CallFrame *push_frame(VM *st, Chunk *chunk, CloObj *closure, uint32_t slot_base)
{
    if (st->nframe == st->fcap) {
        st->fcap = st->fcap ? st->fcap * 2 : 64;
        st->frames = realloc(st->frames, st->fcap * sizeof *st->frames);
        if (!st->frames) abort();
    }
    CallFrame *fr = &st->frames[st->nframe++];
    fr->chunk = chunk; fr->ip = 0; fr->slot_base = slot_base;
    fr->mark_base = st->nmark;
    fr->closure = closure;
    /* the locals: above self and the parameters, which the caller has pushed */
    for (uint32_t k = 0; k < chunk->nlocals; k++) {
        if (st->sp >= st->cap) stack_grow(st);
        st->stack[st->sp++] = slot_empty();
    }
    fr->temp_base = st->sp;
    return fr;
}

/* Backtrace: walk the running VM chain innermost-first, printing each
 * FUNCTION frame (closure != null; top-level statement frames carry no
 * name) as "    in <name> (line N)". A builtin boundary between nested
 * VMs prints as its own hop only through the next function frame — the
 * chain is logical, not a C stack walk. At most 8 frames, then an
 * ellipsis with the suppressed count: deep recursion stays readable. */
void vm_backtrace(Interp *I, char *buf, size_t cap)
{
    size_t off = 0;
    int shown = 0, total = 0;
    for (VM *st = I->vm_cur; st; st = st->prev)
        for (uint32_t f = st->nframe; f-- > 0; )
            if (st->frames[f].closure) total++;
    if (total == 0) { if (cap) buf[0] = '\0'; return; }
    for (VM *st = I->vm_cur; st && off + 40 < cap; st = st->prev)
        for (uint32_t f = st->nframe; f-- > 0 && off + 40 < cap; ) {
            CallFrame *fr = &st->frames[f];
            if (!fr->closure) continue;
            if (shown == 8) {
                off += (size_t)snprintf(buf + off, cap - off,
                                        "\n    ... %d more frame(s)", total - shown);
                if (cap) buf[cap - 1] = '\0';
                return;
            }
            uint32_t ip = fr->ip > 0 ? fr->ip - 1 : 0;
            if (ip >= fr->chunk->code_len && fr->chunk->code_len)
                ip = fr->chunk->code_len - 1;
            uint32_t line = fr->chunk->lines ? fr->chunk->lines[ip] : 0;
            const Chunk *c = fr->chunk;
            if (c->fnamelen)
                off += (size_t)snprintf(buf + off, cap - off,
                                        "\n    in %.*s (line %u)",
                                        (int)(c->fnamelen > 80 ? 80 : c->fnamelen),
                                        c->fname, line);
            else
                off += (size_t)snprintf(buf + off, cap - off,
                                        "\n    in <fn> (line %u)", line);
            shown++;
        }
}

/* A run of the VM needs a value stack, a frame stack and two small side
 * stacks. Builtins that take a function (map, fzero, integral, the index-
 * bound reductions) start a run for every call they make, and each used to
 * allocate and free all four; the finished ones are kept and handed out
 * again. Nested runs each hold their own. */
enum { VM_POOL_MAX = 16 };
static VM *vm_pool[VM_POOL_MAX];
static int vm_pool_n = 0;

static VM *vm_take(void)
{
    VM *st;
    if (vm_pool_n > 0) st = vm_pool[--vm_pool_n];
    else {
        st = malloc(sizeof *st);
        if (!st) abort();
        *st = (VM){ 0 };
    }
    st->sp = 0; st->nframe = 0; st->end_sp = 0; st->nmark = 0; st->nred = 0;
    return st;
}
static void vm_give(VM *st)
{
    if (vm_pool_n < VM_POOL_MAX && st->cap <= 4096) { vm_pool[vm_pool_n++] = st; return; }
    free(st->stack); free(st->frames); free(st->end_dims); free(st->marks); free(st->reds); free(st);
}
void vm_pool_drain(void)
{
    while (vm_pool_n > 0) {
        VM *st = vm_pool[--vm_pool_n];
        free(st->stack); free(st->frames); free(st->end_dims); free(st->marks); free(st->reds); free(st);
    }
}

/* Put a call of `clo` on an EMPTY stack: the closure in slot 0, the arguments
 * (lent; retained here) in slots 1..argc, null for the parameters not given
 * (entry 13: their defaults fill them), then the frame with its locals. */
static CallFrame *enter_closure(VM *st, Chunk *ch, Value closure, CloObj *clo, const Value *args, uint32_t argc)
{
    uint32_t np = clo->chunk->nparams;
    while (st->cap < (size_t)np + 1) stack_grow(st);
    st->stack[0] = value_retain(closure);               /* slot 0 = self        */
    for (uint32_t i = 0; i < argc; i++) st->stack[1 + i] = value_retain(args[i]);
    for (uint32_t i = argc; i < np; i++) st->stack[1 + i] = val_null();
    st->sp = np + 1;
    return push_frame(st, ch, clo, /*slot_base=*/0);
}

/* closure: VAL_CLOSURE -> run it with callee at slot 0 and args at slots 1..argc;
 * VAL_NULL -> top-level statement chunk (no slots).
 * each: not null -> when the closure returns, hand the result to each->take,
 * ask each->next for the next arguments, and call it again in this same run
 * (nrt.h); the value returned is then null. */
static Value vm_run(Interp *I, Chunk *ch, Value closure, Value *args, uint32_t argc, VMEach *each, bool *ok)
{
    if (vm_depth >= VM_MAX_DEPTH) runtime_error(I, "maximum call depth exceeded");
    {
        char here;
        uintptr_t at = (uintptr_t)&here;
        if (vm_depth == 0) vm_stack_top = at;
        else if ((at < vm_stack_top ? vm_stack_top - at : at - vm_stack_top) > vm_stack_budget())
            runtime_error(I, "maximum call depth exceeded");
    }
    vm_depth++;
    jmp_buf saved;
    memcpy(saved, I->jmp, sizeof(jmp_buf));

    VM *st = vm_take();
    st->globals = I->globals;
    st->prev = I->vm_cur; I->vm_cur = st;               /* backtrace chain */
    if (st->cap == 0) stack_grow(st);

    CloObj *clo = closure.kind == VAL_CLOSURE ? as_clo(closure) : nullptr;
    if (clo) enter_closure(st, ch, closure, clo, args, argc);
    else     push_frame(st, ch, nullptr, /*slot_base=*/0);

    if (setjmp(I->jmp)) {                                /* runtime_error landed here */
        for (uint32_t k = 0; k < st->sp; k++) value_release(st->stack[k]);
        for (uint32_t k = 0; k < st->nred; k++) collect_abort(&st->reds[k].c);   /* reductions cut short */
        I->vm_cur = st->prev;
        vm_give(st);
        memcpy(I->jmp, saved, sizeof(jmp_buf));
        vm_depth--;
        *ok = false;
        return val_null();
    }

    CallFrame *fr = &st->frames[st->nframe - 1];

    /* The running frame's instruction pointer and the stack pointer live in
     * locals (registers) and are written through to fr->ip and st->sp at
     * every change. Memory therefore holds what it always held at any
     * moment a helper can run or raise -- the error sweep reads st->sp,
     * backtraces and the return to a caller's frame read fr->ip -- but the
     * loop no longer loads them back for every byte it decodes and every
     * value it pushes: each of those was a store-then-load of several
     * cycles, one after another, and together they were most of what a
     * scalar loop waited for (0.1.39). `stack` follows st->stack when it
     * grows. FRAME_IN() after fr changes or a frame is pushed. */
    uint32_t ip, sp, base;
    const uint8_t *code;
    Value *stack;
    uint8_t call_argc = 0;                              /* OP_CALL's operand; OP_RED_END calls too */
#define FRAME_IN() do { ip = fr->ip; code = fr->chunk->code; base = fr->slot_base; \
                        sp = st->sp; stack = st->stack; } while (0)
    FRAME_IN();

#define PUSH(v)   do { if (sp >= st->cap) { stack_grow(st); stack = st->stack; } \
                       stack[sp] = (v); st->sp = ++sp; } while (0)
#define PEEK(d)   (stack[sp - 1 - (d)])
#define DROP(n)   (st->sp = (sp -= (n)))
#define RD_U8()   (fr->ip = ip + 1, code[ip++])
#define RD_U16()  (ip += 2, fr->ip = ip, (uint16_t)(code[ip - 2] | (code[ip - 1] << 8)))
#define JUMP_BY(d) (fr->ip = (ip += (d)))
#define JUMP_BACK(d) (fr->ip = (ip -= (d)))

    for (;;) {
        /* no end-of-chunk test: the compiler ends every chunk in OP_RETURN */
        Chunk *ch_ = fr->chunk;
        I->cur_line = ch_->lines[ip];
        switch ((OpCode)RD_U8()) {

        case OP_CONST: { uint16_t k = RD_U16(); PUSH(value_retain(ch_->consts[k])); break; }
        case OP_NULL:  PUSH(val_null()); break;

        case OP_GET_LOCAL: { uint8_t s = RD_U8(); PUSH(vm_retain(stack[base + s])); break; }
        case OP_SET_LOCAL: {
            uint8_t s = RD_U8(); uint32_t idx = base + s;
            Value old = stack[idx];
            slot_put(&stack[idx], &PEEK(0));
            vm_release(old);
            break;
        }
        case OP_GET_LOCAL_OR: {                          /* a `let` that may not have run */
            uint8_t s = RD_U8(); uint16_t off = RD_U16();
            Value v = stack[base + s];
            if (!slot_is_empty(v)) { PUSH(vm_retain(v)); JUMP_BY(off); }
            break;                                       /* empty: the fallback follows */
        }
        case OP_SET_LOCAL_OR: {
            uint8_t s = RD_U8(); uint16_t off = RD_U16();
            uint32_t idx = base + s;
            Value old = stack[idx];
            if (!slot_is_empty(old)) {
                slot_put(&stack[idx], &PEEK(0));
                vm_release(old);
                JUMP_BY(off);
            }
            break;
        }
        case OP_CLEAR_LOCALS: {                          /* a scope ends */
            uint8_t s = RD_U8(); uint8_t cnt = RD_U8();
            Value *v = &stack[base + s];
            for (uint8_t k = 0; k < cnt; k++) {
                Value old = v[k];
                v[k] = slot_empty();
                vm_release(old);
            }
            break;
        }
        case OP_GET_UPVALUE: { uint8_t i = RD_U8(); PUSH(vm_retain(fr->closure->upvalues[i])); break; }
        case OP_SET_UPVALUE: {
            uint8_t i = RD_U8();
            value_release(fr->closure->upvalues[i]); fr->closure->upvalues[i] = value_retain(PEEK(0));
            break;
        }

        case OP_GET_VAR: {
            uint16_t k = RD_U16();
            Value out;
            EnvObj *g = st->globals;
            EnvCache *gc = &ch_->name_cache[k];          /* where this global was last time */
            if (env_cache_hit(g, gc)) {
                PUSH(vm_retain(g->vals[gc->slot]));
                break;
            }
            if (!env_lookup_at(g, ch_->names[k], ch_->namelens[k], gc, &out))
                {
                    /* teach the load line when the name is a known package
                     * export (pkg_hints.h, generated from packages/) — born
                     * from the owner typing minimize into a fresh session */
                    const char *nm = ch_->names[k]; size_t nl = ch_->namelens[k];
                    for (size_t hi = 0; hi < sizeof pkg_hints / sizeof *pkg_hints; hi++)
                        if (strlen(pkg_hints[hi].name) == nl && memcmp(pkg_hints[hi].name, nm, nl) == 0)
                            runtime_error(I, "undefined name '%.*s' — it lives in %s; "
                                             "load(\"%s\") first", (int)nl, nm,
                                          pkg_hints[hi].pkg, pkg_hints[hi].pkg);
                    runtime_error(I, "undefined name '%.*s'", (int)nl, nm);
                }
            PUSH(out);
            break;
        }
        case OP_DEFINE: {
            uint16_t k = RD_U16();
            env_define(st->globals, ch_->names[k], ch_->namelens[k], PEEK(0));
            break;
        }
        case OP_SET_VAR: {
            uint16_t k = RD_U16();
            EnvObj *g = st->globals;
            EnvCache *gc = &ch_->name_cache[k];
            if (env_cache_hit(g, gc) && gc->slot >= g->n_protected) {
                Value old = g->vals[gc->slot];           /* (a library name is shadowed by let, never assigned) */
                g->vals[gc->slot] = vm_retain(PEEK(0));
                value_release(old);
                break;
            }
            if (!env_assign_at(g, ch_->names[k], ch_->namelens[k], gc, PEEK(0))) {
                Value held;
                if (env_lookup(st->globals, ch_->names[k], ch_->namelens[k], &held)) {
                    value_release(held);             /* the name exists, but only as a builtin */
                    runtime_error(I, "cannot assign to the builtin '%.*s' — write let %.*s = ... to make a variable of that name",
                                  (int)ch_->namelens[k], ch_->names[k], (int)ch_->namelens[k], ch_->names[k]);
                }
                runtime_error(I, "assignment to undefined name '%.*s' (declare it with 'let' first)",
                              (int)ch_->namelens[k], ch_->names[k]);
            }
            break;
        }
        case OP_GET_AT:                                  /* inside a pipe '@' is a slot */
            runtime_error(I, "'@' is only meaningful on the right-hand side of '|>'");

        case OP_BINOP: {
            enum TokenKind op = (enum TokenKind)RD_U8();
            Value r;
            if (binop_numbers(op, &PEEK(1), &PEEK(0), &r)) {   /* two numbers: nothing to release */
                DROP(1);
                PEEK(0) = r;
                break;
            }
            r = apply_binop(I, op, PEEK(1), PEEK(0));
            value_release(PEEK(0)); value_release(PEEK(1)); DROP(2);
            PUSH(r);
            break;
        }
        case OP_UNARY: {
            enum TokenKind op = (enum TokenKind)RD_U8();
            Value r = apply_unary(I, op, PEEK(0));
            value_release(PEEK(0)); DROP(1);
            PUSH(r);
            break;
        }
        case OP_TRANSPOSE: {
            bool conj = RD_U8() != 0;
            Value r = transpose(I, PEEK(0), conj);
            value_release(PEEK(0)); DROP(1);
            PUSH(r);
            break;
        }
        case OP_RANGE: {
            Value r = make_range(I, PEEK(2), PEEK(0), PEEK(1));
            value_release(PEEK(0)); value_release(PEEK(1)); value_release(PEEK(2)); DROP(3);
            PUSH(r);
            break;
        }

        case OP_CALL:
            call_argc = RD_U8();
        do_call: {
            uint8_t argc = call_argc;
            Value  callee = PEEK(argc);
            if (callee.kind == VAL_CLOSURE) {            /* direct call: push a frame */
                CloObj *cl = as_clo(callee);
                uint32_t np2 = cl->chunk->nparams, nd2 = cl->chunk->ndefaults;
                if (argc < np2 - nd2 || argc > np2) {
                    if (nd2 == 0)
                        runtime_error(I, "function expects %u argument(s), got %u", np2, argc);
                    runtime_error(I, "function expects %u to %u argument(s), got %u",
                                  np2 - nd2, np2, argc);
                }
                for (uint32_t pad = argc; pad < np2; pad++)  /* absent -> null; the */
                    PUSH(val_null());                        /* ARGDEF prologue fills */
                if (st->nframe >= VM_MAX_FRAMES) runtime_error(I, "call stack overflow");
                uint32_t sb = sp - np2 - 1;           /* callee + args become slots */
                fr = push_frame(st, cl->chunk, cl, sb); FRAME_IN();   /* and its locals go above them */
            } else {                                      /* builtin (or error): in place */
                Value *args2 = &stack[sp - argc];
                Value r = call_value(I, callee, args2, argc);
                for (uint32_t i = 0; i < argc; i++) value_release(args2[i]);
                value_release(callee);
                DROP((uint32_t)argc + 1);
                PUSH(r);
            }
            break;
        }

        case OP_ARGDEF: {                            /* entry 13: default fill */
            uint8_t  slot = RD_U8();
            uint16_t off  = RD_U16();
            if (stack[base + slot].kind != VAL_NULL)
                JUMP_BY(off);                           /* provided: skip default */
            break;
        }

        case OP_AUTOCALL: {                              /* bare callable name -> call with 0 args */
            Value callee = PEEK(0);
            if (callee.kind == VAL_CLOSURE && as_clo(callee)->chunk->nparams == 0) {
                if (st->nframe >= VM_MAX_FRAMES) runtime_error(I, "call stack overflow");
                fr = push_frame(st, as_clo(callee)->chunk, as_clo(callee), sp - 1); FRAME_IN();
            } else if (callee.kind == VAL_BUILTIN && as_blt(callee)->min_arity == 0) {
                Value r = call_value(I, callee, nullptr, 0);
                value_release(callee);
                DROP(1);
                PUSH(r);
            }                                            /* otherwise leave the value untouched */
            break;
        }

        case OP_RETURN: {
            if (each && st->nframe == 1) {
                /* The function of a vm_call_each returns (nrt.h). Its frame is
                 * kept for the next call: slot 0 still holds the closure, and
                 * only the arguments, the locals and the temporaries go. While
                 * take and next run there is no frame (nframe 0, the stack one
                 * value deep), so an error they raise is swept and reported as
                 * one raised after a finished call always was. */
                Value result = sp > fr->temp_base ? stack[DROP(1)] : val_null();
                while (sp > base + 1) vm_release(stack[DROP(1)]);
                st->nmark = fr->mark_base;
                st->nframe = 0;
                Value again[VM_EACH_MAXARGS];
                each->take(I, each, result);            /* the caller owns it; may raise */
                if (each->next(I, each, again)) {       /* the next call, in the same frame */
                    uint32_t np = fr->chunk->nparams, argc2 = each->argc;
                    for (uint32_t i = 0; i < argc2; i++) slot_put(&stack[base + 1 + i], &again[i]);
                    for (uint32_t i = argc2; i < np; i++) stack[base + 1 + i] = val_null();
                    for (uint32_t i = base + 1 + np; i < fr->temp_base; i++) stack[i] = slot_empty();
                    st->sp = sp = fr->temp_base;
                    fr->ip = ip = 0;
                    st->end_sp = 0;                     /* (a return from inside an index left its 'end') */
                    st->nframe = 1;
                    break;
                }
                value_release(stack[DROP(1)]);          /* the closure: the calls are over */
                I->vm_cur = st->prev;
                vm_give(st);
                memcpy(I->jmp, saved, sizeof(jmp_buf));
                vm_depth--;
                *ok = true;
                return val_null();
            }
            Value result = sp > fr->temp_base ? stack[DROP(1)] : val_null();
            while (sp > fr->slot_base) value_release(stack[DROP(1)]);
            st->nmark = fr->mark_base;                  /* discard this frame's loop marks */
            st->nframe--;
            if (st->nframe == 0) {
                I->vm_cur = st->prev;
                vm_give(st);
                memcpy(I->jmp, saved, sizeof(jmp_buf));
                vm_depth--;
                *ok = true;
                return result;
            }
            fr = &st->frames[st->nframe - 1];
            FRAME_IN();
            PUSH(result);
            break;
        }

        case OP_FIELD: {
            uint16_t k = RD_U16();
            Value t = PEEK(0);
            if (t.kind != VAL_RECORD) runtime_error(I, "field access on a non-record value");
            RecObj *rec = as_rec(t);
            Value out; bool found = false;
            for (uint32_t i = 0; i < rec->count; i++)
                if (rec->keylens[i] == ch_->namelens[k] &&
                    memcmp(rec->keys[i], ch_->names[k], ch_->namelens[k]) == 0) {
                    out = value_retain(rec->vals[i]); found = true; break;
                }
            if (!found) runtime_error(I, "record has no field '%.*s'", (int)ch_->namelens[k], ch_->names[k]);
            value_release(t); DROP(1);
            PUSH(out);
            break;
        }

        case OP_ASSERT_BOOL:
            if (PEEK(0).kind != VAL_BOOL)
                runtime_error(I, "operand of '&&'/'||' must be Bool, got %s", value_type_name(PEEK(0)));
            break;

        case OP_JUMP: { uint16_t off = RD_U16(); JUMP_BY(off); break; }
        case OP_LOOP: { uint16_t off = RD_U16(); JUMP_BACK(off); break; }
        case OP_JUMP_IF_FALSE: {
            uint16_t off = RD_U16();
            if (PEEK(0).kind != VAL_BOOL)
                runtime_error(I, "condition must be Bool, got %s", value_type_name(PEEK(0)));
            if (!PEEK(0).as.b) JUMP_BY(off);
            break;
        }
        case OP_JUMP_IF_TRUE: {
            uint16_t off = RD_U16();
            if (PEEK(0).kind != VAL_BOOL)
                runtime_error(I, "condition must be Bool, got %s", value_type_name(PEEK(0)));
            if (PEEK(0).as.b) JUMP_BY(off);
            break;
        }
        case OP_POP: DROP(1); vm_release(stack[sp]); break;
        case OP_TEE: {
            value_print(vout(), PEEK(0));
            fputc('\n', vout());
            break;
        }

        case OP_FOR_BEGIN: {
            Value it = PEEK(0);
            if (it.kind != VAL_ARRAY && !vm_is_num(it))
                runtime_error(I, "for: can only iterate a number or array, got %s", value_type_name(it));
            PUSH(val_int(0));
            break;
        }
        case OP_FOR_NEXT: {
            uint8_t  slot = RD_U8();
            uint16_t endoff = RD_U16();
            Value iter = PEEK(1);
            int64_t idx = PEEK(0).as.i;
            int64_t count = iter.kind == VAL_ARRAY
                          ? (int64_t)as_arr(iter)->rows * as_arr(iter)->cols : 1;
            Value *var = &stack[base + slot];
            Value old = *var;
            if (idx >= count) {                          /* done: the variable's scope ends */
                *var = slot_empty();
                vm_release(old);
                JUMP_BY(endoff);
                break;
            }
            /* arr_get lends a string element rather than giving a reference:
             * the slot must hold one of its own (a for loop over a string
             * array used to corrupt the heap) */
            if (iter.kind == VAL_ARRAY) {
                Value elem = arr_get(as_arr(iter), (size_t)idx);
                slot_put(var, &elem);
            } else slot_put(var, &iter);
            vm_release(old);
            PEEK(0) = val_int(idx + 1);
            break;
        }
        case OP_FOR_END:
            value_release(PEEK(1)); DROP(2);
            break;

        /* An indexed reduction run inline. The Collect lives in st->reds, a
         * stack of its own beside the marks: reductions nest (a double sum),
         * and the body may call anything, but none is left except by its
         * END or by an error, which the handler above cleans up after. The
         * operand stack holds [r, src] below the body's temporaries, src
         * being the array the elements come from, or null for an Int range. */
        case OP_RED_BEGIN: {
            uint8_t ranged = RD_U8();
            if (st->nred == st->cred) {
                st->cred = st->cred ? st->cred * 2 : 2;
                st->reds = realloc(st->reds, st->cred * sizeof *st->reds);
                if (!st->reds) abort();
                for (uint32_t k = 0; k < st->nred; k++)   /* they moved: so did the room they were lent */
                    if (st->reds[k].c.small) st->reds[k].c.small = st->reds[k].small;
            }
            Collect *c = &st->reds[st->nred].c;
            int64_t *room = st->reds[st->nred].small;
            if (ranged) {
                Value s0 = PEEK(2), stp = PEEK(1), e0 = PEEK(0);
                if (s0.kind == VAL_INT && stp.kind == VAL_INT && e0.kind == VAL_INT) {
                    collect_begin_range(I, c, PEEK(3), room, s0.as.i, stp.as.i, e0.as.i);
                    DROP(3);
                    PUSH(val_null());
                } else {                                 /* a Float or date range: made as it always was */
                    Value arr = make_range(I, s0, e0, stp);
                    vm_release(PEEK(0)); vm_release(PEEK(1)); vm_release(PEEK(2)); DROP(3);
                    PUSH(arr);
                    collect_begin_array(I, c, PEEK(1), room, PEEK(0));
                }
            } else
                collect_begin_array(I, c, PEEK(1), room, PEEK(0));
            st->nred++;
            break;
        }
        case OP_RED_NEXT: {
            uint8_t  slot = RD_U8();
            uint16_t endoff = RD_U16();
            Value *var = &stack[base + slot];
            Value old = *var, elem;
            if (!collect_next(&st->reds[st->nred - 1].c, &elem)) { /* done: the variable's scope ends */
                *var = slot_empty();
                vm_release(old);
                JUMP_BY(endoff);
                break;
            }
            slot_put(var, &elem);
            vm_release(old);
            break;
        }
        case OP_RED_STEP: {                              /* the bottom of the body */
            uint8_t  slot = RD_U8();
            uint16_t back = RD_U16();
            Collect *c = &st->reds[st->nred - 1].c;
            DROP(1);                                     /* the Collect owns it from here */
            collect_take(I, c, &stack[sp]);
            Value *var = &stack[base + slot];
            Value old = *var, elem;
            if (collect_next(c, &elem)) {
                slot_put(var, &elem);
                vm_release(old);
                JUMP_BACK(back);
            } else {                                     /* done: the variable's scope ends */
                *var = slot_empty();
                vm_release(old);
            }
            break;
        }
        case OP_RED_END: {
            Value r;
            bool reduced = collect_finish(I, &st->reds[st->nred - 1].c, PEEK(1), &r);
            st->nred--;
            vm_release(PEEK(0)); DROP(1);                /* the source */
            if (reduced) {                               /* a short sum or prod: r is the answer */
                value_release(PEEK(0));
                PEEK(0) = r;
                break;
            }
            PUSH(r);                                     /* [r, results]: call r on them */
            call_argc = 1;
            goto do_call;
        }

        case OP_MARK_PUSH:
            if (st->nmark == st->mcap) {
                st->mcap = st->mcap ? st->mcap * 2 : 16;
                st->marks = realloc(st->marks, st->mcap * sizeof *st->marks);
                if (!st->marks) abort();
            }
            st->marks[st->nmark++] = sp;
            break;
        case OP_MARK_POP:
            st->nmark--;
            break;
        case OP_MARK_RESET:                              /* break/continue: drop mid-expression temporaries */
            while (sp > st->marks[st->nmark - 1]) { DROP(1); value_release(stack[sp]); }
            break;

        case OP_CLOSURE: {
            uint16_t pidx = RD_U16();
            uint8_t  nup  = RD_U8();
            Value *ups = nup ? malloc(nup * sizeof *ups) : nullptr;
            for (uint8_t i = 0; i < nup; i++) ups[i] = stack[sp - nup + i];  /* move */
            DROP(nup);
            PUSH(val_closure_vm(ch_->protos[pidx], ups, nup));
            break;
        }

        case OP_MATRIX: {
            uint16_t k = RD_U16();
            ArrObj *shape = as_arr(ch_->consts[k]);
            uint32_t nrows = shape->rows * shape->cols;
            const int64_t *rc = (const int64_t *)shape->data;
            uint32_t ntot = 0;
            for (uint32_t r = 0; r < nrows; r++) ntot += (uint32_t)rc[r];
            Value *ev = &stack[sp - ntot];
            Value result = build_matrix(I, ev, nrows, rc);
            for (uint32_t r = 0; r < ntot; r++) value_release(ev[r]);
            DROP(ntot);
            PUSH(result);
            break;
        }
        case OP_INDEX: {
            uint8_t argc = RD_U8();
            uint8_t colonmask = RD_U8();
            Value  target = PEEK(argc);
            Value *idx    = &stack[sp - argc];
            Value result = do_index(I, target, idx, argc, colonmask);
            for (uint32_t k = 0; k < argc; k++) value_release(idx[k]);
            value_release(target);
            DROP((uint32_t)argc + 1);
            PUSH(result);
            break;
        }

        case OP_END_PUSH: {
            uint8_t argc = RD_U8();
            Value tgt = PEEK(0);                       /* the just-compiled target */
            int64_t d0 = 1, d1 = 1;
            if (tgt.kind == VAL_ARRAY) {
                ArrObj *ta = as_arr(tgt);
                if (argc == 1) { d0 = d1 = (int64_t)ta->rows * ta->cols; }
                else           { d0 = ta->rows; d1 = ta->cols; }
            } else if (tgt.kind == VAL_STRING) {
                d0 = d1 = (int64_t)((StrObj *)tgt.as.obj)->len;   /* s[end] */
            }
            if (st->end_sp == st->end_cap) {
                st->end_cap = st->end_cap ? st->end_cap * 2 : 8;
                st->end_dims = realloc(st->end_dims, st->end_cap * 2 * sizeof *st->end_dims);
                if (!st->end_dims) abort();
            }
            st->end_dims[2 * st->end_sp]     = d0;
            st->end_dims[2 * st->end_sp + 1] = d1;
            st->end_sp++;
            break;
        }
        case OP_END_GET: {
            uint8_t dim = RD_U8();
            PUSH(val_int(st->end_dims[2 * (st->end_sp - 1) + dim]));
            break;
        }
        case OP_END_POP:
            st->end_sp--;
            break;

        case OP_INDEX_SET: {
            uint8_t argc = RD_U8();
            uint8_t colonmask = RD_U8();
            Value  value  = PEEK(0);                       /* top: the value */
            Value *idx    = &stack[sp - 1 - argc]; /* below it: the indices */
            Value  target = PEEK(argc + 1);                /* below those: the array */
            Value  result = do_index_set(I, target, idx, argc, colonmask, value);
            value_release(value);
            for (uint32_t k = 0; k < argc; k++) value_release(idx[k]);
            value_release(target);
            DROP((uint32_t)argc + 2);
            PUSH(result);
            break;
        }
        case OP_RECORD: {
            uint16_t cnt = RD_U16();
            Value rec = val_record(cnt);
            RecObj *rrec = as_rec(rec);
            for (uint32_t k = 0; k < cnt; k++) {
                uint16_t ni = RD_U16();
                rrec->keys[k]    = ch_->names[ni];
                rrec->keylens[k] = ch_->namelens[ni];
            }
            Value *vals = &stack[sp - cnt];
            for (uint32_t k = 0; k < cnt; k++) rrec->vals[k] = vals[k];   /* move ownership */
            DROP(cnt);
            PUSH(rec);
            break;
        }
        }
    }

#undef PUSH
#undef PEEK
#undef DROP
#undef RD_U8
#undef RD_U16
#undef JUMP_BY
#undef JUMP_BACK
#undef FRAME_IN
}

static void want_arity(Interp *I, const CloObj *c, uint32_t n)
{
    uint32_t np3 = c->chunk->nparams, nd3 = c->chunk->ndefaults;
    if (n < np3 - nd3 || n > np3) {
        if (nd3 == 0)
            runtime_error(I, "function expects %u argument(s), got %u", np3, n);
        runtime_error(I, "function expects %u to %u argument(s), got %u", np3 - nd3, np3, n);
    }
}

/* Builtin -> closure path: nests a fresh vm_run with the callee as slot 0. */
Value vm_run_closure(Interp *I, Value callee, Value *args, uint32_t n)
{
    CloObj *c = as_clo(callee);
    want_arity(I, c, n);
    bool ok;
    Value r = vm_run(I, c->chunk, callee, args, n, nullptr, &ok);
    if (!ok) longjmp(I->jmp, 1);
    return r;
}

/* One function, many calls, one run of the VM (nrt.h). Nothing is checked
 * or set up before there is a first call to make, so a function that
 * cannot be called is no error over no elements, as it never was. */
void vm_call_each(Interp *I, Value callee, VMEach *e)
{
    Value args[VM_EACH_MAXARGS];
    if (!e->next(I, e, args)) return;
    if (callee.kind != VAL_CLOSURE) {                   /* a builtin (or the error for neither) */
        do e->take(I, e, call_value(I, callee, args, e->argc));
        while (e->next(I, e, args));
        return;
    }
    CloObj *c = as_clo(callee);
    want_arity(I, c, e->argc);
    bool ok;
    vm_run(I, c->chunk, callee, args, e->argc, e, &ok);
    if (!ok) longjmp(I->jmp, 1);
}

Value vm_eval_program(Interp *I, AstNode *block, EnvObj *globals, bool echo)
{
    I->globals   = globals;
    I->had_error = false;
    I->line_borrows_src = false;      /* compiler sets it; sessions consult it */

    AstNode  *single = block;
    AstNode **items  = &single;
    uint32_t  count  = 1;
    if (block->kind == AST_BLOCK) { items = block->as.list.items; count = block->as.list.count; }

    Value last = val_null();
    for (uint32_t i = 0; i < count; i++) {
        AstNode *s = items[i];
        Chunk *ch = malloc(sizeof *ch);
        if (!vm_compile(I, s, ch)) { free(ch); value_release(last); return val_null(); }
        bool ok;
        Value r = vm_run(I, ch, val_null(), nullptr, 0, nullptr, &ok);
        if (ch->nproto > 0) keep_chunk(ch);
        else { chunk_free(ch); free(ch); }
        if (!ok) { value_release(last); return val_null(); }
        value_release(last);
        last = r;
        if (echo && !s->silent && last.kind != VAL_NULL) {
            value_print(vout(), last);
            fputc('\n', vout());
            /* ans: the last value you saw and didn't name. Echo-coupled by
             * design — suppressed statements and load()ed scripts (echo off)
             * never touch it, so ans always matches the screen. */
            /* let STATEMENTS name their result (no ans); a let..in
             * EXPRESSION (body != null) is anonymous — including every
             * where-clause, which desugars to let..in — and echoes, so it
             * sets ans like any expression. */
            if (s->kind != AST_LET || s->as.let.body != nullptr)
                env_define(globals, "ans", 3, last);
        }
    }
    return last;
}
