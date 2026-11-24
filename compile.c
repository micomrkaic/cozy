/* compile.c — Cozy AST -> bytecode, with lexical addressing.
 *
 * A resolver pass (the Compiler stack) classifies every identifier as a frame
 * slot (a local), an upvalue (a free variable captured from an enclosing
 * function by value-snapshot), or a name (a global, found in the session's
 * environment when the instruction runs). Function calls thus need no
 * per-call environment, and closures capture only the values they use, so a
 * closure never holds a reference back to the scope that made it (the
 * env/closure cycle is gone). Globals stay late-bound by name, which is
 * what keeps recursion and mutual recursion working. A function's own bound
 * name resolves to slot 0 (the self-slot), so direct recursion is a slot
 * read, not a global lookup.
 *
 * Since 0.1.39 every local is a slot: self and the parameters as before,
 * and now loop variables, `let`s inside loops, blocks and functions, and
 * the '@' of a pipe. Until then those were names in a chain of run-time
 * scopes (one environment made and dropped per pass of a loop). What a
 * scope meant is kept exactly:
 *   - a scope's slots are emptied when it ends (OP_CLEAR_LOCALS), at the
 *     end of every pass of a loop, and on `break` and `continue` — so a
 *     value is released when its scope would have been dropped, and a
 *     `let` of one pass is not there in the next;
 *   - a `let` that is a statement of a block or a loop body has always run
 *     by the time anything after it in that scope does, so it is read with
 *     OP_GET_LOCAL. A `let` written where it may be skipped — in
 *     parentheses, as in `if c then (let q = 1) end` — is read with
 *     OP_GET_LOCAL_OR: when the slot is empty the code that follows looks
 *     outward (an outer local of the name, an upvalue, the global), which
 *     is what the chain of scopes did.
 * A top-level `let` is a global, as it must be: every statement typed at
 * the prompt is its own chunk. */
#include "compile.h"
#include "nrt.h"
#include "lexer.h"
#include <stdarg.h>
#include <stdio.h>
#include <setjmp.h>
#include <stdlib.h>
#include <string.h>

[[noreturn]] static void compile_error(Interp *I, AstNode *n, const char *fmt, ...)
{
    char msg[200];
    va_list ap; va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    I->cur_line = n->line; I->cur_col = n->col;
    snprintf(I->err, sizeof I->err, "%s", msg);
    I->had_error = true;
    longjmp(I->jmp, 1);
}

/* ---- resolver state ------------------------------------------------------ */

typedef enum {
    LK_SLOT,                      /* holds a value wherever it can be referred to      */
    LK_MAYBE                      /* a `let` that may not have run: read with a check  */
} LKind;

typedef struct {
    const char *name; uint32_t len;
    LKind   kind;
    uint8_t slot;                 /* frame slot                                        */
    int     depth;                /* lexical depth where declared                      */
} CLocal;

typedef struct {
    bool    from_local;           /* captured from the enclosing function's local ...  */
    int     local;                /*   ... this one (index in its locals[])            */
    uint8_t up_index;             /* if !from_local: enclosing upvalue index           */
    const char *name; uint32_t len;
} CUpval;

typedef struct Compiler {
    struct Compiler *enclosing;
    Chunk  *chunk;
    CLocal  locals[256];  int nlocals;
    int     nslots;              /* slots in use here; a scope's are given back at its end */
    int     max_slots;           /* the most in use at once: the frame's size            */
    CUpval  upvals[256];  int nupvals;
    int     depth;                /* 0 = global (top-level); >0 = inside a scope        */
    int     index_dim;           /* dimension being compiled inside an index, else -1   */
    bool    in_function;         /* compiling a lambda body? (enables 'return')         */
    AstNode *direct_let;         /* the `let` statement now being compiled as an item
                                    of a block or loop body (it cannot be skipped)      */
    int     loop_floor;          /* loops[] below this index belong to code an inline
                                    reduction body must not break out of or continue    */
    struct { int break_base;     /* `break` empties the slots from here up              */
             int cont_base;      /* `continue` from here up (a for keeps its variable)  */
             uint32_t cont_target; bool is_for;
             uint32_t breaks[32]; int nbreaks; } loops[8];
    int     nloops;
} Compiler;

static void compile_node(Interp *I, Compiler *cc, AstNode *n);
static void compile_lambda(Interp *I, Compiler *cc, AstNode *n,
                           const char *selfname, uint32_t selflen);
static void emit_name_op_g(Chunk *c, uint8_t op, const char *s, uint32_t len, uint32_t line);

static int resolve_local(Compiler *cc, const char *name, uint32_t len)
{
    for (int i = cc->nlocals - 1; i >= 0; i--)
        if (cc->locals[i].len == len && memcmp(cc->locals[i].name, name, len) == 0)
            return i;
    return -1;
}

static int add_upval(Compiler *cc, CUpval uv)
{
    for (int i = 0; i < cc->nupvals; i++) {           /* dedup */
        CUpval *e = &cc->upvals[i];
        if (e->from_local == uv.from_local && e->local == uv.local &&
            e->up_index == uv.up_index &&
            e->len == uv.len && (uv.len == 0 || memcmp(e->name, uv.name, uv.len) == 0))
            return i;
    }
    cc->upvals[cc->nupvals] = uv;
    return cc->nupvals++;
}

/* Is `name` a local (any kind) of some enclosing function? If so register an
 * upvalue chain down to `cc` and return its index here; else -1 (it's global). */
static int resolve_upvalue(Compiler *cc, const char *name, uint32_t len)
{
    if (!cc->enclosing) return -1;
    int li = resolve_local(cc->enclosing, name, len);
    if (li >= 0) {
        CUpval uv = { .from_local = true, .local = li,
                      .up_index = 0, .name = name, .len = len };
        return add_upval(cc, uv);
    }
    int ui = resolve_upvalue(cc->enclosing, name, len);
    if (ui >= 0) {
        CUpval uv = { .from_local = false, .local = 0,
                      .up_index = (uint8_t)ui, .name = name, .len = len };
        return add_upval(cc, uv);
    }
    return -1;
}

static void emit_get_below(Compiler *cc, int top, const char *name, uint32_t len, uint32_t L);
static void emit_set_below(Compiler *cc, int top, const char *name, uint32_t len, uint32_t L);

/* an OR instruction's skip distance: the fallback code is a handful of bytes */
static uint32_t emit_or(Chunk *c, uint8_t op, uint8_t slot, uint32_t L)
{
    chunk_emit(c, op, L); chunk_emit(c, slot, L);
    chunk_emit_u16(c, 0xffff, L);
    return c->code_len - 2;
}
static void patch_or(Chunk *c, uint32_t at)
{
    uint32_t off = c->code_len - (at + 2);
    c->code[at] = (uint8_t)(off & 0xff); c->code[at + 1] = (uint8_t)(off >> 8);
}

/* Read the local locals[li]. One that may not have run falls back to
 * whatever the name means outside it. */
static void emit_get_local(Compiler *cc, int li, uint32_t L)
{
    Chunk *c = cc->chunk;
    CLocal l = cc->locals[li];
    if (l.kind == LK_SLOT) { chunk_emit(c, OP_GET_LOCAL, L); chunk_emit(c, l.slot, L); return; }
    uint32_t at = emit_or(c, OP_GET_LOCAL_OR, l.slot, L);
    emit_get_below(cc, li, l.name, l.len, L);
    patch_or(c, at);
}
static void emit_set_local(Compiler *cc, int li, uint32_t L)
{
    Chunk *c = cc->chunk;
    CLocal l = cc->locals[li];
    if (l.kind == LK_SLOT) { chunk_emit(c, OP_SET_LOCAL, L); chunk_emit(c, l.slot, L); return; }
    uint32_t at = emit_or(c, OP_SET_LOCAL_OR, l.slot, L);
    emit_set_below(cc, li, l.name, l.len, L);
    patch_or(c, at);
}

/* Resolve `name` among the locals below index `top`, then the upvalues,
 * then the globals. */
static void emit_get_below(Compiler *cc, int top, const char *name, uint32_t len, uint32_t L)
{
    for (int i = top - 1; i >= 0; i--)
        if (cc->locals[i].len == len && memcmp(cc->locals[i].name, name, len) == 0) {
            emit_get_local(cc, i, L);
            return;
        }
    int ui = resolve_upvalue(cc, name, len);
    if (ui >= 0) { chunk_emit(cc->chunk, OP_GET_UPVALUE, L); chunk_emit(cc->chunk, (uint8_t)ui, L); return; }
    emit_name_op_g(cc->chunk, OP_GET_VAR, name, len, L);     /* global */
}
static void emit_set_below(Compiler *cc, int top, const char *name, uint32_t len, uint32_t L)
{
    for (int i = top - 1; i >= 0; i--)
        if (cc->locals[i].len == len && memcmp(cc->locals[i].name, name, len) == 0) {
            emit_set_local(cc, i, L);
            return;
        }
    int ui = resolve_upvalue(cc, name, len);
    if (ui >= 0) { chunk_emit(cc->chunk, OP_SET_UPVALUE, L); chunk_emit(cc->chunk, (uint8_t)ui, L); return; }
    emit_name_op_g(cc->chunk, OP_SET_VAR, name, len, L);     /* global */
}

static void emit_get_named(Compiler *cc, const char *name, uint32_t len, uint32_t L)
{ emit_get_below(cc, cc->nlocals, name, len, L); }
static void emit_set_named(Compiler *cc, const char *name, uint32_t len, uint32_t L)
{ emit_set_below(cc, cc->nlocals, name, len, L); }

/* Declare a local in the current scope and give it a slot; returns its
 * index in locals[]. `rebind`: a `let` of a name this very scope already
 * has takes that local over (as defining a name twice in one environment
 * did), so a loop that says `let x = ...` twice does not use two slots. */
static int declare_local(Interp *I, Compiler *cc, AstNode *at, const char *name, uint32_t len,
                         LKind kind, bool rebind)
{
    if (rebind)
        for (int i = cc->nlocals - 1; i >= 0 && cc->locals[i].depth == cc->depth; i--)
            if (cc->locals[i].len == len && memcmp(cc->locals[i].name, name, len) == 0)
                return i;
    if (cc->nslots >= 256 || cc->nlocals >= 256)
        compile_error(I, at, "too many local variables in one function (at most 255 at once)");
    cc->locals[cc->nlocals] = (CLocal){ name, len, kind, (uint8_t)cc->nslots, cc->depth };
    cc->nslots++;
    if (cc->nslots > cc->max_slots) cc->max_slots = cc->nslots;
    return cc->nlocals++;
}
static void begin_scope(Compiler *cc) { cc->depth++; }
/* Leave a scope: forget its locals and give back their slots. `clear`
 * emits the instruction that empties them at run time; a for loop's
 * variable is emptied by OP_FOR_NEXT itself when the loop runs out. */
static void end_scope(Compiler *cc, bool clear, uint32_t L)
{
    cc->depth--;
    int first = cc->nslots;
    while (cc->nlocals > 0 && cc->locals[cc->nlocals - 1].depth > cc->depth) {
        cc->nlocals--;
        if (cc->locals[cc->nlocals].slot < first) first = cc->locals[cc->nlocals].slot;
    }
    if (clear && first < cc->nslots) {
        chunk_emit(cc->chunk, OP_CLEAR_LOCALS, L);
        chunk_emit(cc->chunk, (uint8_t)first, L);
        chunk_emit(cc->chunk, (uint8_t)(cc->nslots - first), L);
    }
    cc->nslots = first;
}
/* break / continue: empty the slots of the scopes being left, without
 * forgetting them (the code after the jump is still compiled in them). */
static void emit_clear_from(Compiler *cc, int base, uint32_t L)
{
    if (base >= cc->nslots) return;
    chunk_emit(cc->chunk, OP_CLEAR_LOCALS, L);
    chunk_emit(cc->chunk, (uint8_t)base, L);
    chunk_emit(cc->chunk, (uint8_t)(cc->nslots - base), L);
}

/* ---- emit helpers -------------------------------------------------------- */

static uint32_t emit_jump(Chunk *c, uint8_t op, uint32_t line)
{
    chunk_emit(c, op, line);
    chunk_emit_u16(c, 0xffff, line);
    return c->code_len - 2;
}
static void patch_jump(Interp *I, AstNode *n, Chunk *c, uint32_t at)
{
    uint32_t off = c->code_len - (at + 2);
    if (off > 0xffff) compile_error(I, n, "branch too large for the VM");
    c->code[at] = (uint8_t)(off & 0xff); c->code[at + 1] = (uint8_t)(off >> 8);
}
static void emit_loop(Interp *I, AstNode *n, Chunk *c, uint32_t loop_start, uint32_t line)
{
    chunk_emit(c, OP_LOOP, line);
    uint32_t off = c->code_len + 2 - loop_start;
    if (off > 0xffff) compile_error(I, n, "loop body too large for the VM");
    chunk_emit_u16(c, (uint16_t)off, line);
}
static void emit_const(Chunk *c, Value v, uint32_t line)
{
    uint32_t idx = chunk_add_const(c, v);
    chunk_emit(c, OP_CONST, line);
    chunk_emit_u16(c, (uint16_t)idx, line);
}
static void emit_name_op_g(Chunk *c, uint8_t op, const char *s, uint32_t len, uint32_t line)
{
    uint32_t idx = chunk_add_name(c, s, len);
    chunk_emit(c, op, line);
    chunk_emit_u16(c, (uint16_t)idx, line);
}

/* ---- running a reduction's body inline --------------------------------------
 * r[k = src] body is r(map(fn k -> body, src)): a function made, and called
 * once per element. When the body is an ordinary expression that is all
 * overhead, and the compiler runs it as a loop in the frame that wrote it
 * (OP_RED_*): k is a local of that frame, and what the body reads from
 * outside it reads directly.
 *
 * A function is still made whenever being one could be told apart. As a
 * function, the body
 *   - `return`s from itself, not from the function around the reduction;
 *   - has no `@` of an outer pipe (that is an error, and stays one);
 *   - sees the outer LOCALS as they were when the reduction began, and
 *     assigns to its own copies of them: `sum[k = 1:n] (t = t + k; t)`
 *     inside a function leaves that function's t alone.
 * So a body with a `return`, a free `@`, or an assignment to a name that is
 * a local or a captured variable of an enclosing function is compiled as
 * the function it is, as before. Assigning a global or a variable the body
 * declares is no obstacle. The reducer and the source are evaluated before
 * the body ever runs and the function used to be made between them; if
 * either assigns or declares anything, the same caution applies.
 *
 * Nothing else changes hands: `break` and `continue` in the body may not
 * reach a loop outside it (loop_floor keeps that the compile error it
 * was), `end` in the body does not mean an index outside it, and the lets
 * of one element are gone before the next, as a fresh call's were. */

static bool name_in_enclosing_function(const Compiler *cc, const char *name, uint32_t len)
{
    for (const Compiler *c = cc; c; c = c->enclosing)
        for (int i = c->nlocals - 1; i >= 0; i--)
            if (c->locals[i].len == len && memcmp(c->locals[i].name, name, len) == 0) return true;
    return false;
}

/* May this expression run inline? `at_ok`: an '@' here belongs to a pipe
 * inside the expression (or, for the reducer and the source, to the pipe
 * around the whole reduction, where it always belonged). `pure`: the reducer
 * or the source, which may not assign or declare. Anything this function
 * does not know is answered no. */
static bool inline_ok(const Compiler *cc, const AstNode *n, bool at_ok, bool pure)
{
    if (!n) return true;
    switch (n->kind) {
    case AST_INT: case AST_FLOAT: case AST_IMAG: case AST_STRING: case AST_BOOL: case AST_NULL:
    case AST_IDENT: case AST_COLON: case AST_END: case AST_BREAK: case AST_CONTINUE:
        return true;
    case AST_AT:     return at_ok;
    case AST_RETURN: return false;
    case AST_LAMBDA: return true;                      /* its own function: nothing of it runs here */
    case AST_UNARY: case AST_POSTFIX:
        return inline_ok(cc, n->as.unary.operand, at_ok, pure);
    case AST_BINARY:
        if (n->as.binary.op == TOK_PIPE_GT || n->as.binary.op == TOK_PIPE_GTGT)
            return inline_ok(cc, n->as.binary.lhs, at_ok, pure) && inline_ok(cc, n->as.binary.rhs, true, pure);
        return inline_ok(cc, n->as.binary.lhs, at_ok, pure) && inline_ok(cc, n->as.binary.rhs, at_ok, pure);
    case AST_ASSIGN: {
        if (pure) return false;
        const AstNode *tgt = n->as.binary.lhs;
        const AstNode *name = tgt->kind == AST_INDEX ? tgt->as.call.callee : tgt;
        if (name->kind != AST_IDENT) return false;
        if (name_in_enclosing_function(cc, name->as.lit.text, name->as.lit.len)) return false;
        if (tgt->kind == AST_INDEX)
            for (uint32_t i = 0; i < tgt->as.call.args.count; i++)
                if (!inline_ok(cc, tgt->as.call.args.items[i], at_ok, pure)) return false;
        return inline_ok(cc, n->as.binary.rhs, at_ok, pure);
    }
    case AST_RANGE:
        return inline_ok(cc, n->as.range.start, at_ok, pure) && inline_ok(cc, n->as.range.step, at_ok, pure)
            && inline_ok(cc, n->as.range.stop, at_ok, pure);
    case AST_CALL: case AST_INDEX:
        if (!inline_ok(cc, n->as.call.callee, at_ok, pure)) return false;
        for (uint32_t i = 0; i < n->as.call.args.count; i++)
            if (!inline_ok(cc, n->as.call.args.items[i], at_ok, pure)) return false;
        return true;
    case AST_FIELD:
        return inline_ok(cc, n->as.field.target, at_ok, pure);
    case AST_ROW: case AST_MATRIX: case AST_BLOCK: case AST_BLOCK_EXPR: case AST_RECORD:
        for (uint32_t i = 0; i < n->as.list.count; i++)
            if (!inline_ok(cc, n->as.list.items[i], at_ok, pure)) return false;
        return true;
    case AST_RECORD_FIELD:
        return inline_ok(cc, n->as.recfield.value, at_ok, pure);
    case AST_IF:
        return inline_ok(cc, n->as.iff.cond, at_ok, pure) && inline_ok(cc, n->as.iff.then_e, at_ok, pure)
            && inline_ok(cc, n->as.iff.else_e, at_ok, pure);
    case AST_LET:
        if (pure) return false;
        return inline_ok(cc, n->as.let.value, at_ok, pure) && inline_ok(cc, n->as.let.body, at_ok, pure);
    case AST_FOR:
        return inline_ok(cc, n->as.forloop.iter, at_ok, pure) && inline_ok(cc, n->as.forloop.body, at_ok, pure);
    case AST_WHILE:
        return inline_ok(cc, n->as.whileloop.cond, at_ok, pure) && inline_ok(cc, n->as.whileloop.body, at_ok, pure);
    }
    return false;
}

/* ---- the walk ------------------------------------------------------------ */

/* Does this index argument reference 'end'? Stops at a nested index (whose
 * 'end' belongs to it). Conservative: unusual node types return true so the
 * end-context is always available when in doubt. */
static bool node_has_end(AstNode *n)
{
    if (!n) return false;
    switch (n->kind) {
    case AST_END:   return true;
    case AST_INDEX: return false;
    case AST_INT: case AST_FLOAT: case AST_IMAG: case AST_STRING:
    case AST_BOOL: case AST_NULL: case AST_IDENT: case AST_AT: case AST_COLON:
        return false;
    case AST_BINARY: case AST_ASSIGN:
        return node_has_end(n->as.binary.lhs) || node_has_end(n->as.binary.rhs);
    case AST_UNARY: case AST_POSTFIX:
        return node_has_end(n->as.unary.operand);
    case AST_RANGE:
        return node_has_end(n->as.range.start) || node_has_end(n->as.range.step)
            || node_has_end(n->as.range.stop);
    case AST_FIELD:
        return node_has_end(n->as.field.target);
    case AST_CALL:
        if (node_has_end(n->as.call.callee)) return true;
        for (uint32_t i = 0; i < n->as.call.args.count; i++)
            if (node_has_end(n->as.call.args.items[i])) return true;
        return false;
    case AST_MATRIX:
        for (uint32_t r = 0; r < n->as.list.count; r++) {
            AstNode *row = n->as.list.items[r];
            for (uint32_t k = 0; k < row->as.list.count; k++)
                if (node_has_end(row->as.list.items[k])) return true;
        }
        return false;
    case AST_IF:
        return node_has_end(n->as.iff.cond) || node_has_end(n->as.iff.then_e)
            || node_has_end(n->as.iff.else_e);
    default:                                        /* lambda/record/let/block/loops: rare here */
        return true;
    }
}

#define contains_at ast_contains_at

static void compile_node(Interp *I, Compiler *cc, AstNode *n)
{
    Chunk *c = cc->chunk;
    uint32_t L = n->line;
    switch (n->kind) {
    case AST_INT:    emit_const(c, val_int(parse_int_lit(n->as.lit.text, n->as.lit.len)), L); break;
    case AST_FLOAT:  emit_const(c, val_float(parse_float_lit(n->as.lit.text, n->as.lit.len)), L); break;
    case AST_IMAG:   emit_const(c, val_complex(0.0, parse_float_lit(n->as.lit.text, n->as.lit.len - 1)), L); break;
    case AST_STRING: emit_const(c, decode_string(n->as.lit.text, n->as.lit.len), L); break;
    case AST_BOOL:   emit_const(c, val_bool(n->as.boolean), L); break;
    case AST_NULL:   chunk_emit(c, OP_NULL, L); break;

    case AST_IDENT:  emit_get_named(cc, n->as.lit.text, n->as.lit.len, L); break;
    case AST_AT: {                                  /* the pipe's local; outside one, the error */
        int li = resolve_local(cc, "@", 1);
        if (li >= 0) emit_get_local(cc, li, L);
        else chunk_emit(c, OP_GET_AT, L);
        break;
    }

    case AST_UNARY:
        compile_node(I, cc, n->as.unary.operand);
        chunk_emit(c, OP_UNARY, L); chunk_emit(c, (uint8_t)n->as.unary.op, L);
        break;
    case AST_POSTFIX:
        compile_node(I, cc, n->as.unary.operand);
        chunk_emit(c, OP_TRANSPOSE, L); chunk_emit(c, n->as.unary.op == TOK_CTRANSPOSE ? 1 : 0, L);
        break;

    case AST_BINARY: {
        enum TokenKind op = n->as.binary.op;
        if (op == TOK_AND || op == TOK_OR) {
            compile_node(I, cc, n->as.binary.lhs);
            chunk_emit(c, OP_ASSERT_BOOL, L);
            uint32_t sc = emit_jump(c, op == TOK_AND ? OP_JUMP_IF_FALSE : OP_JUMP_IF_TRUE, L);
            chunk_emit(c, OP_POP, L);
            compile_node(I, cc, n->as.binary.rhs);
            chunk_emit(c, OP_ASSERT_BOOL, L);
            patch_jump(I, n, c, sc);
        } else if (op == TOK_PIPE_GT || op == TOK_PIPE_GTGT) {
            bool tee = (op == TOK_PIPE_GTGT);
            AstNode *rhs = n->as.binary.rhs;
            if (rhs->kind == AST_RECORD) {
                /* fan-out: x |> {a = f, b = g} ==> {a = f(x), b = g(x)} */
                if (contains_at(rhs))
                    compile_error(I, rhs, "the fan-out record cannot use '@' (each field is applied to the piped value)");
                uint32_t cnt = rhs->as.list.count;
                compile_node(I, cc, n->as.binary.lhs);
                if (tee) chunk_emit(c, OP_TEE, L);
                begin_scope(cc);
                int at = declare_local(I, cc, n, "@", 1, LK_SLOT, false);
                emit_set_local(cc, at, L);
                chunk_emit(c, OP_POP, L);
                for (uint32_t k = 0; k < cnt; k++) {
                    compile_node(I, cc, rhs->as.list.items[k]->as.recfield.value);
                    emit_get_local(cc, at, L);
                    chunk_emit(c, OP_CALL, L); chunk_emit(c, 1, L);
                }
                I->line_borrows_src = true;   /* fan-out records borrow keys too */
                chunk_emit(c, OP_RECORD, L); chunk_emit_u16(c, (uint16_t)cnt, L);
                for (uint32_t k = 0; k < cnt; k++) {
                    AstNode *f = rhs->as.list.items[k];
                    uint32_t ni = chunk_add_name(c, f->as.recfield.name, f->as.recfield.namelen);
                    chunk_emit_u16(c, (uint16_t)ni, L);
                }
                end_scope(cc, true, L);
            } else if (!tee && !contains_at(rhs) && n->as.binary.lhs->kind == AST_BINARY
                       && n->as.binary.lhs->as.binary.op == TOK_TILDE_GT) {
                /* (x ~> f) |> r, which is what r[k = a:b] body is: the map
                 * and the reduction in one pass (eval.c, bi_map_reduce). The
                 * operands are evaluated in the order they always were --
                 * r, f, then the range -- and a range written a:b or a:s:b
                 * is handed over as its three numbers, not as an array. */
                AstNode *m = n->as.binary.lhs, *src = m->as.binary.lhs;
                if (m->as.binary.rhs->kind == AST_RECORD)
                    compile_error(I, m->as.binary.rhs, "~> cannot fan out; use |> {...} (fan-out is a whole-value operation)");
                AstNode *lam = m->as.binary.rhs;
                if (lam->kind == AST_LAMBDA && lam->as.lambda.params.count == 1
                    && !(lam->as.lambda.defaults.items && lam->as.lambda.defaults.items[0])
                    && inline_ok(cc, rhs, true, true) && inline_ok(cc, src, true, true)
                    && inline_ok(cc, lam->as.lambda.body, false, false)) {
                    /* the body, inline (see "running a reduction's body inline") */
                    AstNode *par = lam->as.lambda.params.items[0];
                    compile_node(I, cc, rhs);                       /* the reducer */
                    if (src->kind == AST_RANGE) {
                        compile_node(I, cc, src->as.range.start);
                        if (src->as.range.step) compile_node(I, cc, src->as.range.step);
                        else emit_const(c, val_int(1), L);
                        compile_node(I, cc, src->as.range.stop);
                        chunk_emit(c, OP_RED_BEGIN, L); chunk_emit(c, 1, L);
                    } else {
                        compile_node(I, cc, src);
                        chunk_emit(c, OP_RED_BEGIN, L); chunk_emit(c, 0, L);
                    }
                    int saved_floor = cc->loop_floor, saved_dim = cc->index_dim;
                    cc->loop_floor = cc->nloops;                    /* no break / continue past the body */
                    cc->index_dim = -1;                             /* nor an `end` of an index outside it */
                    begin_scope(cc);                                /* the index variable's scope ... */
                    int var = declare_local(I, cc, par, par->as.lit.text, par->as.lit.len, LK_SLOT, false);
                    chunk_emit(c, OP_RED_NEXT, L);                  /* the first element, if any */
                    chunk_emit(c, cc->locals[var].slot, L);
                    uint32_t done = c->code_len; chunk_emit_u16(c, 0xffff, L);
                    uint32_t body_start = c->code_len;
                    begin_scope(cc);                                /* ... and the body's, one per element */
                    compile_node(I, cc, lam->as.lambda.body);
                    end_scope(cc, true, L);                         /* (its value is on the stack, not in a slot) */
                    chunk_emit(c, OP_RED_STEP, L);                  /* take it; next element and back, or on */
                    chunk_emit(c, cc->locals[var].slot, L);
                    uint32_t back = c->code_len + 2 - body_start;
                    if (back > 0xffff) compile_error(I, n, "loop body too large for the VM");
                    chunk_emit_u16(c, (uint16_t)back, L);
                    patch_jump(I, n, c, done);
                    end_scope(cc, false, L);
                    cc->loop_floor = saved_floor; cc->index_dim = saved_dim;
                    chunk_emit(c, OP_RED_END, L);
                    break;
                }
                emit_const(c, eval_map_reduce_builtin(), L);
                compile_node(I, cc, rhs);
                compile_node(I, cc, m->as.binary.rhs);
                if (src->kind == AST_RANGE) {
                    compile_node(I, cc, src->as.range.start);
                    if (src->as.range.step) compile_node(I, cc, src->as.range.step);
                    else emit_const(c, val_int(1), L);
                    compile_node(I, cc, src->as.range.stop);
                    chunk_emit(c, OP_CALL, L); chunk_emit(c, 5, L);
                } else {
                    compile_node(I, cc, src);
                    chunk_emit(c, OP_CALL, L); chunk_emit(c, 3, L);
                }
            } else if (!contains_at(rhs)) {             /* bare callable: x |> f  ==>  f(x) */
                compile_node(I, cc, rhs);
                compile_node(I, cc, n->as.binary.lhs);
                if (tee) chunk_emit(c, OP_TEE, L);
                chunk_emit(c, OP_CALL, L); chunk_emit(c, 1, L);
            } else {
                compile_node(I, cc, n->as.binary.lhs);
                if (tee) chunk_emit(c, OP_TEE, L);
                begin_scope(cc);
                int at = declare_local(I, cc, n, "@", 1, LK_SLOT, false);
                emit_set_local(cc, at, L);
                chunk_emit(c, OP_POP, L);
                compile_node(I, cc, rhs);
                end_scope(cc, true, L);
            }
        } else if (op == TOK_TILDE_GT) {
            /* elementwise pipe: x ~> f ==> map(f, x); the parser has already
             * wrapped an '@'-form rhs into fn @ -> rhs, so rhs is a callable. */
            if (n->as.binary.rhs->kind == AST_RECORD)
                compile_error(I, n->as.binary.rhs, "~> cannot fan out; use |> {...} (fan-out is a whole-value operation)");
            emit_const(c, eval_map_builtin(), L);   /* ~> means map-the-primitive: immune to shadowing */
            compile_node(I, cc, n->as.binary.rhs);
            compile_node(I, cc, n->as.binary.lhs);
            chunk_emit(c, OP_CALL, L); chunk_emit(c, 2, L);
        } else {
            compile_node(I, cc, n->as.binary.lhs);
            compile_node(I, cc, n->as.binary.rhs);
            chunk_emit(c, OP_BINOP, L); chunk_emit(c, (uint8_t)op, L);
        }
        break;
    }

    case AST_RANGE:
        compile_node(I, cc, n->as.range.start);
        if (n->as.range.step) compile_node(I, cc, n->as.range.step);
        else emit_const(c, val_int(1), L);
        compile_node(I, cc, n->as.range.stop);
        chunk_emit(c, OP_RANGE, L);
        break;

    case AST_CALL: {
        uint32_t argc = n->as.call.args.count;
        compile_node(I, cc, n->as.call.callee);
        for (uint32_t i = 0; i < argc; i++) compile_node(I, cc, n->as.call.args.items[i]);
        chunk_emit(c, OP_CALL, L); chunk_emit(c, (uint8_t)argc, L);
        break;
    }

    case AST_FIELD:
        compile_node(I, cc, n->as.field.target);
        emit_name_op_g(c, OP_FIELD, n->as.field.name, n->as.field.namelen, L);
        break;

    case AST_IF: {
        compile_node(I, cc, n->as.iff.cond);
        uint32_t else_j = emit_jump(c, OP_JUMP_IF_FALSE, L);
        chunk_emit(c, OP_POP, L);
        compile_node(I, cc, n->as.iff.then_e);
        uint32_t end_j = emit_jump(c, OP_JUMP, L);
        patch_jump(I, n, c, else_j);
        chunk_emit(c, OP_POP, L);
        if (n->as.iff.else_e) compile_node(I, cc, n->as.iff.else_e);
        else chunk_emit(c, OP_NULL, L);
        patch_jump(I, n, c, end_j);
        break;
    }

    case AST_LET: {
        bool direct = (cc->direct_let == n);          /* an item of a block: cannot be skipped */
        cc->direct_let = nullptr;
        if (n->as.let.body) {                         /* let x = v in body  (expression) */
            begin_scope(cc);
            if (n->as.let.value->kind == AST_LAMBDA)
                compile_lambda(I, cc, n->as.let.value, n->as.let.name, n->as.let.namelen);
            else
                compile_node(I, cc, n->as.let.value);
            int li = declare_local(I, cc, n, n->as.let.name, n->as.let.namelen, LK_SLOT, false);
            emit_set_local(cc, li, L);
            chunk_emit(c, OP_POP, L);                  /* the slot holds it; clear the stack copy */
            compile_node(I, cc, n->as.let.body);       /* leaves the body's value on the stack */
            end_scope(cc, true, L);                    /* empties the slot; value stays on stack */
            break;
        }
        if (n->as.let.value->kind == AST_LAMBDA)
            compile_lambda(I, cc, n->as.let.value, n->as.let.name, n->as.let.namelen);
        else
            compile_node(I, cc, n->as.let.value);
        if (cc->depth > 0) {                           /* a local: its slot */
            int li = declare_local(I, cc, n, n->as.let.name, n->as.let.namelen,
                                   direct ? LK_SLOT : LK_MAYBE, true);
            chunk_emit(c, OP_SET_LOCAL, L); chunk_emit(c, cc->locals[li].slot, L);
        } else                                         /* top level: a global, by name */
            emit_name_op_g(c, OP_DEFINE, n->as.let.name, n->as.let.namelen, L);
        break;
    }

    case AST_ASSIGN: {
        AstNode *tgt = n->as.binary.lhs;
        if (tgt->kind == AST_INDEX) {
            AstNode *name = tgt->as.call.callee;          /* parser guarantees AST_IDENT */
            uint32_t argc = tgt->as.call.args.count;
            if (argc != 1 && argc != 2) compile_error(I, tgt, "arrays take 1 or 2 indices");
            compile_node(I, cc, name);                    /* load the array */
            bool uses_end = false;
            for (uint32_t k = 0; k < argc; k++)
                if (node_has_end(tgt->as.call.args.items[k])) { uses_end = true; break; }
            if (uses_end) { chunk_emit(c, OP_END_PUSH, L); chunk_emit(c, (uint8_t)argc, L); }
            uint8_t colonmask = 0;
            int saved_dim = cc->index_dim;
            for (uint32_t k = 0; k < argc; k++) {
                AstNode *arg = tgt->as.call.args.items[k];
                if (arg->kind == AST_COLON) { colonmask |= (uint8_t)(1u << k); chunk_emit(c, OP_NULL, L); }
                else { cc->index_dim = (int)k; compile_node(I, cc, arg); }
            }
            cc->index_dim = saved_dim;
            if (uses_end) chunk_emit(c, OP_END_POP, L);
            compile_node(I, cc, n->as.binary.rhs);        /* the value */
            chunk_emit(c, OP_INDEX_SET, L); chunk_emit(c, (uint8_t)argc, L); chunk_emit(c, colonmask, L);
            emit_set_named(cc, name->as.lit.text, name->as.lit.len, L);  /* rebind the name */
        } else {
            compile_node(I, cc, n->as.binary.rhs);
            emit_set_named(cc, tgt->as.lit.text, tgt->as.lit.len, L);
        }
        break;
    }

    case AST_BLOCK:                                    /* a loop body: its statements in turn */
        for (uint32_t i = 0; i < n->as.list.count; i++) {
            if (i) chunk_emit(c, OP_POP, L);
            cc->direct_let = n->as.list.items[i];
            compile_node(I, cc, n->as.list.items[i]);
        }
        cc->direct_let = nullptr;
        if (n->as.list.count == 0) chunk_emit(c, OP_NULL, L);
        break;

    case AST_BLOCK_EXPR:                               /* scoped block as an expression */
        begin_scope(cc);
        for (uint32_t i = 0; i < n->as.list.count; i++) {
            if (i) chunk_emit(c, OP_POP, L);
            cc->direct_let = n->as.list.items[i];
            compile_node(I, cc, n->as.list.items[i]);
        }
        cc->direct_let = nullptr;
        if (n->as.list.count == 0) chunk_emit(c, OP_NULL, L);
        end_scope(cc, true, L);                        /* empties the block's slots; value stays */
        break;

    case AST_WHILE: {
        if (cc->nloops >= 8) compile_error(I, n, "loops nested too deeply");
        chunk_emit(c, OP_MARK_PUSH, L);                 /* record the body's stack base */
        uint32_t loop_start = c->code_len;
        compile_node(I, cc, n->as.whileloop.cond);
        uint32_t exit = emit_jump(c, OP_JUMP_IF_FALSE, L);
        chunk_emit(c, OP_POP, L);
        int li = cc->nloops++;
        cc->loops[li].break_base = cc->nslots;          /* break/continue empty the body's slots */
        cc->loops[li].cont_base  = cc->nslots;
        cc->loops[li].cont_target = loop_start;         /* continue re-evaluates the condition */
        cc->loops[li].is_for = false;
        cc->loops[li].nbreaks = 0;
        begin_scope(cc);
        compile_node(I, cc, n->as.whileloop.body);
        chunk_emit(c, OP_POP, L);
        end_scope(cc, true, L);                         /* a pass's lets do not see the next */
        emit_loop(I, n, c, loop_start, L);
        patch_jump(I, n, c, exit);
        chunk_emit(c, OP_POP, L);                        /* drop the false condition */
        for (int b = 0; b < cc->loops[li].nbreaks; b++)  /* break lands on the MARK_POP below */
            patch_jump(I, n, c, cc->loops[li].breaks[b]);
        cc->nloops--;
        chunk_emit(c, OP_MARK_POP, L);
        chunk_emit(c, OP_NULL, L);
        break;
    }

    case AST_FOR: {
        if (cc->nloops >= 8) compile_error(I, n, "loops nested too deeply");
        compile_node(I, cc, n->as.forloop.iter);
        chunk_emit(c, OP_FOR_BEGIN, L);
        chunk_emit(c, OP_MARK_PUSH, L);                 /* base above [iterable, index] */
        int li = cc->nloops++;
        cc->loops[li].break_base = cc->nslots;          /* break empties the variable too */
        begin_scope(cc);                                /* the loop variable's scope ... */
        int var = declare_local(I, cc, n, n->as.forloop.var, n->as.forloop.varlen, LK_SLOT, false);
        uint32_t loop_start = c->code_len;
        chunk_emit(c, OP_FOR_NEXT, L);                  /* sets it each pass; empties it at the end */
        chunk_emit(c, cc->locals[var].slot, L);
        uint32_t exit = c->code_len; chunk_emit_u16(c, 0xffff, L);
        cc->loops[li].cont_base = cc->nslots;           /* continue: the body's lets only */
        cc->loops[li].cont_target = loop_start;         /* continue goes to FOR_NEXT */
        cc->loops[li].is_for = true;
        cc->loops[li].nbreaks = 0;
        begin_scope(cc);                                /* ... and the body's, one per pass */
        compile_node(I, cc, n->as.forloop.body);
        chunk_emit(c, OP_POP, L);
        end_scope(cc, true, L);
        emit_loop(I, n, c, loop_start, L);
        patch_jump(I, n, c, exit);
        end_scope(cc, false, L);
        for (int b = 0; b < cc->loops[li].nbreaks; b++)  /* break lands on the MARK_POP below */
            patch_jump(I, n, c, cc->loops[li].breaks[b]);
        cc->nloops--;
        chunk_emit(c, OP_MARK_POP, L);
        chunk_emit(c, OP_FOR_END, L);
        chunk_emit(c, OP_NULL, L);
        break;
    }

    case AST_BREAK: {
        if (cc->nloops <= cc->loop_floor) compile_error(I, n, "'break' is only valid inside a loop");
        int li = cc->nloops - 1;
        chunk_emit(c, OP_MARK_RESET, L);                /* drop mid-expression temporaries */
        emit_clear_from(cc, cc->loops[li].break_base, L);
        if (cc->loops[li].nbreaks >= 32) compile_error(I, n, "too many 'break' statements in one loop");
        cc->loops[li].breaks[cc->loops[li].nbreaks++] = emit_jump(c, OP_JUMP, L);
        break;
    }

    case AST_CONTINUE: {
        if (cc->nloops <= cc->loop_floor) compile_error(I, n, "'continue' is only valid inside a loop");
        int li = cc->nloops - 1;
        chunk_emit(c, OP_MARK_RESET, L);                /* drop mid-expression temporaries */
        emit_clear_from(cc, cc->loops[li].cont_base, L);
        emit_loop(I, n, c, cc->loops[li].cont_target, L);
        break;
    }

    case AST_RETURN:
        if (!cc->in_function) compile_error(I, n, "'return' is only valid inside a function");
        if (n->as.unary.operand) compile_node(I, cc, n->as.unary.operand);
        else chunk_emit(c, OP_NULL, L);
        chunk_emit(c, OP_RETURN, L);
        break;

    case AST_LAMBDA: compile_lambda(I, cc, n, "", 0); break;

    case AST_MATRIX: {
        uint32_t nrows = n->as.list.count;
        Value shape = val_array(ELT_INT, 1, nrows);
        int64_t *sc = (int64_t *)as_arr(shape)->data;
        for (uint32_t ri = 0; ri < nrows; ri++) {
            AstNode *row = n->as.list.items[ri];
            sc[ri] = row->as.list.count;
            for (uint32_t ci = 0; ci < row->as.list.count; ci++)
                compile_node(I, cc, row->as.list.items[ci]);
        }
        uint32_t k = chunk_add_const(c, shape);
        chunk_emit(c, OP_MATRIX, L); chunk_emit_u16(c, (uint16_t)k, L);
        break;
    }

    case AST_INDEX: {
        uint32_t argc = n->as.call.args.count;
        if (argc != 1 && argc != 2) compile_error(I, n, "arrays take 1 or 2 indices");
        compile_node(I, cc, n->as.call.callee);
        bool uses_end = false;
        for (uint32_t k = 0; k < argc; k++)
            if (node_has_end(n->as.call.args.items[k])) { uses_end = true; break; }
        if (uses_end) { chunk_emit(c, OP_END_PUSH, L); chunk_emit(c, (uint8_t)argc, L); }
        uint8_t colonmask = 0;
        int saved_dim = cc->index_dim;
        for (uint32_t k = 0; k < argc; k++) {
            AstNode *arg = n->as.call.args.items[k];
            if (arg->kind == AST_COLON) { colonmask |= (uint8_t)(1u << k); chunk_emit(c, OP_NULL, L); }
            else { cc->index_dim = (int)k; compile_node(I, cc, arg); }
        }
        cc->index_dim = saved_dim;
        if (uses_end) chunk_emit(c, OP_END_POP, L);
        chunk_emit(c, OP_INDEX, L); chunk_emit(c, (uint8_t)argc, L); chunk_emit(c, colonmask, L);
        break;
    }

    case AST_COLON: compile_error(I, n, "':' is only valid inside an index, e.g. a[:, 2]");

    case AST_END:
        if (cc->index_dim < 0) compile_error(I, n, "'end' is only valid inside an index");
        chunk_emit(c, OP_END_GET, L); chunk_emit(c, (uint8_t)cc->index_dim, L);
        break;

    case AST_RECORD: {
        I->line_borrows_src = true;   /* record keys are non-owning src pointers */
        uint32_t cnt = n->as.list.count;
        for (uint32_t k = 0; k < cnt; k++)
            compile_node(I, cc, n->as.list.items[k]->as.recfield.value);
        chunk_emit(c, OP_RECORD, L); chunk_emit_u16(c, (uint16_t)cnt, L);
        for (uint32_t k = 0; k < cnt; k++) {
            AstNode *f = n->as.list.items[k];
            uint32_t ni = chunk_add_name(c, f->as.recfield.name, f->as.recfield.namelen);
            chunk_emit_u16(c, (uint16_t)ni, L);
        }
        break;
    }

    case AST_ROW:
    case AST_RECORD_FIELD: compile_error(I, n, "VM: malformed AST node");
    }
}

/* Compile a lambda into a proto chunk, then in the enclosing chunk push the
 * captured upvalue values (resolved in the enclosing frame) and OP_CLOSURE. */
static void compile_lambda(Interp *I, Compiler *cc, AstNode *n,
                           const char *selfname, uint32_t selflen)
{
    I->line_borrows_src = true;   /* closures keep chunk->src alive (entry 11) */
    Chunk *proto = malloc(sizeof *proto);
    if (!proto) abort();
    chunk_init(proto);
    proto->src = n->as.lambda.src; proto->srclen = n->as.lambda.srclen;
    uint32_t np = n->as.lambda.params.count;
    proto->nparams = np;                                  /* arity only; params are slots */
    /* synthetic lambdas (sections, ~> rewrites, binder desugars) build
     * AST_LAMBDA without a defaults list; treat that as none */
    AstNode **defs = n->as.lambda.defaults.items;
    uint32_t nd = 0;
    if (defs)
        for (uint32_t i = 0; i < np; i++)
            if (defs[i]) nd++;
    proto->ndefaults = nd;                                /* trailing (parser-enforced) */
    proto->fname = selfname; proto->fnamelen = selflen;   /* backtraces (entry: QoL 1) */
    proto->doc = n->as.lambda.doc; proto->doclen = n->as.lambda.doclen;   /* help() */
    uint32_t idx = chunk_add_proto(cc->chunk, proto);     /* enclosing owns proto */

    Compiler fc = (Compiler){ .enclosing = cc, .chunk = proto, .depth = 1, .index_dim = -1, .in_function = true };
    declare_local(I, &fc, n, selfname, selflen, LK_SLOT, false);   /* slot 0 = self (the closure) */
    for (uint32_t i = 0; i < np; i++) {
        AstNode *p = n->as.lambda.params.items[i];
        declare_local(I, &fc, p, p->as.lit.text, p->as.lit.len, LK_SLOT, false);   /* slots 1..np */
    }
    /* entry 13 prologue: a defaulted param whose slot arrived null (absent
     * or explicit null) evaluates its default here, left to right — so a
     * default may reference any earlier parameter. */
    for (uint32_t i = 0; defs && i < np; i++) {
        AstNode *def = defs[i];
        if (!def) continue;
        chunk_emit(proto, OP_ARGDEF, def->line);
        chunk_emit(proto, (uint8_t)(i + 1), def->line);   /* param slot */
        chunk_emit_u16(proto, 0xffff, def->line);
        uint32_t at = proto->code_len - 2;
        compile_node(I, &fc, def);
        chunk_emit(proto, OP_SET_LOCAL, def->line);
        chunk_emit(proto, (uint8_t)(i + 1), def->line);
        chunk_emit(proto, OP_POP, def->line);             /* SET_LOCAL leaves top */
        patch_jump(I, n, proto, at);
    }
    compile_node(I, &fc, n->as.lambda.body);              /* body leaves one value */
    chunk_emit(proto, OP_RETURN, n->line);                /* every chunk ends by returning it */
    proto->nlocals = (uint32_t)fc.max_slots - (np + 1);   /* its lets, loop variables, pipes */

    for (int i = 0; i < fc.nupvals; i++) {                /* push captures, in order */
        CUpval *uv = &fc.upvals[i];
        if (uv->from_local) {
            emit_get_local(cc, uv->local, n->line);
        } else {
            chunk_emit(cc->chunk, OP_GET_UPVALUE, n->line); chunk_emit(cc->chunk, uv->up_index, n->line);
        }
    }
    chunk_emit(cc->chunk, OP_CLOSURE, n->line);
    chunk_emit_u16(cc->chunk, (uint16_t)idx, n->line);
    chunk_emit(cc->chunk, (uint8_t)fc.nupvals, n->line);
}

bool vm_compile(Interp *I, AstNode *stmt, Chunk *out)
{
    chunk_init(out);
    /* Save and restore the caller's unwind target on every exit. Without
     * this, I->jmp is left pointing at this (returned) frame; any later
     * longjmp — e.g. load() re-raising after an inner vm_eval_program —
     * jumps into a dead frame. Found by the first builtin to raise after
     * running a nested program. */
    jmp_buf saved; memcpy(saved, I->jmp, sizeof(jmp_buf));
    if (setjmp(I->jmp)) {
        chunk_free(out);
        memcpy(I->jmp, saved, sizeof(jmp_buf));
        return false;
    }
    Compiler cc = (Compiler){ .enclosing = nullptr, .chunk = out, .depth = 0, .index_dim = -1 };
    compile_node(I, &cc, stmt);
    out->nlocals = (uint32_t)cc.max_slots;              /* a top-level loop's or block's locals */
    if (stmt->kind == AST_IDENT)        /* bare 'who' / 'rand' / a 0-arg fn name: call it */
        chunk_emit(out, OP_AUTOCALL, stmt->line);
    /* Every chunk ends in OP_RETURN (here and in compile_lambda), so the VM
     * does not ask "is this the end?" before each instruction (0.1.39). */
    chunk_emit(out, OP_RETURN, stmt->line);
    memcpy(I->jmp, saved, sizeof(jmp_buf));
    return true;
}
