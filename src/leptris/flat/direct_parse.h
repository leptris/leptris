/* flat/direct_parse.h — Single-pass parse into LeptrisElement tree
 * (TODO 147 Phase A).
 *
 * Parses XML directly into LeptrisElement records — no FlatDoc
 * intermediate, no promote pass. Applies pugixml's core technique:
 * one pass, bulk-allocated element block, zero-copy names via
 * in-place NUL termination.
 *
 * Used by leptris_parse for plain-XML inputs (no DTD, no entities,
 * default depth). Complex inputs fall back to flat_parse + promote.
 */
#ifndef LEPTRIS_FLAT_DIRECT_PARSE_H
#define LEPTRIS_FLAT_DIRECT_PARSE_H

#include "../leptris_internal.h"

/* Parse XML into a LeptrisDocument with the tree pre-built.
 *
 * Returns NULL on failure. On success, the document has:
 *   - doc->new_dom_root set to the root element
 *   - doc->flat_doc == NULL (no FlatDoc intermediate)
 *   - doc->flat_promoted == 1 (tree is ready)
 *   - doc->pool allocated for attrs/non-element nodes
 *
 * The element block is a single contiguous malloc (bulk-allocated
 * upfront based on input size heuristic). Element names point
 * directly into the writable XML buffer copy (zero-copy).
 */
struct leptris_document* direct_parse(const char* xml, size_t len);

/* In-place variant: parse a caller-owned WRITABLE buffer without
 * copying. The buffer is modified in-place (NUL-terminated at
 * name/value boundaries). The document does NOT free the buffer —
 * caller must ensure it outlives the document.
 *
 * The buffer must have at least len+1 writable bytes (for the NUL
 * at buf[len]).
 */
struct leptris_document* direct_parse_inplace(char* buf, size_t len);

#endif /* LEPTRIS_FLAT_DIRECT_PARSE_H */
