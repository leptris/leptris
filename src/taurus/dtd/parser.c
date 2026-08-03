/**
 * @file dtd/parser.c
 * @brief DTD parser implementation
 *
 * Parses DTD internal subset declarations (ENTITY, ELEMENT, NOTATION, ATTLIST).
 * Uses stateless parsing with hash table storage for O(1) lookup.
 */

#include "model.h"
#include "../memory/pool.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* Parser state.
 *
 * Carries the pool pointer (TODO 16) so static helpers can route
 * entity/element/attribute declarations through the document pool
 * without each helper taking a separate pool argument. */
typedef struct {
    const char* pos;
    const char* end;
    TaurusMemoryPool* pool;
} DTDParser;

/* Helper functions */
static int dtd_at_end(DTDParser* p) {
    return p->pos >= p->end;
}

static char dtd_peek(DTDParser* p) {
    if (dtd_at_end(p)) return '\0';
    return *p->pos;
}

static void dtd_advance(DTDParser* p) {
    if (!dtd_at_end(p)) p->pos++;
}

static void dtd_skip_whitespace(DTDParser* p) {
    while (!dtd_at_end(p) && isspace((unsigned char)dtd_peek(p))) {
        dtd_advance(p);
    }
}

static int dtd_match(DTDParser* p, const char* str) {
    size_t len = strlen(str);
    if (p->pos + len > p->end) return 0;
    return strncmp(p->pos, str, len) == 0;
}

static char* dtd_parse_name(DTDParser* p) {
    dtd_skip_whitespace(p);
    const char* start = p->pos;

    /* XML name: letter, '_', ':', followed by letters, digits, '_', '-', '.', ':', combine chars */
    while (!dtd_at_end(p)) {
        char c = dtd_peek(p);
        if (isalnum((unsigned char)c) || c == '_' || c == ':' || c == '-' ||
            c == '.' || c == '\240' || /* No-break space */
            (c >= 0x80 && c <= 0xFF)) { /* Allow extended chars */
            dtd_advance(p);
        } else {
            break;
        }
    }

    size_t len = p->pos - start;
    if (len == 0) return NULL;

    char* name = (char*)malloc(len + 1);
    if (!name) return NULL;
    memcpy(name, start, len);
    name[len] = '\0';
    return name;
}

/**
 * Parse quoted string (single or double quotes), pool-allocated
 * (TODO 33: was malloc'd, which leaked when stored on pool-owned
 * entity/notation structs). */
static char* dtd_parse_quoted_string(DTDParser* p) {
    dtd_skip_whitespace(p);

    char quote = dtd_peek(p);
    if (quote != '"' && quote != '\'') return NULL;

    dtd_advance(p); /* Skip opening quote */
    const char* start = p->pos;

    /* Find closing quote */
    while (!dtd_at_end(p) && dtd_peek(p) != quote) {
        dtd_advance(p);
    }

    size_t len = p->pos - start;
    char* result = (char*)taurus_pool_alloc(p->pool, len + 1);
    if (!result) return NULL;

    memcpy(result, start, len);
    result[len] = '\0';

    if (dtd_peek(p) == quote) dtd_advance(p); /* Skip closing quote */

    return result;
}

/**
 * Parse <!ENTITY name "value"> or <!ENTITY name SYSTEM "uri">
 */
static DTDEntityDecl* dtd_parse_entity(DTDParser* p) {
    /* Skip "<!ENTITY" */
    p->pos += 8;
    dtd_skip_whitespace(p);

    /* Parse entity name */
    char* name = dtd_parse_name(p);
    if (!name) return NULL;

    DTDEntityDecl* entity = ttdtd_entity_create(name, p->pool);
    free(name);
    if (!entity) return NULL;

    dtd_skip_whitespace(p);

    /* Check for SYSTEM or PUBLIC keyword (external entity) */
    if (dtd_match(p, "SYSTEM")) {
        entity->type = DTD_ENTITY_EXTERNAL;
        p->pos += 6;
        dtd_skip_whitespace(p);

        /* Parse system literal (quoted URI) */
        entity->system_id = dtd_parse_quoted_string(p);
    } else if (dtd_match(p, "PUBLIC")) {
        entity->type = DTD_ENTITY_EXTERNAL;
        p->pos += 6;
        dtd_skip_whitespace(p);

        /* Parse public identifier (quoted string) */
        entity->public_id = dtd_parse_quoted_string(p);
        dtd_skip_whitespace(p);

        /* Optional system literal */
        if (dtd_peek(p) == '"' || dtd_peek(p) == '\'') {
            entity->system_id = dtd_parse_quoted_string(p);
        }
    } else {
        /* Internal entity - parse entity value */
        entity->type = DTD_ENTITY_INTERNAL;
        entity->value = dtd_parse_quoted_string(p);
    }

    /* Check for NDATA notation (unparsed entities) */
    dtd_skip_whitespace(p);
    if (dtd_match(p, "NDATA")) {
        p->pos += 5;
        dtd_skip_whitespace(p);
        entity->notation_name = dtd_parse_name(p);
    }

    /* Skip to '>' */
    while (!dtd_at_end(p) && dtd_peek(p) != '>') {
        dtd_advance(p);
    }
    if (dtd_peek(p) == '>') dtd_advance(p);

    return entity;
}

/**
 * Parse <!ELEMENT name content-model>
 */
static DTDElementDecl* dtd_parse_element(DTDParser* p) {
    /* Skip "<!ELEMENT" */
    p->pos += 9;
    dtd_skip_whitespace(p);

    /* Parse element name */
    char* name = dtd_parse_name(p);
    if (!name) return NULL;

    DTDElementDecl* elem = ttdtd_element_create_pooled(name, p->pool);
    free(name);
    if (!elem) return NULL;

    dtd_skip_whitespace(p);

    /* Parse content model */
    if (dtd_match(p, "EMPTY")) {
        elem->content_type = DTD_CONTENT_EMPTY;
        p->pos += 5;
    } else if (dtd_match(p, "ANY")) {
        elem->content_type = DTD_CONTENT_ANY;
        p->pos += 3;
    } else if (dtd_peek(p) == '(') {
        /* Parse content model - store as string for now */
        const char* start = p->pos;
        int depth = 0;
        int in_quote = 0;
        char quote_char = 0;

        while (!dtd_at_end(p)) {
            char c = dtd_peek(p);

            if (in_quote) {
                if (c == quote_char) in_quote = 0;
            } else if (c == '"' || c == '\'') {
                in_quote = 1;
                quote_char = c;
            } else if (c == '(') {
                depth++;
            } else if (c == ')') {
                depth--;
                dtd_advance(p);
                if (depth == 0) break;
                dtd_advance(p);
                continue;
            }
            dtd_advance(p);
        }

        size_t len = p->pos - start;
        elem->content_model = (char*)malloc(len + 1);
        if (elem->content_model) {
            memcpy(elem->content_model, start, len);
            elem->content_model[len] = '\0';

            /* Determine type based on content */
            if (strstr(elem->content_model, "#PCDATA")) {
                elem->content_type = DTD_CONTENT_MIXED;
            } else {
                elem->content_type = DTD_CONTENT_CHILDREN;
            }
        }
    }

    /* Skip to '>' */
    while (!dtd_at_end(p) && dtd_peek(p) != '>') {
        dtd_advance(p);
    }
    if (dtd_peek(p) == '>') dtd_advance(p);

    return elem;
}

/**
 * Parse <!NOTATION name (SYSTEM "uri" | PUBLIC "pubid" "uri"?>
 */
static DTDNotationDecl* dtd_parse_notation(DTDParser* p) {
    /* Skip "<!NOTATION" */
    p->pos += 10;
    dtd_skip_whitespace(p);

    /* Parse notation name */
    char* name = dtd_parse_name(p);
    if (!name) return NULL;

    DTDNotationDecl* notation = ttdtd_notation_create(name);
    free(name);
    if (!notation) return NULL;

    dtd_skip_whitespace(p);

    /* Check for SYSTEM or PUBLIC */
    if (dtd_match(p, "SYSTEM")) {
        p->pos += 6;
        dtd_skip_whitespace(p);
        notation->system_id = dtd_parse_quoted_string(p);
    } else if (dtd_match(p, "PUBLIC")) {
        p->pos += 6;
        dtd_skip_whitespace(p);
        notation->public_id = dtd_parse_quoted_string(p);
        dtd_skip_whitespace(p);

        /* Optional system literal */
        if (dtd_peek(p) == '"' || dtd_peek(p) == '\'') {
            notation->system_id = dtd_parse_quoted_string(p);
        }
    }

    /* Skip to '>' */
    while (!dtd_at_end(p) && dtd_peek(p) != '>') {
        dtd_advance(p);
    }
    if (dtd_peek(p) == '>') dtd_advance(p);

    return notation;
}

/**
 * Parse DTD internal subset
 *
 * @param dtd_content DTD content string (UTF-8)
 * @param len Length of DTD content in bytes
 * @return Parsed DTD object or NULL on error
 */
TaurusDTD* taurus_dtd_parse_internal_subset(const char* dtd_content, size_t len,
                                              TaurusMemoryPool* pool) {
    if (!dtd_content || len == 0 || !pool) return NULL;

    /* Create DTD container backed by the document's pool (TODO 16). */
    TaurusDTD* dtd = taurus_dtd_create(pool);
    if (!dtd) return NULL;

    DTDParser parser = {dtd_content, dtd_content + len, pool};

    while (!dtd_at_end(&parser)) {
        dtd_skip_whitespace(&parser);

        /* Handle comments in DTD */
        if (dtd_match(&parser, "<!--")) {
            parser.pos += 4;
            while (!dtd_at_end(&parser) && !dtd_match(&parser, "-->")) {
                dtd_advance(&parser);
            }
            if (dtd_match(&parser, "-->")) parser.pos += 3;
            continue;
        }

        /* Handle parameter entity references (%name;) - skip for now */
        if (dtd_peek(&parser) == '%') {
            /* Skip to semicolon */
            while (!dtd_at_end(&parser) && dtd_peek(&parser) != ';') {
                dtd_advance(&parser);
            }
            if (dtd_peek(&parser) == ';') dtd_advance(&parser);
            continue;
        }

        /* Parse declarations */
        if (dtd_match(&parser, "<!ENTITY")) {
            DTDEntityDecl* entity = dtd_parse_entity(&parser);
            if (entity) {
                if (!ttdtd_add_entity(dtd, entity)) {
                    ttdtd_entity_free(entity);
                }
            }
        } else if (dtd_match(&parser, "<!ELEMENT")) {
            DTDElementDecl* elem = dtd_parse_element(&parser);
            if (elem) {
                if (!ttdtd_add_element(dtd, elem)) {
                    ttdtd_element_free(elem);
                }
            }
        } else if (dtd_match(&parser, "<!NOTATION")) {
            DTDNotationDecl* notation = dtd_parse_notation(&parser);
            if (notation) {
                if (!ttdtd_add_notation(dtd, notation)) {
                    ttdtd_notation_free(notation);
                }
            }
        } else if (dtd_match(&parser, "<!ATTLIST")) {
            /* Parse one or more <!ATTLIST element-name attr-decl+>
             * declarations. Each ATTLIST can declare multiple
             * attributes for the same element (whitespace-separated).
             * Phase 2 of TODO 91. */
            parser.pos += 9;
            dtd_skip_whitespace(&parser);

            char* elem_name = dtd_parse_name(&parser);
            if (!elem_name) {
                /* Malformed; skip to '>' */
                while (!dtd_at_end(&parser) && dtd_peek(&parser) != '>') {
                    dtd_advance(&parser);
                }
                if (dtd_peek(&parser) == '>') dtd_advance(&parser);
                continue;
            }

            /* Parse attribute declarations until we hit '>'. */
            while (!dtd_at_end(&parser) && dtd_peek(&parser) != '>') {
                dtd_skip_whitespace(&parser);
                if (dtd_peek(&parser) == '>') break;

                char* attr_name = dtd_parse_name(&parser);
                if (!attr_name) {
                    free(elem_name);
                    break;
                }

                DTDAttributeDecl* decl = dtd_attribute_decl_create_pooled(elem_name, attr_name, parser.pool);
                free(attr_name);
                if (!decl) {
                    free(elem_name);
                    break;
                }

                /* Parse attribute type: CDATA | ID | IDREF | IDREFS |
                 * NMTOKEN | NMTOKENS | (e1|e2|...).
                 * Phase 2 doesn't enforce attribute types, so we
                 * parse-and-discard the type token. (Pool-owning the
                 * strdup'd string would also work but adds bookkeeping
                 * for unused data.) */
                dtd_skip_whitespace(&parser);
                if (dtd_peek(&parser) == '(') {
                    /* Enumerated type — skip to ')'. */
                    while (!dtd_at_end(&parser) && dtd_peek(&parser) != '>' &&
                           dtd_peek(&parser) != ')') {
                        dtd_advance(&parser);
                    }
                    if (dtd_peek(&parser) == ')') dtd_advance(&parser);
                } else {
                    char* type_name = dtd_parse_name(&parser);
                    if (type_name) free(type_name);
                }

                /* Parse default declaration:
                 * #REQUIRED | #IMPLIED | #FIXED "val" | "val" */
                dtd_skip_whitespace(&parser);
                if (dtd_peek(&parser) == '#') {
                    dtd_advance(&parser);
                    char* keyword = dtd_parse_name(&parser);
                    if (keyword) {
                        if (strcmp(keyword, "REQUIRED") == 0) {
                            decl->default_type = DTD_ATTR_REQUIRED;
                        } else if (strcmp(keyword, "IMPLIED") == 0) {
                            decl->default_type = DTD_ATTR_IMPLIED;
                        } else if (strcmp(keyword, "FIXED") == 0) {
                            decl->default_type = DTD_ATTR_FIXED;
                            dtd_skip_whitespace(&parser);
                            char* val = dtd_parse_quoted_string(&parser);
                            if (val) {
                                free(decl->default_value);
                                decl->default_value = val;
                            }
                        }
                        free(keyword);
                    }
                } else if (dtd_peek(&parser) == '"' || dtd_peek(&parser) == '\'') {
                    char* val = dtd_parse_quoted_string(&parser);
                    if (val) {
                        decl->default_type = DTD_ATTR_DEFAULT;
                        free(decl->default_value);
                        decl->default_value = val;
                    }
                }

                ttdtd_add_attribute(dtd, decl);
            }
            if (dtd_peek(&parser) == '>') dtd_advance(&parser);
            free(elem_name);
        } else {
            /* Skip unknown declaration */
            while (!dtd_at_end(&parser) && dtd_peek(&parser) != '>') {
                dtd_advance(&parser);
            }
            if (dtd_peek(&parser) == '>') dtd_advance(&parser);
        }
    }

    return dtd;
}

/**
 * Free DTD (wrapper for ttdtd_free for API compatibility)
 */
void taurus_dtd_free(TaurusDTD* dtd) {
    ttdtd_free(dtd);
}

/**
 * Parse DTD from string (public API - wrapper for internal_subset)
 *
 * This is the public API function declared in include/taurus/dtd.h.
 * It's a wrapper that calls taurus_dtd_parse_internal_subset.
 *
 * NOTE (TODO 16): this public entry point has no document pool to
 * route through, so it creates a temporary pool for the DTD.  Callers
 * using this path must call ttdtd_free() to release it (the wrapper
 * preserves legacy semantics).  Internal callers should use
 * taurus_dtd_parse_internal_subset() directly with the document's
 * pool instead.
 */
TaurusDTD* taurus_dtd_parse(const char* dtd_content, size_t len) {
    TaurusMemoryPool* pool = taurus_pool_create();
    if (!pool) return NULL;
    TaurusDTD* dtd = taurus_dtd_parse_internal_subset(dtd_content, len, pool);
    if (!dtd) {
        taurus_pool_destroy(pool);
        return NULL;
    }
    /* Mark this DTD as owning its pool so ttdtd_free releases it.
     * Document-pool DTDs (created via the internal parser path) leave
     * owns_pool=0; their pool is destroyed with the document. */
    dtd->owns_pool = 1;
    return dtd;
}
