/* flat/flat_parser.h — Flat XML parser (TODO 139 Phase B).
 *
 * Single-pass XML parser that builds a FlatDoc directly from the
 * input buffer. Reuses the tokenizer conventions from parser_new.c
 * (ASCII tight loops, memchr for quote scanning) but produces
 * FlatNode/FlatAttr records instead of pool-allocated TaurusElement
 * nodes — that's where the speedup comes from.
 *
 * Scope: the flat parser handles the syntactic surface of XML:
 *   - XML declaration `<?xml version="1.0" ...?>`
 *   - Elements (open, close, self-closing)
 *   - Attributes (single and double quoted)
 *   - Text content (raw byte range; entity expansion deferred)
 *   - Comments
 *   - CDATA sections
 *   - Processing instructions
 *   - DOCTYPE detection (skipped, not validated)
 *
 * Out of scope (handled by the promote pass or the existing parser):
 *   - DTD validation, entity declarations, parameter entities
 *   - Encoding conversion (input assumed UTF-8)
 *   - Namespace resolution (deferred to promote)
 *   - Entity expansion in attribute values and text (deferred to
 *     promote so the FlatDoc remains zero-copy)
 *
 * On hard parse failure the function returns NULL and the caller
 * falls back to the legacy parser. The flat parser is intentionally
 * strict: it accepts a subset of XML, but it accepts that subset
 * correctly and very fast.
 *
 * Memory model: the returned FlatDoc BORROWS the input buffer.
 * Callers that need the FlatDoc to outlive the buffer must call
 * flat_doc_dup_xml().
 */
#ifndef TAURUS_FLAT_FLAT_PARSER_H
#define TAURUS_FLAT_FLAT_PARSER_H

#include "flat_doc.h"

/* Parse XML into a FlatDoc.
 *
 * Returns NULL on hard parse failure (malformed XML, allocation
 * failure, depth limit exceeded). On success the FlatDoc borrows
 * the input buffer — see flat_doc_dup_xml to take ownership. */
FlatDoc* flat_parse(const char* xml, size_t len);

#endif /* TAURUS_FLAT_FLAT_PARSER_H */
