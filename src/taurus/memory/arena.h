/* lib/src/memory/arena.h — Contiguous per-document arena (TODO 183)
 * Copyright (c) 2024, Ribose Inc.
 *
 * One single contiguous allocation per document. All node/attr/string
 * storage for the document is bump-allocated inside it.
 *
 * WHY (the constraint that motivated this): the page-based pool
 * allocates 32 KB pages via independent mallocs that can land
 * megabytes apart on macOS ASLR / Linux glibc. Compact-pointer tree
 * edges (cp16, ±256 KB) that span pages silently truncate and corrupt
 * the tree — discovered while attempting TODO 180 Phase C (three
 * failing tests incl. a segfault). With one contiguous arena, every
 * two allocations are within `size` bytes of each other by
 * construction, so edge encodings are bounded by the arena size.
 *
 * THE CONTRACT — fail-fast, no silent fallback:
 *   taurus_arena_alloc returns NULL when the request does not fit in
 *   the remaining space. It never falls back to malloc. This is the
 *   load-bearing property: a returned pointer is guaranteed to lie
 *   within [base, base + size), so callers can encode cross pointers
 *   against `base` with a bound known up front. (pugixml behaves the
 *   same way — parse fails on allocator exhaustion.)
 *
 * Allocations are 8-byte aligned, matching the pool, so compact
 * pointers scaled by 8 (align_log2 = 3) remain valid.
 */
#ifndef TAURUS_MEMORY_ARENA_H
#define TAURUS_MEMORY_ARENA_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct taurus_arena {
    char* base;    /* Single contiguous allocation (NULL when empty) */
    size_t size;   /* Total capacity in bytes */
    size_t used;   /* Bump pointer — bytes handed out so far */
    size_t failed; /* 1 once any request has been refused (sticky) */
} TaurusArena;

/* Create an arena of `size` bytes (one malloc). Returns NULL on
 * malloc failure or nonsensical size. The returned base pointer is
 * at least 8-byte aligned (malloc guarantees max_align_t). */
TaurusArena* taurus_arena_create(size_t size);

/* Destroy the arena and the single backing allocation. NULL-safe.
 * All pointers handed out by the arena become invalid. */
void taurus_arena_destroy(TaurusArena* arena);

/* Bump-allocate `size` bytes, 8-byte aligned. NOT zeroed.
 * Returns NULL when the request does not fit in remaining space —
 * no fallback allocation, ever (see the contract above). */
void* taurus_arena_alloc(TaurusArena* arena, size_t size);

/* Like taurus_arena_alloc, but zeroes the returned block. */
void* taurus_arena_alloc_zeroed(TaurusArena* arena, size_t size);

/* Allocate a node struct plus an associated content buffer in one
 * bump, contiguous (cache-friendly — same property the pool provides
 * via taurus_pool_alloc_node_with_content). Fails if the combined
 * size does not fit. */
void* taurus_arena_alloc_node_with_content(TaurusArena* arena,
                                            size_t struct_size,
                                            size_t content_size,
                                            char** content_out);

/* Capacity not yet handed out. */
size_t taurus_arena_remaining(const TaurusArena* arena);

/* The base pointer for compact-pointer encoding. All allocations lie
 * within [base, base + size). */
void* taurus_arena_base(const TaurusArena* arena);

#ifdef __cplusplus
}
#endif

#endif /* TAURUS_MEMORY_ARENA_H */
