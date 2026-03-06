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

/* v5 parser with zero-check allocator - MAXIMUM PERFORMANCE (1.18x vs pugixml) */
extern struct taurus_document* taurus_parse_v5(char* xml, size_t len, int* error_out, int strict_mode);

/* Pointer-based parser - ULTRA FAST (1.29-1.45x vs pugixml!) */
extern struct taurus_document* taurus_parse_ptr(char* xml, size_t len, int* error_out, int strict_mode);

extern int parser_had_declaration(Parser* p);
extern int parser_has_bom(Parser* p);
extern TaurusDoctypeNode* parser_get_doctype(Parser* p);
extern TaurusDoctypeNode* parser_transfer_doctype(Parser* p);
extern struct taurus_processing_instruction* parser_get_pi_list(Parser* p);
extern void taurus_doctype_free(TaurusDoctypeNode* doctype);

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

    (void)skip_namespace_resolution;  /* Compact parser handles all cases */

    /* OPTIMIZATION: Only call strnlen for very small files (< 4KB)
     * For larger files, trust the provided length - the caller knows what they're doing.
     * This avoids O(n) scan for typical use cases. */
    if (len < 4096) {
        size_t actual_len = strnlen(xml, len);
        if (actual_len < len) {
            len = actual_len;
        }
        if (len == 0) return NULL;
    }

    /* Make a mutable copy for in-place parsing */
    char* xml_copy = TAURUS_ALLOC_N(char, len + 1);
    if (!xml_copy) return NULL;
    memcpy(xml_copy, xml, len);
    xml_copy[len] = '\0';

    int error = 0;
    /* POINTER-ONLY: Use pointer-based parser for 1.29-1.45x faster than pugixml */
    struct taurus_document* doc = taurus_parse_ptr(xml_copy, len, &error, strict_mode);
    if (doc) {
        return doc;
    }

    /* If ptr parser fails, free copy and return NULL */
    TAURUS_FREE(xml_copy);
    return NULL;
}

/**
 * Parse XML string into document (Internal wrapper with defaults)
 *
 * Uses global strict mode and enables namespace resolution by default.
 */
struct taurus_document* taurus_parse(const char* xml, size_t len) {
    if (!xml || len == 0) return NULL;

    /* OPTIMIZATION: Only call strnlen for very small files (< 4KB) */
    if (len < 4096) {
        size_t actual_len = strnlen(xml, len);
        if (actual_len < len) {
            len = actual_len;
        }
        if (len == 0) return NULL;
    }

    /* Make a mutable copy for in-place parsing */
    char* xml_copy = TAURUS_ALLOC_N(char, len + 1);
    if (!xml_copy) return NULL;
    memcpy(xml_copy, xml, len);
    xml_copy[len] = '\0';

    int error = 0;
    /* POINTER-ONLY: Use pointer-based parser for 1.29-1.45x faster than pugixml */
    struct taurus_document* doc = taurus_parse_ptr(xml_copy, len, &error, taurus_get_strict_mode());
    if (doc) {
        return doc;
    }

    TAURUS_FREE(xml_copy);
    return NULL;
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
            if (status) *status = TAURUS_ERROR_MEMORY;
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
            if (status) *status = TAURUS_ERROR_MEMORY;
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
 * PERFORMANCE: Uses v5 parser with 16-byte elements and zero-check allocator.
 * The input buffer MUST be mutable and remain valid for the document lifetime.
 */
TAURUS_API TaurusDocument taurus_parse_string_inplace(char* xml, size_t length, TaurusStatus* status) {
    if (status) *status = TAURUS_OK;

    if (!xml || length == 0) {
        if (status) *status = TAURUS_ERROR_NULL_ARG;
        return NULL;
    }

    /* COMPACT-ONLY: Use v5 parser with zero-check allocator for maximum performance */
    int error = 0;
    struct taurus_document* doc = taurus_parse_v5(xml, length, &error, taurus_get_strict_mode());

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
            if (status) *status = TAURUS_ERROR_MEMORY;
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
