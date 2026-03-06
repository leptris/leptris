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
#include "dom/compact_element.h"
#include "dom/text.h"
#include "dom/pi.h"
#include "dom/doctype.h"
#include "dtd/model.h"
#include "serialize/serialize.h"
#include "common/entities.h"
#include "memory/pool.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

/* Forward declarations */
extern void taurus_doctype_free(TaurusDoctypeNode* doctype);
extern void ttdtd_free(TaurusDTD* dtd);

/* Forward declaration for recursive wrapper creation */
static struct ptr_element* create_compact_wrapper_full(
    struct taurus_document* doc,
    uint32_t offset,
    struct ptr_element* parent
);

/* ============================================================================
 * Compact Element Wrapper Creation (Full)
 * ============================================================================ */

/**
 * Create a TaurusTextNode from compact text v2
 * NOTE: Uses TaurusTextNode (not ptr_text) for serialization compatibility
 */
static TaurusTextNode* create_text_wrapper(
    struct taurus_document* doc,
    struct compact_text_v2* compact_text
) {
    if (!doc || !compact_text) return NULL;

    /* Allocate text node using TaurusTextNode structure for serialization compatibility */
    TaurusTextNode* text = (TaurusTextNode*)taurus_pool_alloc(
        doc->pool, sizeof(TaurusTextNode));
    if (!text) return NULL;

    memset(text, 0, sizeof(TaurusTextNode));

    /* Set node type based on compact flags
     * NOTE: Compact type values don't match Taurus type values!
     * COMPACT: TEXT=1, CDATA=2, COMMENT=3, PI=4
     * TAURUS:  TEXT=2, CDATA=7, COMMENT=3, PI=4 */
    uint32_t flags = compact_text->flags;
    uint32_t compact_type = flags & COMPACT_NODE_TYPE_MASK;

    switch (compact_type) {
        case COMPACT_NODE_TYPE_TEXT:
            text->base.type = TAURUS_NODE_TYPE_TEXT;  /* 1 -> 2 */
            break;
        case COMPACT_NODE_TYPE_CDATA:
            text->base.type = TAURUS_NODE_TYPE_CDATA; /* 2 -> 7 */
            break;
        case COMPACT_NODE_TYPE_COMMENT:
            text->base.type = TAURUS_NODE_TYPE_COMMENT; /* 3 -> 3 */
            break;
        case COMPACT_NODE_TYPE_PI:
            text->base.type = TAURUS_NODE_TYPE_PI; /* 4 -> 4 */
            break;
        default:
            text->base.type = TAURUS_NODE_TYPE_TEXT;
    }

    /* Get text content - COMPACT TEXT IS NOT NULL-TERMINATED!
     * Must copy and add null terminator */
    if (compact_text->text_offset != UINT32_MAX && doc->xml_buffer) {
        const char* src = doc->xml_buffer + compact_text->text_offset;
        uint32_t len = compact_text->text_length;

        /* Allocate space for copy with null terminator */
        char* copy = (char*)taurus_pool_alloc(doc->pool, len + 1);
        if (copy) {
            memcpy(copy, src, len);
            copy[len] = '\0';
            text->content = copy;
        }
    }

    return text;
}

/**
 * Create a ptr_attribute from compact attribute v2
 */
static struct ptr_attribute* create_attr_wrapper(
    struct taurus_document* doc,
    struct compact_attribute_v2* compact_attr
) {
    if (!doc || !compact_attr) return NULL;

    /* Allocate attribute */
    struct ptr_attribute* attr = (struct ptr_attribute*)taurus_pool_alloc(
        doc->pool, sizeof(struct ptr_attribute));
    if (!attr) return NULL;

    memset(attr, 0, sizeof(struct ptr_attribute));

    /* Get name (lower 31 bits of name_offset) */
    uint32_t name_off = compact_attr->name_offset & 0x7FFFFFFF;
    if (name_off != UINT32_MAX && doc->xml_buffer) {
        attr->name = (const char*)(doc->xml_buffer + name_off);
    }

    /* Get value */
    if (compact_attr->value_offset != UINT32_MAX && doc->xml_buffer) {
        attr->value = (const char*)(doc->xml_buffer + compact_attr->value_offset);
    }

    return attr;
}

/**
 * Create a full ptr_element wrapper for a compact element (recursive)
 * This populates ALL children, attributes, and text nodes for complete DOM access
 * NOTE: Offset 0 is VALID (first element in buffer)
 */
static struct ptr_element* create_compact_wrapper_full(
    struct taurus_document* doc,
    uint32_t offset,
    struct ptr_element* parent
) {
    /* Offset 0 is VALID (first element in buffer), only UINT32_MAX is invalid */
    if (!doc || offset == UINT32_MAX || !doc->compact_base) {
        return NULL;
    }

    /* Create pool if needed */
    if (!doc->pool) {
        doc->pool = taurus_pool_create();
        if (!doc->pool) return NULL;
    }

    /* Get compact element */
    struct compact_element_v2* compact = (struct compact_element_v2*)
        ((char*)doc->compact_base + offset);
    if (!compact) return NULL;

    /* Allocate wrapper element */
    struct ptr_element* wrapper = (struct ptr_element*)taurus_pool_alloc(
        doc->pool, sizeof(struct ptr_element));
    if (!wrapper) return NULL;

    memset(wrapper, 0, sizeof(struct ptr_element));

    /* Fill in essential fields */
    wrapper->type = PTR_NODE_TYPE_ELEMENT;
    wrapper->document = doc;
    wrapper->parent = parent;

    /* Get name from offset */
    if (compact->name_offset != UINT32_MAX && doc->xml_buffer) {
        wrapper->name = (const char*)(doc->xml_buffer + compact->name_offset);
    }

    /* Process children (attributes, elements, text nodes) */
    uint32_t child_off = compact->first_child;
    struct ptr_element* last_child_elem = NULL;
    struct ptr_attribute* last_attr = NULL;
    struct ptr_text* last_text = NULL;
    void* last_child_node = NULL;  /* Generic pointer for next_sibling linking */

    while (child_off != UINT32_MAX && child_off != 0) {
        char* child_ptr = (char*)doc->compact_base + child_off;

        /* Check first field for attribute marker
         * For element: first field is first_child (offset 0 or UINT32_MAX)
         * For attribute: first field is name_offset with high bit set */
        uint32_t first_field = *(uint32_t*)child_ptr;
        if ((first_field & 0x80000000) && (first_field != UINT32_MAX)) {
            /* Attribute node */
            struct compact_attribute_v2* compact_attr = (struct compact_attribute_v2*)child_ptr;

            struct ptr_attribute* attr = create_attr_wrapper(doc, compact_attr);
            if (attr) {
                if (!wrapper->first_attr) {
                    wrapper->first_attr = attr;
                } else if (last_attr) {
                    last_attr->next_attr = attr;
                }
                last_attr = attr;
                wrapper->attr_count++;
            }

            /* Move to next attribute */
            child_off = compact_attr->next_attr;
        } else {
            /* Check for text node using TEXT_MARKER at offset 12 (flags field)
             * For element: offset 12 is name_offset (string offset, no high bit)
             * For text: offset 12 is flags with COMPACT_V2_TEXT_MARKER set */
            uint32_t offset12_field = *(uint32_t*)(child_ptr + 12);
            if (offset12_field & COMPACT_V2_TEXT_MARKER) {
                /* Text-like node */
                struct compact_text_v2* compact_text = (struct compact_text_v2*)child_ptr;

                TaurusTextNode* text = create_text_wrapper(doc, compact_text);
                if (text) {
                    /* Link as child - text nodes use base.next_sibling for linking */
                    if (last_child_node) {
                        /* Set next_sibling of previous node */
                        TaurusTextNode* prev = (TaurusTextNode*)last_child_node;
                        prev->base.next_sibling = (TaurusNode*)text;
                        text->base.prev_sibling = (TaurusNode*)prev;
                    } else {
                        /* First child */
                        wrapper->first_child = (struct ptr_element*)text;
                    }
                    last_child_node = text;
                    wrapper->child_count++;
                }

                /* Move to next sibling */
                child_off = compact_text->next_sibling;
            } else {
                /* Element child - create wrapper recursively */
                struct compact_element_v2* child_compact = (struct compact_element_v2*)child_ptr;

                struct ptr_element* child_wrapper = create_compact_wrapper_full(doc, child_off, wrapper);
                if (child_wrapper) {
                    /* Link as child */
                    if (last_child_node) {
                        /* Set next_sibling of previous node */
                        struct ptr_element* prev = (struct ptr_element*)last_child_node;
                        prev->next_sibling = child_wrapper;
                        child_wrapper->prev_sibling = prev;
                    } else {
                        /* First child */
                        wrapper->first_child = child_wrapper;
                    }
                    last_child_node = child_wrapper;
                    last_child_elem = child_wrapper;
                    wrapper->child_count++;
                }

                /* Move to next sibling */
                child_off = child_compact->next_sibling;
            }
        }
    }

    /* Set last_child pointer for O(1) append */
    wrapper->last_child = last_child_elem;

    return wrapper;
}

/**
 * Create a minimal ptr_element wrapper for a compact element
 * This provides basic element access for compact mode documents
 * NOTE: Offset 0 is VALID (first element in buffer)
 */
static struct ptr_element* create_compact_wrapper(
    struct taurus_document* doc,
    uint32_t offset
) {
    return create_compact_wrapper_full(doc, offset, NULL);
}

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
 * Also supports compact mode (v5 parser)
 */
TAURUS_API TaurusElement taurus_document_root(struct taurus_document* doc) {
    if (!doc) return NULL;

    /* POINTER-BASED: Use ptr_root directly */
    if (doc->ptr_root) {
        return doc->ptr_root;
    }

    /* COMPACT MODE: Check for compact root offset
     * NOTE: Offset 0 is VALID (first element in buffer), UINT32_MAX means no root */
    if (doc->compact_root_offset != UINT32_MAX && doc->compact_base) {
        /* Check if we already created a wrapper and cached it in new_dom_root */
        if (doc->new_dom_root) {
            return (TaurusElement)doc->new_dom_root;
        }
        /* Create a minimal wrapper for the compact element */
        struct ptr_element* wrapper = create_compact_wrapper(doc, doc->compact_root_offset);
        if (wrapper) {
            /* Cache it in new_dom_root for future access */
            doc->new_dom_root = wrapper;
            return wrapper;
        }
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
