/* compact_element_v3.h - 20-Byte Compact DOM Element Structure (v3)
 * Copyright (c) 2026, Ribose Inc.
 *
 * 20-BYTE ELEMENT ARCHITECTURE (v3):
 *
 * KEY INSIGHT: The v2 16-byte structure mixed attributes with children
 * in first_child chain, requiring a high-bit type check on every child
 * traversal. This added 1-2 cycles of overhead per child access.
 *
 * v3 SOLUTION: Separate attribute storage with dedicated first_attr field.
 * - No type checks needed during child traversal
 * - 20 bytes still beats pugixml's ~28 bytes
 * - Better cache efficiency for traversal-heavy workloads
 *
 * Memory comparison:
 * - Legacy taurus_element: ~168 bytes
 * - compact_element (v1): 36 bytes
 * - compact_element_v2: 16 bytes (but mixed attrs/children)
 * - compact_element_v3: 20 bytes (separate attrs)
 * - pugixml node: ~28 bytes
 */

#ifndef TAURUS_COMPACT_ELEMENT_V3_H
#define TAURUS_COMPACT_ELEMENT_V3_H

#include <stdint.h>
#include <stddef.h>
#include <string.h>

/* ============================================================================
 * 20-Byte Compact Element Structure (v3)
 * ============================================================================ */

/**
 * Compact element v3 - 20 bytes, separate attribute storage
 *
 * Design principles:
 * 1. Tree navigation uses 4-byte offsets (12 bytes)
 * 2. Name reference uses single offset (4 bytes)
 * 3. Attribute chain is SEPARATE from children (4 bytes)
 * 4. No type checks needed for child traversal
 *
 * Memory layout:
 * - first_child: offset to first child (elements and text only, NO attributes)
 * - next_sibling: offset to next sibling
 * - parent: offset to parent
 * - name_offset: offset to null-terminated name
 * - first_attr: offset to first attribute (separate chain)
 */
struct compact_element_v3 {
    uint32_t first_child;    /* Offset to first child (elements/text only) */
    uint32_t next_sibling;   /* Offset to next sibling, 0 if none */
    uint32_t parent;         /* Offset to parent, 0 if root */
    uint32_t name_offset;    /* Offset to null-terminated name */
    uint32_t first_attr;     /* Offset to first attribute, 0 if none (SEPARATE!) */
};

/* ============================================================================
 * 16-Byte Compact Attribute Structure (v3)
 * ============================================================================ */

/**
 * Compact attribute v3 - 16 bytes
 *
 * Attributes are in a SEPARATE chain from children.
 * Linked via first_attr in element, not first_child.
 */
struct compact_attribute_v3 {
    uint32_t name_offset;    /* Offset to null-terminated name */
    uint32_t value_offset;   /* Offset to null-terminated value */
    uint32_t next_attr;      /* Offset to next attribute, 0 if none */
    uint32_t reserved;       /* Reserved for future use (namespace info) */
};

/* ============================================================================
 * 16-Byte Compact Text Node Structure (v3)
 * ============================================================================ */

/**
 * Compact text node v3 - 16 bytes
 *
 * Layout unchanged from v2. next_sibling at offset 4 for fast linking.
 */
struct compact_text_v3 {
    uint32_t text_offset;    /* Offset to null-terminated text content */
    uint32_t next_sibling;   /* Offset to next sibling node */
    uint32_t text_length;    /* Length of text (for whitespace detection) */
    uint32_t flags;          /* Node type (text, cdata, comment, pi) */
};

/* ============================================================================
 * Node Type Flags
 * ============================================================================ */

#define COMPACT_V3_TYPE_ELEMENT   0x00000000
#define COMPACT_V3_TYPE_TEXT      0x00000001
#define COMPACT_V3_TYPE_CDATA     0x00000002
#define COMPACT_V3_TYPE_COMMENT   0x00000003
#define COMPACT_V3_TYPE_PI        0x00000004

/* ============================================================================
 * Accessor Macros for v3
 * ============================================================================ */

/**
 * Convert offset to pointer
 */
#define COMPACT_V3_OFFSET_TO_PTR(base, offset) \
    ((offset) ? (void*)((char*)(base) + (offset)) : NULL)

/**
 * Convert pointer to offset
 */
#define COMPACT_V3_PTR_TO_OFFSET(base, ptr) \
    ((ptr) ? (uint32_t)((char*)(ptr) - (char*)(base)) : 0)

/**
 * Get element name from v3 element
 */
#define COMPACT_V3_ELEMENT_NAME(base, elem) \
    ((const char*)(base) + (elem)->name_offset)

/**
 * Get first child of v3 element (elements/text only, NO attributes)
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
 * Get first attribute of v3 element
 */
#define COMPACT_V3_FIRST_ATTR(base, elem) \
    ((struct compact_attribute_v3*)COMPACT_V3_OFFSET_TO_PTR(base, (elem)->first_attr))

/**
 * Calculate child count by walking the list
 * NOTE: v3 does NOT include attributes in child count (unlike v2)
 */
static inline uint16_t compact_v3_child_count(const struct compact_element_v3* elem,
                                               const void* base) {
    uint16_t count = 0;
    uint32_t child_off = elem->first_child;

    while (child_off != 0) {
        count++;
        const struct compact_element_v3* child =
            (const struct compact_element_v3*)((const char*)base + child_off);
        child_off = child->next_sibling;
    }

    return count;
}

/**
 * Calculate attribute count by walking the list
 */
static inline uint16_t compact_v3_attr_count(const struct compact_element_v3* elem,
                                              const void* base) {
    uint16_t count = 0;
    uint32_t attr_off = elem->first_attr;

    while (attr_off != 0) {
        count++;
        const struct compact_attribute_v3* attr =
            (const struct compact_attribute_v3*)((const char*)base + attr_off);
        attr_off = attr->next_attr;
    }

    return count;
}

/**
 * Get attribute by name (linear search)
 * For O(1) attribute access, use hash table in higher-level API
 */
static inline const struct compact_attribute_v3* compact_v3_get_attr_by_name(
    const struct compact_element_v3* elem,
    const void* base,
    const char* name) {

    uint32_t attr_off = elem->first_attr;

    while (attr_off != 0) {
        const struct compact_attribute_v3* attr =
            (const struct compact_attribute_v3*)((const char*)base + attr_off);

        const char* attr_name = (const char*)base + attr->name_offset;
        if (strcmp(attr_name, name) == 0) {
            return attr;
        }

        attr_off = attr->next_attr;
    }

    return NULL;
}

/* ============================================================================
 * Size Constants
 * ============================================================================ */

#define COMPACT_V3_ELEMENT_SIZE  20
#define COMPACT_V3_ATTR_SIZE     16
#define COMPACT_V3_TEXT_SIZE     16

#endif /* TAURUS_COMPACT_ELEMENT_V3_H */
