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

/* Forward decl — g_taurus_strict_mode is defined later in this file.
 * Documents inherit its value at creation time (TODO 38). */
static __thread int g_taurus_strict_mode;

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

#define TAURUS_VERSION "0.1.0"
#define TAURUS_VERSION_MAJOR 0
#define TAURUS_VERSION_MINOR 1
#define TAURUS_VERSION_PATCH 0

/* ============================================================================
 * Forward Declarations
 * ============================================================================ */

/* Forward declarations for new parser/serializer */
typedef struct Parser Parser;
typedef struct taurus_doctype_node TaurusDoctypeNode;

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

/**
 * Get library version string
 */
TAURUS_API const char* taurus_version(void) {
    return TAURUS_VERSION;
}

/**
 * Get version components
 */
TAURUS_API void taurus_version_components(int* major, int* minor, int* patch) {
    if (major) *major = TAURUS_VERSION_MAJOR;
    if (minor) *minor = TAURUS_VERSION_MINOR;
    if (patch) *patch = TAURUS_VERSION_PATCH;
}

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
            if (status) *status = TAURUS_ERROR_MEMORY_ALLOCATION;
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
            if (status) *status = TAURUS_ERROR_MEMORY_ALLOCATION;
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
 * Parse XML string with automatic encoding detection and conversion (Public API)
 */
TAURUS_API TaurusDocument taurus_parse_string_with_encoding(const char* xml, size_t length, TaurusStatus* status) {
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
            if (status) *status = TAURUS_ERROR_MEMORY_ALLOCATION;
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
            if (status) *status = TAURUS_ERROR_MEMORY_ALLOCATION;
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
    return taurus_parse_string(xml, length, status);
#endif
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

/* Global strict parsing mode flag (default: lenient mode for pugixml compatibility).
 *
 * TODO 27 phase 1: made thread-local so concurrent parses in
 * different threads don't race on this flag.  Phase 2 (deferred)
 * moves this to the document so different documents in the same
 * thread can have different modes. */
static __thread int g_taurus_strict_mode = 0;

/* Thread-default max element-nesting depth.  0 = use compile-time
 * default of TAURUS_MAX_ELEMENT_DEPTH (256).  Set via
 * taurus_set_max_depth (TODO 62). */
static __thread int g_taurus_max_depth = 0;

int taurus_get_max_depth_default(void) {
    return g_taurus_max_depth;
}

TAURUS_API void taurus_set_max_depth(int max_depth) {
    g_taurus_max_depth = max_depth;
}

TAURUS_API int taurus_get_max_depth(void) {
    return g_taurus_max_depth > 0
        ? g_taurus_max_depth
        : 256;  /* TAURUS_MAX_ELEMENT_DEPTH */
}

/**
 * Set strict parsing mode (thread-default).
 *
 * TODO 27 phase 2: per-document overrides via taurus_document_set_strict.
 */
TAURUS_API void taurus_set_strict_mode(int strict) {
    g_taurus_strict_mode = (strict != 0);
}

/**
 * Get current strict parsing mode (thread-default — internal function).
 * Per-document code should call taurus_document_get_strict instead.
 */
TAURUS_API int taurus_get_strict_mode(void) {
    return g_taurus_strict_mode;
}

TAURUS_API TaurusStatus taurus_document_set_strict(TaurusDocument doc, int strict) {
    if (!doc) return TAURUS_ERROR_NULL_ARG;
    doc->strict_mode = (strict != 0);
    return TAURUS_OK;
}

TAURUS_API int taurus_document_get_strict(TaurusDocument doc) {
    return doc ? doc->strict_mode : g_taurus_strict_mode;
}

/**
 * Explicit cleanup function (for testing)
 *
 * This function cleans up the compact pointer overflow table and other
 * thread-local structures that may accumulate stale entries across
 * multiple document operations.
 *
 * IMPORTANT: This should ONLY be called between test runs or when
 * you're certain no documents are active.
 */
TAURUS_API void taurus_explicit_cleanup(void) {
    extern void taurus_compact_cleanup(void);
    taurus_compact_cleanup();
}

/**
 * Get document encoding
 */
TAURUS_API const char* taurus_document_encoding(struct taurus_document* doc) {
    if (!doc) return NULL;
    return doc->encoding; /* May be NULL if not specified */
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
 * Element Functions
 * ============================================================================ */

/**
 * Get element name
 */
TAURUS_API const char* taurus_element_name(TaurusElement elem) {
    if (!elem) return "";

    /* Use compact accessor to get element name */
    const char* name = taurus_element_get_name(elem);
    return name ? name : "";
}

/**
 * Get element text content (concatenated recursively)
 */
TAURUS_API const char* taurus_element_text(TaurusElement elem) {
    if (!elem) return "";

    /* Use compact accessor to extract text content */
    char* text = taurus_element_get_text_content(elem);

    /* Note: Caller must free the returned string */
    return text ? text : "";
}

/**
 * Get child element text value (first text node only, not recursive)
 * This is different from taurus_element_text which concatenates all text recursively
 *
 * Returns nullptr if no text/CDATA child exists, not empty string ""
 *
 * Behavior: Looks at the first child node. If it's text/CDATA, returns its content.
 * If it's an element, recursively gets the child value from that element (one level deep).
 */
TAURUS_API const char* taurus_element_child_value(TaurusElement elem) {
    if (!elem) return NULL;

    /* Get first child node - use node API to get ALL children (not just elements) */
    TaurusNode* child = taurus_node_first_child((TaurusNode*)elem);
    if (!child) return NULL;

    /* If first child is a text node, return its content */
    if (child->type == TAURUS_NODE_TYPE_TEXT) {
        TaurusTextNode* text = (TaurusTextNode*)child;
        return text->content;
    }

    /* If first child is a CDATA node, return its content */
    if (child->type == TAURUS_NODE_TYPE_CDATA) {
        TaurusCDATANode* cdata = (TaurusCDATANode*)child;
        return cdata->content;
    }

    /* If first child is an element, recursively get its child value (one level deep) */
    if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
        return taurus_element_child_value((TaurusElement)child);
    }

    return NULL;  /* Comment, PI, or other node types have no text value */
}

/**
 * Get attribute value by name (Public API)
 * PERFORMANCE: Uses linked list traversal (O(n) where n = attribute count)
 */
TAURUS_API const char* taurus_element_attribute(TaurusElement elem, const char* name) {
    if (!elem || !name) return NULL;

    /* Validate node type - only elements have attributes */
    TaurusNode* node = (TaurusNode*)elem;
    if (node->type != TAURUS_NODE_TYPE_ELEMENT) {
        return NULL;  /* Not an element node */
    }

    /* Use accessor function to find attribute */
    struct taurus_attribute* attr = taurus_element_get_attribute_by_name(elem, name);
    if (!attr) return NULL;

    /* Lazy convert value to NULL-terminated and resolve entities */
    if (!attr->value) {
        /* Get pool from document for string conversion */
        TaurusMemoryPool* pool = elem->document ? elem->document->pool : NULL;

        /* PERFORMANCE: Use pre-computed has_entities flag */
        if (attr->has_entities) {
            attr->value = taurus_decode_entities_view(&attr->value_view, pool);
        }

        /* Fallback: convert without entity resolution */
        if (!attr->value) {
            attr->value = taurus_sv_to_cstr_pooled(&attr->value_view, pool);
        }
    }
    return attr->value;
}

/**
 * Get attribute value as integer (Public API)
 */
TAURUS_API int taurus_element_attribute_int(TaurusElement elem, const char* name, int default_value) {
    const char* value = taurus_element_attribute(elem, name);
    if (!value || !*value) return default_value;

    /* Trim leading whitespace */
    while (isspace((unsigned char)*value)) value++;

    /* Check for valid integer (no partial parsing like "42abc") */
    char* endptr;
    long result = strtol(value, &endptr, 10);

    /* If there's non-whitespace after the number, it's invalid */
    while (*endptr && isspace((unsigned char)*endptr)) endptr++;
    if (*endptr != '\0') return default_value;

    return (int)result;
}

/**
 * Get attribute value as unsigned integer (Public API)
 */
TAURUS_API unsigned int taurus_element_attribute_uint(TaurusElement elem, const char* name, unsigned int default_value) {
    const char* value = taurus_element_attribute(elem, name);
    if (!value || !*value) return default_value;

    /* Trim leading whitespace */
    while (isspace((unsigned char)*value)) value++;

    /* Check for valid unsigned integer (no partial parsing) */
    char* endptr;
    unsigned long result = strtoul(value, &endptr, 10);

    /* If there's non-whitespace after the number, it's invalid */
    while (*endptr && isspace((unsigned char)*endptr)) endptr++;
    if (*endptr != '\0') return default_value;

    return (unsigned int)result;
}

/**
 * Get attribute value as double (Public API)
 */
TAURUS_API double taurus_element_attribute_double(TaurusElement elem, const char* name, double default_value) {
    const char* value = taurus_element_attribute(elem, name);
    if (!value || !*value) return default_value;

    /* Trim leading whitespace */
    while (isspace((unsigned char)*value)) value++;

    /* Check for valid double (no partial parsing) */
    char* endptr;
    double result = strtod(value, &endptr);

    /* If there's non-whitespace after the number, it's invalid */
    while (*endptr && isspace((unsigned char)*endptr)) endptr++;
    if (*endptr != '\0') return default_value;

    return result;
}

/**
 * Get attribute value as float (Public API)
 */
TAURUS_API float taurus_element_attribute_float(TaurusElement elem, const char* name, float default_value) {
    const char* value = taurus_element_attribute(elem, name);
    if (!value || !*value) return default_value;

    /* Trim leading whitespace */
    while (isspace((unsigned char)*value)) value++;

    /* Check for valid float (no partial parsing) */
    char* endptr;
    float result = strtof(value, &endptr);

    /* If there's non-whitespace after the number, it's invalid */
    while (*endptr && isspace((unsigned char)*endptr)) endptr++;
    if (*endptr != '\0') return default_value;

    return result;
}

/**
 * Get attribute value as boolean (Public API)
 */
TAURUS_API int taurus_element_attribute_bool(TaurusElement elem, const char* name, int default_value) {
    const char* value = taurus_element_attribute(elem, name);
    if (!value || !*value) return default_value;

    /* Case-insensitive comparison for true/false/yes/no/on/off/1/0 */
    if (strcasecmp(value, "true") == 0 ||
        strcasecmp(value, "yes") == 0 ||
        strcasecmp(value, "on") == 0 ||
        strcmp(value, "1") == 0) {
        return 1;
    }
    if (strcasecmp(value, "false") == 0 ||
        strcasecmp(value, "no") == 0 ||
        strcasecmp(value, "off") == 0 ||
        strcmp(value, "0") == 0) {
        return 0;
    }

    return default_value;
}

/**
 * Get attribute value as string with default (Public API)
 */
TAURUS_API const char* taurus_element_attribute_string(TaurusElement elem, const char* name, const char* default_value) {
    const char* value = taurus_element_attribute(elem, name);
    return (value != NULL) ? value : default_value;
}

/* ============================================================================
 * Attribute Setter Functions (Type-safe wrappers)
 * ============================================================================ */

/**
 * Set attribute value as double (Public API)
 */
TAURUS_API int taurus_element_set_attribute_double(TaurusElement elem, const char* name, double value) {
    if (!elem || !name) return TAURUS_ERROR_NULL_ARG;

    /* Convert double to string with sufficient precision (17 significant digits for IEEE 754) */
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%.17g", value);
    return taurus_element_set_attribute(elem, name, buffer);
}

/**
 * Set attribute value as float (Public API)
 */
TAURUS_API int taurus_element_set_attribute_float(TaurusElement elem, const char* name, float value) {
    if (!elem || !name) return TAURUS_ERROR_NULL_ARG;

    /* Convert float to string */
    char buffer[64];
    snprintf(buffer, sizeof(buffer), "%g", value);
    return taurus_element_set_attribute(elem, name, buffer);
}

/**
 * Set attribute value as boolean (Public API)
 */
TAURUS_API int taurus_element_set_attribute_bool(TaurusElement elem, const char* name, int value) {
    if (!elem || !name) return TAURUS_ERROR_NULL_ARG;

    /* Convert boolean to string */
    const char* bool_str = value ? "true" : "false";
    return taurus_element_set_attribute(elem, name, bool_str);
}

/**
 * Set attribute value as integer (Public API)
 */
TAURUS_API int taurus_element_set_attribute_int(TaurusElement elem, const char* name, int value) {
    if (!elem || !name) return TAURUS_ERROR_NULL_ARG;

    /* Convert int to string */
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%d", value);
    return taurus_element_set_attribute(elem, name, buffer);
}

/**
 * Set attribute value as unsigned integer (Public API)
 */
TAURUS_API int taurus_element_set_attribute_uint(TaurusElement elem, const char* name, unsigned int value) {
    if (!elem || !name) return TAURUS_ERROR_NULL_ARG;

    /* Convert unsigned int to string */
    char buffer[32];
    snprintf(buffer, sizeof(buffer), "%u", value);
    return taurus_element_set_attribute(elem, name, buffer);
}

/**
 * Get child element by index (Public API)
 * Returns the index-th ELEMENT child (skips text, comments, etc.)
 *
 * PERFORMANCE: Uses inline accessor functions from element.h for maximum speed.
 * The inline functions use a fast-path check that avoids the while loop
 * when the first child/next sibling is already an element (common case).
 */
TAURUS_API TaurusElement taurus_element_child(TaurusElement elem, size_t index) {
    if (!elem) return NULL;

    /* Validate index against element child count */
    if (index >= elem->child_count) {
        return NULL;
    }

    /* Navigate to the requested index using inline accessors
     * These have fast-path optimization for element-only children */
    TaurusElement child = taurus_element_get_first_child(elem);

    for (size_t i = 0; i < index && child != NULL; i++) {
        child = taurus_element_get_next_sibling(child);
    }

    return child;
}

/**
 * Get parent element (Public API)
 */
TAURUS_API TaurusElement taurus_element_parent(TaurusElement elem) {
    if (!elem) return NULL;

    /* Use compact accessor to get parent */
    return taurus_element_get_parent(elem);
}

/**
 * Get root element of document (Public API)
 * Walks up the parent chain to find the element with no parent
 */
TAURUS_API TaurusElement taurus_element_root(TaurusElement elem) {
    if (!elem) return NULL;

    /* Walk up the parent chain until we find an element with no parent */
    TaurusElement current = elem;
    TaurusElement parent = taurus_element_get_parent(current);
    while (parent) {
        current = parent;
        parent = taurus_element_get_parent(current);
    }

    return current;
}

/**
 * Get first child element regardless of name (Public API)
 */
TAURUS_API TaurusElement taurus_element_first_child_any(TaurusElement elem) {
    if (!elem) return NULL;

    /* Iterate through children until we find an element */
    TaurusNode* child = (TaurusNode*)taurus_element_get_first_child(elem);
    while (child) {
        if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
            return (TaurusElement)child;
        }

        /* Get next sibling based on node type - each type has different offset! */
        if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
            child = taurus_node_get_next_sibling(child);
        } else if (child->type == TAURUS_NODE_TYPE_TEXT) {
            child = (TaurusNode*)(((TaurusTextNode*)child)->next_sibling);
        } else if (child->type == TAURUS_NODE_TYPE_CDATA) {
            child = (TaurusNode*)(((TaurusCDATANode*)child)->next_sibling);
        } else if (child->type == TAURUS_NODE_TYPE_COMMENT) {
            child = (TaurusNode*)(((TaurusCommentNode*)child)->next_sibling);
        } else if (child->type == TAURUS_NODE_TYPE_PI) {
            child = (TaurusNode*)(((TaurusPINode*)child)->next_sibling);
        } else {
            child = NULL;
        }
    }

    return NULL;
}

/**
 * Get last child element regardless of name (Public API)
 */
TAURUS_API TaurusElement taurus_element_last_child_any(TaurusElement elem) {
    if (!elem) return NULL;

    /* Iterate through children to find the last element */
    TaurusElement last_elem = NULL;

    TaurusNode* child = (TaurusNode*)taurus_element_get_first_child(elem);
    while (child) {
        if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
            last_elem = (TaurusElement)child;
        }

        /* Get next sibling based on node type - each type has different offset! */
        if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
            child = taurus_node_get_next_sibling(child);
        } else if (child->type == TAURUS_NODE_TYPE_TEXT) {
            child = (TaurusNode*)(((TaurusTextNode*)child)->next_sibling);
        } else if (child->type == TAURUS_NODE_TYPE_CDATA) {
            child = (TaurusNode*)(((TaurusCDATANode*)child)->next_sibling);
        } else if (child->type == TAURUS_NODE_TYPE_COMMENT) {
            child = (TaurusNode*)(((TaurusCommentNode*)child)->next_sibling);
        } else if (child->type == TAURUS_NODE_TYPE_PI) {
            child = (TaurusNode*)(((TaurusPINode*)child)->next_sibling);
        } else {
            child = NULL;
        }
    }

    return last_elem;
}

/**
 * Get next sibling element regardless of name (Public API)
 */
TAURUS_API TaurusElement taurus_element_next_sibling_any(TaurusElement elem) {
    if (!elem) return NULL;

    /* Use compact accessor to get next sibling */
    TaurusNode* sibling = (TaurusNode*)taurus_element_get_next_sibling(elem);

    /* Keep traversing until we find an element (skip text/comment nodes) */
    while (sibling) {
        /* Check if it's an element */
        if (sibling->type == TAURUS_NODE_TYPE_ELEMENT) {
            return (TaurusElement)sibling;
        }

        /* Get next sibling based on node type - each type has different offset! */
        if (sibling->type == TAURUS_NODE_TYPE_TEXT) {
            sibling = (TaurusNode*)(((TaurusTextNode*)sibling)->next_sibling);
        } else if (sibling->type == TAURUS_NODE_TYPE_CDATA) {
            sibling = (TaurusNode*)(((TaurusCDATANode*)sibling)->next_sibling);
        } else if (sibling->type == TAURUS_NODE_TYPE_COMMENT) {
            sibling = (TaurusNode*)(((TaurusCommentNode*)sibling)->next_sibling);
        } else if (sibling->type == TAURUS_NODE_TYPE_PI) {
            sibling = (TaurusNode*)(((TaurusPINode*)sibling)->next_sibling);
        } else {
            sibling = NULL;
        }
    }

    return NULL;
}

/**
 * Get previous sibling element regardless of name (Public API)
 * NOTE: Compact mode doesn't have prev_sibling pointer, so we search from parent
 */
TAURUS_API TaurusElement taurus_element_previous_sibling_any(TaurusElement elem) {
    if (!elem) return NULL;

    /* Get parent */
    TaurusElement parent = taurus_element_get_parent(elem);
    if (!parent) return NULL;

    /* Find previous sibling by walking from parent's first child */
    TaurusNode* child = (TaurusNode*)taurus_element_get_first_child(parent);
    TaurusElement prev = NULL;

    while (child && child != (TaurusNode*)elem) {
        /* Only consider element nodes as previous siblings */
        if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
            prev = (TaurusElement)child;
        }

        /* Get next sibling based on node type */
        if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
            child = taurus_node_get_next_sibling(child);
        } else if (child->type == TAURUS_NODE_TYPE_TEXT) {
            child = (TaurusNode*)(((TaurusTextNode*)child)->next_sibling);
        } else if (child->type == TAURUS_NODE_TYPE_CDATA) {
            child = (TaurusNode*)(((TaurusCDATANode*)child)->next_sibling);
        } else if (child->type == TAURUS_NODE_TYPE_COMMENT) {
            child = (TaurusNode*)(((TaurusCommentNode*)child)->next_sibling);
        } else if (child->type == TAURUS_NODE_TYPE_PI) {
            child = (TaurusNode*)(((TaurusPINode*)child)->next_sibling);
        } else {
            child = NULL;
        }
    }

    return prev;
}

/**
 * Get first child element with specific name (Public API)
 * If name is NULL, returns first child regardless of name
 */
TAURUS_API TaurusElement taurus_element_first_child(TaurusElement elem, const char* name) {
    if (!elem) return NULL;

    /* Get first child and check name if specified */
    TaurusElement child = taurus_element_get_first_child(elem);
    if (!name) return child;

    /* Find first child with matching name */
    while (child) {
        const char* child_name = taurus_element_name(child);
        if (child_name && strcmp(child_name, name) == 0) {
            return child;
        }
        child = taurus_element_get_next_sibling(child);
    }

    return NULL;
}

/**
 * Get last child element with specific name (Public API)
 * If name is NULL, returns last child regardless of name
 */
TAURUS_API TaurusElement taurus_element_last_child(TaurusElement elem, const char* name) {
    if (!elem) return NULL;

    /* Get last child */
    TaurusElement last = taurus_element_get_last_child(elem);
    if (!name || !last) return last;

    /* Walk backwards from last child to find one with matching name */
    /* Since we don't have prev_sibling, we need to walk from first child */
    TaurusElement found = NULL;
    TaurusElement child = taurus_element_get_first_child(elem);
    while (child) {
        const char* child_name = taurus_element_name(child);
        if (child_name && strcmp(child_name, name) == 0) {
            found = child;  /* Keep updating to get the last one */
        }
        child = taurus_element_get_next_sibling(child);
    }

    return found;
}

/**
 * Get next sibling element with specific name (Public API)
 * If name is NULL, returns next sibling regardless of name
 */
TAURUS_API TaurusElement taurus_element_next_sibling(TaurusElement elem, const char* name) {
    if (!elem) return NULL;

    /* Get next sibling */
    TaurusElement sibling = taurus_element_get_next_sibling(elem);
    if (!name) return sibling;

    /* Find next sibling with matching name */
    while (sibling) {
        const char* sibling_name = taurus_element_name(sibling);
        if (sibling_name && strcmp(sibling_name, name) == 0) {
            return sibling;
        }
        sibling = taurus_element_get_next_sibling(sibling);
    }

    return NULL;
}

/**
 * Get previous sibling element with specific name (Public API)
 * If name is NULL, returns previous sibling regardless of name
 */
TAURUS_API TaurusElement taurus_element_previous_sibling(TaurusElement elem, const char* name) {
    if (!elem) return NULL;

    /* Get parent */
    TaurusElement parent = taurus_element_get_parent(elem);
    if (!parent) return NULL;

    /* Find previous sibling by walking from parent's first child */
    TaurusElement child = taurus_element_get_first_child(parent);
    TaurusElement prev = NULL;
    TaurusElement found = NULL;

    while (child && child != elem) {
        if (!name) {
            prev = child;
        } else {
            const char* child_name = taurus_element_name(child);
            if (child_name && strcmp(child_name, name) == 0) {
                found = child;  /* Keep updating to get the most recent matching one */
            }
        }
        child = taurus_element_get_next_sibling(child);
    }

    return name ? found : prev;
}

/**
 * Find child element by name (Public API)
 * Searches all children for one with matching name
 */
TAURUS_API TaurusElement taurus_element_find_child(TaurusElement elem, const char* name) {
    if (!elem || !name) return NULL;

    /* Walk all children to find one with matching name */
    TaurusElement child = taurus_element_get_first_child(elem);
    while (child) {
        const char* child_name = taurus_element_name(child);
        if (child_name && strcmp(child_name, name) == 0) {
            return child;
        }
        child = taurus_element_get_next_sibling(child);
    }

    return NULL;
}

/**
 * Find child element by name and attribute value (Public API)
 * Searches all children for one with matching name and attribute value
 */
TAURUS_API TaurusElement taurus_element_find_child_by_attr(TaurusElement elem,
                                                           const char* child_name,
                                                           const char* attr_name,
                                                           const char* attr_value) {
    if (!elem || !attr_name || !attr_value) return NULL;

    /* Walk all children to find one with matching name and attribute */
    TaurusElement child = taurus_element_get_first_child(elem);
    while (child) {
        /* If child_name is NULL, match any child. Otherwise check name matches. */
        int name_matches = (child_name == NULL);
        if (!name_matches) {
            const char* child_elem_name = taurus_element_name(child);
            name_matches = (child_elem_name && strcmp(child_elem_name, child_name) == 0);
        }

        if (name_matches) {
            /* Child has matching name (or any name if child_name is NULL), check attribute */
            const char* attr_val = taurus_element_attribute(child, attr_name);
            if (attr_val && strcmp(attr_val, attr_value) == 0) {
                return child;
            }
        }
        child = taurus_element_get_next_sibling(child);
    }

    return NULL;
}

/* ============================================================================
 * Text Content Conversion Functions (Public API)
 * ============================================================================ */

/**
 * Get element text content as integer (Public API)
 */
TAURUS_API int taurus_element_text_int(TaurusElement elem, int default_value) {
    const char* text = taurus_element_text(elem);
    if (!text || !text[0]) return default_value;

    /* Skip leading whitespace */
    while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r') {
        text++;
    }

    /* Check for hex prefix (0x or 0X) */
    int is_hex = 0;
    int is_negative = 0;
    const char* start = text;

    if (*text == '-') {
        is_negative = 1;
        text++;
        /* Check for whitespace immediately after minus sign (invalid) */
        if (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r') {
            return default_value;
        }
    }

    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        is_hex = 1;
        text += 2;
    }

    /* Parse the number */
    char* endptr;
    long value;
    if (is_hex) {
        value = strtol(text, &endptr, 16);
    } else {
        /* Use base 10 for decimal (no octal support) */
        value = strtol(text, &endptr, 10);
    }

    /* Check if entire string was consumed */
    if (endptr == text || *endptr != '\0') {
        return default_value;
    }

    return is_negative ? -(int)value : (int)value;
}

/**
 * Get element text content as unsigned integer (Public API)
 */
TAURUS_API unsigned int taurus_element_text_uint(TaurusElement elem, unsigned int default_value) {
    const char* text = taurus_element_text(elem);
    if (!text || !text[0]) return default_value;

    /* Skip leading whitespace */
    while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r') {
        text++;
    }

    /* Check for hex prefix (0x or 0X) */
    int is_hex = 0;
    const char* start = text;

    if (text[0] == '0' && (text[1] == 'x' || text[1] == 'X')) {
        is_hex = 1;
        text += 2;
    }

    /* Parse the number */
    char* endptr;
    unsigned long value;
    if (is_hex) {
        value = strtoul(text, &endptr, 16);
    } else {
        /* Use base 10 for decimal (no octal support) */
        value = strtoul(text, &endptr, 10);
    }

    /* Check if entire string was consumed */
    if (endptr == text || *endptr != '\0') {
        return default_value;
    }

    return (unsigned int)value;
}

/**
 * Get element text content as double (Public API)
 */
TAURUS_API double taurus_element_text_double(TaurusElement elem, double default_value) {
    const char* text = taurus_element_text(elem);
    if (!text || !text[0]) return default_value;

    /* Try to parse as double */
    char* endptr;
    double value = strtod(text, &endptr);

    /* Check if entire string was consumed */
    if (endptr == text || *endptr != '\0') {
        return default_value;
    }

    return value;
}

/**
 * Get element text content as float (Public API)
 */
TAURUS_API float taurus_element_text_float(TaurusElement elem, float default_value) {
    const char* text = taurus_element_text(elem);
    if (!text || !text[0]) return default_value;

    /* Try to parse as float */
    char* endptr;
    float value = strtof(text, &endptr);

    /* Check if entire string was consumed */
    if (endptr == text || *endptr != '\0') {
        return default_value;
    }

    return value;
}

/**
 * Get element text content as boolean (Public API)
 */
TAURUS_API int taurus_element_text_bool(TaurusElement elem, int default_value) {
    const char* text = taurus_element_text(elem);
    if (!text || !text[0]) return 0;  /* Empty string is false */

    /* Skip leading whitespace */
    while (*text == ' ' || *text == '\t' || *text == '\n' || *text == '\r') {
        text++;
    }

    /* Skip trailing whitespace */
    size_t len = strlen(text);
    while (len > 0 && (text[len-1] == ' ' || text[len-1] == '\t' || text[len-1] == '\n' || text[len-1] == '\r')) {
        len--;
    }

    /* Case-insensitive comparison for various false values */
    if ((len == 5 && strncasecmp(text, "false", 5) == 0) ||
        (len == 2 && strncasecmp(text, "no", 2) == 0) ||
        (len == 3 && strncasecmp(text, "off", 3) == 0) ||
        (len == 1 && text[0] == '0')) {
        return 0;
    }

    /* Case-insensitive comparison for various true values */
    if ((len == 4 && strncasecmp(text, "true", 4) == 0) ||
        (len == 3 && strncasecmp(text, "yes", 3) == 0) ||
        (len == 2 && strncasecmp(text, "on", 2) == 0) ||
        (len == 1 && text[0] == '1')) {
        return 1;
    }

    /* Any non-empty text that's not explicitly false is true */
    return len > 0 ? 1 : 0;
}

/* ============================================================================
 * Namespace Operations (Public API)
 * ============================================================================ */

/**
 * Get element's active namespace URI
 * In compact mode, namespace is stored inline in the element.
 */
TAURUS_API TaurusNamespace taurus_element_namespace(TaurusElement elem) {
    if (!elem) return NULL;

    /* In compact mode, namespace_uri is cached from namespace_uri_view */
    /* Trigger lazy conversion if needed */
    return taurus_element_get_namespace_uri(elem);
}

/**
 * Get namespace URI from namespace handle
 * In compact mode, TaurusNamespace is the URI string itself.
 */
TAURUS_API const char* taurus_namespace_uri(TaurusNamespace ns) {
    /* In compact mode, TaurusNamespace IS the URI string */
    return ns;
}

/**
 * Get namespace prefix from namespace handle
 * In compact mode, namespaces are inline, so this returns NULL.
 * Use taurus_element_get_prefix() to get an element's prefix.
 */
TAURUS_API const char* taurus_namespace_prefix(TaurusNamespace ns) {
    /* In compact mode, prefix information is stored per-element */
    /* Use taurus_element_get_prefix() instead */
    (void)ns; /* Unused */
    return NULL;
}

/**
 * Resolve namespace prefix with inheritance
 * Walks up the tree to find an element with matching prefix.
 */
TAURUS_API const char* taurus_element_namespace_for_prefix(TaurusElement elem, const char* prefix) {
    if (!elem) return NULL;

    /* Check this element's prefix */
    const char* elem_prefix = taurus_element_get_prefix(elem);
    if (elem_prefix && prefix && strcmp(elem_prefix, prefix) == 0) {
        /* Prefix matches, return this element's namespace URI */
        return taurus_element_get_namespace_uri(elem);
    }

    /* Not found, check parent using compact accessor */
    TaurusElement parent = taurus_element_get_parent(elem);
    if (parent) {
        return taurus_element_namespace_for_prefix(parent, prefix);
    }

    return NULL;
}

/* ============================================================================
 * XPath Functions
 * ============================================================================ */

/**
 * Evaluate XPath expression against document (Public API - 3 parameter version)
 */
TAURUS_API TaurusXPathResult taurus_xpath_eval(
    TaurusDocument doc,
    TaurusElement context,
    const char* expression
) {
    if (!doc || !expression) return NULL;

    /* Use context element if provided, otherwise use root */
    TaurusElement context_elem = context ? context : taurus_document_root(doc);
    if (!context_elem) return NULL;

    /* Call internal implementation with string length */
    size_t expr_len = strlen(expression);

    /* Parse XPath expression */
    XPathParser* parser = xpath_parser_new(expression, expr_len);
    if (!parser) return NULL;

    XPathASTNode* ast = xpath_parse(parser);
    const char* parse_error = xpath_parser_error(parser);

    if (!ast || parse_error) {
        xpath_parser_free(parser);
        return NULL;
    }

    xpath_parser_free(parser);

    /* Create evaluation context with TaurusElement directly - NO CONVERSION! */
    XPathContext* xpath_ctx = xpath_context_new(doc, context_elem);
    if (!xpath_ctx) {
        ast_node_free(ast);
        return NULL;
    }

    /* Evaluate expression */
    struct taurus_xpath_result* result = xpath_evaluate(xpath_ctx, ast);

    /* Check for evaluation errors */
    const char* eval_error = xpath_context_error(xpath_ctx);
    if (eval_error && !result) {
        /* Error already set in context */
    }

    /* Cleanup */
    xpath_context_free(xpath_ctx);
    ast_node_free(ast);

    return result;
}

/**
 * Get XPath result type (Public API wrapper)
 */
TAURUS_API TaurusXPathResultType taurus_xpath_result_type(TaurusXPathResult result) {
    if (!result) return TAURUS_XPATH_STRING;  /* Default to string for NULL */

    /* Map internal XPathResultType to public TaurusXPathResultType */
    switch (result->type) {
        case XPATH_RESULT_NODESET:
            return TAURUS_XPATH_NODESET;
        case XPATH_RESULT_BOOLEAN:
            return TAURUS_XPATH_BOOLEAN;
        case XPATH_RESULT_NUMBER:
            return TAURUS_XPATH_NUMBER;
        case XPATH_RESULT_STRING:
            return TAURUS_XPATH_STRING;
        default:
            return TAURUS_XPATH_STRING;
    }
}

/**
 * Get nodeset size (Public API wrapper)
 */
TAURUS_API size_t taurus_xpath_result_count(TaurusXPathResult result) {
    if (!result || result->type != XPATH_RESULT_NODESET) return 0;
    return result->value.nodeset_value ? result->value.nodeset_value->count : 0;
}

/**
 * Get node from nodeset by index (Public API wrapper)
 */
TAURUS_API TaurusElement taurus_xpath_result_get(TaurusXPathResult result, size_t index) {
    if (!result || result->type != XPATH_RESULT_NODESET) return NULL;
    if (!result->value.nodeset_value || index >= result->value.nodeset_value->count) return NULL;

    void* node = result->value.nodeset_value->nodes[index];

    /* SAFETY: Validate node pointer before returning
     * Stale pointers from previous operations can cause crashes */
    if ((uintptr_t)node < 0x1000) {
        return NULL;  /* Clearly invalid pointer */
    }

    /* Additional safety: check if node type field is valid */
    TaurusNode* typed_node = (TaurusNode*)node;
    if (typed_node->type < TAURUS_NODE_TYPE_ELEMENT ||
        typed_node->type > TAURUS_NODE_TYPE_DOCTYPE) {
        return NULL;  /* Invalid type field - likely stale pointer */
    }

    return (TaurusElement)node;
}

/**
 * Get boolean value (Public API wrapper)
 */
TAURUS_API int taurus_xpath_result_boolean(TaurusXPathResult result) {
    if (!result) return 0;

    switch (result->type) {
        case XPATH_RESULT_BOOLEAN:
            return result->value.boolean_value ? 1 : 0;
        case XPATH_RESULT_NUMBER:
            return (result->value.number_value != 0.0 && !isnan(result->value.number_value)) ? 1 : 0;
        case XPATH_RESULT_STRING:
            return (result->value.string_value && result->value.string_value[0] != '\0') ? 1 : 0;
        case XPATH_RESULT_NODESET:
            return (result->value.nodeset_value && result->value.nodeset_value->count > 0) ? 1 : 0;
        default:
            return 0;
    }
}

/**
 * Get number value (Public API wrapper)
 */
TAURUS_API double taurus_xpath_result_number(TaurusXPathResult result) {
    if (!result) return 0.0 / 0.0;  /* NaN */

    switch (result->type) {
        case XPATH_RESULT_NUMBER:
            return result->value.number_value;
        case XPATH_RESULT_BOOLEAN:
            return result->value.boolean_value ? 1.0 : 0.0;
        case XPATH_RESULT_STRING:
            if (!result->value.string_value || result->value.string_value[0] == '\0') {
                return 0.0 / 0.0;  /* NaN for empty string */
            }
            /* Parse string to number */
            {
                char* endptr;
                double val = strtod(result->value.string_value, &endptr);
                /* Return NaN if not fully parsed */
                return (endptr == result->value.string_value || *endptr != '\0') ? (0.0 / 0.0) : val;
            }
        case XPATH_RESULT_NODESET:
            /* Convert first node to string, then to number */
            if (result->value.nodeset_value && result->value.nodeset_value->count > 0) {
                TaurusElement elem = (TaurusElement)result->value.nodeset_value->nodes[0];
                const char* text = taurus_element_text(elem);
                if (text && text[0] != '\0') {
                    char* endptr;
                    double val = strtod(text, &endptr);
                    return (endptr == text || *endptr != '\0') ? (0.0 / 0.0) : val;
                }
            }
            return 0.0 / 0.0;  /* NaN */
        default:
            return 0.0 / 0.0;  /* NaN */
    }
}

/**
 * Get string value (Public API wrapper)
 */
TAURUS_API char* taurus_xpath_result_string(TaurusXPathResult result) {
    if (!result) return taurus_strdup("");

    switch (result->type) {
        case XPATH_RESULT_STRING:
            return result->value.string_value ? taurus_strdup(result->value.string_value) : taurus_strdup("");
        case XPATH_RESULT_BOOLEAN:
            return taurus_strdup(result->value.boolean_value ? "true" : "false");
        case XPATH_RESULT_NUMBER:
            {
                /* Convert number to string */
                char buffer[64];
                double num = result->value.number_value;

                if (isnan(num)) {
                    return taurus_strdup("NaN");
                } else if (isinf(num)) {
                    return taurus_strdup(num > 0 ? "Infinity" : "-Infinity");
                } else if (num == (long)num) {
                    /* Integer value - no decimal point */
                    snprintf(buffer, sizeof(buffer), "%ld", (long)num);
                } else {
                    /* Decimal value */
                    snprintf(buffer, sizeof(buffer), "%g", num);
                }
                return taurus_strdup(buffer);
            }
        case XPATH_RESULT_NODESET:
            /* Return string value of first node */
            if (result->value.nodeset_value && result->value.nodeset_value->count > 0) {
                const char* text = taurus_element_text((TaurusElement)result->value.nodeset_value->nodes[0]);
                return text ? taurus_strdup(text) : taurus_strdup("");
            }
            return taurus_strdup("");
        default:
            return taurus_strdup("");
    }
}

/* ============================================================================
 * Canonical XML (C14N) Operations
 * ============================================================================ */

/**
 * Helper function to escape text for C14N output
 *
 * C14N 1.0 escaping rules:
 * - In text content: < and & must be escaped
 * - In attribute values: <, &, and " must be escaped
 *
 * Returns a newly allocated string that must be freed
 */
static char* c14n_escape_text(const char* text, int is_attribute_value) {
    if (!text) return NULL;

    /* Count how much space we need
     * C14N spec requires: <, &, \r always escaped; " escaped in attributes */
    size_t len = strlen(text);
    size_t escape_count = 0;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '<') {
            escape_count += 4;  /* &lt; */
        } else if (text[i] == '&') {
            escape_count += 5;  /* &amp; */
        } else if (text[i] == '\r') {
            escape_count += 5;  /* &#xD; (C14N requires \r to be escaped) */
        } else if (is_attribute_value && text[i] == '"') {
            escape_count += 6;  /* &quot; */
        }
    }

    /* If no escaping needed, return a copy */
    if (escape_count == 0) {
        return taurus_strdup(text);
    }

    /* Allocate buffer */
    size_t new_len = len + escape_count;
    char* escaped = (char*)malloc(new_len + 1);
    if (!escaped) return NULL;

    /* Escape characters (C14N spec: <, &, \r always escaped; " escaped in attributes) */
    size_t j = 0;
    for (size_t i = 0; i < len; i++) {
        if (text[i] == '<') {
            memcpy(&escaped[j], "&lt;", 4);
            j += 4;
        } else if (text[i] == '&') {
            memcpy(&escaped[j], "&amp;", 5);
            j += 5;
        } else if (text[i] == '\r') {
            memcpy(&escaped[j], "&#xD;", 5);
            j += 5;
        } else if (is_attribute_value && text[i] == '"') {
            memcpy(&escaped[j], "&quot;", 6);
            j += 6;
        } else {
            escaped[j++] = text[i];
        }
    }
    escaped[j] = '\0';

    return escaped;
}

/**
 * Compare function for sorting attributes lexicographically (for qsort)
 */
static int compare_attributes(const void* a, const void* b) {
    const struct taurus_attribute* attr_a = *(const struct taurus_attribute**)a;
    const struct taurus_attribute* attr_b = *(const struct taurus_attribute**)b;

    /* Get attribute names */
    const char* name_a = !taurus_sv_is_empty(&attr_a->name_view) ? attr_a->name_view.data : attr_a->name;
    const char* name_b = !taurus_sv_is_empty(&attr_b->name_view) ? attr_b->name_view.data : attr_b->name;
    size_t len_a = !taurus_sv_is_empty(&attr_a->name_view) ? attr_a->name_view.length : strlen(attr_a->name);
    size_t len_b = !taurus_sv_is_empty(&attr_b->name_view) ? attr_b->name_view.length : strlen(attr_b->name);

    /* Lexicographic comparison */
    size_t min_len = len_a < len_b ? len_a : len_b;
    int cmp = memcmp(name_a, name_b, min_len);
    if (cmp != 0) return cmp;
    if (len_a < len_b) return -1;
    if (len_a > len_b) return 1;
    return 0;
}

/**
 * Get hash value of element (Public API)
 * Returns a hash value based on the element's memory address
 */
TAURUS_API size_t taurus_element_hash_value(TaurusElement elem) {
    if (!elem) return 0;

    /* Simple hash based on pointer address */
    /* This works because elements are pool-allocated and stable */
    return (size_t)elem;
}

/**
 * Compare function for sorting namespace declarations lexicographically (for qsort)
 */
static int compare_namespaces(const void* a, const void* b) {
    const struct taurus_namespace* ns_a = *(const struct taurus_namespace**)a;
    const struct taurus_namespace* ns_b = *(const struct taurus_namespace**)b;

    /* Compare prefix (xmlns:prefix) or "xmlns" for default namespace */
    const char* prefix_a = ns_a->prefix ? ns_a->prefix : "";
    const char* prefix_b = ns_b->prefix ? ns_b->prefix : "";

    /* Compare prefixes first */
    int cmp = strcmp(prefix_a, prefix_b);
    if (cmp != 0) return cmp;

    /* If prefixes are equal, compare URIs */
    const char* uri_a = ns_a->uri ? ns_a->uri : "";
    const char* uri_b = ns_b->uri ? ns_b->uri : "";
    return strcmp(uri_a, uri_b);
}

/**
 * Recursive helper to serialize element in C14N format
 */
static void c14n_serialize_element(TaurusElement elem, char** buffer, size_t* size, size_t* capacity) {
    if (!elem) return;

    /* Get element name */
    const char* name = taurus_element_get_name(elem);

    /* Get attribute count using accessor */
    uint8_t attr_count = taurus_element_attribute_count(elem);

    /* Allocate temporary array for sorting attributes */
    struct taurus_attribute** sorted_attrs = NULL;
    if (attr_count > 0) {
        sorted_attrs = (struct taurus_attribute**)malloc(attr_count * sizeof(struct taurus_attribute*));
        if (sorted_attrs) {
            /* Copy attributes from linked list to temporary array */
            for (uint8_t i = 0; i < attr_count; i++) {
                sorted_attrs[i] = taurus_element_get_attribute_by_index(elem, i);
            }
            /* Sort attributes lexicographically */
            qsort(sorted_attrs, attr_count, sizeof(struct taurus_attribute*), compare_attributes);
        }
    }

    /* For namespace declarations - TODO: Implement namespace tracking in compact mode */
    /* Currently namespaces are stored in prefix/namespace_uri fields, not as a list */

    /* Append opening tag: <name */
    char temp[4096];
    int len;

    /* Check if element has content (including text nodes) */
    int has_content = 0;
    int has_children = 0;

    TaurusNode* child = (TaurusNode*)elem->first_child;
    while (child) {
        has_children = 1;
        /* Check if child is text or CDATA by checking node type */
        if (child->type == TAURUS_NODE_TYPE_TEXT) {
            const char* text = taurus_text_get_content((TaurusTextNode*)child);
            if (text && *text) {
                has_content = 1;
                break;
            }
        }
        /* Get next sibling based on node type */
        if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
            child = taurus_node_get_next_sibling(child);
        } else if (child->type == TAURUS_NODE_TYPE_TEXT) {
            child = (TaurusNode*)(((TaurusTextNode*)child)->next_sibling);
        } else if (child->type == TAURUS_NODE_TYPE_CDATA) {
            child = (TaurusNode*)(((TaurusCDATANode*)child)->next_sibling);
        } else if (child->type == TAURUS_NODE_TYPE_COMMENT) {
            child = (TaurusNode*)(((TaurusCommentNode*)child)->next_sibling);
        } else if (child->type == TAURUS_NODE_TYPE_PI) {
            child = (TaurusNode*)(((TaurusPINode*)child)->next_sibling);
        } else {
            child = NULL;
        }
    }

    /* Start opening tag */
    len = snprintf(temp, sizeof(temp), "<%s", name ? name : "element");
    APPEND_STRING(temp, len);

    /* Add namespace declarations */
    struct taurus_namespace* ns = elem->namespaces;
    while (ns) {
        /* Serialize namespace as xmlns:prefix="uri" or xmlns="uri" for default */
        if (ns->prefix) {
            len = snprintf(temp, sizeof(temp), " xmlns:%s=\"%s\"", ns->prefix, ns->uri);
        } else {
            len = snprintf(temp, sizeof(temp), " xmlns=\"%s\"", ns->uri);
        }
        APPEND_STRING(temp, len);
        ns = ns->next;
    }

    /* Add sorted attributes */
    if (sorted_attrs) {
        for (size_t i = 0; i < attr_count; i++) {
            struct taurus_attribute* attr = sorted_attrs[i];
            const char* attr_name = !taurus_sv_is_empty(&attr->name_view) ? attr->name_view.data : attr->name;
            size_t attr_name_len = !taurus_sv_is_empty(&attr->name_view) ? attr->name_view.length : strlen(attr->name);

            /* Get attribute value (convert if needed) */
            const char* attr_value = attr->value;
            if (!attr_value && !taurus_sv_is_empty(&attr->value_view)) {
                /* Need to convert StringView to string with entity expansion
                 * Use lenient mode for C14N to handle edge cases like "&" in attributes */
                int old_strict = taurus_get_strict_mode();
                taurus_set_strict_mode(0);  /* Enable lenient mode */
                attr_value = taurus_decode_entities_view(&attr->value_view, NULL);
                taurus_set_strict_mode(old_strict);  /* Restore strict mode */
            }

            if (attr_name && attr_value) {
                /* Escape attribute value for C14N (<, &, and " need escaping) */
                char* escaped_value = c14n_escape_text(attr_value, 1);
                if (escaped_value) {
                    len = snprintf(temp, sizeof(temp), " %.*s=\"%s\"", (int)attr_name_len, attr_name, escaped_value);
                    APPEND_STRING(temp, len);
                    free(escaped_value);
                }

                /* Free temporary value if we created it (wasn't cached) */
                if (attr_value != attr->value && attr_value) {
                    free((void*)attr_value);
                }
            }
        }
    }

    /* Close opening tag */
    if (!has_children && !has_content) {
        /* Empty element: <tag></tag> (NOT <tag/>) */
        len = snprintf(temp, sizeof(temp), "></%s>", name ? name : "element");
        APPEND_STRING(temp, len);
    } else {
        len = snprintf(temp, sizeof(temp), ">");
        APPEND_STRING(temp, len);

        /* Add children - traverse ALL children including text nodes */
        TaurusNode* child = (TaurusNode*)elem->first_child;
        while (child) {
            if (child->type == TAURUS_NODE_TYPE_ELEMENT) {
                c14n_serialize_element((TaurusElement)child, buffer, size, capacity);
                child = taurus_node_get_next_sibling(child);
            } else if (child->type == TAURUS_NODE_TYPE_TEXT) {
                const char* text = taurus_text_get_content((TaurusTextNode*)child);
                if (text) {
                    /* Escape text content for C14N (only < and & need escaping) */
                    char* escaped_text = c14n_escape_text(text, 0);
                    if (escaped_text) {
                        APPEND_STRING(escaped_text, (int)strlen(escaped_text));
                        free(escaped_text);
                    }
                }
                child = (TaurusNode*)(((TaurusTextNode*)child)->next_sibling);
            } else if (child->type == TAURUS_NODE_TYPE_CDATA) {
                /* CDATA content is treated as character data in C14N */
                const char* text = taurus_cdata_get_content((TaurusCDATANode*)child);
                if (text) {
                    /* Escape CDATA content for C14N (same rules as text content) */
                    char* escaped_text = c14n_escape_text(text, 0);
                    if (escaped_text) {
                        APPEND_STRING(escaped_text, (int)strlen(escaped_text));
                        free(escaped_text);
                    }
                }
                child = (TaurusNode*)(((TaurusCDATANode*)child)->next_sibling);
            } else if (child->type == TAURUS_NODE_TYPE_PI) {
                /* Processing Instruction: <?target data?> */
                TaurusPINode* pi = (TaurusPINode*)child;
                const char* target = taurus_pi_get_target(pi);
                const char* data = taurus_pi_get_data(pi);
                if (target) {
                    len = snprintf(temp, sizeof(temp), "<?%s", target);
                    APPEND_STRING(temp, len);
                    if (data && *data) {
                        APPEND_STRING(" ", 1);
                        APPEND_STRING(data, (int)strlen(data));
                    }
                    APPEND_STRING("?>", 2);
                }
                child = (TaurusNode*)(((TaurusPINode*)child)->next_sibling);
            } else if (child->type == TAURUS_NODE_TYPE_COMMENT) {
                /* Comments are NOT included in C14N - skip */
                child = (TaurusNode*)(((TaurusCommentNode*)child)->next_sibling);
            } else {
                /* Unknown node type - stop */
                child = NULL;
            }
        }

        /* Closing tag */
        len = snprintf(temp, sizeof(temp), "</%s>", name ? name : "element");
        APPEND_STRING(temp, len);
    }

    /* Cleanup */
    if (sorted_attrs) free(sorted_attrs);

cleanup:
    return;
}

/**
 * Canonicalize document to C14N format (Public API)
 */
TAURUS_API char* taurus_c14n_canonicalize(struct taurus_document* doc, int version, int flags) {
    (void)version;  /* Version reserved for future C14N 1.1 differences */
    (void)flags;    /* Flags reserved for future use */

    if (!doc) return NULL;

    /* Get root element */
    TaurusElement root = (TaurusElement)doc->new_dom_root;
    if (!root) return NULL;

    /* Allocate initial buffer */
    size_t capacity = 4096;
    size_t size = 0;
    char* buf = (char*)malloc(capacity);
    if (!buf) return NULL;
    buf[0] = '\0';

    /* Add document-level processing instructions before root element */
    for (struct taurus_processing_instruction* pi = doc->pis; pi; pi = pi->next) {
        char temp[1024];
        int len;
        if (pi->target) {
            len = snprintf(temp, sizeof(temp), "<?%s", pi->target);
            while (size + len + 1 > capacity) {
                size_t new_cap = capacity * 2;
                char* new_buf = (char*)realloc(buf, new_cap);
                if (!new_buf) {
                    free(buf);
                    return NULL;
                }
                buf = new_buf;
                capacity = new_cap;
            }
            memcpy(buf + size, temp, len);
            size += len;
            buf[size] = '\0';

            if (pi->data && *pi->data) {
                len = 1;
                while (size + len + 1 > capacity) {
                    size_t new_cap = capacity * 2;
                    char* new_buf = (char*)realloc(buf, new_cap);
                    if (!new_buf) {
                        free(buf);
                        return NULL;
                    }
                    buf = new_buf;
                    capacity = new_cap;
                }
                memcpy(buf + size, " ", len);
                size += len;
                buf[size] = '\0';

                len = (int)strlen(pi->data);
                while (size + len + 1 > capacity) {
                    size_t new_cap = capacity * 2;
                    char* new_buf = (char*)realloc(buf, new_cap);
                    if (!new_buf) {
                        free(buf);
                        return NULL;
                    }
                    buf = new_buf;
                    capacity = new_cap;
                }
                memcpy(buf + size, pi->data, len);
                size += len;
                buf[size] = '\0';
            }
            len = 2;
            while (size + len + 1 > capacity) {
                size_t new_cap = capacity * 2;
                char* new_buf = (char*)realloc(buf, new_cap);
                if (!new_buf) {
                    free(buf);
                    return NULL;
                }
                buf = new_buf;
                capacity = new_cap;
            }
            memcpy(buf + size, "?>", len);
            size += len;
            buf[size] = '\0';
        }
    }

    /* Serialize document in C14N format */
    char* buffer = buf;
    c14n_serialize_element(root, &buffer, &size, &capacity);
    buf = buffer;

    /* Note: Line ending normalization is done during parsing per XML 1.0 spec.
     * Any remaining \r in the document should be escaped as &#xD; by the parser.
     * We don't do additional line ending transformation here. */
    buf[size] = '\0';

    return buf;
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
 * Memory Allocation Hooks (for testing and custom allocators)
 * ============================================================================ */

/* Global custom allocation functions (NULL = use malloc/free) */
/* Custom allocator hooks.
 *
 * TODO 27 phase 1: thread-local, so each thread can have its own
 * allocator without cross-contamination.  Phase 2 will move these
 * to per-document scope (set at parse time, propagated via the
 * document struct). */
static __thread taurus_allocation_function g_alloc_function = NULL;
static __thread taurus_deallocation_function g_dealloc_function = NULL;

/**
 * Set custom memory management functions
 */
TAURUS_API void taurus_set_memory_management_functions(taurus_allocation_function alloc_function,
                                                         taurus_deallocation_function dealloc_function) {
    g_alloc_function = alloc_function;
    g_dealloc_function = dealloc_function;
}

/**
 * Per-document allocator hooks (TODO 74).
 *
 * Set the allocator hooks for a specific document.  Must be called
 * before parsing.  The document's pool is created with these hooks
 * and uses them for all allocations within the document.
 */
TAURUS_API TaurusStatus taurus_document_set_allocators(
    TaurusDocument doc,
    taurus_allocation_function alloc,
    taurus_deallocation_function dealloc) {
    if (!doc) return TAURUS_ERROR_NULL_ARG;
    doc->alloc_hook = alloc;
    doc->dealloc_hook = dealloc;
    return TAURUS_OK;
}

/**
 * Get current memory allocation function
 */
TAURUS_API taurus_allocation_function taurus_get_memory_allocation_function(void) {
    return g_alloc_function;
}

/**
 * Get current memory deallocation function
 */
TAURUS_API taurus_deallocation_function taurus_get_memory_deallocation_function(void) {
    return g_dealloc_function;
}

/* Wrapper functions used internally for custom allocation */
void* taurus_alloc_hook(size_t size) {
    if (g_alloc_function) {
        return g_alloc_function(size);
    }
    return malloc(size);
}

void taurus_free_hook(void* ptr) {
    if (g_dealloc_function) {
        g_dealloc_function(ptr);
    } else {
        free(ptr);
    }
}

/**
 * Free XPath result
 */
TAURUS_API void taurus_xpath_result_free(struct taurus_xpath_result* result) {
    /* Use internal xpath_result_free from evaluator */
    xpath_result_free(result);
}

/* ============================================================================
 * XPath Variables (XPath 1.0)
 * ============================================================================ */

/**
 * Create a new variable set
 */
TAURUS_API TaurusXPathVariableSet taurus_xpath_variable_set_new(void) {
    return (TaurusXPathVariableSet)xpath_variable_set_new();
}

/**
 * Free a variable set
 */
TAURUS_API void taurus_xpath_variable_set_free(TaurusXPathVariableSet set) {
    xpath_variable_set_free((XPathVariableSet*)set);
}

/**
 * Add a boolean variable to the set
 */
TAURUS_API TaurusStatus taurus_xpath_variable_set_boolean(TaurusXPathVariableSet set, const char* name, int value) {
    if (!set || !name) return TAURUS_ERROR_NULL_ARG;

    XPathVariable* var = xpath_variable_set_add((XPathVariableSet*)set, name, XPATH_VAR_TYPE_BOOLEAN);
    if (!var) return TAURUS_ERROR_MEMORY;

    if (!xpath_variable_set_boolean(var, value)) {
        return TAURUS_ERROR_INVALID_ARG;
    }

    return TAURUS_OK;
}

/**
 * Add a number variable to the set
 */
TAURUS_API TaurusStatus taurus_xpath_variable_set_number(TaurusXPathVariableSet set, const char* name, double value) {
    if (!set || !name) return TAURUS_ERROR_NULL_ARG;

    XPathVariable* var = xpath_variable_set_add((XPathVariableSet*)set, name, XPATH_VAR_TYPE_NUMBER);
    if (!var) return TAURUS_ERROR_MEMORY;

    if (!xpath_variable_set_number(var, value)) {
        return TAURUS_ERROR_INVALID_ARG;
    }

    return TAURUS_OK;
}

/**
 * Add a string variable to the set
 */
TAURUS_API TaurusStatus taurus_xpath_variable_set_string(TaurusXPathVariableSet set, const char* name, const char* value) {
    if (!set || !name) return TAURUS_ERROR_NULL_ARG;

    XPathVariable* var = xpath_variable_set_add((XPathVariableSet*)set, name, XPATH_VAR_TYPE_STRING);
    if (!var) return TAURUS_ERROR_MEMORY;

    if (!xpath_variable_set_string(var, value)) {
        return TAURUS_ERROR_MEMORY;
    }

    return TAURUS_OK;
}

/**
 * Evaluate XPath expression with variables
 */
TAURUS_API TaurusXPathResult taurus_xpath_eval_with_vars(
    TaurusDocument doc,
    const char* expression,
    TaurusXPathVariableSet variables)
{
    if (!doc || !expression) {
        return NULL;
    }

    /* Parse XPath expression */
    XPathParser* parser = xpath_parser_new(expression, strlen(expression));
    if (!parser) return NULL;

    XPathASTNode* ast = xpath_parse(parser);
    const char* parse_error = xpath_parser_error(parser);

    if (!ast || parse_error) {
        xpath_parser_free(parser);
        return NULL;
    }

    xpath_parser_free(parser);

    /* Create evaluation context with TaurusElement directly - NO CONVERSION! */
    XPathContext* xpath_ctx = xpath_context_new(doc, taurus_document_root(doc));
    if (!xpath_ctx) {
        ast_node_free(ast);
        return NULL;
    }

    /* Set variable set in context */
    xpath_ctx->variable_set = variables;

    /* Evaluate expression */
    struct taurus_xpath_result* result = xpath_evaluate(xpath_ctx, ast);

    /* Check for evaluation errors */
    const char* eval_error = xpath_context_error(xpath_ctx);
    if (eval_error && !result) {
        /* Error already set in context */
    }

    /* Cleanup */
    xpath_context_free(xpath_ctx);
    ast_node_free(ast);

    return result;
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
     * taurus_document_finalize_strings; force the pooled path so we
     * never calloc. */
    if (!elem->name && !taurus_sv_is_empty(&elem->name_view)) {
        elem->name = taurus_sv_to_cstr_pooled(&elem->name_view, pool);
    }
    if (!elem->namespace_uri && !taurus_sv_is_empty(&elem->namespace_uri_view)) {
        elem->namespace_uri = taurus_sv_to_cstr_pooled(&elem->namespace_uri_view, pool);
    }
    if (!elem->prefix && !taurus_sv_is_empty(&elem->prefix_view)) {
        elem->prefix = taurus_sv_to_cstr_pooled(&elem->prefix_view, pool);
    }

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
        if (!attr->prefix && !taurus_sv_is_empty(&attr->prefix_view)) {
            if ((uintptr_t)attr->prefix_view.data >= 0x1000) {
                attr->prefix = taurus_sv_to_cstr_pooled(&attr->prefix_view, pool);
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

/* ============================================================================
 * Node API - Public functions for node iteration and content access
 * ============================================================================ */

/**
 * Get node type
 */
TAURUS_API int taurus_node_get_type(TaurusNodeRef node) {
    if (!node) return 0; /* TAURUS_NODE_TYPE_ELEMENT */
    return (int)node->type;
}

/**
 * Get first child node (any type)
 */
TAURUS_API TaurusNodeRef taurus_node_first_child(TaurusNodeRef node) {
    if (!node) return NULL;
    if (node->type == TAURUS_NODE_TYPE_ELEMENT) { /* TAURUS_NODE_TYPE_ELEMENT */
        TaurusElement elem = (TaurusElement)node;
        return (TaurusNodeRef)elem->first_child;
    }
    return NULL;
}

/**
 * Get last child node (any type)
 */
TAURUS_API TaurusNodeRef taurus_node_last_child(TaurusNodeRef node) {
    if (!node) return NULL;
    if (node->type == TAURUS_NODE_TYPE_ELEMENT) { /* TAURUS_NODE_TYPE_ELEMENT */
        TaurusElement elem = (TaurusElement)node;
        return (TaurusNodeRef)elem->last_child;
    }
    return NULL;
}

/**
 * Get next sibling node (any type)
 */
TAURUS_API TaurusNodeRef taurus_node_next_sibling(TaurusNodeRef node) {
    if (!node) return NULL;
    return (TaurusNodeRef)taurus_node_get_next_sibling(node);
}

/**
 * Get previous sibling node (any type)
 */
TAURUS_API TaurusNodeRef taurus_node_previous_sibling(TaurusNodeRef node) {
    if (!node) return NULL;
    if (node->type == TAURUS_NODE_TYPE_ELEMENT) { /* TAURUS_NODE_TYPE_ELEMENT */
        TaurusElement elem = (TaurusElement)node;
        TaurusElement parent = elem->parent;
        if (!parent) return NULL;
        
        TaurusNodeRef prev = NULL;
        TaurusNodeRef child = (TaurusNodeRef)parent->first_child;
        while (child && child != node) {
            prev = child;
            child = (TaurusNodeRef)taurus_node_get_next_sibling(child);
        }
        return prev;
    }
    return NULL;
}

/**
 * Get child count (all node types)
 */
TAURUS_API size_t taurus_node_child_count(TaurusNodeRef node) {
    if (!node) return 0;
    return taurus_node_child_count_internal(node);
}

/**
 * Cast node to element (if node is an element)
 */
TAURUS_API TaurusElement taurus_node_as_element(TaurusNodeRef node) {
    if (!node || node->type != TAURUS_NODE_TYPE_ELEMENT) return NULL;
    return (TaurusElement)node;
}

/**
 * Cast element to its base node handle.
 *
 * Every element begins with a TaurusNode header, so the cast is always
 * safe.  This is the inverse of taurus_node_as_element().
 */
TAURUS_API TaurusNodeRef taurus_element_as_node(TaurusElement elem) {
    return (TaurusNodeRef)elem;
}

/**
 * Get text content from text node
 */
TAURUS_API const char* taurus_text_node_get_content(TaurusNodeRef node) {
    if (!node) return NULL;
    if (node->type == TAURUS_NODE_TYPE_TEXT) { /* TAURUS_NODE_TYPE_TEXT */
        return ((TaurusTextNode*)node)->content;
    }
    if (node->type == TAURUS_NODE_TYPE_CDATA) { /* TAURUS_NODE_TYPE_CDATA */
        return ((TaurusCDATANode*)node)->content;
    }
    return NULL;
}

/**
 * Get comment content
 */
TAURUS_API const char* taurus_comment_node_get_content(TaurusNodeRef node) {
    if (!node || node->type != TAURUS_NODE_TYPE_COMMENT) return NULL;
    return ((TaurusCommentNode*)node)->content;
}

/**
 * Get CDATA content
 */
TAURUS_API const char* taurus_cdata_node_get_content(TaurusNodeRef node) {
    if (!node || node->type != TAURUS_NODE_TYPE_CDATA) return NULL;
    return ((TaurusCDATANode*)node)->content;
}

/**
 * Get processing instruction target
 */
TAURUS_API const char* taurus_pi_node_get_target(TaurusNodeRef node) {
    if (!node || node->type != TAURUS_NODE_TYPE_PI) return NULL;
    return ((TaurusPINode*)node)->target;
}

/**
 * Get processing instruction data
 */
TAURUS_API const char* taurus_pi_node_get_data(TaurusNodeRef node) {
    if (!node || node->type != TAURUS_NODE_TYPE_PI) return NULL;
    return ((TaurusPINode*)node)->data;
}
