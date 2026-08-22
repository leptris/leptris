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
    LeptrisMemoryPool* pool;
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

/**
 * Parse XML name, pool-allocated (TODO 33: was malloc'd; the malloc'd
 * copy leaked when the result was stored on a pool-owned struct).
 */
static char* dtd_parse_name(DTDParser* p) {
    dtd_skip_whitespace(p);
    const char* start = p->pos;

    /* XML name: letter, '_', ':', followed by letters, digits, '_', '-', '.', ':', combine chars.
     * Cast c to unsigned before the 0x80..0xFF range test — on platforms where
     * plain char is signed, the test is otherwise tautological (always false). */
    while (!dtd_at_end(p)) {
        unsigned char c = (unsigned char)dtd_peek(p);
        if (isalnum(c) || c == '_' || c == ':' || c == '-' ||
            c == '.' || c == 0xA0 || /* No-break space */
            c >= 0x80) { /* Allow extended chars */
            dtd_advance(p);
        } else {
            break;
        }
    }

    size_t len = p->pos - start;
    if (len == 0) return NULL;

    char* name = (char*)leptris_pool_alloc(p->pool, len + 1);
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
    char* result = (char*)leptris_pool_alloc(p->pool, len + 1);
    if (!result) return NULL;

    memcpy(result, start, len);
    result[len] = '\0';

    if (dtd_peek(p) == quote) dtd_advance(p); /* Skip closing quote */

    return result;
}

/**
 * Parse <!ENTITY name "value"> or <!ENTITY name SYSTEM "uri">.
 * Also handles parameter entities: <!ENTITY % name "value"> — these
 * are stored with a "%" prefix on the name so they don't collide
 * with general entities. The substitution happens later when %name;
 * is referenced in the DTD (TODO 91 Phase 8b). */
static DTDEntityDecl* dtd_parse_entity(DTDParser* p) {
    /* Skip "<!ENTITY" */
    p->pos += 8;
    dtd_skip_whitespace(p);

    /* Parameter entity declaration: <!ENTITY % name "value">.
     * The "%" indicates a parameter entity (vs general entity). We
     * store it with a "%" prefix on the name to namespace it apart. */
    int is_param = 0;
    if (dtd_peek(p) == '%') {
        is_param = 1;
        dtd_advance(p);
        dtd_skip_whitespace(p);
    }

    /* Parse entity name */
    char* name = dtd_parse_name(p);
    if (!name) return NULL;

    /* For parameter entities, namespace the name with "%" so general
     * and parameter entities with the same name don't collide. */
    char* stored_name = name;
    if (is_param) {
        size_t nlen = strlen(name);
        stored_name = (char*)leptris_pool_alloc(p->pool, nlen + 2);
        if (!stored_name) return NULL;
        stored_name[0] = '%';
        memcpy(stored_name + 1, name, nlen + 1);
    }

    DTDEntityDecl* entity = ttdtd_entity_create(stored_name, p->pool);
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
        elem->content_model = (char*)leptris_pool_alloc(p->pool, len + 1);
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

    DTDNotationDecl* notation = ttdtd_notation_create(name, p->pool);
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

/* Maximum parameter-entity substitution depth (TODO.remaining/03).
 * Self- or mutually-recursive PEs (<!ENTITY % a "%a;">) would splice
 * their own reference forever; at the cap the reference is skipped
 * and parsing continues with the rest of the subset. */
#define DTD_PE_MAX_DEPTH 16

static void dtd_parse_into(LeptrisDTD* dtd, const char* dtd_content,
                           size_t len, int depth);

/**
 * Parse DTD internal subset
 *
 * @param dtd_content DTD content string (UTF-8)
 * @param len Length of DTD content in bytes
 * @return Parsed DTD object or NULL on error
 */
LeptrisDTD* leptris_dtd_parse_internal_subset(const char* dtd_content, size_t len,
                                              LeptrisMemoryPool* pool) {
    if (!dtd_content || len == 0 || !pool) return NULL;

    /* Create DTD container backed by the document's pool (TODO 16). */
    LeptrisDTD* dtd = leptris_dtd_create(pool);
    if (!dtd) return NULL;

    dtd_parse_into(dtd, dtd_content, len, 0);
    return dtd;
}

/**
 * Parse an external subset's content into an EXISTING DTD.
 *
 * The application owns I/O: read the resource named by the DOCTYPE
 * system id (leptris_doctype_get_system_id) and hand the bytes here.
 * Declarations merge with the internal subset per XML 1.0 §4.2 —
 * the first declaration of a name wins, so an internal-subset
 * declaration is never overridden by the external subset.
 *
 * @param dtd DTD to extend (from leptris_dtd_parse or
 *            leptris_document_get_dtd)
 * @param content External subset text (UTF-8)
 * @param len Length in bytes
 * @return 1 on success, -1 on invalid arguments
 */
void leptris_dtd_set_pe_loader(LeptrisDTD* dtd,
                               char* (*loader)(void* user_data,
                                               const char* system_id,
                                               size_t* out_len),
                               void* user_data) {
    if (!dtd) return;
    dtd->pe_loader = loader;
    dtd->pe_loader_user = user_data;
}

int leptris_dtd_parse_external_subset(LeptrisDTD* dtd, const char* content,
                                      size_t len) {
    if (!dtd || !content || len == 0) return -1;
    dtd_parse_into(dtd, content, len, 0);
    return 1;
}

/* Parse declarations from [dtd_content, dtd_content+len) INTO an
 * existing DTD. All recursion (parameter-entity substitution,
 * INCLUDE conditional sections) goes through this function so
 * declarations always land in the same tables — the earlier shape
 * recursed through leptris_dtd_parse_internal_subset, which builds a
 * FRESH DTD per call whose declarations were then discarded. */
/* Substitute %pe; references inside a declaration body (element
 * content models, ATTLIST definition lists — the common real-world
 * pattern). Internal PEs splice their values; EXTERNAL (SYSTEM) PEs
 * splice the loader's content when a loader is registered (the
 * app-owned-I/O pattern of leptris_dtd_parse_external_subset);
 * without one they are left as-is (lenient). Undefined references
 * are left as-is. Results go to a pool buffer; depth-capped like
 * the between-decl substitution. Returns NULL when the text needs
 * no substitution. */
static char* dtd_substitute_decl_pes(LeptrisDTD* dtd, const char* text,
                             size_t len, int depth);

static char* dtd_substitute_decl_pes(LeptrisDTD* dtd, const char* text,
                             size_t len, int depth) {
    /* Quick scan: no '%' -> nothing to do. */
    const char* pct = (const char*)memchr(text, '%', len);
    if (!pct) return NULL;

    LeptrisMemoryPool* pool = (LeptrisMemoryPool*)dtd->pool;
    char* out = (char*)leptris_pool_alloc(pool, len + 1);
    if (!out) return NULL;
    size_t w = 0;
    int substituted = 0;

    size_t i = 0;
    while (i < len) {
if (text[i] == '%' && i + 1 < len) {
    /* %name; reference within the decl body. */
    size_t j = i + 1;
    while (j < len && text[j] != ';' && text[j] != '%' &&
           !isspace((unsigned char)text[j])) {
        j++;
    }
    if (j < len && text[j] == ';' && j > i + 1 && j - i - 1 < 128) {
        char buf[130];
        buf[0] = '%';
        memcpy(buf + 1, text + i + 1, j - i - 1);
        buf[j - i] = '\0';
        DTDEntityDecl* pe = ttdtd_lookup_entity(dtd, buf);
        if (pe && depth < DTD_PE_MAX_DEPTH) {
            const char* val = NULL;
            size_t val_len = 0;
            if (pe->type == DTD_ENTITY_INTERNAL && pe->value) {
                val = pe->value;
                val_len = strlen(val);
            } else if (pe->type == DTD_ENTITY_EXTERNAL &&
                       pe->system_id && dtd->pe_loader) {
                size_t loaded = 0;
                char* got = dtd->pe_loader(dtd->pe_loader_user,
                                            pe->system_id, &loaded);
                if (got) {
                    /* Splice then free: the loaded text is
                     * copied into the pool buffer. Nested
                     * %ref; in loaded content are expanded
                     * by the recursive call below. */
                    char* grown = (char*)leptris_pool_alloc(
                        pool, w + loaded + (len - j) + 1);
                    if (grown) {
                        memcpy(grown, out, w);
                        memcpy(grown + w, got, loaded);
                        memcpy(grown + w + loaded, text + j + 1,
                               len - j - 1);
                        grown[w + loaded + (len - j - 1)] = '\0';
                        free(got);
                        /* Recurse once for nested refs in the
                         * loaded content. */
                        char* rec = dtd_substitute_decl_pes(
                            dtd, grown,
                            w + loaded + (len - j - 1), depth + 1);
                        return rec ? rec : grown;
                    }
                    free(got);
                }
            }
            if (val) {
                char* grown = (char*)leptris_pool_alloc(
                    pool, w + val_len + (len - j) + 1);
                if (!grown) break;
                memcpy(grown, out, w);
                memcpy(grown + w, val, val_len);
                memcpy(grown + w + val_len, text + j + 1, len - j - 1);
                grown[w + val_len + (len - j - 1)] = '\0';
                out = grown;
                w = w + val_len + (len - j - 1);
                substituted = 1;
                i = j + 1;
                continue;
            }
        }
    }
}
out[w++] = text[i++];
    }
    out[w] = '\0';
    return substituted ? out : NULL;
}

static void dtd_parse_into(LeptrisDTD* dtd, const char* dtd_content,
                           size_t len, int depth) {
    LeptrisMemoryPool* pool = (LeptrisMemoryPool*)dtd->pool;

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

        /* Handle parameter entity references (%name;).
         * Phase 8b of TODO 91: substitute the entity value inline
         * so the referenced declarations are parsed. Only internal
         * parameter entities (which carry values) are substituted. */
        if (dtd_peek(&parser) == '%') {
            const char* save = parser.pos;
            parser.pos++;  /* consume '%' */
            const char* name_start = parser.pos;
            while (!dtd_at_end(&parser) &&
                   dtd_peek(&parser) != ';' &&
                   !isspace((unsigned char)dtd_peek(&parser))) {
                dtd_advance(&parser);
            }
            size_t name_len = (size_t)(parser.pos - name_start);
            if (dtd_peek(&parser) == ';' && name_len > 0 && name_len < 128) {
                char buf[130];
                buf[0] = '%';
                memcpy(buf + 1, name_start, name_len);
                buf[name_len + 1] = '\0';
                /* Look up the parameter entity (namespaced with "%"). */
                DTDEntityDecl* pe = ttdtd_lookup_entity(dtd, buf);
                if (pe && pe->type == DTD_ENTITY_INTERNAL && pe->value &&
                    depth < DTD_PE_MAX_DEPTH) {
                    /* Splice the value plus the REMAINING TAIL into a
                     * pool buffer and re-parse into the SAME dtd.
                     * Everything before the reference was already
                     * parsed by this loop, so only value+tail is
                     * re-fed — linear in the input for chains of
                     * references. The recursive call handles nested
                     * %ref; in the value (depth-capped). */
                    size_t value_len = strlen(pe->value);
                    parser.pos++;  /* consume ';' */
                    size_t tail_len = (size_t)(parser.end - parser.pos);
                    char* newbuf = (char*)leptris_pool_alloc(
                        pool, value_len + tail_len);
                    if (newbuf) {
                        memcpy(newbuf, pe->value, value_len);
                        memcpy(newbuf + value_len, parser.pos, tail_len);
                        dtd_parse_into(dtd, newbuf,
                                       value_len + tail_len, depth + 1);
                        /* The recursion consumed value+tail. */
                        parser.pos = parser.end;
                        continue;
                    }
                    /* Pool exhaustion: recover by skipping the
                     * reference instead of losing the tail. */
                    parser.pos = save;
                }
            }
            /* Fallback: skip the reference. */
            parser.pos = save + 1;
            while (!dtd_at_end(&parser) && dtd_peek(&parser) != ';') {
                dtd_advance(&parser);
            }
            if (dtd_peek(&parser) == ';') dtd_advance(&parser);
            continue;
        }



/* Parameter entities inside declaration bodies (element
         * content models, ATTLIST lists — legal in external subsets,
         * accepted leniently everywhere). ENTITY declarations are
         * excluded: their VALUES may legitimately contain '%'. */
        if (dtd_match(&parser, "<!ELEMENT") ||
            dtd_match(&parser, "<!ATTLIST") ||
            dtd_match(&parser, "<!NOTATION")) {
            const char* decl_start = parser.pos;
            const char* scan = parser.pos;
            while (scan < parser.end && *scan != '>') scan++;
            if (scan < parser.end) {
                size_t decl_len = (size_t)(scan + 1 - decl_start);
                char* sub = dtd_substitute_decl_pes(
                    dtd, decl_start, decl_len, depth);
                if (sub) {
                    dtd_parse_into(dtd, sub, strlen(sub), depth + 1);
                    parser.pos = scan + 1;
                    continue;
                }
            }
        }

        /* Parse declarations. All decl structs are POOL-OWNED
         * (ttdtd_*_create_pooled); duplicate adds are ignored —
         * the first declaration wins per XML 1.0 — and nothing is
         * freed on rejection. The old ttdtd_*_free() calls here
         * free()d pool memory on every duplicate declaration. */
        if (dtd_match(&parser, "<!ENTITY")) {
            DTDEntityDecl* entity = dtd_parse_entity(&parser);
            if (entity) {
                ttdtd_add_entity(dtd, entity);
            }
        } else if (dtd_match(&parser, "<!ELEMENT")) {
            DTDElementDecl* elem = dtd_parse_element(&parser);
            if (elem) {
                ttdtd_add_element(dtd, elem);
            }
        } else if (dtd_match(&parser, "<!NOTATION")) {
            DTDNotationDecl* notation = dtd_parse_notation(&parser);
            if (notation) {
                ttdtd_add_notation(dtd, notation);
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
                    break;
                }

                DTDAttributeDecl* decl = dtd_attribute_decl_create_pooled(elem_name, attr_name, parser.pool);
                if (!decl) {
                    break;
                }

                /* Parse attribute type: CDATA | ID | IDREF | IDREFS |
                 * NMTOKEN | NMTOKENS | (e1|e2|...).
                 * Phase 5 needs the type to enforce ID uniqueness.
                 * Pool-allocate so it lives with the DTD. */
                dtd_skip_whitespace(&parser);
                size_t type_len = 0;
                const char* type_start = NULL;
                if (dtd_peek(&parser) == '(') {
                    /* Enumerated type — find ')' */
                    dtd_advance(&parser);  /* consume '(' */
                    type_start = parser.pos;
                    while (!dtd_at_end(&parser) && dtd_peek(&parser) != ')') {
                        dtd_advance(&parser);
                    }
                    type_len = parser.pos - type_start;
                    if (dtd_peek(&parser) == ')') dtd_advance(&parser);
                } else {
                    type_start = parser.pos;
                    while (!dtd_at_end(&parser) && dtd_peek(&parser) != '>' &&
                           dtd_peek(&parser) != '#' && !isspace((unsigned char)dtd_peek(&parser))) {
                        dtd_advance(&parser);
                    }
                    type_len = parser.pos - type_start;
                }
                if (type_len > 0) {
                    char* type_copy = (char*)leptris_pool_alloc(parser.pool, type_len + 1);
                    if (type_copy) {
                        memcpy(type_copy, type_start, type_len);
                        type_copy[type_len] = '\0';
                        decl->attr_type = type_copy;
                    }
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
                                decl->default_value = val;
                            }
                        }
                    }
                } else if (dtd_peek(&parser) == '"' || dtd_peek(&parser) == '\'') {
                    char* val = dtd_parse_quoted_string(&parser);
                    if (val) {
                        decl->default_type = DTD_ATTR_DEFAULT;
                        decl->default_value = val;
                    }
                }

                ttdtd_add_attribute(dtd, decl);
            }
            if (dtd_peek(&parser) == '>') dtd_advance(&parser);
        } else if (dtd_match(&parser, "<![")) {
            /* Conditional section (TODO 91 Phase 8d). Only valid in
             * external subsets per XML 1.0 spec, but we accept them
             * everywhere for leniency. <![INCLUDE[...]]> parses the
             * inner content normally; <![IGNORE[...]]> skips to the
             * matching ]]>. Nested conditional sections are tracked
             * via a depth counter so IGNORE doesn't stop at an inner
             * ]]> belonging to a nested INCLUDE.
             *
             * The implementation here is recursive for INCLUDE (re-parses
             * the inner content as a DTD subset) and iterative for IGNORE. */
            parser.pos += 3;  /* consume "<![" */
            dtd_skip_whitespace(&parser);

            /* Parse the keyword (INCLUDE or IGNORE). */
            const char* kw_start = parser.pos;
            while (!dtd_at_end(&parser) &&
                   (isalnum((unsigned char)dtd_peek(&parser)) ||
                    dtd_peek(&parser) == '_')) {
                dtd_advance(&parser);
            }
            size_t kw_len = (size_t)(parser.pos - kw_start);
            int is_include = (kw_len == 7 && strncmp(kw_start, "INCLUDE", 7) == 0);
            int is_ignore = (kw_len == 6 && strncmp(kw_start, "IGNORE", 6) == 0);

            /* Skip to '[' that opens the body. */
            dtd_skip_whitespace(&parser);
            if (dtd_peek(&parser) == '[') dtd_advance(&parser);

            if (is_ignore) {
                /* Skip until matching ]]>. Track nesting so a nested
                 * <![INCLUDE[...]]> inside the IGNORE doesn't
                 * terminate the skip early. */
                int nest = 1;
                while (!dtd_at_end(&parser) && nest > 0) {
                    if (dtd_match(&parser, "<![")) {
                        nest++;
                        parser.pos += 3;
                    } else if (dtd_match(&parser, "]]>")) {
                        nest--;
                        parser.pos += 3;
                    } else {
                        dtd_advance(&parser);
                    }
                }
            } else if (is_include) {
                /* Parse the inner content as a DTD subset. Find the
                 * matching ]]> (respecting nesting) and recurse. */
                const char* body_start = parser.pos;
                int nest = 1;
                while (!dtd_at_end(&parser) && nest > 0) {
                    if (dtd_match(&parser, "<![")) {
                        nest++;
                        parser.pos += 3;
                    } else if (dtd_match(&parser, "]]>")) {
                        nest--;
                        if (nest == 0) break;
                        parser.pos += 3;
                    } else {
                        dtd_advance(&parser);
                    }
                }
                size_t body_len = (size_t)(parser.pos - body_start);
                /* Recurse into the SAME dtd — the inner content is
                 * itself a DTD subset (TODO.remaining/03: the old
                 * call built a fresh DTD whose declarations were
                 * discarded). */
                if (body_len > 0) {
                    dtd_parse_into(dtd, body_start, body_len, depth);
                }
                if (dtd_match(&parser, "]]>")) parser.pos += 3;
            } else {
                /* Unknown keyword — skip to ]]> (best-effort recovery). */
                int nest = 1;
                while (!dtd_at_end(&parser) && nest > 0) {
                    if (dtd_match(&parser, "<![")) {
                        nest++;
                        parser.pos += 3;
                    } else if (dtd_match(&parser, "]]>")) {
                        nest--;
                        parser.pos += 3;
                    } else {
                        dtd_advance(&parser);
                    }
                }
            }
        } else {
            /* Skip unknown declaration */
            while (!dtd_at_end(&parser) && dtd_peek(&parser) != '>') {
                dtd_advance(&parser);
            }
            if (dtd_peek(&parser) == '>') dtd_advance(&parser);
        }
    }
}

/**
 * Free DTD (wrapper for ttdtd_free for API compatibility)
 */
void leptris_dtd_free(LeptrisDTD* dtd) {
    ttdtd_free(dtd);
}

/**
 * Parse DTD from string (public API - wrapper for internal_subset)
 *
 * This is the public API function declared in include/leptris/dtd.h.
 * It's a wrapper that calls leptris_dtd_parse_internal_subset.
 *
 * NOTE (TODO 16): this public entry point has no document pool to
 * route through, so it creates a temporary pool for the DTD.  Callers
 * using this path must call ttdtd_free() to release it (the wrapper
 * preserves legacy semantics).  Internal callers should use
 * leptris_dtd_parse_internal_subset() directly with the document's
 * pool instead.
 */
LeptrisDTD* leptris_dtd_parse(const char* dtd_content, size_t len) {
    LeptrisMemoryPool* pool = leptris_pool_create();
    if (!pool) return NULL;
    LeptrisDTD* dtd = leptris_dtd_parse_internal_subset(dtd_content, len, pool);
    if (!dtd) {
        leptris_pool_destroy(pool);
        return NULL;
    }
    /* Mark this DTD as owning its pool so ttdtd_free releases it.
     * Document-pool DTDs (created via the internal parser path) leave
     * owns_pool=0; their pool is destroyed with the document. */
    dtd->owns_pool = 1;
    return dtd;
}
