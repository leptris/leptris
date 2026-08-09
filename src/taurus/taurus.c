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
#include "dom/element_index.h"
#include "dom/node.h"
#include "dom/text.h"
#include "dom/comment.h"
#include "dom/cdata.h"
#include "dom/pi.h"
#include "dom/doctype.h"
#include "encoding/utf16.h"
#include "dtd/model.h"
#include "flat/flat_doc.h"
#include "flat/flat_parser.h"
#include "flat/flat_promote.h"
#include "common/entities.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <stdio.h>
#include <ctype.h>

/* Thread-local globals defined in core.c — extern so taurus_parse can
 * read the strict-mode default at document creation time. */
extern __thread int g_taurus_strict_mode;
extern __thread int g_taurus_max_depth;


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
/* Legacy parser (parser_new.c) deleted — direct_parse + flat_parse
 * cover the full XML feature set. These extern declarations are
 * retained as a marker; the functions no longer exist. */
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
/* TODO 139 Phase D: detect DOCTYPE with internal subset.
 *
 * The flat parser silently strips DTD entity declarations. If the
 * input has `<!DOCTYPE ... [...]>`, we must route to the legacy
 * parser so entities expand correctly. The check scans up to 4 KB
 * of input — sufficient for any reasonable DOCTYPE declaration.
 *
 * Returns 1 if the input has a DOCTYPE with internal subset, 0
 * otherwise (including no DOCTYPE at all). */
static int taurus_input_has_internal_dtd_subset(const char* xml, size_t len) {
    size_t scan = len < 4096 ? len : 4096;
    /* Find "<!DOCTYPE". */
    for (size_t i = 0; i + 9 <= scan; i++) {
        if (xml[i] == '<' && xml[i+1] == '!' &&
            memcmp(xml + i + 2, "DOCTYPE", 7) == 0) {
            /* Walk forward to the matching '>', tracking '[' depth. */
            size_t j = i + 9;
            int seen_bracket = 0;
            while (j < len) {
                char c = xml[j++];
                if (c == '[') seen_bracket = 1;
                else if (c == ']' && seen_bracket) {
                    /* Continue to the closing '>'. */
                } else if (c == '>') return seen_bracket ? 1 : 0;
                if (j > i + 65536) break;  /* sanity bound */
            }
            return 0;
        }
    }
    return 0;
}

TAURUS_API struct taurus_document* taurus_parse(const char* xml, size_t len) {
    if (!xml || len == 0) return NULL;

    /* Fast path: try direct_parse first. It now handles DTD internal
     * subsets (custom entity declarations) via taurus_dtd_parse_-
     * internal_subset, so the DTD gate is removed. The flat_parse
     * + lazy promote fallback remains for inputs direct_parse
     * rejects (malformed constructs, edge cases).
     *
     * Predefined entities (&amp;, &lt;, etc.) expand lazily via
     * taurus_text_get_content / attr accessor. Custom entities
     * (&foo; from DTD) expand eagerly via DTD-aware decoder.
     *
     * direct_parse respects g_taurus_max_depth when set by the
     * caller (custom depth limit). No separate parser is needed. */
    {
        /* TODO 147 Phase A: try the single-pass direct parser first.
         * It produces a TaurusElement tree directly — no FlatDoc
         * intermediate, no promote pass. Falls back to flat_parse +
         * lazy promote on failure. */
        extern struct taurus_document* direct_parse(const char*, size_t);
        struct taurus_document* doc = direct_parse(xml, len);
        if (doc) return doc;
        /* Direct parse failed — try flat_parse + lazy promote. */
        FlatDoc* flat = flat_parse(xml, len);
        if (flat) {
            /* The caller's buffer may be transient — UTF-16/iconv
             * conversion paths in taurus_parse_string free their
             * utf8_buffer as soon as taurus_parse returns. Take
             * ownership of a private copy so the FlatDoc outlives
             * the input. */
            if (flat_doc_dup_xml(flat) != 0) {
                flat_doc_free(flat);
                flat = NULL;
            }
        }
        if (flat) {
            struct taurus_document* doc =
                (struct taurus_document*)malloc(sizeof(*doc));
            if (!doc) {
                flat_doc_free(flat);
                return NULL;
            }
            memset(doc, 0, sizeof(*doc));
            doc->strict_mode = g_taurus_strict_mode;
            doc->ref_count = 1;
            doc->flat_doc = flat;
            doc->flat_promoted = 0;
            return doc;
        }
        /* Flat parse failed — malformed input or unsupported
         * construct. direct_parse and flat_parse cover the full
         * XML feature set (elements, attrs, text, CDATA, comments,
         * PIs, DTD entities, namespaces, predefined entities). If
         * both reject the input, it is genuinely unparseable. */
        return NULL;
    }
}

/**
 * Parse XML string into document with in-place optimization (internal implementation)
 *
 * The caller-owned writable buffer is passed to taurus_parse, which
 * copies it into the document's xml_buffer (direct_parse needs a
 * writable copy for in-place NUL termination). The original caller
 * buffer is not freed by the document.
 */
static struct taurus_document* taurus_parse_inplace(char* xml, size_t len) {
    return taurus_parse(xml, len);
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

TAURUS_API TaurusElement taurus_parse_fragment(const char* xml,
                                                size_t length,
                                                TaurusDocument dest_doc,
                                                TaurusStatus* status) {
    if (status) *status = TAURUS_OK;
    if (!xml || length == 0 || !dest_doc) {
        if (status) *status = TAURUS_ERROR_NULL_ARG;
        return NULL;
    }

    /* Wrap the fragment in a synthetic root so the parser sees a
     * well-formed single-rooted document. Use a name with characters
     * that can never appear in real XML names so collisions are
     * impossible. */
    static const char frag_open[] = "<__taurus_frag__>";
    static const char frag_close[] = "</__taurus_frag__>";
    size_t open_len = sizeof(frag_open) - 1;
    size_t close_len = sizeof(frag_close) - 1;

    size_t total = open_len + length + close_len;
    char* wrapped = (char*)malloc(total + 1);
    if (!wrapped) {
        if (status) *status = TAURUS_ERROR_MEMORY;
        return NULL;
    }
    memcpy(wrapped, frag_open, open_len);
    memcpy(wrapped + open_len, xml, length);
    memcpy(wrapped + open_len + length, frag_close, close_len);
    wrapped[total] = '\0';

    TaurusStatus parse_st = TAURUS_OK;
    TaurusDocument tmp_doc = taurus_parse_string(wrapped, total, &parse_st);
    free(wrapped);
    if (!tmp_doc) {
        if (status) *status = parse_st;
        return NULL;
    }

    /* Force lazy promotion so we can read the tree. */
    taurus_document_ensure_promoted(tmp_doc);
    TaurusElement tmp_root = taurus_document_root(tmp_doc);
    if (!tmp_root) {
        taurus_document_free(tmp_doc);
        if (status) *status = TAURUS_ERROR_PARSE;
        return NULL;
    }

    /* Build the synthetic container in dest_doc and move children. */
    TaurusElement frag = taurus_element_create(dest_doc, "#document-fragment");
    if (!frag) {
        taurus_document_free(tmp_doc);
        if (status) *status = TAURUS_ERROR_MEMORY;
        return NULL;
    }

    /* Walk children of tmp_root; copy each into dest_doc via
     * taurus_element_append_copy on a temp parent, then detach.
     * (TODO 148 Phase 1 will add taurus_element_copy which makes
     * this a one-liner; until that lands we use the well-tested
     * append_copy path.) */
    TaurusElement tmp_holder = taurus_element_create(dest_doc, "__frag_holder__");
    if (!tmp_holder) {
        taurus_document_free(tmp_doc);
        if (status) *status = TAURUS_ERROR_MEMORY;
        return NULL;
    }
    TaurusNodeRef child = taurus_node_first_child(taurus_element_as_node(tmp_root));
    while (child) {
        TaurusNodeRef next = taurus_node_next_sibling(child);
        int t = taurus_node_get_type(child);
        if (t == 0 /* ELEMENT */) {
            TaurusElement copy = taurus_element_append_copy(tmp_holder,
                                                             (TaurusElement)child);
            if (copy) {
                /* Detach from tmp_holder, attach to frag. */
                taurus_node_unlink(taurus_element_as_node(copy));
                taurus_element_append_child(frag, copy);
            }
        } else if (t == 1 /* TEXT */) {
            const char* s = taurus_text_node_get_content(child);
            TaurusNodeRef n = taurus_text_node_create(dest_doc, s ? s : "");
            if (n) taurus_element_append_child(frag, (TaurusElement)n);
        } else if (t == 2 /* COMMENT */) {
            const char* s = taurus_comment_node_get_content(child);
            TaurusNodeRef n = taurus_comment_node_create(dest_doc, s ? s : "");
            if (n) taurus_element_append_child(frag, (TaurusElement)n);
        } else if (t == 3 /* CDATA */) {
            const char* s = taurus_cdata_node_get_content(child);
            TaurusNodeRef n = taurus_cdata_node_create(dest_doc, s ? s : "");
            if (n) taurus_element_append_child(frag, (TaurusElement)n);
        } else if (t == 4 /* PI */) {
            const char* tgt = taurus_pi_node_get_target(child);
            const char* data = taurus_pi_node_get_data(child);
            TaurusNodeRef n = taurus_pi_node_create(dest_doc,
                                                     tgt ? tgt : "",
                                                     data ? data : "");
            if (n) taurus_element_append_child(frag, (TaurusElement)n);
        }
        child = next;
    }
    /* tmp_holder is pool-allocated; taurus_document_free reclaims it. */

    taurus_document_free(tmp_doc);
    return frag;
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
    /* Single-linked list threaded through each child's `next_adopted`.
     * Note: child_docs / child_docs_tail are CHILD's OWN adopted-
     * children list (its nested xi:include descendants).  Do NOT
     * touch them here -- overwriting destroys the chain that frees
     * the child's grandchildren when we recurse into taurus_document_free.
     *
     * Bug found by CI leak check: prior version NULLed child->child_docs
     * here, which orphaned all nested xi:include docs that had
     * already been adopted into the child. */
    child->next_adopted = NULL;
    if (parent->child_docs_tail) {
        parent->child_docs_tail->next_adopted = child;
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

    /* Free custom XPath function registrations (TODO 148 Phase 5). */
    extern void taurus_xpath_free_custom_fns(struct taurus_document*);
    taurus_xpath_free_custom_fns(doc);

    /* Free element index (TODO 132) */
    if (doc->element_index) {
        taurus_element_index_free(doc->element_index);
        doc->element_index = NULL;
    }

    /* Free FlatDoc if the document was never promoted (TODO 139
     * Phase D). When the caller parses-and-frees without ever
     * calling taurus_document_root, the lazy promote never fires
     * and the FlatDoc is still owned by the doc. */
    if (doc->flat_doc) {
        flat_doc_free(doc->flat_doc);
        doc->flat_doc = NULL;
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
     * pool must outlive us.  Walk `next_adopted` (siblings of THIS doc)
     * and recurse into each via taurus_document_free. */
    {
        struct taurus_document* ch = doc->child_docs;
        while (ch) {
            struct taurus_document* next = ch->next_adopted;
            ch->new_dom_root = NULL;  /* Detach so taurus_document_free
                                       * doesn't try to walk the
                                       * adopted subtree. */
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
/* TODO 139 Phase D: trigger lazy promote if the doc has a flat_doc
 * that hasn't been built into the compact-pointer tree yet. Safe to
 * call multiple times — no-op once promoted. Internal helper used by
 * taurus_document_root and any other entry point that needs the tree
 * without going through the public accessor. */
void taurus_document_ensure_promoted(struct taurus_document* doc) {
    if (!doc) return;
    if (doc->flat_doc && !doc->flat_promoted) {
        flat_promote_into(doc);
    }
}

TAURUS_API TaurusElement taurus_document_root(struct taurus_document* doc) {
    if (!doc) return NULL;

    /* TODO 139 Phase D: lazy promote. If the document was produced
     * by the flat-parse fast path, the compact-pointer tree isn't
     * built yet. Build it now — the caller is about to traverse. */
    taurus_document_ensure_promoted(doc);

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
     * name_view removed (TODO 90) — name is set eagerly by create_with_view.
     * namespace_uri_view + prefix_view removed (TODO 90) — both are now
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

