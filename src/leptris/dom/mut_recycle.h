#ifndef LEPTRIS_DOM_MUT_RECYCLE_H
#define LEPTRIS_DOM_MUT_RECYCLE_H

/* Per-thread free-lists for mutation blocks (elem / name / attr).
 *
 * A doc-cycle mallocs these blocks, and leptris_document_free walks
 * them back out; the next doc re-mallocs the exact same sizes. The
 * blocks (45KB / 4KB / 5KB) sit across the allocator's small-cache
 * line, so every free pays a madvise and every fresh block re-faults
 * its pages on the carver memsets. Parking freed blocks on the
 * owning thread keeps them hot between doc-cycles.
 *
 * Bounded: each kind refuses beyond its byte cap (the caller then
 * frees normally). POSIX threads drain via a pthread_key destructor
 * at thread exit; every thread can also drop its cache synchronously
 * through the public leptris_explicit_cleanup().
 */

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

struct leptris_mut_elem_block;
struct leptris_mut_name_block;
struct leptris_mut_attr_block;

/* Parks blk on this thread's list. Returns 1 when parked (the caller
 * must NOT free it) or 0 when the cap rejected it (caller frees). */
int leptris_mut_recycle_push_elem(struct leptris_mut_elem_block* blk);
int leptris_mut_recycle_push_name(struct leptris_mut_name_block* blk);
int leptris_mut_recycle_push_attr(struct leptris_mut_attr_block* blk);

/* Takes a parked block, or NULL when the list is empty. */
struct leptris_mut_elem_block* leptris_mut_recycle_pop_elem(void);
struct leptris_mut_name_block* leptris_mut_recycle_pop_name(void);
struct leptris_mut_attr_block* leptris_mut_recycle_pop_attr(void);

/* Frees every block parked on THIS thread. */
void leptris_mut_recycle_drain(void);

/* Bytes currently parked on this thread (all kinds). */
size_t leptris_mut_recycle_cached_bytes(void);

#ifdef __cplusplus
}
#endif

#endif
