/* dtd/validator.c — DTD validation engine.
 *
 * Phase 1 (this file): EMPTY content model + unknown-element detection.
 * Walks the document tree, finds each element's declaration in the DTD,
 * and checks the simplest invariants:
 *   - EMPTY elements must have no element children.
 *   - Elements with declarations should have content matching the model
 *     (Phase 1 only enforces EMPTY; mixed/children models need the
 *     grammar matcher that's Phase 3+).
 *
 * Return codes:
 *   1 = document is valid (no violations found).
 *   0 = at least one violation found; `error` is populated with the first.
 *  -1 = internal error (couldn't run validation).
 *
 * Future phases (TODO 91):
 *   2: ATTLIST parsing + #REQUIRED enforcement.
 *   3: Element-content grammar matcher (sequences, choices, modifiers).
 *   4: Attribute type validation (ID, IDREF, NMTOKEN, enumerated).
 *   5: ENTITY-typed attribute resolution.
 */

#include "../../include/taurus.h"
#include "../../include/taurus/dtd.h"
#include "model.h"
#include "../dom/element.h"
#include "../dom/node.h"
#include "../memory/pool.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdint.h>

/* Forward decl from content_check.c (Phase 4 of TODO 91). */
int taurus_content_model_match(const char* model, const char* elem_name,
                                const char** child_names, size_t child_count,
                                char* out_msg, size_t msg_size);

/* TODO 119: per-validation content-model memoization.
 *
 * Elements with the same content model and the same children signature
 * produce the same match result.  Without memoization, validating a
 * document with N identical elements re-runs the matcher N times.
 * With memoization, the second and subsequent calls return a cached
 * result in O(1).
 *
 * The cache lives for the duration of a single taurus_dtd_validate
 * call -- allocated on the stack, no malloc.  Bounded by MEMO_CAP;
 * LRU eviction via ring buffer.  Most docs have <64 distinct
 * (model, children-shape) pairs. */
#define CONTENT_MODEL_MEMO_CAP 64

typedef struct {
    const char* model;       /* pointer into DTD decl; stable for cache life */
    size_t child_count;
    /* Hash of the child_names array -- avoids storing the names. */
    uint32_t child_hash;
    int      match_result;   /* 1 = match, 0 = mismatch */
    char     error_msg[256];
    int      used;           /* 0 = empty slot */
} ContentModelMemoEntry;

typedef struct {
    ContentModelMemoEntry entries[CONTENT_MODEL_MEMO_CAP];
    size_t next;             /* ring-buffer write index */
} ContentModelMemo;

/* FNV-1a hash over a child_names array.  Stable across calls because
 * the array elements are pool-owned name strings (lifetime = doc). */
static uint32_t hash_child_names(const char** names, size_t count) {
    uint32_t h = 2166136261u;
    for (size_t i = 0; i < count; i++) {
        const char* s = names[i] ? names[i] : "";
        while (*s) {
            h ^= (unsigned char)*s++;
            h *= 16777619u;
        }
        h ^= ',';
        h *= 16777619u;
    }
    return h;
}

/* Look up memo.  Returns NULL on miss.  Match is on (model pointer,
 * child_count, child_hash) -- assumes the model string is stable for
 * the duration of the validation (it's pool-owned on the DTD decl). */
static ContentModelMemoEntry* memo_lookup(ContentModelMemo* m,
                                           const char* model,
                                           size_t child_count,
                                           uint32_t child_hash) {
    for (size_t i = 0; i < CONTENT_MODEL_MEMO_CAP; i++) {
        ContentModelMemoEntry* e = &m->entries[i];
        if (e->used && e->model == model &&
            e->child_count == child_count &&
            e->child_hash == child_hash) {
            return e;
        }
    }
    return NULL;
}

/* Store a new memo entry.  Ring-buffer eviction. */
static void memo_store(ContentModelMemo* m, const char* model,
                        size_t child_count, uint32_t child_hash,
                        int match_result, const char* error_msg) {
    ContentModelMemoEntry* e = &m->entries[m->next];
    m->next = (m->next + 1) % CONTENT_MODEL_MEMO_CAP;
    e->model = model;
    e->child_count = child_count;
    e->child_hash = child_hash;
    e->match_result = match_result;
    snprintf(e->error_msg, sizeof(e->error_msg), "%s",
             error_msg ? error_msg : "");
    e->used = 1;
}

static char* dup_str(const char* src) {
    if (!src) return NULL;
    size_t len = strlen(src);
    char* out = (char*)malloc(len + 1);
    if (out) {
        memcpy(out, src, len + 1);
    }
    return out;
}

static void set_error(TaurusDTDError* error, const char* msg, const char* elem_name) {
    if (!error) return;
    error->message = dup_str(msg);
    error->element_name = elem_name ? dup_str(elem_name) : NULL;
    error->line = 0;
    error->column = 0;
}

/* Phase 5: hash table for tracking ID values across the document. */
typedef struct {
    StringHashTable* ids;  /* maps id value → element name */
    TaurusDTDError* error;
    int found_violation;
} IdCheckContext;

/* IDs need to be tracked in a document-level table, not per-element.
 * The walker accumulates them as it visits each element with an
 * ID-typed attribute. */

static int validate_element_recursive(TaurusElement elem, TaurusDTD* dtd,
                                       TaurusDTDError* error,
                                       StringHashTable* id_table,
                                       ContentModelMemo* memo);

/* Return 1 if this element has any element-type children, 0 otherwise.
 * Used to validate <!ELEMENT name EMPTY> — no element children allowed. */
static int element_has_element_children(TaurusElement elem) {
    TaurusNodeRef child = taurus_node_first_child((TaurusNodeRef)elem);
    while (child) {
        if (taurus_node_get_type(child) == TAURUS_NODE_TYPE_ELEMENT) {
            return 1;
        }
        child = taurus_node_next_sibling(child);
    }
    return 0;
}

/* Phase 3: check #REQUIRED ATTLIST attributes by iterating the DTD's
 * attribute hash table. The iterator context carries the element under
 * validation and the error struct so we can short-circuit on first
 * violation. */
typedef struct {
    TaurusElement elem;
    const char* elem_name;
    size_t elem_name_len;
    TaurusDTDError* error;
    int found_violation;
} AttrCheckContext;

static int attr_check_iter(const char* key, size_t key_len,
                           void* value, void* user_data) {
    AttrCheckContext* ctx = (AttrCheckContext*)user_data;
    DTDAttributeDecl* decl = (DTDAttributeDecl*)value;

    /* Hash key is "element.attr"; does this entry belong to our element? */
    if (key_len <= ctx->elem_name_len + 1) return 1;  /* continue */
    if (memcmp(key, ctx->elem_name, ctx->elem_name_len) != 0) return 1;
    if (key[ctx->elem_name_len] != '.') return 1;

    /* This declaration belongs to the element. If #REQUIRED and the
     * attribute is missing from the document, that's a violation. */
    if (decl->default_type == DTD_ATTR_REQUIRED) {
        const char* attr_name = key + ctx->elem_name_len + 1;
        size_t attr_name_len = key_len - ctx->elem_name_len - 1;
        /* Build a temporary null-terminated name so the lookup helper
         * can use it. Pool the alloc to avoid OOM bookkeeping. */
        char attr_buf[256];
        if (attr_name_len < sizeof(attr_buf)) {
            memcpy(attr_buf, attr_name, attr_name_len);
            attr_buf[attr_name_len] = '\0';
            struct taurus_attribute* present =
                taurus_element_get_attribute_by_name(ctx->elem, attr_buf);
            if (!present) {
                char msg_buf[200];
                snprintf(msg_buf, sizeof(msg_buf),
                         "Element '%s' missing #REQUIRED attribute '%s'",
                         ctx->elem_name, attr_buf);
                set_error(ctx->error, msg_buf, ctx->elem_name);
                ctx->found_violation = 1;
                return 0;  /* stop iteration */
            }
        }
    }
    return 1;  /* continue */
}

static int validate_element_recursive(TaurusElement elem, TaurusDTD* dtd,
                                       TaurusDTDError* error,
                                       StringHashTable* id_table,
                                       ContentModelMemo* memo) {
    if (!elem) return 1;

    const char* name = taurus_element_get_name(elem);
    if (!name) return 1;

    /* Look up the element declaration in the DTD. */
    DTDElementDecl* decl = ttdtd_lookup_element(dtd, name);

    if (decl) {
        /* Phase 1: only EMPTY is enforced. EMPTY means NO element
         * children. Text content (whitespace) is tolerated per common
         * XML processor behavior, since whitespace is typically
         * formatting-only. */
        if (decl->content_type == DTD_CONTENT_EMPTY) {
            if (element_has_element_children(elem)) {
                set_error(error,
                          "Element declared EMPTY has element children",
                          name);
                return 0;
            }
        }
        /* DTD_CONTENT_ANY: any content allowed, always valid. */
        if (decl->content_type == DTD_CONTENT_ANY) {
            /* Valid by definition; fall through. */
        } else if (decl->content_model && decl->content_model[0]) {
            /* Phase 4: walk the element's actual element-type
             * children and match them against the parsed content
             * model. Phase 1's EMPTY check already handles the empty
             * case; the matcher handles the other content types
             * (CHILDREN, MIXED, ELEMENT) where a model is stored. */
            size_t child_count = 0;
            for (TaurusNodeRef c = taurus_node_first_child((TaurusNodeRef)elem);
                 c; c = taurus_node_next_sibling(c)) {
                if (taurus_node_get_type(c) == TAURUS_NODE_TYPE_ELEMENT) {
                    child_count++;
                }
            }
            if (child_count > 0) {
                const char** child_names = (const char**)malloc(
                    child_count * sizeof(const char*));
                if (child_names) {
                    size_t i = 0;
                    for (TaurusNodeRef c = taurus_node_first_child((TaurusNodeRef)elem);
                         c && i < child_count; c = taurus_node_next_sibling(c)) {
                        if (taurus_node_get_type(c) == TAURUS_NODE_TYPE_ELEMENT) {
                            child_names[i++] = taurus_element_get_name((TaurusElement)c);
                        }
                    }
                    /* TODO 119: memoize.  The cache key is (model
                     * pointer, child_count, child_hash) -- pool-owned
                     * model string is stable for the validation
                     * lifetime.  Hit: skip the matcher.  Miss: run
                     * matcher, store result. */
                    uint32_t ch = hash_child_names(child_names, child_count);
                    ContentModelMemoEntry* hit = NULL;
                    if (memo) {
                        hit = memo_lookup(memo, decl->content_model,
                                          child_count, ch);
                    }
                    int ok;
                    char msg_buf[256];
                    if (hit) {
                        ok = hit->match_result;
                        snprintf(msg_buf, sizeof(msg_buf), "%s", hit->error_msg);
                    } else {
                        ok = taurus_content_model_match(
                            decl->content_model, name, child_names, child_count,
                            msg_buf, sizeof(msg_buf));
                        if (memo) {
                            memo_store(memo, decl->content_model, child_count,
                                       ch, ok, msg_buf);
                        }
                    }
                    if (!ok) {
                        set_error(error, msg_buf, name);
                        free((void*)child_names);
                        return 0;
                    }
                    free((void*)child_names);
                }
            }
        }
        /* DTD_CONTENT_PCDATA (text-only): unmatched here; Phase 4
         * only validates element-type children. */
    }
    /* Phase 1 does NOT error on undeclared elements — that's stricter
     * than most real-world DTD validation (which often permits
     * additional elements). Add an option later if needed. */

    /* Phase 3: walk attribute declarations for this element. */
    AttrCheckContext ctx = {
        .elem = elem,
        .elem_name = name,
        .elem_name_len = strlen(name),
        .error = error,
        .found_violation = 0,
    };
    taurus_hash_table_for_each((StringHashTable*)dtd->tables.attributes,
                               attr_check_iter, &ctx);
    if (ctx.found_violation) return 0;

    /* Phase 5: ID uniqueness. Walk the element's attributes; if any
     * is declared as type "ID", record its value in id_table. The
     * first occurrence of a duplicate triggers a violation. */
    uint8_t ac = taurus_element_attribute_count(elem);
    for (uint8_t i = 0; i < ac; i++) {
        struct taurus_attribute* attr = taurus_element_get_attribute_by_index(elem, i);
        if (!attr) continue;
        const char* attr_name = attr->name;
        if (!attr_name || !attr->value) continue;
        DTDAttributeDecl* ad = ttdtd_lookup_attribute(dtd, name, attr_name);
        if (!ad || !ad->attr_type) continue;

        /* #FIXED: the element's attribute value must exactly match
         * the fixed value declared in the DTD. */
        if (ad->default_type == DTD_ATTR_FIXED && ad->default_value) {
            if (strcmp(attr->value, ad->default_value) != 0) {
                char msg[200];
                snprintf(msg, sizeof(msg),
                         "Attribute '%s' has #FIXED value '%s' but document has '%s'",
                         attr_name, ad->default_value, attr->value);
                set_error(error, msg, name);
                return 0;
            }
        }

        if (strcmp(ad->attr_type, "ID") == 0) {
            const char* id_value = attr->value;
            size_t id_len = strlen(id_value);
            if (taurus_hash_table_get(id_table, id_value, id_len) != NULL) {
                char msg[160];
                snprintf(msg, sizeof(msg),
                         "Duplicate ID value '%s'", id_value);
                set_error(error, msg, name);
                return 0;
            }
            if (!taurus_hash_table_set(id_table, id_value, id_len,
                                        (void*)(uintptr_t)1,
                                        (TaurusMemoryPool*)dtd->pool)) {
                /* OOM — would need proper error reporting. */
            }
        }
        /* Phase 6: IDREF. We can't check yet — the referenced ID
         * might be declared later in the document. Defer to a
         * second pass after all IDs are collected. The second pass
         * (validate_idref_pass) walks again and verifies each IDREF
         * against the now-complete id_table. */

        /* Phase 7: NMTOKEN / Enumerated type validation.
         * These can be checked in-place (no cross-element state). */
        if (strcmp(ad->attr_type, "NMTOKEN") == 0) {
            const char* v = attr->value;
            int valid = (v && *v);
            for (const char* p = v; *p; p++) {
                unsigned char c = (unsigned char)*p;
                if (!(isalnum(c) || c == '.' || c == '-' ||
                      c == '_' || c == ':')) {
                    valid = 0;
                    break;
                }
            }
            if (!valid) {
                char msg[180];
                snprintf(msg, sizeof(msg),
                         "Attribute '%s' is not a valid NMTOKEN", attr_name);
                set_error(error, msg, name);
                return 0;
            }
        } else if (strcmp(ad->attr_type, "NMTOKENS") == 0) {
            /* Whitespace-separated list of NMTOKENs. Each non-empty
             * token must match the NMTOKEN character class. */
            const char* p = attr->value;
            while (*p) {
                while (*p && isspace((unsigned char)*p)) p++;
                if (!*p) break;
                int tok_ok = 0;
                while (*p && !isspace((unsigned char)*p)) {
                    unsigned char c = (unsigned char)*p;
                    if (!(isalnum(c) || c == '.' || c == '-' ||
                          c == '_' || c == ':')) {
                        tok_ok = -1;
                    }
                    p++;
                }
                if (tok_ok == -1) {
                    char msg[180];
                    snprintf(msg, sizeof(msg),
                             "Attribute '%s' contains an invalid NMTOKENS token",
                             attr_name);
                    set_error(error, msg, name);
                    return 0;
                }
                tok_ok = 1;
                (void)tok_ok;
            }
        } else if (strchr(ad->attr_type, '|') != NULL) {
            /* Enumerated type: attr_type is "opt1|opt2|opt3" (the
             * parser strips the surrounding parens). The value must
             * match one of the pipe-separated options. */
            const char* val = attr->value;
            const char* model = ad->attr_type;
            int matched = 0;
            while (*model) {
                /* Extract one option token. */
                const char* opt_start = model;
                while (*model && *model != '|') model++;
                size_t opt_len = (size_t)(model - opt_start);
                if (strlen(val) == opt_len &&
                    strncmp(val, opt_start, opt_len) == 0) {
                    matched = 1;
                    break;
                }
                if (*model == '|') model++;
            }
            if (!matched) {
                char msg[200];
                snprintf(msg, sizeof(msg),
                         "Attribute '%s' value '%s' is not in enumerated type %s",
                         attr_name, val, ad->attr_type);
                set_error(error, msg, name);
                return 0;
            }
        }

        /* Phase 8: ENTITY / ENTITIES attribute validation (TODO 91).
         * Each value must be the name of an unparsed entity declared
         * via <!ENTITY name SYSTEM "uri" NDATA notation>, and the
         * referenced notation must itself be declared via <!NOTATION>.
         * Entities are stored in dtd->tables.entities; notations in
         * dtd->tables.notations. */
        if (strcmp(ad->attr_type, "ENTITY") == 0 ||
            strcmp(ad->attr_type, "ENTITIES") == 0) {
            const char* p = attr->value;
            while (*p) {
                /* Skip whitespace between tokens (ENTITIES). */
                while (*p && isspace((unsigned char)*p)) p++;
                if (!*p) break;

                /* Extract one name token. */
                const char* tok_start = p;
                while (*p && !isspace((unsigned char)*p)) p++;
                size_t tok_len = (size_t)(p - tok_start);

                /* Look up in the entity table. */
                char name_buf[256];
                if (tok_len >= sizeof(name_buf)) {
                    char msg[200];
                    snprintf(msg, sizeof(msg),
                             "ENTITY name too long in attribute '%s'", attr_name);
                    set_error(error, msg, name);
                    return 0;
                }
                memcpy(name_buf, tok_start, tok_len);
                name_buf[tok_len] = '\0';

                DTDEntityDecl* entity = ttdtd_lookup_entity(dtd, name_buf);
                if (!entity) {
                    char msg[200];
                    snprintf(msg, sizeof(msg),
                             "Attribute '%s' references undeclared entity '%s'",
                             attr_name, name_buf);
                    set_error(error, msg, name);
                    return 0;
                }
                /* Per XML 1.0 spec: ENTITY-typed attributes must
                 * reference unparsed entities (those with NDATA). */
                if (!entity->notation_name) {
                    char msg[200];
                    snprintf(msg, sizeof(msg),
                             "Attribute '%s' references parsed entity '%s' "
                         "(must be unparsed / have NDATA)",
                             attr_name, name_buf);
                    set_error(error, msg, name);
                    return 0;
                }
                /* The notation itself must be declared. */
                if (!ttdtd_lookup_notation(dtd, entity->notation_name)) {
                    char msg[220];
                    snprintf(msg, sizeof(msg),
                             "Entity '%s' references undeclared notation '%s'",
                             name_buf, entity->notation_name);
                    set_error(error, msg, name);
                    return 0;
                }
            }
        }
    }

    /* Recurse into children. */
    TaurusElement child = taurus_element_first_child_any(elem);
    while (child) {
        int rc = validate_element_recursive(child, dtd, error, id_table, memo);
        if (rc != 1) return rc;  /* propagate first violation */
        child = taurus_element_next_sibling_any(child);
    }
    return 1;
}

/* Phase 6: second pass — verify IDREF attributes resolve to existing IDs.
 * By the time this runs, id_table contains every ID in the document. */
static int validate_idref_pass(TaurusElement elem, TaurusDTD* dtd,
                                StringHashTable* id_table,
                                TaurusDTDError* error) {
    if (!elem) return 1;
    const char* name = taurus_element_get_name(elem);
    if (name) {
        uint8_t ac = taurus_element_attribute_count(elem);
        for (uint8_t i = 0; i < ac; i++) {
            struct taurus_attribute* attr = taurus_element_get_attribute_by_index(elem, i);
            if (!attr) continue;
            const char* attr_name = attr->name;
            if (!attr_name || !attr->value) continue;
            DTDAttributeDecl* ad = ttdtd_lookup_attribute(dtd, name, attr_name);
            if (!ad || !ad->attr_type) continue;
            /* IDREF (single) and IDREFS (whitespace-separated list). */
            if (strcmp(ad->attr_type, "IDREF") == 0) {
                const char* ref = attr->value;
                size_t ref_len = strlen(ref);
                if (taurus_hash_table_get(id_table, ref, ref_len) == NULL) {
                    char msg[180];
                    snprintf(msg, sizeof(msg),
                             "IDREF '%s' does not resolve to any ID", ref);
                    set_error(error, msg, name);
                    return 0;
                }
            } else if (strcmp(ad->attr_type, "IDREFS") == 0) {
                /* Whitespace-separated list of references. */
                const char* p = attr->value;
                while (*p) {
                    while (*p && isspace((unsigned char)*p)) p++;
                    if (!*p) break;
                    const char* start = p;
                    while (*p && !isspace((unsigned char)*p)) p++;
                    size_t one_len = (size_t)(p - start);
                    if (taurus_hash_table_get(id_table, start, one_len) == NULL) {
                        char ref_buf[128];
                        size_t copy = one_len < sizeof(ref_buf) - 1
                                          ? one_len : sizeof(ref_buf) - 1;
                        memcpy(ref_buf, start, copy);
                        ref_buf[copy] = '\0';
                        char msg[200];
                        snprintf(msg, sizeof(msg),
                                 "IDREFS token '%s' does not resolve to any ID",
                                 ref_buf);
                        set_error(error, msg, name);
                        return 0;
                    }
                }
            }
        }
    }
    TaurusElement child = taurus_element_first_child_any(elem);
    while (child) {
        int rc = validate_idref_pass(child, dtd, id_table, error);
        if (rc != 1) return rc;
        child = taurus_element_next_sibling_any(child);
    }
    return 1;
}

int taurus_dtd_validate(TaurusDocument doc, TaurusDTD* dtd, TaurusDTDError* error) {
    if (!doc || !dtd) {
        if (error) {
            set_error(error, "NULL document or DTD passed to taurus_dtd_validate", NULL);
        }
        return -1;
    }

    TaurusElement root = taurus_document_root(doc);
    if (!root) {
        /* Empty document is valid by definition. */
        return 1;
    }

    /* Phase 5: allocate the ID-tracking hash table on the DTD's
     * pool so it shares the DTD's lifetime. */
    StringHashTable* id_table = taurus_hash_table_create(
        (TaurusMemoryPool*)dtd->pool, 16);
    if (!id_table) {
        if (error) set_error(error, "Failed to allocate ID table", NULL);
        return -1;
    }
    /* TODO 119: per-validation memo cache.  Stack-allocated; no
     * malloc; freed when this function returns. */
    ContentModelMemo memo = {{{0}}};
    memo.next = 0;

    int rc = validate_element_recursive(root, dtd, error, id_table, &memo);
    if (rc == 1) {
        /* Phase 6: IDREF resolution. Run after Phase 5 completes so
         * the ID table contains every ID in the document. */
        rc = validate_idref_pass(root, dtd, id_table, error);
    }
    taurus_hash_table_destroy(id_table);
    return rc;
}

void taurus_dtd_error_free(TaurusDTDError* error) {
    if (!error) return;
    if (error->message) {
        free(error->message);
        error->message = NULL;
    }
    if (error->element_name) {
        free(error->element_name);
        error->element_name = NULL;
    }
    error->line = 0;
    error->column = 0;
}
