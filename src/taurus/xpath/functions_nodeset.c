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
    TaurusElement root = (TaurusElement)context->document->new_dom_root;
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
    char* temp_name = NULL;  /* For temporary allocations */

    if (IS_ELEMENT_NODE(node)) {
        /* For elements, get qualified name (prefix:local if prefix exists) */
        TaurusElement elem = (TaurusElement)node;
        const char* local_name = taurus_element_get_name(elem);
        const char* prefix = taurus_element_get_prefix(elem);

        if (prefix && prefix[0] != '\0') {
            /* Construct qualified name: prefix:local */
            size_t prefix_len = strlen(prefix);
            size_t local_len = strlen(local_name);
            temp_name = TAURUS_ALLOC_N(char, prefix_len + 1 + local_len + 1);
            if (temp_name) {
                memcpy(temp_name, prefix, prefix_len);
                temp_name[prefix_len] = ':';
                memcpy(temp_name + prefix_len + 1, local_name, local_len);
                temp_name[prefix_len + 1 + local_len] = '\0';
                name = temp_name;
            }
        } else {
            /* No prefix, just use local name */
            name = local_name;
        }
    } else if (IS_ATTRIBUTE_NODE(node)) {
        /* For attribute nodes, get the attribute name (stored as C string in TaurusAttributeNode) */
        TaurusAttributeNode* attr_node = (TaurusAttributeNode*)node;
        name = attr_node->name;  /* Already a C string from create_attribute_node() */
    }

    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
    if (!result) {
        if (temp_name) TAURUS_FREE(temp_name);
        return NULL;
    }
    result->value.string_value = name ? taurus_strdup(name) : taurus_strdup("");

    /* Free temporary allocation */
    if (temp_name) TAURUS_FREE(temp_name);

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
    /* XML namespace URI for xml:lang */
    const char* xml_ns_uri = "http://www.w3.org/XML/1998/namespace";
    int match = 0;

    /* Start from context node and go up through ancestors */
    TaurusElement node = (TaurusElement)context->context_node;
    while (node && !match) {

        /* Check for xml:lang attribute (in XML namespace) */
        /* First try by namespace URI */
        const char* lang_attr = NULL;

        /* Check attributes with namespace URI - walk linked list */
        struct taurus_attribute* attr = taurus_element_get_first_attribute(node);
        while (attr && !lang_attr) {
            if (!attr) continue;

            /* Get namespace URI */
            const char* ns_uri = attr->namespace_uri;
            if (!ns_uri && !taurus_sv_is_empty(&attr->namespace_uri_view)) {
                ns_uri = taurus_sv_to_cstr(&attr->namespace_uri_view);
            }

            /* Get attribute name */
            const char* attr_name = attr->name;
            if (!attr_name && !taurus_sv_is_empty(&attr->name_view)) {
                attr_name = taurus_sv_to_cstr(&attr->name_view);
            }

            /* Check if this is xml:lang (try by namespace URI, by prefixed name, or by prefix) */
            int is_xml_lang_attr = 0;
            if (attr_name) {
                /* Check for plain "lang" name (namespace should be XML namespace) */
                if (strcmp(attr_name, "lang") == 0) {
                    is_xml_lang_attr = 1;
                }
                /* Also check for "xml:lang" prefixed form (when stored with prefix) */
                else if (strcmp(attr_name, "xml:lang") == 0) {
                    is_xml_lang_attr = 1;
                }
            }

            if (is_xml_lang_attr) {
                /* Check by namespace URI first */
                int is_xml_lang = 0;
                if (ns_uri && strcmp(ns_uri, xml_ns_uri) == 0) {
                    is_xml_lang = 1;
                }
                /* Also check if the prefix is "xml" (for compatibility) */
                else {
                    const char* prefix = attr->prefix;
                    if (!prefix && !taurus_sv_is_empty(&attr->prefix_view)) {
                        prefix = taurus_sv_to_cstr(&attr->prefix_view);
                    }
                    if (prefix && strcmp(prefix, "xml") == 0) {
                        is_xml_lang = 1;
                    }
                    if (attr->prefix != prefix && prefix) free((char*)prefix);
                }

                /* Also check if there's no namespace URI at all (xml:lang might be stored without ns) */
                if (!is_xml_lang && !ns_uri) {
                    /* This might be xml:lang stored without namespace information */
                    is_xml_lang = 1;
                }

                if (is_xml_lang) {
                    lang_attr = attr->value;
                    if (!lang_attr && !taurus_sv_is_empty(&attr->value_view)) {
                        lang_attr = taurus_sv_to_cstr(&attr->value_view);
                    }
                }
            }

            /* Free temporary strings if we converted them */
            if (attr->name != attr_name && attr_name) free((char*)attr_name);
            if (attr->namespace_uri != ns_uri && ns_uri) free((char*)ns_uri);

            attr = attr->next;
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
