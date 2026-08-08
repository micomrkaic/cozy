/* arena.h — bump allocator for AST nodes (freed all at once). */
#ifndef COZY_ARENA_H
#define COZY_ARENA_H

#include <stddef.h>

typedef struct Arena Arena;

Arena *arena_new(void);
void  *arena_alloc(Arena *a, size_t n);
void   arena_free(Arena *a);

#endif
