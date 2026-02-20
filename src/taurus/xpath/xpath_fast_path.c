/* xpath_fast_path.c - Fast path XPath evaluation for common patterns
 * Copyright (c) 2024, Ribose Inc.
 *
 * PERFORMANCE: Detects common XPath patterns and bypasses full parsing.
 * Provides 10-50x speedup for simple expressions like "child::*" or "@attr".
 */

#include "xpath_fast_path.h"
#include "evaluator.h"
#include "evaluator_internal.h"
#include "../taurus_internal.h"
#include "../dom/element.h"
#include "../common/string_view.h"
#include <string.h>
#include <ctype.h>

/* ============================================================================
 * Pattern Detection
 * ============================================================================ */

/* Skip leading whitespace */
static inline const char* skip_ws(const char* s, const char* end) {
    while (s < end && (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')) s++;
    return s;
}

/* Check if string starts with prefix (case-sensitive) */
static inline int starts_with_len(const char* s, size_t s_len,
                                   const char* prefix, size_t prefix_len) {
    if (s_len < prefix_len) return 0;
    return memcmp(s, prefix, prefix_len) == 0;
}

/* Check if character is valid XML name start */
static inline int is_name_start(char c) {
    return isalpha((unsigned char)c) || c == '_' || c == ':';
}

/* Check if character is valid XML name character */
static inline int is_name_char(char c) {
    return is_name_start(c) || isdigit((unsigned char)c) ||
           c == '-' || c == '.' || c == '\xB7';
}

/* Find end of XML name, stopping at :: (axis separator) */
static inline const char* find_name_end(const char* s, const char* end) {
    if (s >= end || !is_name_start(*s)) return s;
    s++;
    while (s < end && is_name_char(*s)) {
        /* Stop at :: (axis separator) */
        if (s[0] == ':' && s + 1 < end && s[1] == ':') {
            break;
        }
        s++;
    }
    return s;
}

/* Check if remaining content is empty (with whitespace check) */
static inline int is_end_of_expr(const char* s, const char* end) {
    s = skip_ws(s, end);
    return s >= end;
}

/* Forward declarations for recursive helper functions */
static void collect_descendants_fast(TaurusElement elem, XPathNodeSet* result,
                                    const char* name, size_t name_len, int star);
static void collect_descendants_filter(TaurusElement elem, XPathNodeSet* result,
                                     const char* name, size_t name_len, int star);
static void collect_descendants_attrs(TaurusElement elem, XPathNodeSet* result,
                                     const char* elem_name, size_t elem_name_len,
                                     const char* attr_name, size_t attr_name_len, int star);
static void collect_descendants_children(TaurusElement elem, XPathNodeSet* result,
                                         const char* name, size_t name_len, int star);
static void collect_descendants_children_name(TaurusElement elem, XPathNodeSet* result,
                                              const char* elem_name, size_t elem_name_len,
                                              const char* child_name, size_t child_name_len);

/**
 * Detect fast path pattern from expression string
 *
 * This is a performance-critical function. We want to detect common
 * patterns as quickly as possible without any memory allocation.
 */
int xpath_fast_path_detect(const char* expr, size_t len, XPathFastPathPattern* pattern) {
    if (!expr || len == 0 || !pattern) return 0;

    /* Initialize pattern */
    pattern->type = XPATH_FAST_PATH_NONE;
    pattern->name = NULL;
    pattern->name_len = 0;
    pattern->is_absolute = 0;
    pattern->second_name = NULL;
    pattern->second_name_len = 0;

    const char* s = expr;
    const char* end = expr + len;

    /* Skip leading whitespace */
    s = skip_ws(s, end);
    if (s >= end) return 0;

    /* Check for absolute path */
    if (*s == '/') {
        pattern->is_absolute = 1;
        s++;
        if (s >= end) return 0;

        /* Check for descendant-or-self (//) */
        if (*s == '/') {
            s++;
            s = skip_ws(s, end);
            if (s >= end) return 0;

            /* //* or //name */
            if (*s == '*') {
                s++;
                s = skip_ws(s, end);
                /* Check for multi-step: //* /step */
                if (s < end && *s == '/') {
                    s++;
                    s = skip_ws(s, end);
                    if (s >= end) return 0;
                    /* Check for self::*, @attr, or child step */
                    if (*s == '.' || (s[0] == 's' && starts_with_len(s, end - s, "self::", 6))) {
                        if (*s == '.') s++;
                        else s += 6;
                        s = skip_ws(s, end);
                        if (s < end && *s == '*') s++;
                        if (!is_end_of_expr(s, end)) return 0;
                        pattern->type = XPATH_FAST_PATH_DESCENDANT_SELF;
                        return 1;
                    }
                    if (*s == '@') {
                        s++;
                        s = skip_ws(s, end);
                        if (s >= end) return 0;
                        if (*s == '*') {
                            s++;
                            if (!is_end_of_expr(s, end)) return 0;
                            pattern->type = XPATH_FAST_PATH_DESCENDANT_ATTR;
                            return 1;
                        }
                        if (is_name_start(*s)) {
                            const char* name_end = find_name_end(s, end);
                            if (!is_end_of_expr(name_end, end)) return 0;
                            pattern->type = XPATH_FAST_PATH_DESCENDANT_ATTR;
                            pattern->second_name = s;
                            pattern->second_name_len = name_end - s;
                            return 1;
                        }
                    }
                    return 0;
                }
                if (!is_end_of_expr(s, end)) return 0;
                pattern->type = XPATH_FAST_PATH_DESCENDANT_STAR;
                return 1;
            }
            if (is_name_start(*s)) {
                const char* name_end = find_name_end(s, end);
                const char* rest = skip_ws(name_end, end);

                /* Check if name has a namespace prefix (contains single :) */
                /* If so, don't match in fast path - let full parser handle it */
                int has_ns_prefix = 0;
                for (const char* p = s; p < name_end; p++) {
                    if (*p == ':') {
                        /* Check it's not :: (axis separator) */
                        if (p + 1 >= name_end || p[1] != ':') {
                            has_ns_prefix = 1;
                            break;
                        }
                    }
                }
                if (has_ns_prefix) {
                    return 0;  /* Let full parser handle namespaced names */
                }

                /* Check for multi-step: //name/step */
                if (rest < end && *rest == '/') {
                    const char* step_start = rest + 1;
                    step_start = skip_ws(step_start, end);
                    if (step_start >= end) return 0;

                    /* //name/self::* or //name/. */
                    if (*step_start == '.' || (step_start[0] == 's' && starts_with_len(step_start, end - step_start, "self::", 6))) {
                        const char* after_step;
                        if (*step_start == '.') {
                            after_step = step_start + 1;
                        } else {
                            after_step = step_start + 6;
                            after_step = skip_ws(after_step, end);
                            if (after_step < end && *after_step == '*') after_step++;
                        }
                        if (!is_end_of_expr(after_step, end)) return 0;
                        pattern->type = XPATH_FAST_PATH_DESCENDANT_SELF;
                        pattern->name = s;
                        pattern->name_len = name_end - s;
                        return 1;
                    }

                    /* //name/@attr or //name/attribute::* */
                    if (*step_start == '@' || starts_with_len(step_start, end - step_start, "attribute::", 11)) {
                        const char* attr_start;
                        if (*step_start == '@') {
                            attr_start = step_start + 1;
                        } else {
                            attr_start = step_start + 11;
                        }
                        attr_start = skip_ws(attr_start, end);
                        if (attr_start >= end) return 0;

                        if (*attr_start == '*') {
                            attr_start++;
                            if (!is_end_of_expr(attr_start, end)) return 0;
                            pattern->type = XPATH_FAST_PATH_DESCENDANT_ATTR;
                            pattern->name = s;
                            pattern->name_len = name_end - s;
                            return 1;
                        }
                        if (is_name_start(*attr_start)) {
                            const char* attr_end = find_name_end(attr_start, end);
                            if (!is_end_of_expr(attr_end, end)) return 0;
                            pattern->type = XPATH_FAST_PATH_DESCENDANT_ATTR;
                            pattern->name = s;
                            pattern->name_len = name_end - s;
                            pattern->second_name = attr_start;
                            pattern->second_name_len = attr_end - attr_start;
                            return 1;
                        }
                    }

                    /* //name/* or //name/child::* */
                    if (*step_start == '*' || starts_with_len(step_start, end - step_start, "child::", 7)) {
                        const char* after_step;
                        if (*step_start == '*') {
                            after_step = step_start + 1;
                        } else {
                            after_step = step_start + 7;
                            after_step = skip_ws(after_step, end);
                            if (after_step < end && *after_step == '*') after_step++;
                        }
                        if (!is_end_of_expr(after_step, end)) return 0;
                        pattern->type = XPATH_FAST_PATH_DESCENDANT_CHILD;
                        pattern->name = s;
                        pattern->name_len = name_end - s;
                        return 1;
                    }

                    /* //name/child */
                    if (is_name_start(*step_start)) {
                        const char* child_end = find_name_end(step_start, end);
                        if (!is_end_of_expr(child_end, end)) return 0;
                        pattern->type = XPATH_FAST_PATH_DESCENDANT_CHILD_NAME;
                        pattern->name = s;
                        pattern->name_len = name_end - s;
                        pattern->second_name = step_start;
                        pattern->second_name_len = child_end - step_start;
                        return 1;
                    }

                    return 0;
                }

                if (!is_end_of_expr(name_end, end)) return 0;
                pattern->type = XPATH_FAST_PATH_DESCENDANT_NAME;
                pattern->name = s;
                pattern->name_len = name_end - s;
                return 1;
            }
            return 0;
        }

        s = skip_ws(s, end);
        if (s >= end) return 0;

        /* /name - root element lookup (only if entire expression consumed) */
        if (is_name_start(*s)) {
            const char* name_end = find_name_end(s, end);

            /* Check if there's more content after the name */
            const char* rest = skip_ws(name_end, end);
            if (rest < end) {
                /* More content exists - not a simple root lookup */
                return 0;
            }

            pattern->type = XPATH_FAST_PATH_ROOT_NAME;
            pattern->name = s;
            pattern->name_len = name_end - s;
            return 1;
        }
        return 0;
    }

    /* Check for parent (..) */
    if (*s == '.' && s + 1 < end && s[1] == '.') {
        s += 2;
        if (!is_end_of_expr(s, end)) return 0;
        pattern->type = XPATH_FAST_PATH_PARENT;
        return 1;
    }

    /* Check for self (.) */
    if (*s == '.') {
        s++;
        if (!is_end_of_expr(s, end)) return 0;
        pattern->type = XPATH_FAST_PATH_SELF;
        return 1;
    }

    /* Check for attribute (@) */
    if (*s == '@') {
        s++;
        s = skip_ws(s, end);
        if (s >= end) return 0;

        if (*s == '*') {
            s++;
            if (!is_end_of_expr(s, end)) return 0;
            pattern->type = XPATH_FAST_PATH_ATTR_STAR;
            return 1;
        }
        if (is_name_start(*s)) {
            const char* name_end = find_name_end(s, end);
            if (!is_end_of_expr(name_end, end)) return 0;
            pattern->type = XPATH_FAST_PATH_ATTR_NAME;
            pattern->name = s;
            pattern->name_len = name_end - s;
            return 1;
        }
        return 0;
    }

    /* Check for wildcard (*) */
    if (*s == '*') {
        s++;
        if (!is_end_of_expr(s, end)) return 0;
        pattern->type = XPATH_FAST_PATH_CHILD_STAR;
        return 1;
    }

    /* Check for axis:: prefix */
    if (starts_with_len(s, end - s, "child::", 7)) {
        s += 7;
        s = skip_ws(s, end);
        if (s >= end) return 0;

        if (*s == '*') {
            s++;
            if (!is_end_of_expr(s, end)) return 0;
            pattern->type = XPATH_FAST_PATH_CHILD_STAR;
            return 1;
        }
        if (starts_with_len(s, end - s, "text()", 6)) {
            s += 6;
            if (!is_end_of_expr(s, end)) return 0;
            pattern->type = XPATH_FAST_PATH_CHILD_TEXT;
            return 1;
        }
        if (is_name_start(*s)) {
            const char* name_end = find_name_end(s, end);
            if (!is_end_of_expr(name_end, end)) return 0;
            pattern->type = XPATH_FAST_PATH_CHILD_NAME;
            pattern->name = s;
            pattern->name_len = name_end - s;
            return 1;
        }
        return 0;
    }

    if (starts_with_len(s, end - s, "attribute::", 11) ||
        starts_with_len(s, end - s, "attr::", 6)) {
        s += (s[5] == ':' ? 11 : 6);
        s = skip_ws(s, end);
        if (s >= end) return 0;

        if (*s == '*') {
            s++;
            if (!is_end_of_expr(s, end)) return 0;
            pattern->type = XPATH_FAST_PATH_ATTR_STAR;
            return 1;
        }
        if (is_name_start(*s)) {
            const char* name_end = find_name_end(s, end);
            if (!is_end_of_expr(name_end, end)) return 0;
            pattern->type = XPATH_FAST_PATH_ATTR_NAME;
            pattern->name = s;
            pattern->name_len = name_end - s;
            return 1;
        }
        return 0;
    }

    if (starts_with_len(s, end - s, "parent::", 8)) {
        s += 8;
        s = skip_ws(s, end);
        if (s >= end) return 0;

        if (*s == '*') {
            s++;
            if (!is_end_of_expr(s, end)) return 0;
            pattern->type = XPATH_FAST_PATH_PARENT;
            return 1;
        }
        if (is_name_start(*s)) {
            const char* name_end = find_name_end(s, end);
            if (!is_end_of_expr(name_end, end)) return 0;
            pattern->type = XPATH_FAST_PATH_PARENT_NAME;
            pattern->name = s;
            pattern->name_len = name_end - s;
            return 1;
        }
        return 0;
    }

    if (starts_with_len(s, end - s, "self::", 6)) {
        s += 6;
        s = skip_ws(s, end);
        if (s >= end) return 0;

        if (*s == '*') {
            s++;
            if (!is_end_of_expr(s, end)) return 0;
            pattern->type = XPATH_FAST_PATH_SELF;
            return 1;
        }
        if (is_name_start(*s)) {
            const char* name_end = find_name_end(s, end);
            if (!is_end_of_expr(name_end, end)) return 0;
            pattern->type = XPATH_FAST_PATH_SELF_NAME;
            pattern->name = s;
            pattern->name_len = name_end - s;
            return 1;
        }
        return 0;
    }

    if (starts_with_len(s, end - s, "descendant::", 12)) {
        s += 12;
        s = skip_ws(s, end);
        if (s >= end) return 0;

        if (*s == '*') {
            s++;
            if (!is_end_of_expr(s, end)) return 0;
            pattern->type = XPATH_FAST_PATH_DESCENDANT_STAR;
            return 1;
        }
        if (is_name_start(*s)) {
            const char* name_end = find_name_end(s, end);
            if (!is_end_of_expr(name_end, end)) return 0;
            pattern->type = XPATH_FAST_PATH_DESCENDANT_NAME;
            pattern->name = s;
            pattern->name_len = name_end - s;
            return 1;
        }
        return 0;
    }

    if (starts_with_len(s, end - s, "ancestor::", 10)) {
        s += 10;
        s = skip_ws(s, end);
        if (s >= end) return 0;

        if (*s == '*') {
            s++;
            if (!is_end_of_expr(s, end)) return 0;
            pattern->type = XPATH_FAST_PATH_ANCESTOR_STAR;
            return 1;
        }
        if (is_name_start(*s)) {
            const char* name_end = find_name_end(s, end);
            if (!is_end_of_expr(name_end, end)) return 0;
            pattern->type = XPATH_FAST_PATH_ANCESTOR_NAME;
            pattern->name = s;
            pattern->name_len = name_end - s;
            return 1;
        }
        return 0;
    }

    /* Check for text() */
    if (starts_with_len(s, end - s, "text()", 6)) {
        s += 6;
        if (!is_end_of_expr(s, end)) return 0;
        pattern->type = XPATH_FAST_PATH_CHILD_TEXT;
        return 1;
    }

    /* Check for count(*) */
    if (starts_with_len(s, end - s, "count(", 6)) {
        s += 6;
        s = skip_ws(s, end);
        if (s >= end) return 0;

        if (*s == '*') {
            s++;
            s = skip_ws(s, end);
            if (s >= end || *s != ')') return 0;
            s++;
            if (!is_end_of_expr(s, end)) return 0;
            pattern->type = XPATH_FAST_PATH_COUNT_CHILDREN;
            return 1;
        }
        return 0;
    }

    /* Check for string() */
    if (starts_with_len(s, end - s, "string(", 7)) {
        s += 7;
        s = skip_ws(s, end);
        if (s >= end) return 0;
        if (*s == ')') {
            s++;
            if (!is_end_of_expr(s, end)) return 0;
            pattern->type = XPATH_FAST_PATH_STRING_VALUE;
            return 1;
        }
        /* string(.) case */
        if (*s == '.' && s + 1 < end && s[1] == ')') {
            s += 2;
            if (!is_end_of_expr(s, end)) return 0;
            pattern->type = XPATH_FAST_PATH_STRING_VALUE;
            return 1;
        }
        return 0;
    }

    /* Check for simple element name (child axis default) */
    if (is_name_start(*s)) {
        const char* name_end = find_name_end(s, end);

        /* Check if name has a namespace prefix (contains single :) */
        /* If so, don't match in fast path - let full parser handle it */
        int has_ns_prefix = 0;
        for (const char* p = s; p < name_end; p++) {
            if (*p == ':') {
                /* Check it's not :: (axis separator) */
                if (p + 1 >= name_end || p[1] != ':') {
                    has_ns_prefix = 1;
                    break;
                }
            }
        }
        if (has_ns_prefix) {
            return 0;  /* Let full parser handle namespaced names */
        }

        /* Make sure there's nothing else after the name */
        if (is_end_of_expr(name_end, end)) {
            pattern->type = XPATH_FAST_PATH_CHILD_NAME;
            pattern->name = s;
            pattern->name_len = name_end - s;
            return 1;
        }
    }

    return 0;
}

/* ============================================================================
 * Fast Path Evaluation
 * ============================================================================ */

/* Helper: Check if element name matches pattern name using StringView */
static inline int name_matches(TaurusElement elem, const char* name, size_t name_len) {
    if (!elem || !name) return 0;

    /* Check if element has a prefix - if so, it shouldn't match unprefixed name */
    TaurusStringView prefix = taurus_element_prefix_view(elem);
    if (!taurus_sv_is_empty(&prefix)) {
        /* Element has a namespace prefix, won't match unprefixed query */
        return 0;
    }

    /* Use StringView for O(1) comparison without conversion */
    TaurusStringView elem_name = taurus_element_name_view(elem);
    if (elem_name.length != name_len) return 0;

    return memcmp(elem_name.data, name, name_len) == 0;
}

/* Helper: Check if attribute name matches pattern name */
static inline int attr_name_matches(struct taurus_attribute* attr, const char* name, size_t name_len) {
    if (!attr || !name) return 0;

    /* Use StringView for O(1) comparison without conversion */
    TaurusStringView attr_name = taurus_attribute_name_view(attr);
    if (attr_name.length != name_len) return 0;

    return memcmp(attr_name.data, name, name_len) == 0;
}

/* Helper: Get attribute value as C string (converts if needed) */
static inline const char* attr_value_cstr(struct taurus_attribute* attr) {
    if (!attr) return NULL;

    /* Return cached value if available */
    if (attr->value) return attr->value;

    /* Convert StringView to C string */
    if (!taurus_sv_is_empty(&attr->value_view)) {
        size_t len = attr->value_view.length;
        char* str = TAURUS_ALLOC_N(char, len + 1);
        if (str) {
            memcpy(str, attr->value_view.data, len);
            str[len] = '\0';
            attr->value = str;  /* Cache for future use */
            return str;
        }
    }
    return NULL;
}

/* Helper: Get attribute name as C string (converts if needed) */
static inline const char* attr_name_cstr(struct taurus_attribute* attr) {
    if (!attr) return NULL;

    /* Return cached name if available */
    if (attr->name) return attr->name;

    /* Convert StringView to C string */
    if (!taurus_sv_is_empty(&attr->name_view)) {
        size_t len = attr->name_view.length;
        char* str = TAURUS_ALLOC_N(char, len + 1);
        if (str) {
            memcpy(str, attr->name_view.data, len);
            str[len] = '\0';
            attr->name = str;  /* Cache for future use */
            return str;
        }
    }
    return NULL;
}

/* Helper: Create number result */
static struct taurus_xpath_result* make_number_result(double value) {
    struct taurus_xpath_result* result = TAURUS_ALLOC(struct taurus_xpath_result);
    if (!result) return NULL;

    result->type = XPATH_RESULT_NUMBER;
    result->value.number_value = value;
    return result;
}

/* Helper: Create string result */
static struct taurus_xpath_result* make_string_result(const char* str) {
    struct taurus_xpath_result* result = TAURUS_ALLOC(struct taurus_xpath_result);
    if (!result) return NULL;

    result->type = XPATH_RESULT_STRING;
    result->value.string_value = str ? taurus_strdup(str) : taurus_strdup("");
    return result;
}

/* Helper: Create empty nodeset */
static struct taurus_xpath_result* make_empty_nodeset(void) {
    struct taurus_xpath_result* result = TAURUS_ALLOC(struct taurus_xpath_result);
    if (!result) return NULL;

    result->type = XPATH_RESULT_NODESET;
    result->value.nodeset_value = xpath_nodeset_new();
    if (!result->value.nodeset_value) {
        TAURUS_FREE(result);
        return NULL;
    }
    return result;
}

/* Helper: Create nodeset with single element */
static struct taurus_xpath_result* make_single_element_nodeset(TaurusElement elem) {
    struct taurus_xpath_result* result = make_empty_nodeset();
    if (!result) return NULL;

    if (elem) {
        xpath_nodeset_add(result->value.nodeset_value, elem);
    }
    return result;
}

/**
 * Evaluate fast path pattern directly
 */
struct taurus_xpath_result* xpath_fast_path_eval(
    struct taurus_document* doc,
    TaurusElement context,
    const XPathFastPathPattern* pattern
) {
    if (!doc || !pattern) return NULL;

    /* Get context element (default to root) */
    TaurusElement ctx = context ? context : taurus_document_root(doc);
    if (!ctx && pattern->type != XPATH_FAST_PATH_ROOT_NAME) return NULL;

    switch (pattern->type) {
        case XPATH_FAST_PATH_CHILD_STAR: {
            /* Return all element children */
            struct taurus_xpath_result* result = make_empty_nodeset();
            if (!result) return NULL;

            TaurusElement child = taurus_element_get_first_child(ctx);
            while (child) {
                TaurusNode* node = (TaurusNode*)child;
                if (node->type == TAURUS_NODE_TYPE_ELEMENT) {
                    xpath_nodeset_add(result->value.nodeset_value, child);
                }
                child = taurus_element_get_next_sibling(child);
            }
            return result;
        }

        case XPATH_FAST_PATH_CHILD_NAME: {
            /* Return children matching name */
            struct taurus_xpath_result* result = make_empty_nodeset();
            if (!result) return NULL;

            TaurusElement child = taurus_element_get_first_child(ctx);
            while (child) {
                TaurusNode* node = (TaurusNode*)child;
                if (node->type == TAURUS_NODE_TYPE_ELEMENT &&
                    name_matches(child, pattern->name, pattern->name_len)) {
                    xpath_nodeset_add(result->value.nodeset_value, child);
                }
                child = taurus_element_get_next_sibling(child);
            }
            return result;
        }

        case XPATH_FAST_PATH_CHILD_TEXT: {
            /* Return text content as a nodeset */
            const char* text = taurus_element_text(ctx);
            if (!text || text[0] == '\0') {
                return make_empty_nodeset();
            }

            /* Create text node */
            XPathTextNode* text_node = TAURUS_ALLOC(XPathTextNode);
            if (!text_node) return NULL;

            text_node->node_type = TAURUS_NODE_TEXT;
            text_node->content = taurus_strdup(text);
            text_node->owner = ctx;

            struct taurus_xpath_result* result = make_empty_nodeset();
            if (!result) {
                TAURUS_FREE(text_node);
                return NULL;
            }

            xpath_nodeset_add(result->value.nodeset_value, text_node);
            return result;
        }

        case XPATH_FAST_PATH_ATTR_STAR: {
            /* Return all attributes */
            struct taurus_xpath_result* result = make_empty_nodeset();
            if (!result) return NULL;

            struct taurus_attribute* attr = taurus_element_get_first_attribute(ctx);
            while (attr) {
                TaurusAttributeNode* attr_node = TAURUS_ALLOC(TaurusAttributeNode);
                if (attr_node) {
                    attr_node->node_type = TAURUS_NODE_ATTRIBUTE;
                    attr_node->name = taurus_strdup(attr_name_cstr(attr));
                    attr_node->value = taurus_strdup(attr_value_cstr(attr));
                    attr_node->namespace_uri = NULL;
                    attr_node->owner = ctx;
                    xpath_nodeset_add(result->value.nodeset_value, attr_node);
                }
                attr = attr->next;
            }
            return result;
        }

        case XPATH_FAST_PATH_ATTR_NAME: {
            /* Return specific attribute - use StringView comparison for speed */
            struct taurus_attribute* attr = taurus_element_get_first_attribute(ctx);
            while (attr) {
                if (attr_name_matches(attr, pattern->name, pattern->name_len)) {
                    break;
                }
                attr = attr->next;
            }

            if (!attr) return make_empty_nodeset();

            TaurusAttributeNode* attr_node = TAURUS_ALLOC(TaurusAttributeNode);
            if (!attr_node) return NULL;

            attr_node->node_type = TAURUS_NODE_ATTRIBUTE;
            attr_node->name = taurus_strdup(attr_name_cstr(attr));
            attr_node->value = taurus_strdup(attr_value_cstr(attr));
            attr_node->namespace_uri = NULL;
            attr_node->owner = ctx;

            struct taurus_xpath_result* result = make_empty_nodeset();
            if (!result) {
                TAURUS_FREE(attr_node);
                return NULL;
            }

            xpath_nodeset_add(result->value.nodeset_value, attr_node);
            return result;
        }

        case XPATH_FAST_PATH_PARENT: {
            TaurusElement parent = taurus_element_get_parent(ctx);
            return make_single_element_nodeset(parent);
        }

        case XPATH_FAST_PATH_PARENT_NAME: {
            TaurusElement parent = taurus_element_get_parent(ctx);
            if (parent && name_matches(parent, pattern->name, pattern->name_len)) {
                return make_single_element_nodeset(parent);
            }
            return make_empty_nodeset();
        }

        case XPATH_FAST_PATH_SELF: {
            return make_single_element_nodeset(ctx);
        }

        case XPATH_FAST_PATH_SELF_NAME: {
            if (name_matches(ctx, pattern->name, pattern->name_len)) {
                return make_single_element_nodeset(ctx);
            }
            return make_empty_nodeset();
        }

        case XPATH_FAST_PATH_ROOT_NAME: {
            /* Get root element and check name */
            TaurusElement root = taurus_document_root(doc);
            if (root && name_matches(root, pattern->name, pattern->name_len)) {
                return make_single_element_nodeset(root);
            }
            return make_empty_nodeset();
        }

        case XPATH_FAST_PATH_DESCENDANT_STAR:
        case XPATH_FAST_PATH_DESCENDANT_NAME: {
            /* Collect all descendants - for // (absolute), this is
             * descendant-or-self semantics from the document root.
             * Since we start from the root element (not document node),
             * we need to include the root element itself in the result. */
            struct taurus_xpath_result* result = make_empty_nodeset();
            if (!result) return NULL;

            /* Use a simple recursive collection */
            void collect_descendants_fast(TaurusElement elem, XPathNodeSet* result,
                                          const char* name, size_t name_len, int star);

            /* For absolute paths (//), include the context element itself first */
            if (pattern->is_absolute) {
                int star = (pattern->type == XPATH_FAST_PATH_DESCENDANT_STAR);
                if (star || name_matches(ctx, pattern->name, pattern->name_len)) {
                    xpath_nodeset_add(result->value.nodeset_value, ctx);
                }
            }

            collect_descendants_fast(ctx, result->value.nodeset_value,
                                     pattern->name, pattern->name_len,
                                     pattern->type == XPATH_FAST_PATH_DESCENDANT_STAR);
            return result;
        }

        case XPATH_FAST_PATH_ANCESTOR_STAR:
        case XPATH_FAST_PATH_ANCESTOR_NAME: {
            /* Collect ancestors */
            struct taurus_xpath_result* result = make_empty_nodeset();
            if (!result) return NULL;

            TaurusElement ancestor = taurus_element_get_parent(ctx);
            while (ancestor) {
                if (pattern->type == XPATH_FAST_PATH_ANCESTOR_STAR ||
                    name_matches(ancestor, pattern->name, pattern->name_len)) {
                    xpath_nodeset_add(result->value.nodeset_value, ancestor);
                }
                ancestor = taurus_element_get_parent(ancestor);
            }
            return result;
        }

        case XPATH_FAST_PATH_COUNT_CHILDREN: {
            size_t count = 0;
            TaurusElement child = taurus_element_get_first_child(ctx);
            while (child) {
                TaurusNode* node = (TaurusNode*)child;
                if (node->type == TAURUS_NODE_TYPE_ELEMENT) {
                    count++;
                }
                child = taurus_element_get_next_sibling(child);
            }
            return make_number_result((double)count);
        }

        case XPATH_FAST_PATH_STRING_VALUE: {
            const char* text = taurus_element_text(ctx);
            return make_string_result(text);
        }

        /* Multi-step patterns */
        case XPATH_FAST_PATH_DESCENDANT_SELF: {
            /* //name/self::* - return descendants with matching name */
            struct taurus_xpath_result* result = make_empty_nodeset();
            if (!result) return NULL;

            void collect_descendants_filter(TaurusElement elem, XPathNodeSet* result,
                                            const char* name, size_t name_len, int star);

            /* For absolute paths, include context if it matches */
            if (pattern->is_absolute) {
                if (pattern->name == NULL || pattern->name_len == 0 ||
                    name_matches(ctx, pattern->name, pattern->name_len)) {
                    xpath_nodeset_add(result->value.nodeset_value, ctx);
                }
            }

            collect_descendants_filter(ctx, result->value.nodeset_value,
                                       pattern->name, pattern->name_len,
                                       pattern->name == NULL);
            return result;
        }

        case XPATH_FAST_PATH_DESCENDANT_ATTR: {
            /* //name/@attr - get attribute from matching descendants */
            struct taurus_xpath_result* result = make_empty_nodeset();
            if (!result) return NULL;

            void collect_descendants_attrs(TaurusElement elem, XPathNodeSet* result,
                                           const char* elem_name, size_t elem_name_len,
                                           const char* attr_name, size_t attr_name_len, int star);

            /* For absolute paths, check context first */
            if (pattern->is_absolute) {
                if (pattern->name == NULL || pattern->name_len == 0 ||
                    name_matches(ctx, pattern->name, pattern->name_len)) {
                    /* Get the requested attribute(s) from context */
                    if (pattern->second_name == NULL) {
                        /* @* - all attributes */
                        struct taurus_attribute* attr = ctx->first_attribute;
                        while (attr) {
                            TaurusAttributeNode* attr_node = TAURUS_ALLOC(TaurusAttributeNode);
                            if (attr_node) {
                                attr_node->node_type = TAURUS_NODE_ATTRIBUTE;
                                attr_node->name = taurus_strdup(attr_name_cstr(attr));
                                attr_node->value = taurus_strdup(attr_value_cstr(attr));
                                attr_node->namespace_uri = NULL;
                                attr_node->owner = ctx;
                                xpath_nodeset_add(result->value.nodeset_value, attr_node);
                            }
                            attr = attr->next;
                        }
                    } else {
                        /* @name - specific attribute */
                        struct taurus_attribute* attr = ctx->first_attribute;
                        while (attr) {
                            if (attr_name_matches(attr, pattern->second_name, pattern->second_name_len)) {
                                TaurusAttributeNode* attr_node = TAURUS_ALLOC(TaurusAttributeNode);
                                if (attr_node) {
                                    attr_node->node_type = TAURUS_NODE_ATTRIBUTE;
                                    attr_node->name = taurus_strdup(attr_name_cstr(attr));
                                    attr_node->value = taurus_strdup(attr_value_cstr(attr));
                                    attr_node->namespace_uri = NULL;
                                    attr_node->owner = ctx;
                                    xpath_nodeset_add(result->value.nodeset_value, attr_node);
                                }
                                break;
                            }
                            attr = attr->next;
                        }
                    }
                }
            }

            collect_descendants_attrs(ctx, result->value.nodeset_value,
                                      pattern->name, pattern->name_len,
                                      pattern->second_name, pattern->second_name_len,
                                      pattern->second_name == NULL);
            return result;
        }

        case XPATH_FAST_PATH_DESCENDANT_CHILD: {
            /* //name/* - get children of matching descendants */
            struct taurus_xpath_result* result = make_empty_nodeset();
            if (!result) return NULL;

            void collect_descendants_children(TaurusElement elem, XPathNodeSet* result,
                                              const char* name, size_t name_len, int star);

            /* For absolute paths, check context first */
            if (pattern->is_absolute) {
                if (pattern->name == NULL || pattern->name_len == 0 ||
                    name_matches(ctx, pattern->name, pattern->name_len)) {
                    TaurusElement child = taurus_element_get_first_child(ctx);
                    while (child) {
                        xpath_nodeset_add(result->value.nodeset_value, child);
                        child = taurus_element_get_next_sibling(child);
                    }
                }
            }

            collect_descendants_children(ctx, result->value.nodeset_value,
                                         pattern->name, pattern->name_len,
                                         pattern->name == NULL);
            return result;
        }

        case XPATH_FAST_PATH_DESCENDANT_CHILD_NAME: {
            /* //name/child - get named children of matching descendants */
            struct taurus_xpath_result* result = make_empty_nodeset();
            if (!result) return NULL;

            void collect_descendants_children_name(TaurusElement elem, XPathNodeSet* result,
                                                   const char* elem_name, size_t elem_name_len,
                                                   const char* child_name, size_t child_name_len);

            /* For absolute paths, check context first */
            if (pattern->is_absolute) {
                if (pattern->name == NULL || pattern->name_len == 0 ||
                    name_matches(ctx, pattern->name, pattern->name_len)) {
                    TaurusElement child = taurus_element_get_first_child(ctx);
                    while (child) {
                        if (name_matches(child, pattern->second_name, pattern->second_name_len)) {
                            xpath_nodeset_add(result->value.nodeset_value, child);
                        }
                        child = taurus_element_get_next_sibling(child);
                    }
                }
            }

            collect_descendants_children_name(ctx, result->value.nodeset_value,
                                              pattern->name, pattern->name_len,
                                              pattern->second_name, pattern->second_name_len);
            return result;
        }

        default:
            return NULL;
    }
}

/* Helper for descendant collection */
static void collect_descendants_fast(TaurusElement elem, XPathNodeSet* result,
                                    const char* name, size_t name_len, int star) {
    if (!elem) return;

    TaurusElement child = taurus_element_get_first_child(elem);
    while (child) {
        TaurusNode* node = (TaurusNode*)child;
        if (node->type == TAURUS_NODE_TYPE_ELEMENT) {
            if (star || name_matches(child, name, name_len)) {
                xpath_nodeset_add(result, child);
            }
            /* Recurse into children */
            collect_descendants_fast(child, result, name, name_len, star);
        }
        child = taurus_element_get_next_sibling(child);
    }
}

/* Helper for descendant collection with filter */
static void collect_descendants_filter(TaurusElement elem, XPathNodeSet* result,
                                      const char* name, size_t name_len, int star) {
    if (!elem) return;

    TaurusElement child = taurus_element_get_first_child(elem);
    while (child) {
        TaurusNode* node = (TaurusNode*)child;
        if (node->type == TAURUS_NODE_TYPE_ELEMENT) {
            if (star || name_matches(child, name, name_len)) {
                xpath_nodeset_add(result, child);
            }
            /* Recurse into children */
            collect_descendants_filter(child, result, name, name_len, star);
        }
        child = taurus_element_get_next_sibling(child);
    }
}

/* Helper for descendant attribute collection */
static void collect_descendants_attrs(TaurusElement elem, XPathNodeSet* result,
                                     const char* elem_name, size_t elem_name_len,
                                     const char* attr_name, size_t attr_name_len, int star) {
    if (!elem) return;

    TaurusElement child = taurus_element_get_first_child(elem);
    while (child) {
        TaurusNode* node = (TaurusNode*)child;
        if (node->type == TAURUS_NODE_TYPE_ELEMENT) {
            if (star || elem_name_len == 0 || name_matches(child, elem_name, elem_name_len)) {
                /* Get the requested attribute(s) from this element */
                if (attr_name == NULL) {
                    /* @* - all attributes */
                    struct taurus_attribute* attr = child->first_attribute;
                    while (attr) {
                        TaurusAttributeNode* attr_node = TAURUS_ALLOC(TaurusAttributeNode);
                        if (attr_node) {
                            attr_node->node_type = TAURUS_NODE_ATTRIBUTE;
                            attr_node->name = taurus_strdup(attr_name_cstr(attr));
                            attr_node->value = taurus_strdup(attr_value_cstr(attr));
                            attr_node->namespace_uri = NULL;
                            attr_node->owner = child;
                            xpath_nodeset_add(result, attr_node);
                        }
                        attr = attr->next;
                    }
                } else {
                    /* @name - specific attribute */
                    struct taurus_attribute* attr = child->first_attribute;
                    while (attr) {
                        if (attr_name_matches(attr, attr_name, attr_name_len)) {
                            TaurusAttributeNode* attr_node = TAURUS_ALLOC(TaurusAttributeNode);
                            if (attr_node) {
                                attr_node->node_type = TAURUS_NODE_ATTRIBUTE;
                                attr_node->name = taurus_strdup(attr_name_cstr(attr));
                                attr_node->value = taurus_strdup(attr_value_cstr(attr));
                                attr_node->namespace_uri = NULL;
                                attr_node->owner = child;
                                xpath_nodeset_add(result, attr_node);
                            }
                            break;
                        }
                        attr = attr->next;
                    }
                }
            }
            /* Recurse into children */
            collect_descendants_attrs(child, result, elem_name, elem_name_len, attr_name, attr_name_len, star);
        }
        child = taurus_element_get_next_sibling(child);
    }
}

/* Helper for descendant children collection */
static void collect_descendants_children(TaurusElement elem, XPathNodeSet* result,
                                        const char* name, size_t name_len, int star) {
    if (!elem) return;

    TaurusElement child = taurus_element_get_first_child(elem);
    while (child) {
        TaurusNode* node = (TaurusNode*)child;
        if (node->type == TAURUS_NODE_TYPE_ELEMENT) {
            if (star || name_matches(child, name, name_len)) {
                /* Add all children of this element */
                TaurusElement grandchild = taurus_element_get_first_child(child);
                while (grandchild) {
                    xpath_nodeset_add(result, grandchild);
                    grandchild = taurus_element_get_next_sibling(grandchild);
                }
            }
            /* Recurse into children */
            collect_descendants_children(child, result, name, name_len, star);
        }
        child = taurus_element_get_next_sibling(child);
    }
}

/* Helper for descendant named children collection */
static void collect_descendants_children_name(TaurusElement elem, XPathNodeSet* result,
                                             const char* elem_name, size_t elem_name_len,
                                             const char* child_name, size_t child_name_len) {
    if (!elem) return;

    TaurusElement child = taurus_element_get_first_child(elem);
    while (child) {
        TaurusNode* node = (TaurusNode*)child;
        if (node->type == TAURUS_NODE_TYPE_ELEMENT) {
            if (elem_name_len == 0 || name_matches(child, elem_name, elem_name_len)) {
                /* Get named children of this element */
                TaurusElement grandchild = taurus_element_get_first_child(child);
                while (grandchild) {
                    if (name_matches(grandchild, child_name, child_name_len)) {
                        xpath_nodeset_add(result, grandchild);
                    }
                    grandchild = taurus_element_get_next_sibling(grandchild);
                }
            }
            /* Recurse into children */
            collect_descendants_children_name(child, result, elem_name, elem_name_len, child_name, child_name_len);
        }
        child = taurus_element_get_next_sibling(child);
    }
}

/**
 * Combined detect-and-evaluate for convenience
 */
struct taurus_xpath_result* xpath_fast_path_try(
    struct taurus_document* doc,
    TaurusElement context,
    const char* expr,
    size_t len
) {
    XPathFastPathPattern pattern;

    if (!xpath_fast_path_detect(expr, len, &pattern)) {
        return NULL;
    }

    return xpath_fast_path_eval(doc, context, &pattern);
}
