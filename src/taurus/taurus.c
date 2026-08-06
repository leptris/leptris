/* taurus.c - Taurus public API implementation
 * Copyright (c) 2024, Ribose Inc.
 *
 * Pure C XML parser and XPath evaluator - Public API.
 */

#include "../include/taurus.h"
#include "taurus_internal.h"
#include "xpath/parser.h"
#include "xpath/evaluator.h"
#include "xpath/xpath_variables.h"
#include "dom/element.h"
#include "dom/node.h"
#include "dom/text.h"
#include "dom/comment.h"
#include "dom/cdata.h"
#include "dom/pi.h"
#include "dom/doctype.h"
#include "encoding/utf16.h"
#include "dtd/model.h"
#include "common/entities.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

/* Thread-local globals defined in core.c — extern so taurus_parse can
 * read the strict-mode default at document creation time. */
extern __thread int g_taurus_strict_mode;


/* ============================================================================
 * Utility Macros
 * ============================================================================ */

/* Macro for appending strings to a dynamic buffer */
#define APPEND_STRING(str, len) do { \
    if (!buffer) goto cleanup; \
    while (*size + (len) + 1 > *capacity) { \
        size_t new_cap = *capacity * 2; \
        char* new_buf = (char*)realloc(*buffer, new_cap); \
        if (!new_buf) { \
            free(*buffer); \
            *buffer = NULL; \
            goto cleanup; \
        } \
        *buffer = new_buf; \
        *capacity = new_cap; \
    } \
    memcpy(*buffer + *size, str, len); \
    *size += len; \
    (*buffer)[*size] = '\0'; \
} while(0)

/* ============================================================================
 * Version Constants
 * ============================================================================ */


/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

/* Forward declarations for new parser/serializer */
typedef struct Parser Parser;

extern Parser* parser_create(const char* xml, size_t len, TaurusMemoryPool* pool);
extern Parser* parser_create_writable(char* xml, size_t len, TaurusMemoryPool* pool);
extern void parser_free(Parser* p);
extern TaurusElement parser_parse_document(Parser* p);
extern int parser_has_error(Parser* p);
extern const char* parser_get_xml_version(Parser* p);
extern const char* parser_get_encoding(Parser* p);
extern int parser_get_standalone(Parser* p);
extern int parser_had_declaration(Parser* p);
extern int parser_has_bom(Parser* p);
extern TaurusDoctypeNode* parser_get_doctype(Parser* p);
extern TaurusDoctypeNode* parser_transfer_doctype(Parser* p);
extern struct taurus_processing_instruction* parser_get_pi_list(Parser* p);
extern void taurus_element_free(TaurusElement elem);
extern void taurus_doctype_free(TaurusDoctypeNode* doctype);

/* ============================================================================
 * Internal Parse Function Implementation
 * ============================================================================ */

/**
 * Parse XML string into document (internal implementation)
 *
 * PERFORMANCE: Uses in-place parsing to avoid buffer copy.
 * The buffer is stored in the document and freed when document is freed.
 */
TAURUS_API struct taurus_document* taurus_parse(const char* xml, size_t len) {
    if (!xml || len == 0) return NULL;

    /* PERFORMANCE: Use heap allocation for XML buffer
     * The buffer must live as long as the document because StringViews point into it.
     * For stack-allocated buffers, the memory becomes invalid when the function returns.
     * For heap-allocated buffers, we track ownership with xml_buffer_needs_free flag. */
    char* xml_copy = TAURUS_ALLOC_N(char, len);
    if (!xml_copy) return NULL;
    memcpy(xml_copy, xml, len);

    /* Optimize pool page size based on file size
     * Small files (<4KB): Use 4KB pages to avoid memory waste
     * Medium files (<64KB): Use 16KB pages for balanced performance
     * Large files (>=64KB): Use 32KB pages (default) for maximum throughput
     */
    size_t page_size;
    if (len < 4096) {
        page_size = 4096;   /* 4KB for small files */
    } else if (len < 65536) {
        page_size = 16384;  /* 16KB for medium files */
    } else {
        page_size = 32768;  /* 32KB for large files */
    }

    /* Create memory pool with optimized page size */
    TaurusMemoryPool* pool = taurus_pool_create_with_page_size(page_size);
    if (!pool) {
        TAURUS_FREE(xml_copy);
        return NULL;
    }

    /* Enable string deduplication for files >= 256B (PERFORMANCE optimization)
     * This significantly improves performance for documents with repeated strings
     * by using a hash table to intern strings, avoiding duplicate allocations */
    if (len >= 256) {
        pool->string_cache = taurus_hash_table_create(pool, 128);
        /* Note: If hash table creation fails, string_cache will be NULL
         * and code will fall back to non-interned allocation (safe degradation) */
    }

    /* CRITICAL: Create document structure FIRST before parsing
     * This allows overflow table entries to track which document owns them
     * for proper per-document cleanup when multiple documents are active */
    struct taurus_document* doc = TAURUS_ALLOC(struct taurus_document);
    if (!doc) {
        taurus_pool_destroy(pool);
        TAURUS_FREE(xml_copy);
        return NULL;
    }

    /* Initialize all fields to prevent stale data from recycled memory */
    memset(doc, 0, sizeof(struct taurus_document));
    /* TODO 38: inherit strict mode from the thread-default at creation. */
    doc->strict_mode = g_taurus_strict_mode;

    /* Transfer pool ownership to document */
    doc->pool = pool;
    doc->page_base = taurus_pool_get_base(pool);  /* Set page_base for compact pointer decoding */
    doc->ref_count = 1;

    /* Store the copied XML buffer - StringViews point into this
     * IMPORTANT: The buffer is heap-allocated and must be freed when document is destroyed */
    doc->xml_buffer = xml_copy;
    doc->xml_buffer_len = len;
    doc->xml_buffer_needs_free = 1;  /* Always heap-allocated, always needs free */

    /* CRITICAL: Set this document as current for overflow tracking BEFORE parsing
     * This ensures all overflow entries created during parsing are associated
     * with this document for proper per-document cleanup */
    extern void taurus_compact_set_current_document(struct taurus_document* doc);
    taurus_compact_set_current_document(doc);

    /* PERFORMANCE: Use writable parser since we own the buffer
     * This allows in-place modifications for null termination */
    Parser* p = parser_create_writable(xml_copy, len, pool);
    if (!p) {
        taurus_compact_set_current_document(NULL);  /* Clear current document */
        taurus_pool_destroy(pool);
        TAURUS_FREE(xml_copy);
        TAURUS_FREE(doc);
        return NULL;
    }

    TaurusElement root = parser_parse_document(p);

    if (!root || parser_has_error(p)) {
        taurus_compact_set_current_document(NULL);  /* Clear current document */
        parser_free(p);
        taurus_pool_destroy(pool);
        TAURUS_FREE(xml_copy);
        TAURUS_FREE(doc);
        return NULL;
    }

    /* Populate document with parsed data */
    doc->root = NULL;  /* Old API - not used with new parser */
    doc->new_dom_root = (void*)root;  /* Store new DOM tree */
    doc->encoding = NULL;
    doc->pis = NULL;

    /* Transfer XML declaration info from parser using getters */
    const char* version = parser_get_xml_version(p);
    doc->xml_version = version ? taurus_strdup(version) : NULL;
    const char* encoding = parser_get_encoding(p);
    doc->encoding = encoding ? taurus_strdup(encoding) : NULL;
    doc->standalone = parser_get_standalone(p);
    doc->had_declaration = parser_had_declaration(p);
    doc->has_bom = parser_has_bom(p);

    /* Transfer DOCTYPE from parser (ownership transfer) */
    doc->doctype = (void*)parser_transfer_doctype(p);

    /* Transfer PI list from parser for C14N support */
    doc->pis = parser_get_pi_list(p);

    /* Initialize DTD to NULL (will be parsed if internal subset exists) */
    doc->dtd = NULL;

    /* Parse DTD internal subset if present */
    if (doc->doctype) {
        TaurusDoctypeNode* doctype = (TaurusDoctypeNode*)doc->doctype;
        const char* internal_subset = doctype->internal_subset;
        if (internal_subset && strlen(internal_subset) > 0) {
            doc->dtd = taurus_dtd_parse_internal_subset(internal_subset, strlen(internal_subset), doc->pool);
        }
    }

    /* Set document pointer on root element */
    root->document = doc;

    /* CRITICAL: Set document pointer on all elements in the tree
     * This is needed for cross-document copy operations to work correctly.
     * During parsing, child elements don't get their document pointer set
     * because the root element's document pointer is NULL during parsing.
     * We need to recursively set the document pointer on all descendants. */
    taurus_element_set_document_tree(root, doc);

    /* PERFORMANCE: Eagerly convert all StringViews to NULL-terminated strings
     * This eliminates lazy conversion overhead during queries and fixes the
     * catastrophic 33x slowdown on Read-Many benchmarks for large files.
     * We do this while the pool is still "hot" for better cache locality. */
    taurus_document_finalize_strings(doc);

    /* COW (Phase 2.1): Freeze the entire tree after parsing
     * This marks all nodes as frozen (immutable) for copy-on-write semantics */
    taurus_document_freeze_tree(doc);

    parser_free(p);

    /* CRITICAL: Clear current document AFTER all document operations are complete
     * This ensures any overflow entries created during tree operations are tracked correctly */
    taurus_compact_set_current_document(NULL);

    return doc;
}

/**
 * Parse XML string into document with in-place optimization (internal implementation)
 */
static struct taurus_document* taurus_parse_inplace(char* xml, size_t len) {
    if (!xml || len == 0) return NULL;

    /* Optimize pool page size based on file size */
    size_t page_size;
    if (len < 4096) {
        page_size = 4096;   /* 4KB for small files */
    } else if (len < 65536) {
        page_size = 16384;  /* 16KB for medium files */
    } else {
        page_size = 32768;  /* 32KB for large files */
    }

    /* Create memory pool with optimized page size */
    TaurusMemoryPool* pool = taurus_pool_create_with_page_size(page_size);
    if (!pool) return NULL;

    /* Enable deduplication for files >= 256B (PERFORMANCE: reduced from 1KB)
     * This improves performance for small documents with repeated strings */
    if (len >= 100000000) {  /* Disable for now - debugging memory corruption */
        pool->string_cache = taurus_hash_table_create(pool, 128);
        /* Note: If hash table creation fails, string_cache will be NULL
         * and code will fall back to non-interned allocation (safe degradation) */
    }

    /* CRITICAL: Create document structure FIRST before parsing
     * This allows overflow table entries to track which document owns them
     * for proper per-document cleanup when multiple documents are active */
    struct taurus_document* doc = TAURUS_ALLOC(struct taurus_document);
    if (!doc) {
        taurus_pool_destroy(pool);
        return NULL;
    }

    /* Initialize all fields */
    memset(doc, 0, sizeof(struct taurus_document));

    /* Transfer pool ownership to document */
    doc->pool = pool;
    doc->page_base = taurus_pool_get_base(pool);  /* Set page_base for compact pointer decoding */
    doc->ref_count = 1;
    doc->xml_buffer = xml;
    doc->xml_buffer_len = len;
    doc->xml_buffer_needs_free = 0;  /* User owns buffer for inplace parsing */

    /* CRITICAL: Set this document as current for overflow tracking BEFORE parsing
     * This ensures all overflow entries created during parsing are associated
     * with this document for proper per-document cleanup */
    extern void taurus_compact_set_current_document(struct taurus_document* doc);
    taurus_compact_set_current_document(doc);

    /* Use writable parser */
    Parser* p = parser_create_writable(xml, len, pool);
    if (!p) {
        taurus_compact_set_current_document(NULL);
        taurus_pool_destroy(pool);
        TAURUS_FREE(doc);
        return NULL;
    }

    TaurusElement root = parser_parse_document(p);

    if (!root || parser_has_error(p)) {
        taurus_compact_set_current_document(NULL);
        parser_free(p);
        taurus_pool_destroy(pool);
        TAURUS_FREE(doc);
        return NULL;
    }

    /* Clear current document after parsing */
    taurus_compact_set_current_document(NULL);

    /* Populate document */
    doc->root = NULL;  /* Old API - not used with new parser */
    doc->new_dom_root = (void*)root;
    doc->encoding = NULL;
    doc->pis = NULL;

    /* Transfer XML declaration info from parser using getters */
    const char* version = parser_get_xml_version(p);
    doc->xml_version = version ? taurus_strdup(version) : NULL;
    const char* encoding = parser_get_encoding(p);
    doc->encoding = encoding ? taurus_strdup(encoding) : NULL;
    doc->standalone = parser_get_standalone(p);
    doc->had_declaration = parser_had_declaration(p);
    doc->has_bom = parser_has_bom(p);

    /* Transfer DOCTYPE from parser (ownership transfer) */
    doc->doctype = (void*)parser_transfer_doctype(p);

    /* Transfer PI list from parser for C14N support */
    doc->pis = parser_get_pi_list(p);

    /* Initialize DTD to NULL (will be parsed if internal subset exists) */
    doc->dtd = NULL;

    /* Parse DTD internal subset if present */
    if (doc->doctype) {
        TaurusDoctypeNode* doctype = (TaurusDoctypeNode*)doc->doctype;
        const char* internal_subset = doctype->internal_subset;
        if (internal_subset && strlen(internal_subset) > 0) {
            doc->dtd = taurus_dtd_parse_internal_subset(internal_subset, strlen(internal_subset), doc->pool);
        }
    }

    /* Set document pointer on root element */
    root->document = doc;

    /* CRITICAL: Set document pointer on all elements in the tree
     * This is needed for cross-document copy operations to work correctly.
     * During parsing, child elements don't get their document pointer set
     * because the root element's document pointer is NULL during parsing.
     * We need to recursively set the document pointer on all descendants. */
    taurus_element_set_document_tree(root, doc);

    /* PERFORMANCE: Eagerly convert all StringViews to NULL-terminated strings
     * This eliminates lazy conversion overhead during queries and fixes the
     * catastrophic 33x slowdown on Read-Many benchmarks for large files.
     * We do this while the pool is still "hot" for better cache locality. */
    taurus_document_finalize_strings(doc);

    /* COW (Phase 2.1): Freeze the entire tree after parsing
     * This marks all nodes as frozen (immutable) for copy-on-write semantics */
    taurus_document_freeze_tree(doc);

    parser_free(p);

    /* CRITICAL: Clear current document AFTER all document operations are complete
     * This ensures any overflow entries created during tree operations are tracked correctly */
    taurus_compact_set_current_document(NULL);

    return doc;
}

/* ============================================================================
 * Version Information
 * ============================================================================ */

/* ============================================================================
 * Parse Options
 * ============================================================================ */

/**
 * Initialize parse options with defaults
 */
TAURUS_API void taurus_parse_options_init(taurus_parse_options* opts) {
    if (!opts) return;

    opts->strict = 1;              /* Strict mode by default */
    opts->preserve_whitespace = 0; /* Don't preserve whitespace by default */
    opts->track_positions = 0;     /* Don't track positions by default */
}

/* ============================================================================
 * Document Functions
 * ============================================================================ */

/**
 * Parse XML string into document (Public API wrapper)
 *
 * This function automatically detects and converts various encodings to UTF-8,
 * including: UTF-16 (LE/BE), EBCDIC, ISO-8859-*, EUC-JP, Shift-JIS, etc.
 */
TAURUS_API TaurusDocument taurus_parse_string(const char* xml, size_t length, TaurusStatus* status) {
    if (status) *status = TAURUS_OK;

    /* Try native UTF-16 detection first (works without iconv) */
    #include "encoding/utf16.h"

    const unsigned char* data = (const unsigned char*)xml;
    utf16_bom_t bom = utf16_detect_bom(data, length);

    if (bom == UTF16_BOM_LE || bom == UTF16_BOM_BE) {
        /* UTF-16 with BOM - convert to UTF-8 */
        utf16_encoding_t encoding = (bom == UTF16_BOM_LE) ? UTF16_LE : UTF16_BE;

        size_t utf8_size = utf16_to_utf8_size(data, length, encoding);
        char* utf8_buffer = (char*)malloc(utf8_size);
        if (!utf8_buffer) {
            if (status) *status = TAURUS_ERROR_MEMORY;
            return NULL;
        }

        size_t utf8_len = utf16_to_utf8(data, length, utf8_buffer, utf8_size, encoding);
        utf8_buffer[utf8_len] = '\0';

        struct taurus_document* doc = taurus_parse(utf8_buffer, utf8_len);

        /* taurus_parse() heap-copies its input into doc->xml_buffer and
         * parses in-place from there; the document's StringViews point
         * into that inner copy, NOT into utf8_buffer.  Free our
         * intermediate conversion buffer — its contents are already
         * preserved inside the document. */
        free(utf8_buffer);

        if (doc) {
            if (doc->encoding) {
                TAURUS_FREE(doc->encoding);
            }
            doc->encoding = taurus_strdup((encoding == UTF16_LE) ? "UTF-16LE" : "UTF-16BE");
        }

        if (!doc && status) {
            *status = TAURUS_ERROR_PARSE;
        }

        return doc;
    }

    /* Check for UTF-16 without BOM using heuristic detection */
    utf16_encoding_t detected = utf16_detect_encoding(data, length);
    if (detected == UTF16_LE || detected == UTF16_BE) {
        /* UTF-16 without BOM - convert to UTF-8 */
        size_t utf8_size = utf16_to_utf8_size(data, length, detected);
        char* utf8_buffer = (char*)malloc(utf8_size);
        if (!utf8_buffer) {
            if (status) *status = TAURUS_ERROR_MEMORY;
            return NULL;
        }

        size_t utf8_len = utf16_to_utf8(data, length, utf8_buffer, utf8_size, detected);
        utf8_buffer[utf8_len] = '\0';

        struct taurus_document* doc = taurus_parse(utf8_buffer, utf8_len);

        /* See UTF-16-with-BOM comment above: taurus_parse already copied
         * the buffer; ours is now redundant. */
        free(utf8_buffer);

        if (doc) {
            if (doc->encoding) {
                TAURUS_FREE(doc->encoding);
            }
            doc->encoding = taurus_strdup((detected == UTF16_LE) ? "UTF-16LE" : "UTF-16BE");
        }

        if (!doc && status) {
            *status = TAURUS_ERROR_PARSE;
        }

        return doc;
    }

#ifdef TAURUS_HAS_ICONV
    /* Include encoding support for other encodings */
    #include "encoding/encoding.h"

    /* Auto-detect and convert to UTF-8 */
    size_t utf8_len = 0;
    char* detected_encoding = NULL;
    char* utf8_xml = taurus_encoding_auto_convert(xml, length, &utf8_len, &detected_encoding);

    if (!utf8_xml) {
        if (status) *status = TAURUS_ERROR_PARSE;
        if (detected_encoding) free(detected_encoding);
        return NULL;
    }

    /* Parse the UTF-8 content */
    struct taurus_document* doc = taurus_parse(utf8_xml, utf8_len);

    /* Store detected encoding in document if parsed successfully */
    if (doc && detected_encoding) {
        if (doc->encoding) {
            TAURUS_FREE(doc->encoding);
        }
        doc->encoding = taurus_strdup(detected_encoding);
    }

    /* taurus_parse() already heap-copied utf8_xml into doc->xml_buffer;
     * the document's StringViews point into that inner copy.  Our
     * conversion buffer is redundant — free it. */
    if (utf8_xml != xml) {
        free(utf8_xml);
    }

    if (detected_encoding) {
        free(detected_encoding);
    }

    if (!doc && status) {
        *status = TAURUS_ERROR_PARSE;
    }

    return doc;
#else
    /* No iconv support - fall back to regular parsing (assumes UTF-8) */
    struct taurus_document* doc = taurus_parse(xml, length);

    if (!doc && status) {
        *status = TAURUS_ERROR_PARSE;
    }

    return doc;
#endif
}

/**
 * Parse XML string into document with in-place optimization (Public API wrapper)
 */
TAURUS_API TaurusDocument taurus_parse_string_inplace(char* xml, size_t length, TaurusStatus* status) {
    if (status) *status = TAURUS_OK;

    struct taurus_document* doc = taurus_parse_inplace(xml, length);

    if (!doc && status) {
        *status = TAURUS_ERROR_PARSE;
    }

    return doc;
}


/**
 * Parse XML with custom options
 */
TAURUS_API struct taurus_document* taurus_parse_with_options(
    const char* xml,
    size_t len,
    const taurus_parse_options* opts
) {
    if (!xml || len == 0) return NULL;

    /* For now, ignore options and use new parser
     * TODO: Implement options support in parser */
    (void)opts; /* Suppress unused parameter warning */
    return taurus_parse(xml, len);
}

/* ============================================================================
 * File I/O Operations
 * ============================================================================ */

/**
 * Load file into memory buffer (Public API)
 */
TAURUS_API char* taurus_load_file(const char* filepath, size_t* out_size) {
    if (!filepath) {
        if (out_size) *out_size = 0;
        return NULL;
    }

    FILE* f = fopen(filepath, "rb");
    if (!f) {
        if (out_size) *out_size = 0;
        return NULL;
    }

    /* Get file size */
    fseek(f, 0, SEEK_END);
    long fsize = ftell(f);
    fseek(f, 0, SEEK_SET);

    if (fsize < 0) {
        fclose(f);
        if (out_size) *out_size = 0;
        return NULL;
    }

    /* Allocate buffer (+1 for null terminator) */
    char* buffer = TAURUS_ALLOC_N(char, fsize + 1);
    if (!buffer) {
        fclose(f);
        if (out_size) *out_size = 0;
        return NULL;
    }

    /* Read file */
    size_t read_size = fread(buffer, 1, fsize, f);
    fclose(f);

    /* Null terminate */
    buffer[read_size] = '\0';

    if (out_size) *out_size = read_size;

    return buffer;
}

/**
 * Parse XML file directly (Public API)
 */
TAURUS_API TaurusDocument taurus_parse_file(const char* filepath, TaurusStatus* status) {
    if (status) *status = TAURUS_OK;

    if (!filepath) {
        if (status) *status = TAURUS_ERROR_NULL_ARG;
        return NULL;
    }

    size_t size;
    char* buffer = taurus_load_file(filepath, &size);
    if (!buffer) {
        if (status) *status = TAURUS_ERROR_NOT_FOUND;
        return NULL;
    }

    /* Parse the buffer */
    TaurusDocument doc = taurus_parse_string(buffer, size, status);

    /* Free the buffer (document makes its own copies) */
    TAURUS_FREE(buffer);

    return doc;
}

/**
 * Free document and all its contents
 */
TAURUS_API void taurus_document_adopt_child(TaurusDocument parent,
                                           TaurusDocument child) {
    if (!parent || !child) return;
    /* Single-link list of adopted docs.  Append in O(1). */
    child->child_docs = NULL;
    child->child_docs_tail = NULL;
    if (parent->child_docs_tail) {
        parent->child_docs_tail->child_docs = child;
    } else {
        parent->child_docs = child;
    }
    parent->child_docs_tail = child;
}

TAURUS_API void taurus_document_free(struct taurus_document* doc) {
    if (!doc) return;

    /* Decrement reference count */
    if (doc->ref_count > 0) {
        doc->ref_count--;
        if (doc->ref_count > 0) return;
    }

    /* Handle new DOM tree cleanup based on allocation method */
    if (doc->new_dom_root) {
        /* In compact mode, elements are pool-allocated and freed with the pool
         * No individual free needed */
        /* The pool will be destroyed below, freeing all elements */
    }

    /* Free document fields */
    if (doc->encoding) {
        TAURUS_FREE(doc->encoding);
    }

    if (doc->xml_version) {
        TAURUS_FREE(doc->xml_version);
    }

    /* Free DOCTYPE if present */
    if (doc->doctype) {
        taurus_doctype_free((TaurusDoctypeNode*)doc->doctype);
    }

    /* Free DTD if present */
    if (doc->dtd) {
        ttdtd_free((TaurusDTD*)doc->dtd);
    }

    /* Free processing instructions */
    struct taurus_processing_instruction* pi = doc->pis;
    while (pi) {
        struct taurus_processing_instruction* next = pi->next;
        if (pi->target) TAURUS_FREE(pi->target);
        if (pi->data) TAURUS_FREE(pi->data);
        TAURUS_FREE(pi);
        pi = next;
    }

    /* TODO 117: release adopted child documents from xi:include
     * parse="xml".  Each child was parsed into its own pool; its
     * nodes were MOVED (not copied) into our tree, so the child's
     * pool must outlive us.  Free after our own cleanup so the
     * walk over own metadata precedes the release of pools. */
    {
        struct taurus_document* ch = doc->child_docs;
        while (ch) {
            struct taurus_document* next = ch->child_docs;
            ch->new_dom_root = NULL;  /* Detach so taurus_document_free
                                       * doesn't try to walk our tree
                                       * (which contains its nodes). */
            ch->root = NULL;
            taurus_document_free(ch);
            ch = next;
        }
        doc->child_docs = NULL;
        doc->child_docs_tail = NULL;
    }

    /* Free owned XML buffer if present
     * For regular parsing, the document owns the buffer (copied during parsing)
     * For in-place parsing, the document also owns the buffer for consistency
     * The buffer is freed here to ensure StringViews remain valid for document lifetime
     * For stack-allocated buffers (files <= 4KB), xml_buffer_needs_free = 0 */
    if (doc->xml_buffer && doc->xml_buffer_needs_free) {
        TAURUS_FREE(doc->xml_buffer);
    }

    /* CRITICAL: Cleanup overflow table BEFORE destroying the pool
     * The new per-document cleanup only removes entries for this document,
     * preserving entries for other active documents.
     * This ensures that the overflow table doesn't have stale entries
     * pointing to memory that will be freed when the pool is destroyed. */
    if (doc->ref_count == 0) {
        extern void taurus_compact_cleanup_document(struct taurus_document* doc);
        taurus_compact_cleanup_document(doc);
    }

    /* Destroy memory pool (frees all DOM nodes allocated from it) */
    if (doc->pool) {
        taurus_pool_destroy(doc->pool);
    }

    /* Free document */
    TAURUS_FREE(doc);
}

/**
 * Get root element of document
 */
TAURUS_API TaurusElement taurus_document_root(struct taurus_document* doc) {
    if (!doc) return NULL;

    /* Check new_dom_root first (new parser), then fall back to root (old parser) */
    if (doc->new_dom_root) {
        return (TaurusElement)doc->new_dom_root;
    }
    return (TaurusElement)doc->root;
}

/**
 * Serialize document to XML string
 */
TAURUS_API char* taurus_serialize_document(struct taurus_document* doc) {
    if (!doc) return NULL;

    /* Set up serialization options based on document properties */
    TaurusSerializeOptions opts = { 0 };

    /* If document had XML declaration, include it */
    if (doc->had_declaration && doc->xml_version) {
        opts.xml_declaration = 1;
        opts.encoding = doc->encoding;
    } else {
        opts.xml_declaration = 0;
        opts.encoding = NULL;
    }

    /* Default to compact mode */
    opts.indent = 0;

    /* Use the new serialization API */
    return taurus_document_serialize(doc, &opts);
}

/* ============================================================================
 * Memory Management Helpers
 * ============================================================================ */

/**
 * Free string returned by libtaurus (Public API)
 */
TAURUS_API void taurus_free_string(char* str) {
    if (str) {
        TAURUS_FREE(str);
    }
}


/* ============================================================================
 * Document String Finalization
 * ============================================================================ */

/* Forward declaration for recursive helper */
static void finalize_element_strings(TaurusElement elem, TaurusMemoryPool* pool);

/* Helper: Finalize strings for a single element.
 *
 * `pool` is passed explicitly (rather than read from elem->document)
 * because child elements may not have their document back-pointer set
 * yet during this recursion — TODO 25.
 *
 * We bypass the lazy-conversion accessors (taurus_element_get_name etc.)
 * and route directly through taurus_sv_to_cstr_pooled so the resulting
 * strings are pool-owned.  The accessors would otherwise fall back to
 * calloc when elem->document is NULL, leaking. */
static void finalize_element_strings(TaurusElement elem, TaurusMemoryPool* pool) {
    if (!elem) return;

    /* Convert element name StringView to NULL-terminated string.
     *
     * TODO 25: pool is always non-NULL when called from
    /* name_view removed (TODO 90) — name is set eagerly by create_with_view. */
    /* namespace_uri_view + prefix_view removed (TODO 90) — both are now
     * set eagerly by the parser via pool-strdup. */

    /* Convert all attribute StringViews */
    size_t attr_count = taurus_element_attribute_count(elem);
    for (size_t i = 0; i < attr_count; i++) {
        struct taurus_attribute* attr = taurus_element_get_attribute_by_index(elem, i);
        if (!attr) continue;

        /* Validate attribute pointer before accessing */
        if ((uintptr_t)attr < 0x1000) continue;  /* Invalid pointer */

        /* Convert attribute name and value StringViews.
         *
         * TODO 25: pool is always non-NULL here; force pooled path. */
        if (!attr->name && !taurus_sv_is_empty(&attr->name_view)) {
            if ((uintptr_t)attr->name_view.data >= 0x1000) {
                attr->name = taurus_sv_to_cstr_pooled(&attr->name_view, pool);
            }
        }
        if (!attr->value && !taurus_sv_is_empty(&attr->value_view)) {
            if ((uintptr_t)attr->value_view.data >= 0x1000) {
                if (attr->has_entities) {
                    attr->value = taurus_decode_entities_view(&attr->value_view, pool);
                }
                if (!attr->value) {
                    attr->value = taurus_sv_to_cstr_pooled(&attr->value_view, pool);
                }
            }
        }
        if (!attr->namespace_uri && !taurus_sv_is_empty(&attr->namespace_uri_view)) {
            if ((uintptr_t)attr->namespace_uri_view.data >= 0x1000) {
                attr->namespace_uri = taurus_sv_to_cstr_pooled(&attr->namespace_uri_view, pool);
            }
        }
    }

    /* Recursively finalize strings for all children */
    TaurusNode* child = (TaurusNode*)taurus_element_get_first_child(elem);
    while (child) {
        /* Only process element children recursively */
        if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
            finalize_element_strings((TaurusElement)child, pool);
        }

        /* CRITICAL FIX: Use generic next_sibling accessor instead of manual field access!
         * The old code assumed all node types had next_sibling at the same offset,
         * but TaurusPINode has extra fields (target, data) before next_sibling.
         * This was causing bus errors when accessing next_sibling for PI nodes.
         * The generic taurus_node_get_next_sibling() function handles all node types correctly. */
        child = taurus_node_get_next_sibling(child);
    }
}

/**
 * Finalize all StringViews in document to NULL-terminated strings (Public API)
 *
 * This function eagerly converts all StringViews to NULL-terminated strings,
 * eliminating lazy conversion overhead during queries. This is especially
 * important for Read-Many workloads where the same document is queried multiple times.
 */
TAURUS_API int taurus_document_finalize_strings(TaurusDocument doc) {
    if (!doc) return 0;

    TaurusElement root = taurus_document_root(doc);
    if (!root) return 0;

    /* Recursively finalize all strings in the document tree.
     * Pass doc->pool explicitly — see TODO 25. */
    finalize_element_strings(root, doc->pool);

    return 1; /* Success */
}

/* ---- Freeze API (TODO 88) ---- */

TAURUS_API TaurusStatus taurus_document_freeze(TaurusDocument doc) {
    if (!doc) return TAURUS_ERROR_NULL_ARG;
    taurus_document_freeze_tree(doc);
    return TAURUS_OK;
}

TAURUS_API int taurus_document_is_frozen(TaurusDocument doc) {
    if (!doc) return 0;
    TaurusElement root = taurus_document_root(doc);
    if (!root) return 0;
    TaurusNode* root_node = (TaurusNode*)root;
    return root_node->frozen ? 1 : 0;
}

