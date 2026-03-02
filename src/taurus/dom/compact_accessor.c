/* compact_accessor.c - Compact Element Accessor Functions
 * Copyright (c) 2024, Ribose Inc.
 *
 * Provides accessor functions that work directly with compact elements.
 * This is the key to achieving pugixml-level performance - no conversion
 * to legacy format, just direct access to compact data.
 *
 * Architecture:
 * - Compact elements use 4-byte offsets instead of 8-byte pointers
 * - All data is in a single contiguous memory block (cache efficiency)
 * - No individual allocations = O(1) operations
 *
 * WRAPPER CACHE:
 * - On-demand wrapper creation for compact elements
 * - Hash table maps offsets to wrapper elements
 * - Wrappers are 168-byte taurus_element structs that reference compact data
 */

#include "compact_accessor.h"
#include "compact_element.h"  /* For struct compact_element_v2 (16-byte) */
#include "element.h"
#include "text.h"             /* For TaurusTextNode */
#include "../memory/pool.h"      /* For taurus_pool_create, taurus_pool_alloc */
#include "../memory/zero_check_alloc.h"
#include "../taurus_internal.h"
#include "../common/entities.h"
#include <string.h>

/* ============================================================================
 * Internal Helper Functions
 * ============================================================================ */

/**
 * Get the compact base pointer from a document (for node structs)
 * V5 PARSER: Uses ZeroCheckAlloc, not CompactSingleAllocator
 */
static inline char* get_compact_base(struct taurus_document* doc) {
    if (!doc) return NULL;
    ZeroCheckAlloc* alloc = (ZeroCheckAlloc*)doc->compact_alloc;
    return alloc ? alloc->base : NULL;
}

/**
 * Get the string base pointer from a document (for strings)
 * HYBRID IN-PLACE: Strings are stored in xml_buffer, not compact block!
 */
static inline char* get_string_base(struct taurus_document* doc) {
    return doc ? doc->xml_buffer : NULL;
}

/* ============================================================================
 * Wrapper Cache Implementation
 * ============================================================================ */

/* Simple hash table for wrapper cache */
#define WRAPPER_CACHE_BUCKETS 256

typedef struct WrapperCacheEntry {
    uint32_t offset;              /* Compact element offset */
    TaurusElement wrapper;        /* Wrapper element */
    struct WrapperCacheEntry* next; /* Collision chain */
} WrapperCacheEntry;

typedef struct WrapperCache {
    WrapperCacheEntry* buckets[WRAPPER_CACHE_BUCKETS];
    TaurusMemoryPool* pool;       /* Pool for wrapper allocations */
} WrapperCache;

/* FNV-1a hash for offset */
static inline uint32_t wrapper_cache_hash(uint32_t offset) {
    return offset & (WRAPPER_CACHE_BUCKETS - 1);
}

/* Get or create wrapper cache for document */
static WrapperCache* get_wrapper_cache(struct taurus_document* doc) {
    if (!doc) return NULL;
    if (!doc->wrapper_cache) {
        doc->wrapper_cache = calloc(1, sizeof(WrapperCache));
        if (doc->wrapper_cache) {
            ((WrapperCache*)doc->wrapper_cache)->pool = doc->pool;
        }
    }
    return (WrapperCache*)doc->wrapper_cache;
}

/* Look up wrapper by offset */
static TaurusElement wrapper_cache_lookup(struct taurus_document* doc, uint32_t offset) {
    if (!doc || offset == UINT32_MAX) return NULL;

    WrapperCache* cache = get_wrapper_cache(doc);
    if (!cache) return NULL;

    uint32_t bucket = wrapper_cache_hash(offset);
    WrapperCacheEntry* entry = cache->buckets[bucket];

    while (entry) {
        if (entry->offset == offset) {
            return entry->wrapper;
        }
        entry = entry->next;
    }

    return NULL;
}

/* Insert wrapper into cache */
static void wrapper_cache_insert(struct taurus_document* doc, uint32_t offset, TaurusElement wrapper) {
    if (!doc || offset == UINT32_MAX || !wrapper) return;

    WrapperCache* cache = get_wrapper_cache(doc);
    if (!cache) return;

    uint32_t bucket = wrapper_cache_hash(offset);

    /* Allocate entry from pool */
    WrapperCacheEntry* entry = (WrapperCacheEntry*)taurus_pool_alloc(cache->pool, sizeof(WrapperCacheEntry));
    if (!entry) return;

    entry->offset = offset;
    entry->wrapper = wrapper;
    entry->next = cache->buckets[bucket];
    cache->buckets[bucket] = entry;
}

/* ============================================================================
 * V2 WRAPPER CREATION - Uses compact_element_v2 (16-byte)
 * ============================================================================ */

/**
 * Create wrapper for compact element v2
 * POINTER-ONLY: Converts offsets to pointers and populates children/attributes
 */
static TaurusElement create_wrapper_for_compact_v2(
    struct taurus_document* doc,
    struct compact_element_v2* compact,
    uint32_t offset
) {
    if (!doc || !compact) return NULL;

    /* Create pool on-demand if needed */
    if (!doc->pool) {
        doc->pool = taurus_pool_create();
        if (!doc->pool) return NULL;
    }

    /* Allocate wrapper from pool */
    TaurusElement wrapper = (TaurusElement)taurus_pool_alloc(doc->pool, sizeof(struct taurus_element));
    if (!wrapper) return NULL;

    memset(wrapper, 0, sizeof(struct taurus_element));

    /* Set document pointer */
    wrapper->document = doc;

    /* Set base node type */
    wrapper->base.type = TAURUS_NODE_TYPE_ELEMENT;

    /* Get bases for pointer conversion */
    char* compact_base = get_compact_base(doc);
    char* string_base = get_string_base(doc);

    /* Set name from offset */
    uint32_t name_off = COMPACT_V2_NAME_OFF(compact);
    if (name_off != UINT32_MAX && string_base) {
        wrapper->name = (char*)COMPACT_V2_OFFSET_TO_PTR(string_base, name_off);
        wrapper->name_view = taurus_sv_from_cstr(wrapper->name);
    }

    /* Cache the wrapper by offset BEFORE populating children (to handle cycles) */
    wrapper_cache_insert(doc, offset, wrapper);

    /* Populate children and attributes from compact element */
    if (compact_base && string_base) {
        uint32_t child_off = compact->first_child;
        TaurusNode* last_child = NULL;
        struct taurus_attribute* last_attr = NULL;

        while (child_off != UINT32_MAX) {
            char* child_ptr = compact_base + child_off;
            struct compact_element_v2* v2_child = (struct compact_element_v2*)child_ptr;

            /* Check if this is an attribute (high bit set AND not UINT32_MAX) */
            if (COMPACT_V2_IS_ATTR(v2_child)) {
                /* Create wrapper attribute from compact attribute */
                struct compact_attribute_v2* compact_attr = (struct compact_attribute_v2*)child_ptr;

                struct taurus_attribute* attr = (struct taurus_attribute*)taurus_pool_alloc(
                    doc->pool, sizeof(struct taurus_attribute));
                if (attr) {
                    memset(attr, 0, sizeof(struct taurus_attribute));

                    /* Get name and value */
                    uint32_t attr_name_off = compact_attr->name_offset & 0x7FFFFFFF;
                    if (attr_name_off != UINT32_MAX) {
                        attr->name = (char*)COMPACT_V2_OFFSET_TO_PTR(string_base, attr_name_off);
                        attr->name_view = taurus_sv_from_cstr(attr->name);
                    }

                    if (compact_attr->value_offset != UINT32_MAX) {
                        attr->value = (char*)COMPACT_V2_OFFSET_TO_PTR(string_base, compact_attr->value_offset);
                        attr->value_view = taurus_sv_from_cstr(attr->value);
                    }

                    attr->next = NULL;

                    /* Add to attribute list */
                    if (!wrapper->first_attribute) {
                        wrapper->first_attribute = attr;
                    } else if (last_attr) {
                        last_attr->next = attr;
                    }
                    last_attr = attr;
                    wrapper->attr_count++;
                }

                child_off = compact_attr->next_attr;
            } else {
                /* Check if it's a text-like node */
                struct compact_text_v2* text_child = (struct compact_text_v2*)child_ptr;
                uint32_t flags = text_child->flags;

                if (flags & COMPACT_V2_TEXT_MARKER) {
                    /* Text-like node */
                    uint32_t node_type = flags & 0x0F;

                    if (node_type == COMPACT_V2_TYPE_TEXT) {
                        /* Create text node */
                        struct taurus_text_node* text = (struct taurus_text_node*)taurus_pool_alloc(
                            doc->pool, sizeof(struct taurus_text_node));
                        if (text) {
                            memset(text, 0, sizeof(struct taurus_text_node));
                            text->base.type = TAURUS_NODE_TYPE_TEXT;
                            if (text_child->text_offset != UINT32_MAX && text_child->text_length > 0) {
                                /* Text is NOT null-terminated in buffer - must copy */
                                const char* src = (const char*)COMPACT_V2_OFFSET_TO_PTR(string_base, text_child->text_offset);
                                char* copy = (char*)taurus_pool_alloc(doc->pool, text_child->text_length + 1);
                                if (copy) {
                                    memcpy(copy, src, text_child->text_length);
                                    copy[text_child->text_length] = '\0';
                                    text->content = copy;
                                }
                            }

                            /* Add to child list */
                            if (!wrapper->first_child) {
                                wrapper->first_child = (TaurusNode*)text;
                            } else if (last_child) {
                                last_child->next_sibling = (TaurusNode*)text;
                                text->base.prev_sibling = last_child;
                            }
                            last_child = (TaurusNode*)text;
                            wrapper->child_count++;
                        }
                    }
                    /* TODO: Handle CDATA, COMMENT, PI nodes */

                    child_off = text_child->next_sibling;
                } else {
                    /* Element child - create wrapper recursively */
                    TaurusElement child_wrapper = compact_get_or_create_wrapper(doc, child_off);
                    if (child_wrapper) {
                        /* Add to child list */
                        if (!wrapper->first_child) {
                            wrapper->first_child = (TaurusNode*)child_wrapper;
                        } else if (last_child) {
                            last_child->next_sibling = (TaurusNode*)child_wrapper;
                            child_wrapper->base.prev_sibling = last_child;
                        }
                        child_wrapper->parent = wrapper;
                        last_child = (TaurusNode*)child_wrapper;
                        wrapper->child_count++;
                    }

                    child_off = v2_child->next_sibling;
                }
            }
        }

        /* Set last_child pointer */
        wrapper->last_child = last_child;
    }

    return wrapper;
}

/**
 * Get or create wrapper for compact element by offset
 * Uses v2 structure (16 bytes)
 * NOTE: offset 0 is valid (first element in buffer), UINT32_MAX means null
 */
TaurusElement compact_get_or_create_wrapper(struct taurus_document* doc, uint32_t offset) {
    if (!doc || offset == UINT32_MAX) return NULL;

    /* Check if this is the root element - return cached root wrapper if available */
    if (doc->compact_root_offset == offset && doc->new_dom_root) {
        return (TaurusElement)doc->new_dom_root;
    }

    /* Check cache first */
    TaurusElement cached = wrapper_cache_lookup(doc, offset);
    if (cached) return cached;

    /* Get compact element v2 */
    char* base = get_compact_base(doc);
    if (!base) return NULL;

    /* Bounds check: ensure offset is within allocated memory */
    if (doc->compact_alloc) {
        ZeroCheckAlloc* alloc = (ZeroCheckAlloc*)doc->compact_alloc;
        if (offset + 16 > alloc->size) {
            return NULL;
        }
    }

    struct compact_element_v2* compact = (struct compact_element_v2*)COMPACT_V2_OFFSET_TO_PTR(base, offset);
    if (!compact) return NULL;

    /* Create and cache wrapper */
    return create_wrapper_for_compact_v2(doc, compact, offset);
}

/* ============================================================================
 * Compact Element V2 Accessors - NULL-TERMINATED STRINGS
 * V2 uses null-terminated strings, not length-based
 * ============================================================================ */

/**
 * Get element name from compact element v2
 * NULL-TERMINATED: Name is null-terminated in v2 format
 */
const char* compact_element_get_name(struct compact_element_v2* elem, struct taurus_document* doc) {
    if (!elem || !doc) return NULL;

    char* string_base = get_string_base(doc);
    if (!string_base) return NULL;

    return (const char*)COMPACT_V2_OFFSET_TO_PTR(string_base, COMPACT_V2_NAME_OFF(elem));
}

/**
 * Get element name length (calculated via strlen for v2)
 */
uint16_t compact_element_get_name_length(struct compact_element_v2* elem) {
    if (!elem) return 0;
    /* V2 names are null-terminated, use strlen */
    const char* name = (const char*)COMPACT_V2_NAME_OFF(elem);
    return name ? (uint16_t)strlen(name) : 0;
}

/**
 * Get element name with length (most efficient)
 */
const char* compact_element_get_name_ex(
    struct compact_element_v2* elem,
    struct taurus_document* doc,
    uint16_t* out_length
) {
    if (!elem || !doc || !out_length) {
        if (out_length) *out_length = 0;
        return NULL;
    }

    const char* name = compact_element_get_name(elem, doc);
    if (!name) {
        *out_length = 0;
        return NULL;
    }

    *out_length = (uint16_t)strlen(name);
    return name;
}

/**
 * Get attribute name length
 */
uint16_t compact_attribute_get_name_length(struct compact_attribute_v2* attr) {
    if (!attr) return 0;
    const char* name = (const char*)COMPACT_V2_OFFSET_TO_PTR(NULL, attr->name_offset);
    return name ? (uint16_t)strlen(name) : 0;
}

/**
 * Get attribute value length
 */
uint16_t compact_attribute_get_value_length(struct compact_attribute_v2* attr) {
    if (!attr) return 0;
    const char* value = (const char*)COMPACT_V2_OFFSET_TO_PTR(NULL, attr->value_offset);
    return value ? (uint16_t)strlen(value) : 0;
}

/**
 * Get first child of compact element v2 (skipping attributes and text nodes)
 */
struct compact_element_v2* compact_element_get_first_child(
    struct compact_element_v2* elem,
    struct taurus_document* doc
) {
    if (!elem || !doc) return NULL;

    char* base = get_compact_base(doc);
    if (!base) return NULL;

    /* Get allocation size for bounds checking */
    size_t alloc_size = 0;
    if (doc->compact_alloc) {
        ZeroCheckAlloc* alloc = (ZeroCheckAlloc*)doc->compact_alloc;
        alloc_size = alloc->size;
    }

    /* Walk children, find first element child (skip attributes and text nodes)
     * V2: Parser writes UINT32_MAX for null directly */
    uint32_t child_off = elem->first_child;
    while (child_off != UINT32_MAX) {
        /* Bounds check */
        if (alloc_size > 0 && child_off + 16 > alloc_size) break;

        char* child_ptr = base + child_off;

        /* Check first field for attribute marker */
        uint32_t first_field = *(uint32_t*)(child_ptr + 0);
        if ((first_field & 0x80000000) && (first_field != UINT32_MAX)) {
            /* Attribute - skip using next_attr at offset 8 */
            struct compact_attribute_v2* attr = (struct compact_attribute_v2*)child_ptr;
            child_off = attr->next_attr;
            continue;
        }

        /* Check for text node using TEXT_MARKER at offset 12 */
        uint32_t offset12_field = *(uint32_t*)(child_ptr + 12);
        if (offset12_field & COMPACT_V2_TEXT_MARKER) {
            /* Text node - skip using next_sibling at offset 4 */
            child_off = *(uint32_t*)(child_ptr + 4);
            continue;
        }

        /* Element child - return it */
        return (struct compact_element_v2*)child_ptr;
    }

    return NULL;
}

/**
 * Get next sibling of compact element v2 (skipping text nodes)
 */
struct compact_element_v2* compact_element_get_next_sibling(
    struct compact_element_v2* elem,
    struct taurus_document* doc
) {
    if (!elem || !doc) return NULL;

    char* base = get_compact_base(doc);
    if (!base) return NULL;

    /* Get allocation size for bounds checking */
    size_t alloc_size = 0;
    if (doc->compact_alloc) {
        ZeroCheckAlloc* alloc = (ZeroCheckAlloc*)doc->compact_alloc;
        alloc_size = alloc->size;
    }

    /* Walk siblings, find next element (skip text nodes)
     * V2: Parser writes UINT32_MAX for null directly */
    uint32_t sibling_off = elem->next_sibling;
    while (sibling_off != UINT32_MAX) {
        /* Bounds check */
        if (alloc_size > 0 && sibling_off + 16 > alloc_size) break;

        char* sibling_ptr = base + sibling_off;

        /* Check for text node using TEXT_MARKER at offset 12 */
        uint32_t offset12_field = *(uint32_t*)(sibling_ptr + 12);
        if (offset12_field & COMPACT_V2_TEXT_MARKER) {
            /* Text node - skip using next_sibling at offset 4 */
            sibling_off = *(uint32_t*)(sibling_ptr + 4);
            continue;
        }

        /* Element sibling - return it */
        return (struct compact_element_v2*)sibling_ptr;
    }

    return NULL;
}

/**
 * Get parent of compact element v2
 * V2: Parser writes UINT32_MAX for null directly
 */
struct compact_element_v2* compact_element_get_parent(
    struct compact_element_v2* elem,
    struct taurus_document* doc
) {
    if (!elem || !doc) return NULL;

    char* base = get_compact_base(doc);
    if (!base) return NULL;

    /* V2: Parser writes UINT32_MAX for null directly */
    return (struct compact_element_v2*)COMPACT_V2_OFFSET_TO_PTR(base, elem->parent);
}

/**
 * Get last child of compact element v2
 */
struct compact_element_v2* compact_element_get_last_child(
    struct compact_element_v2* elem,
    struct taurus_document* doc
) {
    if (!elem || !doc) return NULL;

    char* base = get_compact_base(doc);
    if (!base) return NULL;

    /* Walk to last element child (using updated next_sibling that skips text) */
    struct compact_element_v2* child = compact_element_get_first_child(elem, doc);
    struct compact_element_v2* last = child;

    while (child) {
        last = child;
        child = compact_element_get_next_sibling(child, doc);
    }

    return last;
}

/**
 * Get child count of compact element v2 (calculated on demand)
 */
uint16_t compact_element_get_child_count(struct compact_element_v2* elem) {
    return elem ? compact_v2_child_count(elem, NULL) : 0;
}

/**
 * Get attribute count of compact element v2
 * Attributes are in first_child chain with high bit set
 */
uint16_t compact_element_get_attr_count(struct compact_element_v2* elem) {
    /* TODO: Count attributes in first_child chain */
    return 0;
}

/**
 * Get first attribute of compact element v2
 * Attributes are linked from first_child with high bit set
 */
struct compact_attribute_v2* compact_element_get_first_attr(
    struct compact_element_v2* elem,
    struct taurus_document* doc
) {
    if (!elem || !doc) return NULL;

    char* base = get_compact_base(doc);
    if (!base) return NULL;

    /* V2: Parser writes UINT32_MAX for null directly */
    uint32_t child_off = elem->first_child;

    /* Bounds check: ensure offset is within allocated memory */
    if (child_off == UINT32_MAX) return NULL;

    if (doc->compact_alloc) {
        ZeroCheckAlloc* alloc = (ZeroCheckAlloc*)doc->compact_alloc;
        if (child_off + sizeof(struct compact_element_v2) > alloc->size) {
            return NULL;  /* Invalid offset - out of bounds */
        }
    }

    const char* child_ptr = base + child_off;

    /* Check if this is an attribute by reading FIRST field (offset 0)
     * For attribute: offset 0 is name_offset with high bit set
     * For element: offset 0 is first_child (could be UINT32_MAX or valid offset)
     * IMPORTANT: Check that it's NOT just UINT32_MAX (element with no children)
     */
    uint32_t first_field = *(const uint32_t*)(child_ptr + 0);
    if ((first_field & 0x80000000) && (first_field != UINT32_MAX)) {
        return (struct compact_attribute_v2*)child_ptr;
    }

    return NULL;
}

/**
 * Get attribute name from compact attribute v2
 * NULL-TERMINATED: Reads from xml_buffer
 * Note: name_offset has high bit set for attribute marker, must mask it off
 */
const char* compact_attribute_get_name(
    struct compact_attribute_v2* attr,
    struct taurus_document* doc
) {
    if (!attr || !doc) return NULL;

    char* string_base = get_string_base(doc);
    if (!string_base) return NULL;

    /* Mask off high bit (attribute marker) to get actual offset */
    return (const char*)COMPACT_V2_OFFSET_TO_PTR(string_base, attr->name_offset & 0x7FFFFFFF);
}

/**
 * Get attribute value from compact attribute v2
 * NULL-TERMINATED: Reads from xml_buffer
 */
const char* compact_attribute_get_value(
    struct compact_attribute_v2* attr,
    struct taurus_document* doc
) {
    if (!attr || !doc) return NULL;

    char* string_base = get_string_base(doc);
    if (!string_base) return NULL;

    return (const char*)COMPACT_V2_OFFSET_TO_PTR(string_base, attr->value_offset);
}

/**
 * Find attribute by name in compact element v2
 */
struct compact_attribute_v2* compact_element_find_attr(
    struct compact_element_v2* elem,
    struct taurus_document* doc,
    const char* name
) {
    if (!elem || !doc || !name) return NULL;

    char* node_base = get_compact_base(doc);
    char* string_base = get_string_base(doc);
    if (!node_base || !string_base) return NULL;

    /* Get allocation size for bounds checking */
    size_t alloc_size = 0;
    if (doc->compact_alloc) {
        ZeroCheckAlloc* alloc = (ZeroCheckAlloc*)doc->compact_alloc;
        alloc_size = alloc->size;
    }

    /* Walk first_child chain looking for attribute
     * V2: Parser writes UINT32_MAX for null directly */
    uint32_t child_off = elem->first_child;
    while (child_off != UINT32_MAX) {
        /* Bounds check */
        if (alloc_size > 0 && child_off + 16 > alloc_size) break;

        /* First, check if this is an attribute by looking at the name_offset high bit */
        struct compact_attribute_v2* attr =
            (struct compact_attribute_v2*)COMPACT_V2_OFFSET_TO_PTR(node_base, child_off);

        /* Check if high bit is set AND it's not UINT32_MAX (element with no children)
         * For attributes: name_offset has high bit set as type marker
         * For elements: first_child = UINT32_MAX also has high bit, but means "no children"
         */
        uint32_t first_field = attr->name_offset;  /* For attr: name_offset, for elem: first_child */
        if ((first_field & 0x80000000) && (first_field != UINT32_MAX)) {
            /* This is an attribute - mask off high bit to get actual name offset */
            uint32_t name_off = first_field & 0x7FFFFFFF;
            const char* attr_name = (const char*)COMPACT_V2_OFFSET_TO_PTR(string_base, name_off);

            if (attr_name && strcmp(attr_name, name) == 0) {
                return attr;
            }

            /* Move to next attribute using next_attr field */
            child_off = attr->next_attr;
        } else {
            /* Not an attribute - this means we've reached element children, stop */
            break;
        }
    }

    return NULL;
}

/**
 * Get attribute value by name from compact element v2
 */
const char* compact_element_get_attr_value(
    struct compact_element_v2* elem,
    struct taurus_document* doc,
    const char* name
) {
    struct compact_attribute_v2* attr = compact_element_find_attr(elem, doc, name);
    if (!attr) return NULL;

    char* string_base = get_string_base(doc);
    if (!string_base) return NULL;

    const char* raw_value = (const char*)COMPACT_V2_OFFSET_TO_PTR(string_base, attr->value_offset);
    if (!raw_value) return NULL;

    /* V2 strings are null-terminated, just strdup */
    char* result = strdup(raw_value);
    if (!result) return NULL;

    /* Check if value contains entities (look for '&' character) */
    if (strchr(result, '&') != NULL) {
        /* Decode entities - returns allocated string that caller must free */
        char* decoded = taurus_decode_entities(result);
        if (decoded) {
            free(result);
            return decoded;
        }
    }

    return result;
}

/* ============================================================================
 * Child Iteration Helpers - V2
 * ============================================================================ */

/**
 * Get child by index (O(n) walk, but cache-friendly)
 */
struct compact_element_v2* compact_element_get_child_by_index(
    struct compact_element_v2* elem,
    struct taurus_document* doc,
    uint16_t index
) {
    if (!elem || !doc) return NULL;

    char* base = get_compact_base(doc);
    if (!base) return NULL;

    struct compact_element_v2* child = compact_element_get_first_child(elem, doc);
    for (uint16_t i = 0; i < index && child; i++) {
        child = COMPACT_V2_NEXT_SIBLING(base, child);
    }

    return child;
}

/**
 * Find first child by name - V2
 */
struct compact_element_v2* compact_element_find_child_by_name(
    struct compact_element_v2* elem,
    struct taurus_document* doc,
    const char* name
) {
    if (!elem || !doc || !name) return NULL;

    char* node_base = get_compact_base(doc);
    char* string_base = get_string_base(doc);
    if (!node_base || !string_base) return NULL;

    struct compact_element_v2* child = compact_element_get_first_child(elem, doc);
    while (child) {
        const char* child_name = (const char*)COMPACT_V2_OFFSET_TO_PTR(string_base, COMPACT_V2_NAME_OFF(child));
        if (child_name && strcmp(child_name, name) == 0) {
            return child;
        }
        child = COMPACT_V2_NEXT_SIBLING(node_base, child);
    }

    return NULL;
}

/* ============================================================================
 * Namespace Accessors - V2
 * ============================================================================ */

/**
 * Get namespace URI from compact element v2
 * TODO: Implement namespace resolution for v2
 */
const char* compact_element_get_namespace(
    struct compact_element_v2* elem,
    struct taurus_document* doc
) {
    /* TODO: V2 doesn't have namespace_offset field - need to implement resolution */
    (void)elem;
    (void)doc;
    return NULL;
}

/* ============================================================================
 * Document Root Accessor - V2
 * ============================================================================ */

/**
 * Get root compact element v2 from document - COMPACT-ONLY
 * NOTE: compact_root_offset uses UINT32_MAX for "no root" (0 is valid offset)
 */
struct compact_element_v2* compact_document_get_root(struct taurus_document* doc) {
    if (!doc || doc->compact_root_offset == UINT32_MAX) return NULL;

    char* base = get_compact_base(doc);
    if (!base) return NULL;

    return (struct compact_element_v2*)COMPACT_V2_OFFSET_TO_PTR(base, doc->compact_root_offset);
}

/* ============================================================================
 * Dispatch Layer Helpers - V2
 * ============================================================================ */

/**
 * Check if document is in compact mode - COMPACT-ONLY (always returns 1)
 */
int compact_is_compact_mode(struct taurus_document* doc) {
    /* COMPACT-ONLY: Always true if document exists */
    return doc != NULL;
}

/**
 * Get the compact element v2 from a TaurusElement handle - DEPRECATED
 *
 * POINTER-ONLY: This function is no longer needed as all data is in the wrapper.
 * Returns NULL always.
 */
struct compact_element_v2* compact_from_element(
    struct taurus_element* elem,
    struct taurus_document* doc
) {
    /* POINTER-ONLY: No compact offset field exists anymore */
    (void)elem;
    (void)doc;
    return NULL;
}

/**
 * Get element text content from compact element v2
 * TODO: Implement text content retrieval for v2
 */
const char* compact_element_get_text(
    struct compact_element_v2* elem,
    struct taurus_document* doc
) {
    /* TODO: Implement text content retrieval from v2 format */
    (void)elem;
    (void)doc;
    return NULL;
}
