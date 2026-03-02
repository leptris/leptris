/* compact_element_v3.h - 20-Byte Compact DOM Element Structure
 * Copyright (c) 2026, Ribose Inc.
 *
 * 20-BYTE ELEMENT ARCHITECTURE WITH SEPARATE ATTRIBUTE STORAGE:
 *
 * KEY DESIGN CHANGE FROM v2:
 * - v2 (16 bytes): Mixed attribute/child storage in first_child
 * - v3 (20 bytes): Separate first_attr field for attributes
 *
 * WHY THIS MATTERS FOR PERFORMANCE:
 * - v2 requires O(n) attribute chain walking for every element
 * - v3 gives O(1) access to both children AND attributes
 * - Eliminates high-bit checking on every first_child access
 * - Simpler parser code = faster execution
 *
 * Memory comparison:
 * - pugixml: ~28 bytes per element
 * - v3: 20 bytes per element (28% smaller)
 */

#ifndef TAURUS_COMPACT_ELEMENT_V3_H
#define TAURUS_COMPACT_ELEMENT_V3_H

#include <stdint.h>
#include <stddef.h>

/* ============================================================================
 * 20-Byte Compact Element Structure v3
 * ============================================================================ */

/**
 * Compact element v3 - 20 bytes with separate attribute storage
 *
 * Design principles:
 * 1. Tree navigation uses 4-byte offsets (12 bytes)
 * 2. Separate attribute pointer (4 bytes) - KEY CHANGE FROM v2
 * 3. Name reference uses single offset (4 bytes)
 * 4. All offsets use UINT32_MAX for null (0 is valid offset)
 */
struct compact_element_v3 {
    uint32_t first_child;    /* Offset to first CHILD element (or UINT32_MAX) */
    uint32_t next_sibling;   /* Offset to next sibling element */
    uint32_t parent;         /* Offset to parent element */
    uint32_t first_attr;     /* Offset to first attribute (or UINT32_MAX) */
    uint32_t name_offset;    /* Offset to null-terminated name in string table */
};

/* ============================================================================
 * 16-Byte Compact Attribute Structure (unchanged from v2)
 * ============================================================================ */

struct compact_attribute_v3 {
    uint32_t name_offset;    /* Offset to null-terminated name (NO high bit) */
    uint32_t value_offset;   /* Offset to null-terminated value */
    uint32_t next_attr;      /* Offset to next attribute (or UINT32_MAX) */
    uint32_t flags;          /* Namespace info in upper 16 bits */
};

/* ============================================================================
 * 16-Byte Compact Text Node Structure (unchanged from v2)
 * ============================================================================ */

struct compact_text_v3 {
    uint32_t text_offset;    /* Offset to null-terminated text content */
    uint32_t next_sibling;   /* Offset to next sibling node */
    uint32_t text_length;    /* Length of text (for whitespace detection) */
    uint32_t flags;          /* Node type (text, cdata, comment, pi) */
};

/* ============================================================================
 * Node Type Detection (unchanged from v2)
 * ============================================================================ */

#define COMPACT_V3_TYPE_MASK      0x0000000F
#define COMPACT_V3_TYPE_ELEMENT   0x00000000
#define COMPACT_V3_TYPE_TEXT      0x00000001
#define COMPACT_V3_TYPE_CDATA     0x00000002
#define COMPACT_V3_TYPE_COMMENT   0x00000003
#define COMPACT_V3_TYPE_PI        0x00000004
#define COMPACT_V3_TYPE_DOCTYPE   0x00000005

/* Text node marker - set HIGH BIT on flags field to distinguish from elements */
#define COMPACT_V3_TEXT_MARKER    0x80000000

/* ============================================================================
 * Accessor Macros for v3
 * ============================================================================ */

/**
 * Convert offset to pointer
 * NOTE: offset 0 is valid (first element in buffer)
 * Use UINT32_MAX for "null" offset
 */
#define COMPACT_V3_OFFSET_TO_PTR(base, offset) \
    ((offset) != UINT32_MAX ? (void*)((char*)(base) + (offset)) : NULL)

/**
 * Convert pointer to offset
 */
#define COMPACT_V3_PTR_TO_OFFSET(base, ptr) \
    ((ptr) ? (uint32_t)((char*)(ptr) - (char*)(base)) : UINT32_MAX)

/**
 * Get element name from v3 element
 */
#define COMPACT_V3_ELEMENT_NAME(base, elem) \
    ((const char*)(base) + (elem)->name_offset)

/**
 * Get first child of v3 element - SIMPLE, no high-bit checking!
 */
#define COMPACT_V3_FIRST_CHILD(base, elem) \
    ((struct compact_element_v3*)COMPACT_V3_OFFSET_TO_PTR(base, (elem)->first_child))

/**
 * Get next sibling of v3 element
 */
#define COMPACT_V3_NEXT_SIBLING(base, elem) \
    ((struct compact_element_v3*)COMPACT_V3_OFFSET_TO_PTR(base, (elem)->next_sibling))

/**
 * Get parent of v3 element
 */
#define COMPACT_V3_PARENT(base, elem) \
    ((struct compact_element_v3*)COMPACT_V3_OFFSET_TO_PTR(base, (elem)->parent))

/**
 * Get first attribute of v3 element - SIMPLE, direct access!
 */
#define COMPACT_V3_FIRST_ATTR(base, elem) \
    ((struct compact_attribute_v3*)COMPACT_V3_OFFSET_TO_PTR(base, (elem)->first_attr))

/**
 * Check if element has attributes
 */
#define COMPACT_V3_HAS_ATTRS(elem) ((elem)->first_attr != UINT32_MAX)

/**
 * Check if element has children
 */
#define COMPACT_V3_HAS_CHILDREN(elem) ((elem)->first_child != UINT32_MAX)

/* ============================================================================
 * Inline Helper Functions
 * ============================================================================ */

/**
 * Get first attribute of v3 element
 * O(1) - no chain walking, no high-bit checking!
 */
static inline const struct compact_attribute_v3* compact_v3_first_attr(
    const struct compact_element_v3* elem, const void* base) {
    return COMPACT_V3_FIRST_ATTR(base, elem);
}

/**
 * Get next attribute in the chain
 */
static inline const struct compact_attribute_v3* compact_v3_next_attr(
    const struct compact_attribute_v3* attr, const void* base) {
    if (!attr || attr->next_attr == UINT32_MAX) return NULL;
    return (const struct compact_attribute_v3*)((const char*)base + attr->next_attr);
}

/**
 * Calculate child count by walking the list
 * Simpler now - no attribute checking needed!
 */
static inline uint16_t compact_v3_child_count(const struct compact_element_v3* elem,
                                               const void* base) {
    uint16_t count = 0;
    uint32_t child_off = elem->first_child;

    while (child_off != UINT32_MAX) {
        const char* child_ptr = (const char*)base + child_off;

        /* Check for text node using TEXT_MARKER at offset 12 */
        uint32_t offset12_field = *(const uint32_t*)(child_ptr + 12);
        if (offset12_field & COMPACT_V3_TEXT_MARKER) {
            /* Text node - skip using next_sibling at offset 4 */
            child_off = *(const uint32_t*)(child_ptr + 4);
            continue;
        }

        /* Element child - count it */
        count++;

        /* Move to next sibling */
        const struct compact_element_v3* child = (const struct compact_element_v3*)child_ptr;
        child_off = child->next_sibling;
    }

    return count;
}

/**
 * Calculate attribute count
 */
static inline uint16_t compact_v3_attr_count(const struct compact_element_v3* elem,
                                              const void* base) {
    uint16_t count = 0;
    uint32_t attr_off = elem->first_attr;

    while (attr_off != UINT32_MAX) {
        count++;
        const struct compact_attribute_v3* attr =
            (const struct compact_attribute_v3*)((const char*)base + attr_off);
        attr_off = attr->next_attr;
    }

    return count;
}

#endif /* TAURUS_COMPACT_ELEMENT_V3_H */
