/* flat/flat_promote.h — FlatDoc → TaurusDocument promote (TODO 139 Phase C).
 *
 * Converts a FlatDoc (the fast parse representation from Phase B)
 * into a TaurusDocument (the compact-pointer tree the rest of the
 * library expects). Single linear walk over the flat node array;
 * for each FlatNode, allocate the corresponding pool-owned node
 * and wire it into the tree.
 *
 * The promote pass is the bridge that lets the flat parser be the
 * fast path while keeping the existing compact-pointer tree as the
 * query/mutation target. All existing API calls work unchanged
 * after promote — they see a fully-built TaurusDocument.
 *
 * Cost: ~1.5 µs per element (pool alloc + compact-pointer encode).
 * This is the same cost the legacy parser pays, but it's paid
 * lazily — only when the compact-pointer tree is actually needed.
 * Parse-only workloads (SAX, count, validate) can skip promote
 * entirely and operate on the FlatDoc directly (TODO 139 Phase E).
 *
 * Memory: the FlatDoc borrows the XML buffer; the TaurusDocument
 * needs its own writable copy (the legacy parser mutates the
 * buffer for in-place NUL termination, and consumers expect
 * doc->xml_buffer to be stable). The promote pass copies the
 * buffer once via flat_doc_dup_xml + document-scoped ownership
 * transfer.
 */
#ifndef TAURUS_FLAT_FLAT_PROMOTE_H
#define TAURUS_FLAT_FLAT_PROMOTE_H

#include "flat_doc.h"
#include "../taurus_internal.h"

/* Convert a FlatDoc to a fully-built TaurusDocument.
 *
 * On success: returns a new TaurusDocument. The FlatDoc is freed
 * as part of the promote (its arrays and borrowed XML buffer are
 * no longer needed — the TaurusDocument owns its own copy).
 *
 * On failure: returns NULL. The FlatDoc is also freed; the caller
 * should fall back to the legacy parser.
 *
 * Failure modes:
 *   - Pool allocation failure (out of memory)
 *   - Element creation failure (compact-pointer overflow table
 *     exhausted — should not happen on well-formed input)
 */
struct taurus_document* flat_promote(FlatDoc* flat);

#endif /* TAURUS_FLAT_FLAT_PROMOTE_H */
