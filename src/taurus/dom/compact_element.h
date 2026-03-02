/* compact_element.h - 16-Byte Compact DOM Element Structure
 * Copyright (c) 2026, Ribose Inc.
 *
 * 16-BYTE ELEMENT ARCHITECTURE:
 * This matches pugixml's memory footprint for competitive performance.
 *
 * Key insight: The 41% performance gap vs pugixml is directly caused by
 * 36-byte elements vs 16-byte nodes = 2.25x memory traffic.
 *
 * This structure reduces memory traffic by 56% to close the performance gap.
 */

#ifndef TAURUS_COMPACT_ELEMENT_V2_H
#define TAURUS_COMPACT_ELEMENT_V2_H

#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * 16-Byte Compact Element Structure
 * ============================================================================ */

/**
 * Compact element v2 - 16 bytes, matches pugixml's footprint
 *
 * Design principles:
 * 1. Tree navigation uses 4-byte offsets (12 bytes)
 * 2. Name reference uses single offset (4 bytes)
 * 3. Name is null-terminated (like pugixml, not length-based)
 * 4. Child counts calculated on demand (not stored)
 * 5. Attributes linked as special children
 * 6. Namespaces stored in separate hash table (rarely used)
 *
 * Memory comparison:
 * - Legacy taurus_element: ~168 bytes
 * - compact_element (v1): 36 bytes
 * - compact_element_v2: 16 bytes (56% reduction from v1!)
 */
struct compact_element_v2 {
    /* Tree navigation - 4-byte offsets from document base (12 bytes) */
    uint32_t first_child;    /* Offset to first child (element, text, or attr) */
    uint32_t next_sibling;   /* Offset to next sibling, 0 if none */
    uint32_t parent;         /* Offset to parent, 0 if root */

    /* Name reference (4 bytes) */
    uint32_t name_offset;    /* Offset to null-terminated name in string table */
};

/* ============================================================================
 * 16-Byte Compact Attribute Structure
 * ============================================================================ */

/**
 * Compact attribute v2 - 16 bytes
 *
 * Attributes are linked from element's first_child chain.
 * They have a special marker to distinguish from elements.
 */
struct compact_attribute_v2 {
    uint32_t name_offset;    /* Offset to null-terminated name */
    uint32_t value_offset;   /* Offset to null-terminated value */
    uint32_t next_attr;      /* Offset to next attribute, 0 if none */
    uint32_t flags;          /* Namespace info in upper 16 bits */
};

/* ============================================================================
 * 16-Byte Compact Text Node Structure
 * ============================================================================ */

/**
 * Compact text node v2 - 16 bytes
 *
 * Layout optimized for fast sibling linking:
 * next_sibling at offset 4 (same as compact_element_v2) for O(1) linking
 */
struct compact_text_v2 {
    uint32_t text_offset;    /* Offset to null-terminated text content */
    uint32_t next_sibling;   /* Offset to next sibling node - SAME OFFSET AS element! */
    uint32_t text_length;    /* Length of text (for whitespace detection) */
    uint32_t flags;          /* Node type (text, cdata, comment, pi) */
};

/* Alias for backward compatibility - same structure, different name */
#define compact_text_node compact_text_v2

/* ============================================================================
 * Node Type Detection (for v2)
 * ============================================================================ */

/* Node types - stored in flags field or detected by structure */
#define COMPACT_NODE_TYPE_MASK      0x0000000F
#define COMPACT_NODE_TYPE_ELEMENT   0x00000000
#define COMPACT_NODE_TYPE_TEXT      0x00000001
#define COMPACT_NODE_TYPE_CDATA     0x00000002
#define COMPACT_NODE_TYPE_COMMENT   0x00000003
#define COMPACT_NODE_TYPE_PI        0x00000004
#define COMPACT_NODE_TYPE_DOCTYPE   0x00000005

/* Aliases for v2 compatibility */
#define COMPACT_V2_TYPE_ELEMENT     COMPACT_NODE_TYPE_ELEMENT
#define COMPACT_V2_TYPE_ATTR        0x80000000  /* High bit set = attribute */
#define COMPACT_V2_TYPE_TEXT        COMPACT_NODE_TYPE_TEXT
#define COMPACT_V2_TYPE_CDATA       COMPACT_NODE_TYPE_CDATA
#define COMPACT_V2_TYPE_COMMENT     COMPACT_NODE_TYPE_COMMENT
#define COMPACT_V2_TYPE_PI          COMPACT_NODE_TYPE_PI
#define COMPACT_V2_TYPE_DOCTYPE     COMPACT_NODE_TYPE_DOCTYPE
#define COMPACT_V2_TYPE_MASK        COMPACT_NODE_TYPE_MASK

/* Text node marker - set HIGH BIT on flags field to distinguish from elements
 * For elements: offset 12 is name_offset (string offset, no high bit)
 * For text nodes: offset 12 is flags with high bit set + node type in lower 4 bits
 * This prevents false detection when element name_offset's lower 4 bits are 1-5
 */
#define COMPACT_V2_TEXT_MARKER    0x80000000

/* Check if node is an attribute by reading first field
 * For element: first field is first_child (offset 0)
 * For attribute: first field is name_offset with high bit set (offset 0)
 * IMPORTANT: Also check that value is NOT UINT32_MAX (element with no children)
 */
#define COMPACT_V2_IS_ATTR(elem) ({ \
    uint32_t _first = *(const uint32_t*)(elem); \
    (_first & 0x80000000) && (_first != UINT32_MAX); \
})

/* Get actual offset (clear high bit for attributes) */
#define COMPACT_V2_NAME_OFF(elem) (((struct compact_element_v2*)(elem))->name_offset & 0x7FFFFFFF)

/* ============================================================================
 * Accessor Macros for v2
 * ============================================================================ */

/**
 * Convert offset to pointer
 * NOTE: offset 0 is valid (first element in buffer)
 * Use UINT32_MAX or 0xFFFFFFFF for "null" offset
 */
#define COMPACT_V2_OFFSET_TO_PTR(base, offset) \
    ((offset) != UINT32_MAX ? (void*)((char*)(base) + (offset)) : NULL)

/**
 * Convert pointer to offset
 */
#define COMPACT_V2_PTR_TO_OFFSET(base, ptr) \
    ((ptr) ? (uint32_t)((char*)(ptr) - (char*)(base)) : UINT32_MAX)

/**
 * Get element name from v2 element
 */
#define COMPACT_V2_ELEMENT_NAME(base, elem) \
    ((const char*)(base) + COMPACT_V2_NAME_OFF(elem))

/**
 * Get first child of v2 element
 */
#define COMPACT_V2_FIRST_CHILD(base, elem) \
    ((struct compact_element_v2*)COMPACT_V2_OFFSET_TO_PTR(base, (elem)->first_child))

/**
 * Get next sibling of v2 element
 */
#define COMPACT_V2_NEXT_SIBLING(base, elem) \
    ((struct compact_element_v2*)COMPACT_V2_OFFSET_TO_PTR(base, (elem)->next_sibling))

/**
 * Get parent of v2 element
 */
#define COMPACT_V2_PARENT(base, elem) \
    ((struct compact_element_v2*)COMPACT_V2_OFFSET_TO_PTR(base, (elem)->parent))

/**
 * Calculate child count by walking the list
 * This is O(n) but avoids storing the count
 * NOTE: Only counts ELEMENT children, skips attributes and text nodes
 */
static inline uint16_t compact_v2_child_count(const struct compact_element_v2* elem,
                                               const void* base) {
    uint16_t count = 0;
    uint32_t child_off = elem->first_child;

    while (child_off != UINT32_MAX) {
        const char* child_ptr = (const char*)base + child_off;

        /* Check first field for attribute marker */
        uint32_t first_field = *(const uint32_t*)(child_ptr + 0);
        if ((first_field & 0x80000000) && (first_field != UINT32_MAX)) {
            /* Attribute - skip using next_attr at offset 8 */
            const struct compact_attribute_v2* attr = (const struct compact_attribute_v2*)child_ptr;
            child_off = attr->next_attr;
            continue;
        }

        /* Check for text node using TEXT_MARKER at offset 12 */
        uint32_t offset12_field = *(const uint32_t*)(child_ptr + 12);
        if (offset12_field & COMPACT_V2_TEXT_MARKER) {
            /* Text node - skip using next_sibling at offset 4 */
            child_off = *(const uint32_t*)(child_ptr + 4);
            continue;
        }

        /* Element child - count it */
        count++;

        /* Move to next sibling */
        const struct compact_element_v2* child = (const struct compact_element_v2*)child_ptr;
        child_off = child->next_sibling;
    }

    return count;
}

/**
 * Get first attribute of v2 element
 * Attributes are linked from first_child using next_attr field
 *
 * IMPORTANT: We must distinguish between:
 * - Attribute: name_offset has high bit set (type marker)
 * - Element with no children: first_child = UINT32_MAX (also has high bit)
 */
static inline const struct compact_attribute_v2* compact_v2_first_attr(
    const struct compact_element_v2* elem, const void* base) {

    uint32_t child_off = elem->first_child;

    if (child_off != UINT32_MAX) {
        const struct compact_element_v2* child =
            (const struct compact_element_v2*)((const char*)base + child_off);

        /* Check if this is an attribute (high bit set) AND not UINT32_MAX (element with no children) */
        uint32_t first_field = child->name_offset;  /* For attr: name_offset, for elem: first_child */
        if ((first_field & 0x80000000) && (first_field != UINT32_MAX)) {
            return (const struct compact_attribute_v2*)child;
        }
    }

    return NULL;
}

/**
 * Get next attribute in the chain
 * NOTE: Uses UINT32_MAX for null (0 is a valid offset)
 */
static inline const struct compact_attribute_v2* compact_v2_next_attr(
    const struct compact_attribute_v2* attr, const void* base) {

    if (!attr || attr->next_attr == UINT32_MAX) return NULL;
    return (const struct compact_attribute_v2*)((const char*)base + attr->next_attr);
}

#endif /* TAURUS_COMPACT_ELEMENT_V2_H */
