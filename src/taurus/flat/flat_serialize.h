/* flat/flat_serialize.h — FlatDoc-direct XML serialization (TODO 145 Phase 2).
 *
 * Serialize a FlatDoc to an XML string without triggering promote.
 * The output matches taurus_document_serialize for documents that
 * haven't been mutated.
 *
 * The flat path skips:
 *   - Pool allocation for each element
 *   - Compact-pointer encoding
 *   - The promote walk
 *
 * For read-only workloads (parse + serialize), this is the entire
 * savings vs the compact path.
 */
#ifndef TAURUS_FLAT_FLAT_SERIALIZE_H
#define TAURUS_FLAT_FLAT_SERIALIZE_H

#include "flat_doc.h"
#include "../taurus_internal.h"

/* Serialize the document's FlatDoc to an XML string. Skips promote.
 *
 * Options mirror TaurusSerializeOptions:
 *   - xml_declaration: include `<?xml ...?>` if true
 *   - indent: number of spaces per nesting level (0 = no indent)
 *   - encoding: encoding name to write into xml_declaration
 *
 * Returns NULL if doc has no flat_doc (caller should fall back to
 * the compact serialize path). Otherwise returns a malloc'd string
 * that the caller must free via taurus_free_string. */
char* flat_serialize_document(struct taurus_document* doc,
                               int xml_declaration,
                               int indent,
                               const char* encoding);

/* Same for a subtree rooted at the given FlatNode index. */
char* flat_serialize_subtree(struct taurus_document* doc,
                              uint32_t root_flat_idx,
                              int indent);

#endif /* TAURUS_FLAT_FLAT_SERIALIZE_H */
