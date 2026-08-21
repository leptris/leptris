/* lib/src/memory/arena.c — Contiguous per-document arena (TODO 183)
 * Copyright (c) 2024, Ribose Inc.
 */
#include "arena.h"

#include <stdlib.h>
#include <string.h>

/* Allocation hooks (core.c): honoring the public
 * leptris_set_memory_management_functions contract for arena
 * allocations too — OOM injection and custom allocators must see
 * every byte the library takes. */
void* leptris_alloc_hook(size_t size);
void leptris_free_hook(void* ptr);

#define ARENA_ALIGNMENT 8u

static inline size_t align_up(size_t n) {
    return (n + ARENA_ALIGNMENT - 1) & ~(size_t)(ARENA_ALIGNMENT - 1);
}

/* ---- Retained-block free list (parse fault fix) -----------------------
 *
 * Libc routes mallocs above the magazine-zone threshold (256 KB on
 * macOS, similar on glibc) straight to mmap, and munmaps them on
 * free. A parse of an 884 KB many-attr document builds a ~7.5 MB
 * arena, so every parse/free/parse cycle faults ~1,600 zero-fill
 * pages — measured at 0.21 us/page, ~400 us of a 1.36 ms parse.
 * That repeated-fault tax is why the parse ratio vs pugixml grew
 * with document size: their node memory grows through incremental
 * small pages, which libc zone-reuses without unmapping.
 *
 * Retaining a handful of freed blocks and handing them back on the
 * next create keeps the pages mapped across documents.
 *
 * Semantics identical to malloc: the memory is NOT zeroed (callers
 * memset what they need — the same contract as a fresh malloc), so
 * a reused block behaves exactly like a new allocation.
 *
 * Bounded: at most 4 blocks / 32 MB retained process-wide. Blocks
 * below ARENA_RETAIN_MIN go straight to free() — small allocations
 * are zone-reused by libc anyway, so retaining them buys nothing.
 * Retained blocks stay reachable through this table, so leak
 * checkers classify them as still-reachable, not leaked.
 *
 * Thread safety: a spinlock guards the table. It is held for ~50 ns
 * twice per document lifetime, so contention is irrelevant. */
#define ARENA_RETAIN_MIN (256u * 1024u)
#define ARENA_RETAIN_MAX_BLOCKS 4u
#define ARENA_RETAIN_MAX_BYTES (32u * 1024u * 1024u)

static struct {
    char* base[ARENA_RETAIN_MAX_BLOCKS];
    size_t size[ARENA_RETAIN_MAX_BLOCKS];
    size_t count;
    size_t bytes;
} g_retain;

#if defined(_MSC_VER)
#include <intrin.h>
static volatile long g_retain_lock;
static void retain_lock(void) {
    while (_InterlockedCompareExchange(&g_retain_lock, 1, 0)) {
        while (g_retain_lock) { /* spin */ }
    }
}
static void retain_unlock(void) { _InterlockedExchange(&g_retain_lock, 0); }
#else
static volatile int g_retain_lock;
static void retain_lock(void) {
    while (!__sync_bool_compare_and_swap(&g_retain_lock, 0, 1)) {
        while (g_retain_lock) { /* spin */ }
    }
}
static void retain_unlock(void) { __sync_lock_release(&g_retain_lock); }
#endif

/* Best fit: the smallest retained block that covers the request, so
 * oversized blocks aren't burned on small documents. Returns the
 * block and reports its true capacity; NULL when nothing fits (or
 * the request is below the retain threshold — malloc territory). */
static char* retain_take(size_t request, size_t* capacity) {
    if (request < ARENA_RETAIN_MIN) return NULL;
    char* found = NULL;
    size_t found_size = 0;
    retain_lock();
    for (size_t i = 0; i < g_retain.count; i++) {
        if (g_retain.size[i] >= request &&
            (found_size == 0 || g_retain.size[i] < found_size)) {
            found = g_retain.base[i];
            found_size = g_retain.size[i];
            g_retain.base[i] = g_retain.base[g_retain.count - 1];
            g_retain.size[i] = g_retain.size[g_retain.count - 1];
            g_retain.count--;
            break;
        }
    }
    g_retain.bytes -= found_size;
    retain_unlock();
    if (found) *capacity = found_size;
    return found;
}

static void retain_give(char* base, size_t size) {
    if (size < ARENA_RETAIN_MIN) {
        leptris_free_hook(base);
        return;
    }
    retain_lock();
    if (g_retain.count >= ARENA_RETAIN_MAX_BLOCKS ||
        g_retain.bytes + size > ARENA_RETAIN_MAX_BYTES) {
        retain_unlock();
        leptris_free_hook(base);
        return;
    }
    g_retain.base[g_retain.count] = base;
    g_retain.size[g_retain.count] = size;
    g_retain.count++;
    g_retain.bytes += size;
    retain_unlock();
}

char* leptris_arena_buffer_alloc(size_t size) {
    size_t capacity = size;
    char* base = retain_take(size, &capacity);
    return base ? base : (char*)leptris_alloc_hook(size);
}

void leptris_arena_buffer_release(void* p, size_t size) {
    if (!p) return;
    retain_give((char*)p, size);
}

LeptrisArena* leptris_arena_create(size_t size) {
    if (size == 0 || size > (size_t)-1 - ARENA_ALIGNMENT) return NULL;
    LeptrisArena* arena = (LeptrisArena*)leptris_alloc_hook(sizeof(LeptrisArena));
    if (!arena) return NULL;
    size_t capacity = size;
    char* base = retain_take(size, &capacity);
    if (!base) {
        base = (char*)leptris_alloc_hook(size);
        capacity = size;
        if (!base) {
            leptris_free_hook(arena);
            return NULL;
        }
    }
    arena->base = base;
    /* For a reused block this is the BLOCK capacity, so the
     * fail-fast bound [base, base + size) and remaining() stay
     * exact; it is >= the requested size, never smaller. */
    arena->size = capacity;
    arena->used = 0;
    arena->failed = 0;
    return arena;
}

void leptris_arena_destroy(LeptrisArena* arena) {
    if (!arena) return;
    retain_give(arena->base, arena->size);
    leptris_free_hook(arena);
}

void* leptris_arena_alloc(LeptrisArena* arena, size_t size) {
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

void* leptris_arena_alloc_zeroed(LeptrisArena* arena, size_t size) {
    void* p = leptris_arena_alloc(arena, size);
    if (p) memset(p, 0, size);
    return p;
}

void* leptris_arena_alloc_node_with_content(LeptrisArena* arena,
                                            size_t struct_size,
                                            size_t content_size,
                                            char** content_out) {
    /* One combined bump keeps struct + content contiguous. The NUL
     * terminator slot is included in the content region. Both parts
     * are aligned so the NEXT bump stays on the 8-byte grid — an
     * unaligned tail here would misalign every subsequent allocation
     * (pool parity: ALIGN_SIZE(content_size + 1)). */
    size_t total = align_up(struct_size) + align_up(content_size + 1);
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

size_t leptris_arena_remaining(const LeptrisArena* arena) {
    if (!arena) return 0;
    return arena->size - arena->used;
}

void* leptris_arena_base(const LeptrisArena* arena) {
    return arena ? arena->base : NULL;
}
