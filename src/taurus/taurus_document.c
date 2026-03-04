/* taurus_document.c - Taurus document lifecycle API
 * Copyright (c) 2024, Ribose Inc.
 *
 * Pure C XML parser and XPath evaluator - Document API.
 * POINTER-BASED ARCHITECTURE: Uses ptr_element directly.
 */

#include "../include/taurus.h"
#include "taurus_internal.h"
#include "dom/element.h"
#include "dom/ptr_element.h"
#include "dom/ptr_accessor.h"
#include "dom/pi.h"
#include "dom/doctype.h"
#include "dtd/model.h"
#include "serialize/serialize.h"
#include "common/entities.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Forward declarations */
extern void taurus_doctype_free(TaurusDoctypeNode* doctype);
extern void ttdtd_free(TaurusDTD* dtd);

/* ============================================================================
 * Document Lifecycle
 * ============================================================================ */

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

    /* Notify observers and cleanup observer system */
    taurus_observer_cleanup_document(doc);

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

    /* Free wrapper cache if present */
    if (doc->wrapper_cache) {
        free(doc->wrapper_cache);
        doc->wrapper_cache = NULL;
    }

    /* POINTER-BASED: Free pointer mode pools */
    if (doc->ptr_elem_pool) {
        taurus_pool_destroy(doc->ptr_elem_pool);
        doc->ptr_elem_pool = NULL;
    }
    if (doc->ptr_attr_pool) {
        taurus_pool_destroy(doc->ptr_attr_pool);
        doc->ptr_attr_pool = NULL;
    }
    if (doc->ptr_text_pool) {
        taurus_pool_destroy(doc->ptr_text_pool);
        doc->ptr_text_pool = NULL;
    }

    /* Destroy memory pool (frees all DOM nodes allocated from it) */
    if (doc->pool) {
        taurus_pool_destroy(doc->pool);
    }

    /* Free document */
    TAURUS_FREE(doc);
}

/**
 * Get root element of document (POINTER-BASED)
 * V2: Uses ptr_element structures directly
 */
TAURUS_API TaurusElement taurus_document_root(struct taurus_document* doc) {
    if (!doc) return NULL;

    /* POINTER-BASED: Use ptr_root directly */
    if (doc->ptr_root) {
        return doc->ptr_root;
    }

    /* No root - document may be empty or not parsed */
    return NULL;
}

/**
 * Check if element is null/empty
 */
TAURUS_API int taurus_element_is_null(TaurusElement elem) {
    return elem == NULL;
}

/**
 * Get a null element handle
 */
TAURUS_API TaurusElement taurus_element_handle_null(void) {
    return NULL;
}

/**
 * Get document encoding
 */
TAURUS_API const char* taurus_document_encoding(struct taurus_document* doc) {
    if (!doc) return NULL;
    return doc->encoding; /* May be NULL if not specified */
}

/**
 * Set strict parsing mode for a document (per-document, thread-safe)
 *
 * @param doc    Document to set mode for
 * @param strict 1 for strict XML 1.0, 0 for lenient (pugixml compatibility)
 */
TAURUS_API void taurus_document_set_strict(struct taurus_document* doc, int strict) {
    if (doc) {
        doc->strict_mode = (strict != 0);
    }
}

/**
 * Get strict parsing mode for a document
 *
 * @param doc Document to get mode for
 * @return 1 if strict mode, 0 if lenient mode
 */
TAURUS_API int taurus_document_get_strict(struct taurus_document* doc) {
    return doc ? doc->strict_mode : 0;
}

/**
 * Get compact base pointer for compact element resolution
 *
 * @param doc Document to get compact base for
 * @return Compact base pointer
 */
void* taurus_document_get_compact_base(struct taurus_document* doc) {
    if (!doc) return NULL;
    return doc->compact_base;
}

/**
 * Set compact base pointer for compact element resolution
 *
 * @param doc Document to set compact base for
 * @param base Compact base pointer
 */
void taurus_document_set_compact_base(struct taurus_document* doc, void* base) {
    if (doc) {
        doc->compact_base = base;
    }
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
 * Document String Finalization
 * ============================================================================ */

/* Forward declaration for recursive helper */
static void finalize_element_strings(TaurusElement elem);

/* Helper: Finalize strings for a single element
 *
 * POINTER-BASED ARCHITECTURE:
 * In ptr_element, strings are already null-terminated during parsing.
 * This function primarily exists for API compatibility and to handle
 * any edge cases where lazy conversion might be needed.
 *
 * For ptr_element:
 * - elem->name is already null-terminated
 * - No prefix/namespace_uri fields (extracted from name or xmlns attributes)
 * - No namespaces linked list (namespaces are xmlns:prefix attributes)
 * - ptr_attribute name/value are already null-terminated
 */
static void finalize_element_strings(TaurusElement elem) {
    if (!elem) return;

    /* In ptr_element, strings are already finalized during parsing.
     * Just recursively process children. */

    /* Recursively finalize children */
    TaurusElement child = taurus_element_get_first_child(elem);
    while (child) {
        finalize_element_strings(child);
        child = taurus_element_get_next_sibling(child);
    }
}

/**
 * Finalize all strings in document (Public API)
 *
 * This converts all StringView fields to NULL-terminated strings.
 * Called automatically after parsing for performance optimization.
 */
TAURUS_API int taurus_document_finalize_strings(TaurusDocument doc) {
    if (!doc) return TAURUS_ERROR_NULL_ARG;

    TaurusElement root = taurus_document_root(doc);
    if (root) {
        finalize_element_strings(root);
    }

    return TAURUS_OK;
}
