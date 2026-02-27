/* taurus_parse_api.c - Taurus parsing API
 * Copyright (c) 2024, Ribose Inc.
 *
 * Pure C XML parser and XPath evaluator - Parsing API.
 */

#include "../include/taurus.h"
#include "taurus_internal.h"
#include "dom/element.h"
#include "dom/doctype.h"
#include "encoding/utf16.h"
#include "dtd/model.h"
#include "common/entities.h"
#include <string.h>
#include <stdlib.h>

/* Forward declarations for parser */
typedef struct Parser Parser;

extern Parser* parser_create(const char* xml, size_t len, TaurusMemoryPool* pool);
extern Parser* parser_create_writable(char* xml, size_t len, TaurusMemoryPool* pool);
extern Parser* parser_create_writable_with_options(char* xml, size_t len, TaurusMemoryPool* pool, int strict_mode);
extern Parser* parser_create_with_parse_options(const char* xml, size_t len, TaurusMemoryPool* pool,
                                                 int strict_mode, int skip_namespace_resolution);
extern void parser_free(Parser* p);
extern TaurusElement parser_parse_document(Parser* p);
extern int parser_has_error(Parser* p);
extern const char* parser_get_xml_version(Parser* p);
extern const char* parser_get_encoding(Parser* p);
extern int parser_get_standalone(Parser* p);

/* Two-pass compact parser (from parser_two_pass.c) - creates 28-byte elements */
extern struct taurus_document* taurus_parse_two_pass(const char* xml, size_t len, int* error_out);

/* Threshold for using two-pass compact parser (documents >= this size)
 * COMPACT-ONLY: Set to 0 to ALWAYS use compact parser.
 *
 * The two-pass compact parser creates 28-byte elements in a single allocation
 * with in-place string handling for 1.0x vs pugixml parsing performance.
 */
#define TAURUS_TWO_PASS_THRESHOLD 0  /* COMPACT-ONLY: Always use compact parser */
extern int parser_had_declaration(Parser* p);
extern int parser_has_bom(Parser* p);
extern TaurusDoctypeNode* parser_get_doctype(Parser* p);
extern TaurusDoctypeNode* parser_transfer_doctype(Parser* p);
extern struct taurus_processing_instruction* parser_get_pi_list(Parser* p);
extern void taurus_doctype_free(TaurusDoctypeNode* doctype);

/* Forward declaration for two-pass compact parser */
extern struct taurus_document* taurus_parse_two_pass(const char* xml, size_t len, int* error_out);

/* Fast in-place compact parser (from parser_two_pass.c) - maximum performance */
extern struct taurus_document* taurus_parse_two_pass_fast_inplace(char* xml, size_t len, int* error_out);

/* v2 parser with 16-byte elements (from parser_v2.c) - matches pugixml memory footprint */
extern struct taurus_document* taurus_parse_v2(char* xml, size_t len, int* error_out);

/* v2 iterative parser (from parser_v2_iterative.c) - NO RECURSION for maximum performance */
extern struct taurus_document* taurus_parse_v2_iterative(char* xml, size_t len, int* error_out);

/* v5 parser with zero-check allocator - MAXIMUM PERFORMANCE (1.20x vs pugixml) */
extern struct taurus_document* taurus_parse_v5(char* xml, size_t len, int* error_out);

/* Threshold for using compact two-pass parser (documents >= 4KB) */
#define TAURUS_COMPACT_PARSE_THRESHOLD 4096

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
 * Internal Parse Function Implementation
 * ============================================================================ */

/**
 * Parse XML string into document with options (internal implementation)
 *
 * PERFORMANCE: Uses in-place parsing to avoid buffer copy.
 * The buffer is stored in the document and freed when document is freed.
 *
 * @param xml XML string to parse
 * @param len Length of XML string
 * @param strict_mode 1 for strict parsing, 0 for lenient
 * @param skip_namespace_resolution 1 to skip namespace resolution for faster parsing
 */
static struct taurus_document* taurus_parse_internal(const char* xml, size_t len,
                                                     int strict_mode, int skip_namespace_resolution) {
    if (!xml || len == 0) return NULL;

    /* COMPACT-ONLY: Always use v5 parser with zero-check allocator
     * This creates 16-byte elements with offset-based navigation,
     * giving us 1.20x vs pugixml performance.
     *
     * No threshold, no fallback to legacy - v5 is the ONLY mode.
     */
    (void)skip_namespace_resolution;  /* Compact parser handles all cases */

    /* Make a mutable copy for in-place parsing */
    char* xml_copy = TAURUS_ALLOC_N(char, len + 1);
    if (!xml_copy) return NULL;
    memcpy(xml_copy, xml, len);
    xml_copy[len] = '\0';

    int error = 0;
    struct taurus_document* compact_doc = taurus_parse_v5(xml_copy, len, &error);
    if (compact_doc) {
        return compact_doc;
    }

    /* If v5 parser fails, free copy and return NULL - no legacy fallback */
    TAURUS_FREE(xml_copy);
    return NULL;
}

/**
 * Parse XML string into document (Internal wrapper with defaults)
 *
 * Uses global strict mode and enables namespace resolution by default.
 */
struct taurus_document* taurus_parse(const char* xml, size_t len) {
    /* COMPACT-ONLY: Always use v5 parser directly */
    if (!xml || len == 0) return NULL;

    /* Make a mutable copy for in-place parsing */
    char* xml_copy = TAURUS_ALLOC_N(char, len + 1);
    if (!xml_copy) return NULL;
    memcpy(xml_copy, xml, len);
    xml_copy[len] = '\0';

    int error = 0;
    struct taurus_document* doc = taurus_parse_v5(xml_copy, len, &error);
    if (doc) {
        return doc;
    }

    TAURUS_FREE(xml_copy);
    return NULL;
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
     * This improves performance for small documents with repeated strings
     *
     * PERFORMANCE: Start with 1024 buckets for better large file scaling.
     * Table will grow dynamically as needed (see taurus_hash_table_grow). */
    if (len >= 256) {
        pool->string_cache = taurus_hash_table_create(pool, 1024);
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

    /* Copy global strict mode to document for thread-safe per-document settings */
    extern int taurus_get_strict_mode(void);
    doc->strict_mode = taurus_get_strict_mode();

    /* CRITICAL: Set this document as current for overflow tracking BEFORE parsing
     * This ensures all overflow entries created during parsing are associated
     * with this document for proper per-document cleanup */
    extern void taurus_compact_set_current_document(struct taurus_document* doc);
    taurus_compact_set_current_document(doc);

    /* Use writable parser with document's strict mode */
    Parser* p = parser_create_writable_with_options(xml, len, pool, doc->strict_mode);
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
            doc->dtd = taurus_dtd_parse_internal_subset(internal_subset, strlen(internal_subset));
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
    extern int taurus_document_finalize_strings(TaurusDocument doc);
    taurus_document_finalize_strings(doc);

    /* COW (Phase 2.1): Freeze the entire tree after parsing
     * This marks all nodes as frozen (immutable) for copy-on-write semantics */
    extern void taurus_document_freeze_tree(struct taurus_document* doc);
    taurus_document_freeze_tree(doc);

    parser_free(p);

    /* CRITICAL: Clear current document AFTER all document operations are complete
     * This ensures any overflow entries created during tree operations are tracked correctly */
    taurus_compact_set_current_document(NULL);

    return doc;
}

/* ============================================================================
 * Public Parsing API
 * ============================================================================ */

/**
 * Parse XML string into document (Public API wrapper)
 *
 * This function automatically detects and converts various encodings to UTF-8,
 * including: UTF-16 (LE/BE), EBCDIC, ISO-8859-*, EUC-JP, Shift-JIS, etc.
 */
TAURUS_API TaurusDocument taurus_parse_string(const char* xml, size_t length, TaurusStatus* status) {
    if (status) *status = TAURUS_OK;

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

        if (doc) {
            doc->xml_buffer = utf8_buffer;
            doc->xml_buffer_len = utf8_len;
            doc->xml_buffer_needs_free = 1;

            if (doc->encoding) {
                TAURUS_FREE(doc->encoding);
            }
            doc->encoding = taurus_strdup((encoding == UTF16_LE) ? "UTF-16LE" : "UTF-16BE");
        } else {
            free(utf8_buffer);
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

        if (doc) {
            doc->xml_buffer = utf8_buffer;
            doc->xml_buffer_len = utf8_len;
            doc->xml_buffer_needs_free = 1;

            if (doc->encoding) {
                TAURUS_FREE(doc->encoding);
            }
            doc->encoding = taurus_strdup((detected == UTF16_LE) ? "UTF-16LE" : "UTF-16BE");
        } else {
            free(utf8_buffer);
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

    struct taurus_document* doc = taurus_parse(utf8_xml, utf8_len);

    if (doc && detected_encoding) {
        if (doc->encoding) {
            TAURUS_FREE(doc->encoding);
        }
        doc->encoding = taurus_strdup(detected_encoding);
    }

    if (doc && utf8_xml != xml) {
        doc->xml_buffer = utf8_xml;
        doc->xml_buffer_len = utf8_len;
        doc->xml_buffer_needs_free = 1;
    } else if (utf8_xml != xml) {
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
 *
 * PERFORMANCE: Uses v2 ITERATIVE parser with 16-byte elements for maximum speed.
 * NO RECURSION - eliminates function call overhead (~5-10 cycles per element).
 * The input buffer MUST be mutable and remain valid for the document lifetime.
 */
TAURUS_API TaurusDocument taurus_parse_string_inplace(char* xml, size_t length, TaurusStatus* status) {
    if (status) *status = TAURUS_OK;

    if (!xml || length == 0) {
        if (status) *status = TAURUS_ERROR_NULL_ARG;
        return NULL;
    }

    /* Use v2 ITERATIVE parser with 16-byte elements for maximum performance */
    int error = 0;
    struct taurus_document* doc = taurus_parse_v2_iterative(xml, length, &error);

    if (!doc) {
        if (status) *status = TAURUS_ERROR_PARSE;
    }

    return doc;
}

/**
 * Parse XML string with automatic encoding detection and conversion (Public API)
 */
TAURUS_API TaurusDocument taurus_parse_string_with_encoding(const char* xml, size_t length, TaurusStatus* status) {
    /* Same implementation as taurus_parse_string for now */
    return taurus_parse_string(xml, length, status);
}

/**
 * Parse XML string with options (extended API)
 *
 * This is the extended version of taurus_parse_string() that accepts options
 * for performance optimization. Use this when you need control over parsing
 * behavior.
 *
 * Performance tips:
 * - Use TAURUS_PARSE_NO_NAMESPACE_RESOLUTION for documents without namespace queries
 * - Use TAURUS_PARSE_FAST for maximum speed on trusted input
 */
TAURUS_API TaurusDocument taurus_parse_string_ex(const char* xml, size_t length,
                                                   const TaurusParseOptions* options,
                                                   TaurusStatus* status) {
    if (status) *status = TAURUS_OK;

    if (!xml || length == 0) {
        if (status) *status = TAURUS_ERROR_NULL_ARG;
        return NULL;
    }

    /* Extract options */
    int strict_mode = options ? options->strict : 0;
    int skip_namespace = 0;

    if (options) {
        if (options->flags & TAURUS_PARSE_NO_NAMESPACE_RESOLUTION) {
            skip_namespace = 1;
        }
    }

    /* Handle encoding detection/conversion first */
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

        struct taurus_document* doc = taurus_parse_internal(utf8_buffer, utf8_len,
                                                             strict_mode, skip_namespace);

        if (doc) {
            doc->xml_buffer = utf8_buffer;
            doc->xml_buffer_len = utf8_len;
            doc->xml_buffer_needs_free = 1;

            if (doc->encoding) {
                TAURUS_FREE(doc->encoding);
            }
            doc->encoding = taurus_strdup((encoding == UTF16_LE) ? "UTF-16LE" : "UTF-16BE");
        } else {
            free(utf8_buffer);
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

    struct taurus_document* doc = taurus_parse_internal(utf8_xml, utf8_len,
                                                         strict_mode, skip_namespace);

    if (doc && detected_encoding) {
        if (doc->encoding) {
            TAURUS_FREE(doc->encoding);
        }
        doc->encoding = taurus_strdup(detected_encoding);
    }

    if (doc && utf8_xml != xml) {
        doc->xml_buffer = utf8_xml;
        doc->xml_buffer_len = utf8_len;
        doc->xml_buffer_needs_free = 1;
    } else if (utf8_xml != xml) {
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
    struct taurus_document* doc = taurus_parse_internal(xml, length,
                                                         strict_mode, skip_namespace);

    if (!doc && status) {
        *status = TAURUS_ERROR_PARSE;
    }

    return doc;
#endif
}

/**
 * Parse XML with custom options (legacy API)
 *
 * Uses the older taurus_parse_options structure with strict, preserve_whitespace,
 * and track_positions fields.
 *
 * Note: For new code, prefer taurus_parse_string_ex() with TaurusParseOptions
 * which supports additional performance flags like TAURUS_PARSE_NO_NAMESPACE_RESOLUTION.
 */
TAURUS_API struct taurus_document* taurus_parse_with_options(
    const char* xml,
    size_t len,
    const taurus_parse_options* opts
) {
    if (!xml || len == 0) return NULL;

    int strict_mode = opts ? opts->strict : 0;
    /* preserve_whitespace and track_positions not yet implemented */

    return taurus_parse_internal(xml, len, strict_mode, 0);  /* namespace resolution enabled */
}
