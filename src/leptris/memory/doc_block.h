/* lib/src/memory/doc_block.h — the node-layout fork's document
 * block (TODO.max-perf/node-layout-fork, slice 1).
 *
 * Two contiguous, geometrically-grown regions per document — a
 * dense node array and a string arena. Every reference to either
 * is a byte OFFSET within its region, so growth (realloc) never
 * invalidates an edge: callers hold offsets, not pointers, and
 * refetch region bases after any allocation.
 *
 * Differences vs memory/pool.c (which stays for mutation paths):
 * the pool chains fixed 32KB pages, scattering one tree across
 * pages; each parse node pays compact-edge encoding and detached
 * nodes pay root-doc registration. Here the tree is contiguous
 * (walk locality), edges are plain offsets (no cp16 overflow
 * table), and the owning document is implied by the region base.
 */
#ifndef LEPTRIS_MEMORY_DOC_BLOCK_H
#define LEPTRIS_MEMORY_DOC_BLOCK_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct LeptrisDocBlock LeptrisDocBlock;

/* Create with an initial byte capacity per region (grown
 * geometrically on demand; 0 = lazy). NULL on failure. */
LeptrisDocBlock* leptris_doc_block_create(size_t initial_capacity);

void leptris_doc_block_free(LeptrisDocBlock* b);

/* Node region: bump-allocate sz bytes, 8-byte aligned. Returns
 * the node's offset in the region (stable across growth) — never
 * a pointer; resolve via leptris_doc_block_node. (size_t)-1 on
 * failure. Growth may realloc: refetch any held pointers. */
size_t leptris_doc_block_node_alloc(LeptrisDocBlock* b, size_t sz);

/* String region: append len bytes plus a NUL. Returns the
 * string's offset in ITS region (stable across growth) or
 * (size_t)-1 on failure. */
size_t leptris_doc_block_str(LeptrisDocBlock* b, const char* s,
                             size_t len);

/* Resolve an offset (region base + off, one add). */
void* leptris_doc_block_node(const LeptrisDocBlock* b, size_t off);

void* leptris_doc_block_nodes_base(const LeptrisDocBlock* b);
size_t leptris_doc_block_nodes_used(const LeptrisDocBlock* b);
const char* leptris_doc_block_str_at(const LeptrisDocBlock* b,
                                     size_t off);
size_t leptris_doc_block_bytes_used(const LeptrisDocBlock* b);
size_t leptris_doc_block_capacity(const LeptrisDocBlock* b);

#ifdef __cplusplus
}
#endif

#endif /* LEPTRIS_MEMORY_DOC_BLOCK_H */
