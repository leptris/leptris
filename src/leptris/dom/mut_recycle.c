/* lib/src/dom/mut_recycle.c - per-thread recycle of mutation blocks
 * Copyright (c) 2024, Ribose Inc.
 *
 * See mut_recycle.h for the design contract.
 */

#include "mut_recycle.h"
#include "../leptris_internal.h"
#include "../common/port.h"
#include "element.h"
#include <stdlib.h>

/* Steady-state caps: enough blocks to serve back-to-back doc-cycles
 * without re-entering malloc, small enough that a thread which parks
 * and never builds again holds bounded residue. */
#define RECYCLE_CAP_ELEM_BYTES (8u * 1024u * 1024u)
#define RECYCLE_CAP_NAME_BYTES (4u * 1024u * 1024u)
#define RECYCLE_CAP_ATTR_BYTES (4u * 1024u * 1024u)

/* Thread-exit wiring: without it, a thread that parks blocks and
 * exits loses its TLS heads — the parked bytes become unreachable
 * and LSan (Linux CI) reports them. A pthread_key destructor runs
 * before TLS teardown and drains. */
#if !defined(_WIN32)
#  include <pthread.h>
static pthread_key_t g_recycle_exit_key;
static int g_recycle_exit_key_ok;
static pthread_once_t g_recycle_exit_once = PTHREAD_ONCE_INIT;

static void recycle_thread_exit(void* unused) {
    (void)unused;
    leptris_mut_recycle_drain();
}

static void recycle_exit_key_create(void) {
    g_recycle_exit_key_ok =
        (pthread_key_create(&g_recycle_exit_key, recycle_thread_exit) == 0);
}

static void recycle_register_thread_exit(void) {
    static LEPTRIS_THREAD_LOCAL int tls_registered;
    if (tls_registered) return;
    tls_registered = 1;
    pthread_once(&g_recycle_exit_once, recycle_exit_key_create);
    if (g_recycle_exit_key_ok)
        pthread_setspecific(g_recycle_exit_key, (const void*)1);
}
#else
/* MSVC __declspec(thread) has no destructor hook short of FlsAlloc;
 * residue is bounded by the caps and any later
 * leptris_explicit_cleanup() on the thread drains it. Windows CI
 * does not run LSan. */
static void recycle_register_thread_exit(void) {}
#endif

/* One instantiation per block kind. BLOCK_BYTES is the exact
 * expression the matching carve malloc'd, so the byte accounting
 * matches the allocator's view. */
#define DEFINE_MUT_RECYCLE(SUFFIX, BLOCK_TYPE, BLOCK_BYTES, CAP_BYTES)      \
    static LEPTRIS_THREAD_LOCAL struct BLOCK_TYPE* tls_recycle_##SUFFIX;    \
    static LEPTRIS_THREAD_LOCAL size_t tls_recycle_##SUFFIX##_bytes;        \
    int leptris_mut_recycle_push_##SUFFIX(struct BLOCK_TYPE* blk) {         \
        recycle_register_thread_exit();                                     \
        if (tls_recycle_##SUFFIX##_bytes >= (CAP_BYTES)) return 0;          \
        blk->next = tls_recycle_##SUFFIX;                                   \
        tls_recycle_##SUFFIX = blk;                                         \
        tls_recycle_##SUFFIX##_bytes += (BLOCK_BYTES);                      \
        return 1;                                                           \
    }                                                                       \
    struct BLOCK_TYPE* leptris_mut_recycle_pop_##SUFFIX(void) {             \
        struct BLOCK_TYPE* blk = tls_recycle_##SUFFIX;                      \
        if (!blk) return NULL;                                              \
        tls_recycle_##SUFFIX = blk->next;                                   \
        tls_recycle_##SUFFIX##_bytes -= (BLOCK_BYTES);                      \
        return blk;                                                         \
    }                                                                       \
    static void recycle_drain_##SUFFIX(void) {                              \
        while (tls_recycle_##SUFFIX) {                                      \
            struct BLOCK_TYPE* next = tls_recycle_##SUFFIX->next;           \
            free(tls_recycle_##SUFFIX);                                     \
            tls_recycle_##SUFFIX = next;                                    \
        }                                                                   \
        tls_recycle_##SUFFIX##_bytes = 0;                                   \
    }

DEFINE_MUT_RECYCLE(elem, leptris_mut_elem_block,
                   (sizeof(struct leptris_mut_elem_block) +
                    (size_t)MUT_ELEM_BLOCK_COUNT *
                        sizeof(struct leptris_element)),
                   RECYCLE_CAP_ELEM_BYTES)

DEFINE_MUT_RECYCLE(name, leptris_mut_name_block,
                   (sizeof(struct leptris_mut_name_block) +
                    MUT_NAME_BLOCK_BYTES),
                   RECYCLE_CAP_NAME_BYTES)

DEFINE_MUT_RECYCLE(attr, leptris_mut_attr_block,
                   (sizeof(struct leptris_mut_attr_block) +
                    (size_t)MUT_ATTR_BLOCK_COUNT *
                        sizeof(struct leptris_attribute)),
                   RECYCLE_CAP_ATTR_BYTES)

void leptris_mut_recycle_drain(void) {
    recycle_drain_elem();
    recycle_drain_name();
    recycle_drain_attr();
}

size_t leptris_mut_recycle_cached_bytes(void) {
    return tls_recycle_elem_bytes + tls_recycle_name_bytes +
           tls_recycle_attr_bytes;
}
