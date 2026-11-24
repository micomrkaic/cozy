/* chunk.h — a compiled unit of Cozy bytecode.
 *
 * A Chunk holds a flat byte stream, a constant pool (owned Values, computed
 * once at compile time — this also folds literals so "3.14" is parsed once,
 * not on every evaluation), and a name pool (non-owning slices into the parse
 * arena, used for global get/define/assign and field access).
 *
 * Every local variable is a frame slot (0.1.39): a function's self and
 * parameters, and since then loop variables, `let`s inside loops, blocks
 * and functions, and the '@' of a pipe. Names are left only for globals. */
#ifndef COZY_CHUNK_H
#define COZY_CHUNK_H

#include <stdint.h>
#include "value.h"

typedef enum : uint8_t {
    OP_CONST,          /* u16 const : push constants[idx] (+1)                */
    OP_NULL,           /*           : push null                               */
    OP_GET_VAR,        /* u16 name  : env_lookup -> push (+1); err if missing */
    OP_DEFINE,         /* u16 name  : env_define(name, peek); leaves value    */
    OP_SET_VAR,        /* u16 name  : env_assign(name, peek); err if undeclared */
    OP_GET_AT,         /*           : '@' outside a pipe: always the error    */
    OP_BINOP,          /* u8 token  : b=peek,a=peek; push apply_binop; pop 2  */
    OP_UNARY,          /* u8 token  : v=peek; push apply_unary; pop 1         */
    OP_TRANSPOSE,      /* u8 conj   : v=peek; push transpose; pop 1           */
    OP_RANGE,          /*           : [start,step,stop] -> push range; pop 3  */
    OP_CALL,           /* u8 argc   : [callee,args...] -> push result         */
    OP_AUTOCALL,       /*           : if top is a 0-arg callable, call it       */
    OP_RETURN,         /*           : return top of stack from the function    */
    OP_FIELD,          /* u16 name  : rec=peek -> push field; pop 1           */
    OP_ASSERT_BOOL,    /*           : err unless top is Bool (for && / ||)    */
    OP_JUMP,           /* u16 off   : ip += off                               */
    OP_JUMP_IF_FALSE,  /* u16 off   : top must be Bool; if false ip += off    */
    OP_JUMP_IF_TRUE,   /* u16 off   : top must be Bool; if true  ip += off    */
    OP_POP,            /*           : pop and release                         */
    OP_ARGDEF,         /* u8 slot, u16 off : if frame slot is non-null, skip the
                          default-value code that follows (entry 13)            */
    OP_TEE,            /*           : print top of stack (tee pipe), no pop   */
    /* --- stage 2: scopes, loops, iteration --- */
    OP_LOOP,           /* u16 off   : ip -= off (backward jump)               */
    OP_FOR_BEGIN,      /*           : validate iterable (top); push index 0   */
    OP_FOR_NEXT,       /* u8 slot, u16 end : if done, empty the slot and jump */
                       /*           end; else slot = iter[idx], idx++         */
    OP_FOR_END,        /*           : pop [iterable, index]                   */
    OP_MARK_PUSH,      /*           : record sp (loop-body base) on the mark stack */
    OP_MARK_POP,       /*           : drop the top mark (normal loop exit)    */
    OP_MARK_RESET,     /*           : release stack down to the top mark (break/continue) */
    /* --- stage 2b: functions --- */
    OP_CLOSURE,        /* u16 proto, u8 nup : pop nup captured values as the    */
                       /*           closure's upvalue snapshots                  */
    /* --- lexical addressing: slot locals + value-capture upvalues --- */
    OP_GET_LOCAL,      /* u8 slot   : push frame slot                           */
    OP_SET_LOCAL,      /* u8 slot   : frame slot = top (leaves top)             */
    OP_GET_LOCAL_OR,   /* u8 slot, u16 skip : a `let` that may not have run: if */
                       /*           the slot holds a value push it and skip the */
                       /*           fallback code that follows; else fall into it */
    OP_SET_LOCAL_OR,   /* u8 slot, u16 skip : likewise for assignment           */
    OP_CLEAR_LOCALS,   /* u8 first, u8 n : a scope ends: release and empty the  */
                       /*           slots first .. first+n-1                    */
    OP_GET_UPVALUE,    /* u8 idx    : push a captured snapshot                  */
    OP_SET_UPVALUE,    /* u8 idx    : captured snapshot = top (leaves top)      */
    /* --- stage 3: aggregate builders --- */
    OP_MATRIX,         /* u16 shape : consts[shape] is an int row-count vector;*/
                       /*           build a matrix from the top sum(shape) vals */
    OP_INDEX,          /* u8 argc, u8 colonmask : [target, idx...] -> element/slice */
    /* --- 'end' inside an index: a dimension-size context bracketing args --- */
    OP_END_PUSH,       /* u8 argc : peek target, push its dim sizes              */
    OP_END_GET,        /* u8 dim  : push the current index's size along dim      */
    OP_END_POP,        /*         : pop the dim-size frame                       */
    OP_INDEX_SET,      /* u8 argc, u8 colonmask : a[idx] = value (copy-on-write) */
    OP_RECORD,         /* u16 n, then n u16 key-name indices: top n vals -> rec */
    /* --- an indexed reduction whose body runs inline (0.1.44; compile.c) ---
     * r[k = src] body, as a loop in the frame that wrote it: no closure, no
     * call per element. The results go to a Collect (nrt.h) the VM holds. */
    OP_RED_BEGIN,      /* u8 mode : mode 1: [r, start, step, stop] -> [r, src];   */
                       /*           mode 0: [r, src]. Start collecting.           */
    OP_RED_NEXT,       /* u8 slot, u16 end : slot = the first element; if there   */
                       /*           is none, empty the slot and jump to end       */
    OP_RED_STEP,       /* u8 slot, u16 back : pop the body's value into the       */
                       /*           Collect; slot = the next element and jump     */
                       /*           back to the body, or empty the slot and go on */
                       /*           (one instruction per element, not three)      */
    OP_RED_END,        /*           : [r, src] -> r(the results)                  */
} OpCode;

typedef struct Chunk {
    uint8_t  *code;     uint32_t code_len, code_cap;
    uint32_t *lines;    /* parallel to code: source line per byte (diagnostics) */
    Value    *consts;   uint32_t nconst, cconst;   /* owned */
    const char **names; uint32_t *namelens;
    uint32_t  nname, cname;                          /* non-owning arena slices */
    EnvCache *name_cache;   /* parallel to names: where a global of that name was last
                               found (value.h). One entry per place the name is written,
                               since names are pooled by source position. */
    /* function protos (owned sub-chunks, one per lambda compiled inside) */
    struct Chunk **protos; uint32_t nproto, cproto;
    const char *src; uint32_t srclen;   /* lambda source span (session-lived buffer), for body()/save() */
    /* parameters, when this chunk is a function proto (non-owning arena slices) */
    const char **params; uint32_t *paramlens; uint32_t nparams;
    uint32_t ndefaults;   /* trailing params with defaults; min arity = nparams - ndefaults */
    uint32_t nlocals;     /* frame slots for locals, above self and the parameters
                             (in a top-level statement: all of its slots) */
    const char *fname; uint32_t fnamelen;   /* function name for backtraces
                                               (the let-binding; empty = anonymous) */
    const char *doc; uint32_t doclen;       /* docstring span (into the kept source) */
} Chunk;

void     chunk_init(Chunk *c);
void     chunk_free(Chunk *c);                       /* releases constants + protos */
uint32_t chunk_add_const(Chunk *c, Value v);         /* takes ownership of v   */
uint32_t chunk_add_name(Chunk *c, const char *s, uint32_t len);
uint32_t chunk_add_proto(Chunk *c, Chunk *proto);    /* takes ownership of proto */
void     chunk_emit(Chunk *c, uint8_t b, uint32_t line);
void     chunk_emit_u16(Chunk *c, uint16_t w, uint32_t line);
void     chunk_disassemble(FILE *out, const Chunk *c, const char *title);

#endif
