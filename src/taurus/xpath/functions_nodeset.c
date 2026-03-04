/* functions_nodeset.c - XPath node-set functions
 * Copyright (c) 2024, Ribose Inc.
 *
 * Implements XPath 1.0 node-set functions:
 * - count(node-set) - Count nodes
 * - id(object) - Find element by ID
 * - local-name(node-set?) - Get local name
 * - namespace-uri(node-set?) - Get namespace URI
 * - name(node-set?) - Get qualified name
 * - lang(string) - Check xml:lang
 */

#include "functions_internal.h"
#include "../dom/ptr_element.h"  /* For struct ptr_attribute */

/* ============================================================================
 * Helper Functions
 * ============================================================================ */

/* Recursively find elements by id attribute */
static void find_elements_by_id(TaurusElement node, const char* id,
    XPathNodeSet* result) {
    if (!node) return;

    /* Check this node's id attribute */
    const char* id_attr = taurus_element_attribute(node, "id");
    if (id_attr && strcmp(id_attr, id) == 0) {
        xpath_nodeset_add(result, node);
    }

    /* Recursively check children */
    TaurusElement child_elem = taurus_element_get_first_child(node);
    while (child_elem) {
        /* Only recurse into element nodes */
        find_elements_by_id(child_elem, id, result);
        child_elem = taurus_element_get_next_sibling(child_elem);
    }
}

/* Check if target language is a sublanguage of specified language */
static int is_sublanguage(const char* lang, const char* target) {
    /* Per XPath spec: lang('en') matches 'en', 'en-US', 'en-GB', etc. */
    if (!lang || !target) return 0;

    size_t lang_len = strlen(lang);
    size_t target_len = strlen(target);

    /* Target must be at least as long as lang */
    if (target_len < lang_len) return 0;

    /* Check if target starts with lang (case-insensitive) */
    if (strncasecmp(lang, target, lang_len) != 0) return 0;

    /* If lengths match, it's an exact match */
    if (target_len == lang_len) return 1;

    /* If target is longer, next char must be '-' for sublanguage */
    return (target[lang_len] == '-');
}

/* ============================================================================
 * XPath Node-Set Functions
 * ============================================================================ */

/* count(node-set) - Returns the number of nodes */
struct taurus_xpath_result* xpath_func_count(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    (void)context;  /* Unused for argument validation */

    if (arg_count != 1) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "count() requires exactly 1 argument");
        return NULL;
    }

    struct taurus_xpath_result* arg_result = xpath_evaluate(context, args[0]);
    if (!arg_result) return NULL;

    if (arg_result->type != XPATH_RESULT_NODESET) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "count() argument must be a nodeset");
        xpath_result_free(arg_result);
        return NULL;
    }

    XPathNodeSet* nodeset = arg_result->value.nodeset_value;
    size_t count = nodeset ? xpath_nodeset_count(nodeset) : 0;
    xpath_result_free(arg_result);

    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_NUMBER);
    if (!result) return NULL;
    result->value.number_value = (double)count;
    return result;
}

/* id(object) - Find elements by ID */
struct taurus_xpath_result* xpath_func_id(
    XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    if (arg_count != 1) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "id() requires exactly 1 argument");
        return NULL;
    }

    /* Get ID string argument */
    struct taurus_xpath_result* arg_result = xpath_evaluate(context, args[0]);
    if (!arg_result) return NULL;

    char* id_value = xpath_to_string(arg_result);
    if (!id_value) {
        xpath_result_free(arg_result);
        return NULL;
    }

    /* Create nodeset result */
    XPathNodeSet* nodeset = xpath_nodeset_new();
    if (!nodeset) {
        TAURUS_FREE(id_value);
        xpath_result_free(arg_result);
        return NULL;
    }

    /* Search document for elements with matching id attribute */
    /* Use ptr_root for pointer-based mode, new_dom_root for legacy mode */
    TaurusElement root = (TaurusElement)context->document->ptr_root;
    if (!root) {
        root = (TaurusElement)context->document->new_dom_root;
    }
    if (root && id_value[0] != '\0') {
        /* XPath spec: id(string) can contain multiple space-separated IDs */
        /* Tokenize the string and search for each ID */
        char* str = id_value;
        char* token;
        char* rest = str;
        while ((token = strtok_r(rest, " \t\n\r", &rest))) {
            find_elements_by_id(root, token, nodeset);
        }
    }

    TAURUS_FREE(id_value);
    xpath_result_free(arg_result);

    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_NODESET);
    if (!result) {
        xpath_nodeset_free(nodeset);
        return NULL;
    }
    result->value.nodeset_value = nodeset;
    return result;
}

/* local-name(node-set?) - Get local name of node */
struct taurus_xpath_result* xpath_func_local_name(
    XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    (void)context;
    void* node = NULL;

    /* Get context node if no argument */
    if (arg_count == 0) {
        node = context->context_node;
    } else if (arg_count == 1) {
        /* Use first node from nodeset */
        struct taurus_xpath_result* arg_result = xpath_evaluate(context, args[0]);
        if (!arg_result) return NULL;

        if (arg_result->type == XPATH_RESULT_NODESET) {
            XPathNodeSet* nodeset = arg_result->value.nodeset_value;
            if (nodeset && xpath_nodeset_count(nodeset) > 0) {
                node = xpath_nodeset_get(nodeset, 0);
            }
        }
        xpath_result_free(arg_result);
    } else {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "local-name() requires 0 or 1 argument");
        return NULL;
    }

    if (!node) {
        /* Return empty string for empty nodeset or no node */
        struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
        if (!result) return NULL;
        result->value.string_value = taurus_strdup("");
        return result;
    }

    /* Get local name (everything after last colon, or full name if no colon) */
    const char* full_name = NULL;
    if (IS_ELEMENT_NODE(node)) {
        full_name = taurus_element_get_name((TaurusElement)node);
    } else if (IS_ATTRIBUTE_NODE(node)) {
        /* For attribute nodes, get the attribute name (stored as C string) */
        TaurusAttributeNode* attr_node = (TaurusAttributeNode*)node;
        full_name = attr_node->name;
    }

    if (!full_name) {
        struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
        if (!result) return NULL;
        result->value.string_value = taurus_strdup("");
        return result;
    }

    /* Find last colon */
    const char* last_colon = strrchr(full_name, ':');
    const char* local_name = last_colon ? last_colon + 1 : full_name;

    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
    if (!result) return NULL;
    result->value.string_value = taurus_strdup(local_name);
    return result;
}

/* namespace-uri(node-set?) - Get namespace URI of node */
struct taurus_xpath_result* xpath_func_namespace_uri(
    XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    (void)context;
    void* node = NULL;

    /* Get context node if no argument */
    if (arg_count == 0) {
        node = context->context_node;
    } else if (arg_count == 1) {
        /* Use first node from nodeset */
        struct taurus_xpath_result* arg_result = xpath_evaluate(context, args[0]);
        if (!arg_result) return NULL;

        if (arg_result->type == XPATH_RESULT_NODESET) {
            XPathNodeSet* nodeset = arg_result->value.nodeset_value;
            if (nodeset && xpath_nodeset_count(nodeset) > 0) {
                node = xpath_nodeset_get(nodeset, 0);
            }
        }
        xpath_result_free(arg_result);
    } else {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "namespace-uri() requires 0 or 1 argument");
        return NULL;
    }

    if (!node) {
        /* Return empty string for empty nodeset or no node */
        struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
        if (!result) return NULL;
        result->value.string_value = taurus_strdup("");
        return result;
    }

    /* Get namespace URI */
    const char* uri = NULL;
    if (IS_ELEMENT_NODE(node)) {
        uri = taurus_element_get_namespace_uri((TaurusElement)node);
    } else if (IS_ATTRIBUTE_NODE(node)) {
        /* For attribute nodes, return the namespace URI if present */
        TaurusAttributeNode* attr_node = (TaurusAttributeNode*)node;
        uri = attr_node->namespace_uri;
    }

    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
    if (!result) return NULL;
    result->value.string_value = uri ? taurus_strdup(uri) : taurus_strdup("");
    return result;
}

/* name(node-set?) - Get qualified name of node */
struct taurus_xpath_result* xpath_func_name(
    XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    (void)context;
    void* node = NULL;

    /* Get context node if no argument */
    if (arg_count == 0) {
        node = context->context_node;
    } else if (arg_count == 1) {
        /* Use first node from nodeset */
        struct taurus_xpath_result* arg_result = xpath_evaluate(context, args[0]);
        if (!arg_result) return NULL;

        if (arg_result->type == XPATH_RESULT_NODESET) {
            XPathNodeSet* nodeset = arg_result->value.nodeset_value;
            if (nodeset && xpath_nodeset_count(nodeset) > 0) {
                node = xpath_nodeset_get(nodeset, 0);
            }
        }
        xpath_result_free(arg_result);
    } else {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "name() requires 0 or 1 argument");
        return NULL;
    }

    if (!node) {
        /* Return empty string for empty nodeset or no node */
        struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
        if (!result) return NULL;
        result->value.string_value = taurus_strdup("");
        return result;
    }

    /* Get qualified name */
    const char* name = NULL;

    if (IS_ELEMENT_NODE(node)) {
        /* For elements, get the full element name (which includes prefix if present) */
        TaurusElement elem = (TaurusElement)node;

        /* The element name field stores the full QName (prefix:local if namespaced) */
        if (elem->name) {
            name = elem->name;
        }
    } else if (IS_ATTRIBUTE_NODE(node)) {
        /* For attribute nodes, get the attribute name (stored as C string in TaurusAttributeNode) */
        TaurusAttributeNode* attr_node = (TaurusAttributeNode*)node;
        name = attr_node->name;  /* Already a C string from create_attribute_node() */
    }

    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
    if (!result) return NULL;
    result->value.string_value = name ? taurus_strdup(name) : taurus_strdup("");

    return result;
}

/* lang(string) - Check xml:lang matches */
struct taurus_xpath_result* xpath_func_lang(XPathContext* context,
    XPathASTNode** args,
    size_t arg_count
) {
    if (arg_count != 1) {
        snprintf(context->error_msg, sizeof(context->error_msg),
                "lang() requires exactly 1 argument, got %zu", arg_count);
        return NULL;
    }

    struct taurus_xpath_result* arg_result = xpath_evaluate(context, args[0]);
    if (!arg_result) return NULL;

    char* language = xpath_to_string(arg_result);
    xpath_result_free(arg_result);

    /* Walk up the ancestor chain looking for xml:lang attribute */
    int match = 0;

    /* Start from context node and go up through ancestors */
    TaurusElement node = (TaurusElement)context->context_node;
    while (node && !match) {

        /* Check for xml:lang attribute - use ptr_attribute directly */
        const char* lang_attr = NULL;

        /* CRITICAL: Use ptr_attribute, not taurus_attribute!
         * They have completely different memory layouts:
         * - ptr_attribute: name at offset 0, value at 8, next_attr at 16
         * - taurus_attribute: name_view at offset 0, name at offset 64
         */
        struct ptr_attribute* attr = node->first_attr;
        while (attr && !lang_attr) {
            /* Get attribute name */
            const char* attr_name = attr->name;
            if (!attr_name && attr->name_view_data && attr->name_view_length > 0) {
                /* StringView - not null terminated, use length comparison */
                if (attr->name_view_length == 4 &&
                    memcmp(attr->name_view_data, "lang", 4) == 0) {
                    attr_name = "lang";  /* Match */
                } else if (attr->name_view_length == 8 &&
                           memcmp(attr->name_view_data, "xml:lang", 8) == 0) {
                    attr_name = "xml:lang";  /* Match */
                }
            }

            /* Check if this is xml:lang */
            if (attr_name) {
                int is_xml_lang_attr = 0;
                if (strcmp(attr_name, "lang") == 0 || strcmp(attr_name, "xml:lang") == 0) {
                    is_xml_lang_attr = 1;
                }

                if (is_xml_lang_attr) {
                    /* Get attribute value */
                    if (attr->value) {
                        lang_attr = attr->value;
                    } else if (attr->value_view_data && attr->value_view_length > 0) {
                        /* StringView - need to compare with length */
                        /* For now, just check if not empty */
                        lang_attr = "";  /* Placeholder */
                    }
                }
            }

            attr = attr->next_attr;
        }

        /* Check if we found xml:lang attribute */
        if (lang_attr && lang_attr[0] != '\0') {
            match = is_sublanguage(language, lang_attr);
        }

        /* Move to parent if no match */
        if (!match) {
            /* Get parent element */
            TaurusElement parent = taurus_element_get_parent(node);
            if (parent) {
                node = parent;
            } else {
                break; /* Reached root */
            }
        }
    }

    /* Create result */
    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_BOOLEAN);
    if (!result) {
        TAURUS_FREE(language);
        return NULL;
    }
    result->value.boolean_value = match;

    TAURUS_FREE(language);
    return result;
}
