/* lib/src/memory/arena.c — Contiguous per-document arena (TODO 183)
 * Copyright (c) 2024, Ribose Inc.
 */
#include "arena.h"

#include <stdlib.h>
#include <string.h>

#define ARENA_ALIGNMENT 8u

static inline size_t align_up(size_t n) {
    return (n + (ARENA_ALIGNMENT - 1)) & ~(size_t)(ARENA_ALIGNMENT - 1);
}

TaurusArena* taurus_arena_create(size_t size) {
    if (size == 0 || size > (size_t)-1 - ARENA_ALIGNMENT) return NULL;
    TaurusArena* arena = (TaurusArena*)malloc(sizeof(TaurusArena));
    if (!arena) return NULL;
    arena->base = (char*)malloc(size);
    if (!arena->base) {
        free(arena);
        return NULL;
    }
    arena->size = size;
    arena->used = 0;
    arena->failed = 0;
    return arena;
}

void taurus_arena_destroy(TaurusArena* arena) {
    if (!arena) return;
    free(arena->base);
    free(arena);
}

void* taurus_arena_alloc(TaurusArena* arena, size_t size) {
    if (!arena || size == 0) return NULL;
    size_t aligned = align_up(size);
    if (aligned > arena->size - arena->used) {
        arena->failed = 1;  /* sticky: allocator has been exhausted */
        return NULL;
    }
    void* p = arena->base + arena->used;
    arena->used += aligned;
    return p;
}

void* taurus_arena_alloc_zeroed(TaurusArena* arena, size_t size) {
    void* p = taurus_arena_alloc(arena, size);
    if (p) memset(p, 0, size);
    return p;
}

void* taurus_arena_alloc_node_with_content(TaurusArena* arena,
                                            size_t struct_size,
                                            size_t content_size,
                                            char** content_out) {
    /* One combined bump keeps struct + content contiguous. The NUL
     * terminator slot is included in the content region. */
    size_t total = align_up(struct_size) + content_size + 1;
    if (!arena || !content_out) return NULL;
    if (total > arena->size - arena->used) {
        arena->failed = 1;
        return NULL;
    }
    char* node = arena->base + arena->used;
    arena->used += total;
    *content_out = node + align_up(struct_size);
    return node;
}

size_t taurus_arena_remaining(const TaurusArena* arena) {
    if (!arena) return 0;
    return arena->size - arena->used;
}

void* taurus_arena_base(const TaurusArena* arena) {
    return arena ? arena->base : NULL;
}
