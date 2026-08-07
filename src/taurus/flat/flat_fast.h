/* flat/flat_fast.h — Flat-mode query fast paths (TODO 139 Phase E).
 *
 * Internal helpers that answer simple queries directly from a FlatDoc,
 * bypassing the promote pass. Used by:
 *   - The benchmark suite to demonstrate the lazy-promote win.
 *   - Future XPath VM optimizations (count(//name) specialization).
 *
 * Public API callers never see these helpers; they continue to call
 * taurus_document_root + element walks, which trigger promote if
 * needed. The fast paths are an internal optimization.
 */
#ifndef TAURUS_FLAT_FLAT_FAST_H
#define TAURUS_FLAT_FLAT_FAST_H

#include "flat_doc.h"
#include "../taurus_internal.h"

/* Count elements with the given name across the entire document.
 *
 * Walks the FlatDoc node array directly, comparing each element's
 * name against `name`. Returns 0 if doc has no flat_doc (already
 * promoted) or if name is NULL.
 *
 * O(N) where N = flat_doc->node_count. Faster than the
 * promote-then-walk-element-index path because it skips pool
 * allocation + compact-pointer encoding. */
size_t flat_fast_count_elements_named(struct taurus_document* doc,
                                       const char* name);

/* Count all elements in the document (any name).
 *
 * Equivalent to flat_fast_count_elements_named(doc, NULL) but
 * skips the name comparison. */
size_t flat_fast_count_elements_all(struct taurus_document* doc);

/* Get the root element's name without promoting. The returned
 * pointer is valid until the document is freed OR promoted. Caller
 * must NOT free it.
 *
 * Returns NULL if doc has no flat_doc, or if the flat doc has no
 * root element. */
const char* flat_fast_root_name(struct taurus_document* doc);

#endif /* TAURUS_FLAT_FLAT_FAST_H */
