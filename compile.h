/* compile.h — compile one Cozy statement (AST) into a bytecode Chunk. */
#ifndef COZY_COMPILE_H
#define COZY_COMPILE_H

#include "chunk.h"
#include "ast.h"
#include "eval.h"

/* Compiles a single top-level statement. Returns true on success; on an
 * unsupported construct it sets I->err / I->had_error and returns false
 * (the chunk is freed). The chunk leaves exactly one value (the statement's
 * result) on the operand stack at runtime. */
bool vm_compile(Interp *I, AstNode *stmt, Chunk *out);

#endif
