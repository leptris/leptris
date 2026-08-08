/* flat/direct_parse.h — Single-pass parse into TaurusElement tree
 * (TODO 147 Phase A).
 *
 * Parses XML directly into TaurusElement records — no FlatDoc
 * intermediate, no promote pass. Applies pugixml's core technique:
 * one pass, bulk-allocated element block, zero-copy names via
 * in-place NUL termination.
 *
 * Used by taurus_parse for plain-XML inputs (no DTD, no entities,
 * default depth). Complex inputs fall back to flat_parse + promote.
 */
#ifndef TAURUS_FLAT_DIRECT_PARSE_H
#define TAURUS_FLAT_DIRECT_PARSE_H

#include "../taurus_internal.h"

/* Parse XML into a TaurusDocument with the tree pre-built.
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
struct taurus_document* direct_parse(const char* xml, size_t len);

#endif /* TAURUS_FLAT_DIRECT_PARSE_H */
