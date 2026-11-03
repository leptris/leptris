/* lib/src/memory/arena.c — Contiguous per-document arena (TODO 183)
 * Copyright (c) 2024, Ribose Inc.
 */
/* glibc hides MAP_ANONYMOUS under -std=c99 (__STRICT_ANSI__); the
 * feature-test macros must precede every include of this TU. The
 * build adds _POSIX_C_SOURCE=200809L publicly, which puts Apple's
 * headers in strict mode — _DARWIN_C_SOURCE restores the BSD names
 * (MAP_ANON/MAP_ANONYMOUS) there. */
#if defined(__linux__) && !defined(_DEFAULT_SOURCE)
#define _DEFAULT_SOURCE 1
#endif
#if defined(__APPLE__) && !defined(_DARWIN_C_SOURCE)
#define _DARWIN_C_SOURCE 1
#endif

/* ---- #1947 lever 2: direct (anonymous-mmap) spans ---------------------
 *
 * A span above the retain budget can never be parked (retain_give
 * frees it unconditionally), so acquiring it through malloc buys
 * nothing and the freed pages linger in allocator magazines — the
 * ~2 GB post-drop residency the XMI/CityGML pipeline reports. Oversized
 * spans are taken mmap-direct and returned with munmap on destroy, so
 * the drop face collapses RSS deterministically.
 *
 * Stands down under ASAN (the malloc path keeps use-after-free visible
 * to the shadow) and under custom allocators (hooked accounting must
 * see every byte the library takes). Within-budget spans keep the
 * park-and-reuse lane — the repeated-parse fault fix depends on it. */
#if defined(_WIN32)
#define LEPTRIS_HAS_DIRECT_SPANS 1
#elif !defined(__wasm__) && !defined(__EMSCRIPTEN__) && \
    (defined(__unix__) || defined(__APPLE__) || defined(__MACH__))
#define LEPTRIS_HAS_DIRECT_SPANS 1
#endif

#include "arena.h"

#include "../common/port.h" /* LEPTRIS_THREAD_LOCAL */
#include <stdint.h> /* uintptr_t: was transitive via glibc's mman graph */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#if defined(__linux__) || \
    (defined(LEPTRIS_HAS_DIRECT_SPANS) && !defined(_WIN32))
#include <sys/mman.h>   /* madvise(MADV_HUGEPAGE), mmap/munmap */
#endif

#if defined(_WIN32)
#include <windows.h>
#endif

/* BSD headers name it MAP_ANON; the POSIX spelling may be absent. */
#if defined(LEPTRIS_HAS_DIRECT_SPANS) && !defined(MAP_ANONYMOUS) && \
    defined(MAP_ANON)
#define MAP_ANONYMOUS MAP_ANON
#endif

/* Allocation hooks (core.c): honoring the public
 * leptris_set_memory_management_functions contract for arena
 * allocations too — OOM injection and custom allocators must see
 * every byte the library takes. */
void* leptris_alloc_hook(size_t size);
void leptris_free_hook(void* ptr);
int leptris_custom_allocator_active(void);

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
/* #1846 slice 4: the global tier is split by size class. With ONE
 * 4-slot pool, every parked block kind competes for the same slots —
 * and parse input copies (freed at document close, never taken back)
 * permanently occupy slots, so arena spans hit the FULL free path and
 * the next same-size parse lands on a cold mapping (fresh mmap +
 * full first-touch: 2 of 5 takes missed on the 306KB fixture). Mid
 * [256KB, 2MB) and large [2MB, ..) now churn among themselves. */
#define ARENA_RETAIN_LARGE_MIN (2u * 1024u * 1024u)
/* 96MB: a large-document parse arena (~4x input with the
 * pre-sized estimate) must survive the free to be reused by the
 * next parse — at the old 32MB ceiling, every big-doc cycle
 * freed and re-malloc'd tens of MB (libc madvise showed ~2% of
 * repeated-parse wall time). One document's worth of parking. */
#define ARENA_RETAIN_MAX_BYTES (96u * 1024u * 1024u)

#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define ARENA_RETAIN_ASAN 1
#endif
#elif defined(__SANITIZE_ADDRESS__)
#define ARENA_RETAIN_ASAN 1
#endif

static struct {
    char* base[ARENA_RETAIN_MAX_BLOCKS];
    size_t size[ARENA_RETAIN_MAX_BLOCKS];
    size_t count;
    size_t bytes;
} g_retain_mid, g_retain_large;

/* Shared byte ceiling across both tiers (the footprint bound). */
static size_t retain_bytes_total(void) {
    return g_retain_mid.bytes + g_retain_large.bytes;
}

#if defined(_MSC_VER)
#include <intrin.h>
static volatile long g_retain_lock;
static void retain_lock(void) {
    /* Pure-CAS spin: the secondary plain read of the lock variable
     * is a non-atomic access racing the atomics on other threads
     * (TSan, surfaced by the worker-exit drain). CAS-only spins. */
    while (_InterlockedCompareExchange(&g_retain_lock, 1, 0)) {
    }
}
static void retain_unlock(void) { _InterlockedExchange(&g_retain_lock, 0); }
#else
static volatile int g_retain_lock;
static void retain_lock(void) {
    while (!__sync_bool_compare_and_swap(&g_retain_lock, 0, 1)) {
    }
}
static void retain_unlock(void) { __sync_lock_release(&g_retain_lock); }
#endif

/* ---- Small-span TLS recycle (#1436 tiny-doc fixed row) ---------
 * The global retain deliberately skips spans below ARENA_RETAIN_MIN
 * ("libc zone-reuses them"), but the per-document fixed-cost profile
 * names arena create+destroy as the heart of the ~185 ns/doc
 * tiny-doc row: even zone-recycled, malloc/free pay bookkeeping per
 * span, and the batched-GC shape pays first-touch faults when a
 * batch's spans outrun the zone cache. A per-thread LIFO of small
 * spans hands the previous document's span back with no allocator,
 * no global spinlock, and no cross-thread traffic. Same contract as
 * the global tier: NOT zeroed, callers memset what they need. */
#define ARENA_TLS_SPANS 8u
#define ARENA_TLS_MAX_BYTES (2u * 1024u * 1024u)

typedef struct {
    char* base[ARENA_TLS_SPANS];
    size_t size[ARENA_TLS_SPANS];
    size_t count;
    size_t bytes;
} ArenaSmallCache;

static LEPTRIS_THREAD_LOCAL ArenaSmallCache g_small;

/* LIFO take, EXACT size only: a reused span must report exactly the
 * requested capacity (same as a fresh malloc), or the fail-fast
 * bound silently widens for whoever sized a span to its budget —
 * the exhaustion specs pin that. Small spans are near-homogeneous
 * per workload (a 64 KB-doc stream recycles 64 KB spans), so
 * exact-match still catches the hot case. */
static char* small_take(size_t request, size_t* capacity) {
    if (request >= ARENA_RETAIN_MIN) return NULL;
    /* Stands down under custom allocators, same as the pool
     * recycler: hooked accounting must see every malloc/free pair. */
    if (leptris_custom_allocator_active()) return NULL;
    for (size_t i = g_small.count; i > 0; i--) {
        size_t idx = i - 1;
        if (g_small.size[idx] == request) {
            char* found = g_small.base[idx];
            *capacity = g_small.size[idx];
            g_small.bytes -= g_small.size[idx];
            g_small.base[idx] = g_small.base[g_small.count - 1];
            g_small.size[idx] = g_small.size[g_small.count - 1];
            g_small.count--;
            return found;
        }
    }
    return NULL;
}

/* Returns 1 when the span was parked, 0 when the cache is full or
 * the size belongs to the global tier — the caller frees then. */
static int small_give(char* base, size_t size) {
    if (size >= ARENA_RETAIN_MIN) return 0;
    if (leptris_custom_allocator_active()) return 0;
    if (g_small.count >= ARENA_TLS_SPANS ||
        g_small.bytes + size > ARENA_TLS_MAX_BYTES)
        return 0;
    g_small.base[g_small.count] = base;
    g_small.size[g_small.count] = size;
    g_small.count++;
    g_small.bytes += size;
    return 1;
}

/* Best fit: the smallest retained block that covers the request, so
 * oversized blocks aren't burned on small documents. Returns the
 * block and reports its true capacity; NULL when nothing fits. */
static char* retain_take(size_t request, size_t* capacity) {
    /* 'parked', never 'small' — MSVC's headers #define small (the
     * OLE `#define small char`) and turn the local into a type
     * token (C2059). */
    char* parked = small_take(request, capacity);
    if (parked) return parked;
    if (request < ARENA_RETAIN_MIN) return NULL;
    char* found = NULL;
    size_t found_size = 0;
    retain_lock();
    /* #1846: search ONLY the request's size class — mid blocks can't
     * serve large requests, and a large block must never serve a mid
     * request (it would burn the only large slots on input-copy
     * churn). */
    struct {
        char** base;
        size_t* size;
        size_t* count;
    } t;
    size_t tcount;
    if (request >= ARENA_RETAIN_LARGE_MIN) {
        t.base = g_retain_large.base;
        t.size = g_retain_large.size;
        tcount = g_retain_large.count;
    } else {
        t.base = g_retain_mid.base;
        t.size = g_retain_mid.size;
        tcount = g_retain_mid.count;
    }
    for (size_t i = 0; i < tcount; i++) {
        if (t.size[i] >= request &&
            (found_size == 0 || t.size[i] < found_size)) {
            found = t.base[i];
            found_size = t.size[i];
            t.base[i] = t.base[tcount - 1];
            t.size[i] = t.size[tcount - 1];
            tcount--;
            break;
        }
    }
    if (request >= ARENA_RETAIN_LARGE_MIN) {
        g_retain_large.count = tcount;
        g_retain_large.bytes -= found_size;
    } else {
        g_retain_mid.count = tcount;
        g_retain_mid.bytes -= found_size;
    }
    retain_unlock();
    if (found) *capacity = found_size;
    return found;
}

static void retain_give(char* base, size_t size) {
#ifdef ARENA_RETAIN_ASAN
    /* ASAN: retained blocks have no TLS destructor and read as
     * LSan leaks — free for real (the recycle win is measurable
     * only in repeated-parse loops, which ASAN runs don't gate). */
    (void)size;
    leptris_free_hook(base);
    return;
#endif
    if (size < ARENA_RETAIN_MIN) {
        if (!small_give(base, size))
            leptris_free_hook(base);
        return;
    }
    retain_lock();
    const int large_class = size >= ARENA_RETAIN_LARGE_MIN;
    size_t count = large_class ? g_retain_large.count : g_retain_mid.count;
    if (count >= ARENA_RETAIN_MAX_BLOCKS ||
        retain_bytes_total() + size > ARENA_RETAIN_MAX_BYTES) {
        retain_unlock();
        leptris_free_hook(base);
        return;
    }
    if (large_class) {
        g_retain_large.base[count] = base;
        g_retain_large.size[count] = size;
        g_retain_large.count = count + 1;
        g_retain_large.bytes += size;
    } else {
        g_retain_mid.base[count] = base;
        g_retain_mid.size[count] = size;
        g_retain_mid.count = count + 1;
        g_retain_mid.bytes += size;
    }
    retain_unlock();
}

/* Host-invoked drain (leptris_thread_cleanup): the retained
 * blocks are process-lifetime parking by design; release them so
 * embedded hosts and leak checkers see a clean exit. */
void leptris_arena_retain_drain(void) {
    /* The small tier drains the CALLING thread's cache — the shape
     * leptris_thread_cleanup serves. Other threads' cached spans
     * stay reachable through their TLS slots and die with the
     * process (the same still-reachable parking as the global
     * tier). */
    for (size_t i = 0; i < g_small.count; i++)
        leptris_free_hook(g_small.base[i]);
    g_small.count = 0;
    g_small.bytes = 0;

    retain_lock();
    for (size_t i = 0; i < g_retain_mid.count; i++)
        leptris_free_hook(g_retain_mid.base[i]);
    g_retain_mid.count = 0;
    g_retain_mid.bytes = 0;
    for (size_t i = 0; i < g_retain_large.count; i++)
        leptris_free_hook(g_retain_large.base[i]);
    g_retain_large.count = 0;
    g_retain_large.bytes = 0;
    retain_unlock();
}

char* leptris_arena_buffer_alloc(size_t size) {
    size_t capacity = size;
    char* base = retain_take(size, &capacity);
    return base ? base : (char*)leptris_alloc_hook(size);
}

/* ---- Direct-span transport (#1947) ----------------------------------- */

static size_t g_span_direct_taken;
static size_t g_span_direct_released;

int leptris_arena_has_direct_spans(void) {
#if defined(LEPTRIS_HAS_DIRECT_SPANS)
    return 1;
#else
    return 0;
#endif
}

void leptris_arena_span_stats(size_t* direct_taken, size_t* direct_released) {
    if (direct_taken) *direct_taken = g_span_direct_taken;
    if (direct_released) *direct_released = g_span_direct_released;
}

#if defined(_WIN32)

static int span_direct_acquire(char** out, size_t size) {
    void* p = VirtualAlloc(NULL, size, MEM_RESERVE | MEM_COMMIT,
                           PAGE_READWRITE);
    if (!p) return 0;
    *out = (char*)p;
    return 1;
}

static void span_direct_release(char* base, size_t size) {
    (void)size;
    VirtualFree(base, 0, MEM_RELEASE);
}

#elif defined(LEPTRIS_HAS_DIRECT_SPANS)

static int span_direct_acquire(char** out, size_t size) {
    void* p = mmap(NULL, size, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) return 0;
    *out = (char*)p;
    return 1;
}

static void span_direct_release(char* base, size_t size) {
    munmap(base, size);
}

#else

static int span_direct_acquire(char** out, size_t size) {
    (void)out; (void)size;
    return 0;
}

static void span_direct_release(char* base, size_t size) {
    (void)base; (void)size;
}

#endif

void leptris_arena_buffer_release(void* p, size_t size) {
    if (!p) return;
    retain_give((char*)p, size);
}

#if defined(__linux__) && defined(MADV_HUGEPAGE)
/* TODO.remaining/07: hint transparent huge pages for the parse
 * arena. The contiguous arena is the parser's dominant memory
 * region; on Linux servers with THP set to madvise, one madvise
 * call converts its TLB footprint to 2 MB pages. A hint only —
 * failure is ignored (unprivileged systems without THP just keep
 * 4 KB pages). Measured unmeasurable on macOS (no equivalent), so
 * this is Linux-only. */
static void arena_advise_hugepages(const void* base, size_t len) {
    if (len < (2u << 20)) return;           /* below a huge page */
    const uintptr_t page = 4096;
    uintptr_t start = ((uintptr_t)base + page - 1) & ~(page - 1);
    uintptr_t end = ((uintptr_t)base + len) & ~(page - 1);
    if (end <= start) return;
    (void)madvise((void*)start, end - start, MADV_HUGEPAGE);
}
#else
static void arena_advise_hugepages(const void* base, size_t len) {
    (void)base; (void)len;
}
#endif

LeptrisArena* leptris_arena_create(size_t size) {
    /* Lane-18 round 17b: the header is carved from the head of the
     * span itself — one system allocation per arena instead of two.
     * The CALLER's budget is preserved exactly: want = size + hdr is
     * taken from the retain pool / libc, and the bump region still
     * offers the full requested `size`. */
    const size_t hdr = (sizeof(LeptrisArena) + ARENA_ALIGNMENT - 1) &
                       ~(ARENA_ALIGNMENT - 1);
    if (size == 0 || size > (size_t)-1 - ARENA_ALIGNMENT - hdr)
        return NULL;
    size_t want = size + hdr;
    size_t capacity = want;
    char* span = NULL;
    int direct = 0;
#ifndef ARENA_RETAIN_ASAN
    /* #1947: above the retain budget the span can never be parked, so
     * the libc lane only adds magazine residency. Take it direct. */
    if (want > ARENA_RETAIN_MAX_BYTES && !leptris_custom_allocator_active() &&
        span_direct_acquire(&span, want)) {
        capacity = want;
        direct = 1;
        retain_lock();
        g_span_direct_taken++;
        retain_unlock();
        arena_advise_hugepages(span, capacity);
    }
#endif
    if (!span) {
        span = retain_take(want, &capacity);
        if (!span) {
            span = (char*)leptris_alloc_hook(want);
            capacity = want;
            if (!span) return NULL;
            arena_advise_hugepages(span, capacity);
        }
    }
    LeptrisArena* arena = (LeptrisArena*)span;
    arena->span = span;
    arena->base = span + hdr;
    /* For a reused block this is the BLOCK capacity minus the
     * header, so the fail-fast bound [base, base + size) and
     * remaining() stay exact; it is >= the requested size. */
    arena->size = capacity - hdr;
    arena->used = 0;
    arena->failed = 0;
    arena->direct = direct;
    return arena;
}

void leptris_arena_reset(LeptrisArena* arena) {
    if (!arena) return;
    arena->used = 0;
    arena->failed = 0;
}

void leptris_arena_destroy(LeptrisArena* arena) {
    if (!arena) return;
    /* The header lives inside the span — releasing the block
     * releases everything. */
    const size_t hdr = (sizeof(LeptrisArena) + ARENA_ALIGNMENT - 1) &
                       ~(ARENA_ALIGNMENT - 1);
    if (arena->direct) {
        span_direct_release(arena->span, arena->size + hdr);
        retain_lock();
        g_span_direct_released++;
        retain_unlock();
        return;
    }
    retain_give(arena->span, arena->size + hdr);
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
