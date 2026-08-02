/* parse_content.c - Content and namespace processing for XML parser
 * Copyright (c) 2024, Ribose Inc.
 * All rights reserved.
 *
 * Session 84: Modularized from taurus_parse.c
 * Session 85: Added entity reference expansion
 * Functions for processing element content, namespace declarations, and attributes
 */

#include "parse_internal.h"
#include <string.h>
#include <ctype.h>
#include <stdio.h>

/* ==================================================================
 * ENTITY REFERENCE EXPANSION
 * ================================================================== */

/* Built-in XML entities (predefined in XML spec) */
static struct {
    const char* name;
    const char* value;
} builtin_entities[] = {
    {"lt", "<"},
    {"gt", ">"},
    {"amp", "&"},
    {"apos", "'"},
    {"quot", "\""},
    {NULL, NULL}
};

/* Expand entity reference &name; or &#number; or &#xhex;
 * Returns expanded string (MUST be freed by caller) or NULL if unknown
 *
 * Handles:
 * - Built-in entities: &lt; &gt; &amp; &apos; &quot;
 * - Decimal character refs: &#65; -> 'A'
 * - Hexadecimal character refs: &#x41; -> 'A'
 */
char* expand_entity_reference(const char* ref, size_t len) {
    size_t i;
    char* result;
    unsigned long code;
    char* endptr;
    char number_buf[32];
    
    if (!ref || len == 0) return NULL;
    
    /* Check for character reference: &#... or &#x... */
    if (len >= 2 && ref[0] == '#') {
        /* Decimal: &#65; */
        if (ref[1] != 'x' && ref[1] != 'X') {
            /* Copy number part to null-terminated buffer */
            if (len - 1 >= sizeof(number_buf)) {
                return NULL;  /* Number too long */
            }
            memcpy(number_buf, ref + 1, len - 1);
            number_buf[len - 1] = '\0';
            
            /* Parse decimal number */
            code = strtoul(number_buf, &endptr, 10);
            if (*endptr != '\0' || code > 0x10FFFF) {
                return NULL;  /* Invalid number or out of Unicode range */
            }
        }
        /* Hexadecimal: &#x41; or &#X41; */
        else {
            if (len < 3) return NULL;
            
            /* Copy hex part to null-terminated buffer */
            if (len - 2 >= sizeof(number_buf)) {
                return NULL;  /* Number too long */
            }
            memcpy(number_buf, ref + 2, len - 2);
            number_buf[len - 2] = '\0';
            
            /* Parse hexadecimal number */
            code = strtoul(number_buf, &endptr, 16);
            if (*endptr != '\0' || code > 0x10FFFF) {
                return NULL;  /* Invalid number or out of Unicode range */
            }
        }
        
        /* Convert code point to UTF-8 (simplified: only ASCII for now) */
        if (code <= 0x7F) {
            result = (char*)taurus_malloc(2);
            if (result) {
                result[0] = (char)code;
                result[1] = '\0';
            }
            return result;
        } else {
            /* TODO: Full UTF-8 encoding for higher code points */
            return NULL;  /* For now, only ASCII supported */
        }
    }
    
    /* Named entity reference - check built-in table */
    for (i = 0; builtin_entities[i].name != NULL; i++) {
        size_t name_len = strlen(builtin_entities[i].name);
        if (len == name_len && memcmp(ref, builtin_entities[i].name, len) == 0) {
            /* Found match - return copy of value */
            return taurus_strdup(builtin_entities[i].value);
        }
    }
    
    /* Unknown entity */
    return NULL;
}

/* ==================================================================
 * TEXT CONTENT PROCESSING
 * ================================================================== */

/* Add text content to element with entity expansion */
void add_text_to_element(struct taurus_element *elem, const char *text, size_t len) {
    char *text_copy;
    char *expanded_text;
    char *new_content;
    size_t existing_len;
    const char *p, *start;
    const char *entity_start, *entity_end;
    size_t result_len, result_cap;
    char *result;
    char *entity_value;
    size_t entity_len;
    
    if (!elem || !text || len == 0) return;
    
    /* Check if text contains entities (&...) */
    p = text;
    entity_start = NULL;
    while (p < text + len) {
        if (*p == '&') {
            entity_start = p;
            break;
        }
        p++;
    }
    
    /* No entities found - simple path */
    if (!entity_start) {
        text_copy = taurus_strndup(text, len);
        if (!text_copy) return;
        
        if (!elem->text_content) {
            elem->text_content = text_copy;
        } else {
            existing_len = strlen(elem->text_content);
            new_content = (char*)taurus_realloc(elem->text_content, existing_len + len + 1);
            if (new_content) {
                memcpy(new_content + existing_len, text, len);
                new_content[existing_len + len] = '\0';
                elem->text_content = new_content;
                taurus_free(text_copy);
            } else {
                taurus_free(text_copy);
            }
        }
        return;
    }
    
    /* Entities found - expand them */
    result_cap = len + 64;  /* Initial capacity with some padding */
    result = (char*)taurus_malloc(result_cap);
    if (!result) return;
    result_len = 0;
    
    start = text;
    p = text;
    
    while (p < text + len) {
        if (*p == '&') {
            /* Copy text before entity */
            if (p > start) {
                size_t copy_len = p - start;
                if (result_len + copy_len >= result_cap) {
                    result_cap = (result_len + copy_len + 1) * 2;
                    result = (char*)taurus_realloc(result, result_cap);
                    if (!result) return;
                }
                memcpy(result + result_len, start, copy_len);
                result_len += copy_len;
            }
            
            /* Find end of entity (;) */
            entity_start = p + 1;  /* Skip '&' */
            entity_end = entity_start;
            while (entity_end < text + len && *entity_end != ';') {
                entity_end++;
            }
            
            if (entity_end >= text + len || *entity_end != ';') {
                /* Unterminated entity - keep as-is */
                if (result_len + 1 >= result_cap) {
                    result_cap = (result_len + 2) * 2;
                    result = (char*)taurus_realloc(result, result_cap);
                    if (!result) return;
                }
                result[result_len++] = '&';
                start = p + 1;
                p++;
                continue;
            }
            
            /* Extract and expand entity */
            entity_len = entity_end - entity_start;
            entity_value = expand_entity_reference(entity_start, entity_len);
            
            if (entity_value) {
                /* Copy expanded value */
                size_t value_len = strlen(entity_value);
                if (result_len + value_len >= result_cap) {
                    result_cap = (result_len + value_len + 1) * 2;
                    result = (char*)taurus_realloc(result, result_cap);
                    if (!result) {
                        taurus_free(entity_value);
                        return;
                    }
                }
                memcpy(result + result_len, entity_value, value_len);
                result_len += value_len;
                taurus_free(entity_value);
                
                /* Move past entity */
                p = entity_end + 1;
                start = p;
            } else {
                /* Unknown entity - keep as-is */
                size_t keep_len = entity_end - p + 1;  /* Include & and ; */
                if (result_len + keep_len >= result_cap) {
                    result_cap = (result_len + keep_len + 1) * 2;
                    result = (char*)taurus_realloc(result, result_cap);
                    if (!result) return;
                }
                memcpy(result + result_len, p, keep_len);
                result_len += keep_len;
                p = entity_end + 1;
                start = p;
            }
        } else {
            p++;
        }
    }
    
    /* Copy remaining text */
    if (start < text + len) {
        size_t copy_len = (text + len) - start;
        if (result_len + copy_len >= result_cap) {
            result_cap = result_len + copy_len + 1;
            result = (char*)taurus_realloc(result, result_cap);
            if (!result) return;
        }
        memcpy(result + result_len, start, copy_len);
        result_len += copy_len;
    }
    
    /* Null-terminate */
    result[result_len] = '\0';
    expanded_text = result;
    
    /* Add to element */
    if (!elem->text_content) {
        elem->text_content = expanded_text;
    } else {
        existing_len = strlen(elem->text_content);
        new_content = (char*)taurus_realloc(elem->text_content, existing_len + result_len + 1);
        if (new_content) {
            memcpy(new_content + existing_len, expanded_text, result_len);
            new_content[existing_len + result_len] = '\0';
            elem->text_content = new_content;
            taurus_free(expanded_text);
        } else {
            taurus_free(expanded_text);
        }
    }
}

/* ==================================================================
 * NAMESPACE PROCESSING
 * ================================================================== */

/* Process namespace declarations from attribute stack
 * Extracts xmlns and xmlns:prefix attributes and creates namespace objects */
void process_namespace_declarations(struct taurus_element *elem, TaurusAttrStack *attr_stack) {
    size_t i, count;
    ParseAttribute *attrs;
    const char *name;
    struct taurus_namespace *ns;
    
    if (!elem || !attr_stack) return;
    
    count = taurus_attr_stack_size(attr_stack);
    attrs = taurus_attr_stack_to_array(attr_stack);
    
    for (i = 0; i < count; i++) {
        name = attrs[i].name;
        
        /* Check if this is a namespace declaration */
        if (strncmp(name, "xmlns", 5) == 0) {
            if (name[5] == '\0') {
                /* Default namespace: xmlns="uri" */
                char *uri = taurus_strndup(attrs[i].value, strlen(attrs[i].value));
                ns = taurus_namespace_new(NULL, uri);
                taurus_free(uri);
                if (ns) {
                    taurus_element_add_namespace(elem, ns);
                }
            } else if (name[5] == ':') {
                /* Prefixed namespace: xmlns:prefix="uri" */
                const char *prefix = name + 6;
                char *uri = taurus_strndup(attrs[i].value, strlen(attrs[i].value));
                char *prefix_copy = taurus_strdup(prefix);
                ns = taurus_namespace_new(prefix_copy, uri);
                taurus_free(uri);
                taurus_free(prefix_copy);
                if (ns) {
                    taurus_element_add_namespace(elem, ns);
                }
            }
        }
    }
}

/* Set element namespace from prefix or parent inheritance */
void set_element_namespace(struct taurus_element *elem, const char *prefix,
                          struct taurus_element *parent) {
    struct taurus_namespace *ns;
    
    if (!elem) return;
    
    /* Set prefix if provided */
    if (prefix) {
        elem->prefix = taurus_strdup(prefix);
    }
    
    /* Find namespace URI - search element first, then parent if provided
     * (parent relationship not yet established at call time) */
    ns = taurus_namespace_find(elem, prefix);
    if (!ns && parent) {
        /* Not found on element, search parent for inheritance */
        ns = taurus_namespace_find(parent, prefix);
    }
    
    if (ns && ns->uri) {
        elem->namespace_uri = taurus_strdup(ns->uri);
    }
}

/* ==================================================================
 * ATTRIBUTE PROCESSING
 * ================================================================== */

/* Add non-xmlns attributes to element */
void add_attributes_to_element(struct taurus_element *elem, TaurusAttrStack *attr_stack,
                               StringInternTable *intern_table) {
    size_t i, count;
    ParseAttribute *attrs;
    const char *name;
    struct taurus_attribute *attr;
    const char *interned_name;
    char *value_copy;
    
    if (!elem || !attr_stack) return;
    
    count = taurus_attr_stack_size(attr_stack);
    attrs = taurus_attr_stack_to_array(attr_stack);
    
    for (i = 0; i < count; i++) {
        name = attrs[i].name;
        
        /* Skip xmlns declarations (already processed) */
        if (strncmp(name, "xmlns", 5) == 0 && 
            (name[5] == '\0' || name[5] == ':')) {
            continue;
        }
        
        /* Intern attribute name */
        interned_name = string_intern_get(intern_table, name, strlen(name));
        if (!interned_name) {
            interned_name = name;  /* Fallback */
        }
        
        /* Create attribute value copy */
        value_copy = taurus_strndup(attrs[i].value, strlen(attrs[i].value));
        
        /* Create attribute */
        attr = taurus_attribute_new(interned_name, value_copy);
        taurus_free(value_copy);
        
        if (attr) {
            taurus_element_add_attribute(elem, attr);
        }
    }
}