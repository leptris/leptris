/**
 * @file dtd/resolver.c
 * @brief DTD Entity Resolver
 *
 * Expands entity references in text content using DTD declarations.
 * Handles recursive entities with cycle detection and depth limits.
 */

#include "model.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Predefined XML entities (built-in) */
typedef struct {
    const char* name;
    const char* value;
} PredefinedEntity;

static const PredefinedEntity predefined_entities[] = {
    {"lt", "<"},
    {"gt", ">"},
    {"amp", "&"},
    {"apos", "'"},
    {"quot", "\""},
    {NULL, NULL}
};

/* Maximum recursion depth for entity expansion */
#define MAX_ENTITY_DEPTH 10

/* Expansion state for tracking recursion */
typedef struct {
    const char** stack;      /* Array of entity names being expanded */
    size_t depth;            /* Current recursion depth */
    size_t capacity;         /* Stack capacity */
} EntityExpansionState;

/**
 * Initialize expansion state
 */
static EntityExpansionState* expansion_state_create(void) {
    EntityExpansionState* state = (EntityExpansionState*)calloc(1, sizeof(EntityExpansionState));
    if (!state) return NULL;

    state->capacity = 16;
    state->stack = (const char**)calloc(state->capacity, sizeof(const char*));
    if (!state->stack) {
        free(state);
        return NULL;
    }

    state->depth = 0;
    return state;
}

/**
 * Free expansion state
 */
static void expansion_state_free(EntityExpansionState* state) {
    if (!state) return;
    free(state->stack);
    free(state);
}

/**
 * Check if entity is already in expansion stack (circular reference)
 */
static int expansion_state_contains(EntityExpansionState* state, const char* name) {
    for (size_t i = 0; i < state->depth; i++) {
        if (strcmp(state->stack[i], name) == 0) {
            return 1;
        }
    }
    return 0;
}

/**
 * Push entity onto expansion stack
 */
static int expansion_state_push(EntityExpansionState* state, const char* name) {
    if (state->depth >= state->capacity) {
        /* Resize stack */
        size_t new_capacity = state->capacity * 2;
        const char** new_stack = (const char**)realloc(state->stack,
                                                       new_capacity * sizeof(const char*));
        if (!new_stack) return 0;

        state->stack = new_stack;
        state->capacity = new_capacity;
    }

    state->stack[state->depth++] = name;
    return 1;
}

/**
 * Pop entity from expansion stack
 */
static void expansion_state_pop(EntityExpansionState* state) {
    if (state->depth > 0) {
        state->depth--;
    }
}

/**
 * Lookup predefined entity
 */
static const char* lookup_predefined_entity(const char* name) {
    for (size_t i = 0; predefined_entities[i].name != NULL; i++) {
        if (strcmp(predefined_entities[i].name, name) == 0) {
            return predefined_entities[i].value;
        }
    }
    return NULL;
}

/**
 * Calculate expanded length of entity value
 *
 * Counts the length of entity value after expanding any nested
 * entity references within it.
 */
static size_t calculate_expanded_length(const LeptrisDTD* dtd,
                                        const char* value,
                                        EntityExpansionState* state) {
    size_t len = 0;
    const char* p = value;

    while (*p) {
        if (*p == '&' && *(p + 1) != '#') {
            /* Entity reference */
            const char* end = strchr(p, ';');
            if (end) {
                /* Extract entity name */
                size_t name_len = end - p - 1;
                char* name = (char*)malloc(name_len + 1);
                if (name) {
                    memcpy(name, p + 1, name_len);
                    name[name_len] = '\0';

                    /* Check for circular reference */
                    if (expansion_state_contains(state, name)) {
                        free(name);
                        return 0; /* Circular reference */
                    }

                    /* Lookup entity */
                    const char* entity_value = lookup_predefined_entity(name);
                    if (!entity_value && dtd) {
                        DTDEntityDecl* entity = ttdtd_lookup_entity(dtd, name);
                        if (entity && entity->type == DTD_ENTITY_INTERNAL && entity->value) {
                            entity_value = entity->value;
                        }
                    }

                    if (entity_value) {
                        /* Recursively calculate nested length */
                        if (state->depth < MAX_ENTITY_DEPTH) {
                            expansion_state_push(state, name);
                            size_t nested_len = calculate_expanded_length(dtd, entity_value, state);
                            expansion_state_pop(state);
                            len += nested_len;
                            if (nested_len == 0) {
                                free(name);
                                return 0; /* Error in nested expansion */
                            }
                        } else {
                            /* Max depth exceeded - use literal value */
                            len += strlen(entity_value);
                        }
                    } else {
                        /* Unknown entity - keep original reference */
                        len += (end - p + 1);
                    }

                    free(name);
                    p = end + 1;
                    continue;
                }
            }
        }
        len++;
        p++;
    }

    return len;
}

/**
 * Expand entity references in a value
 *
 * Recursively expands entity references like &name; with their
 * declared values. Handles nested entities and detects cycles.
 */
static char* expand_entity_references(const LeptrisDTD* dtd,
                                      const char* value,
                                      EntityExpansionState* state,
                                      size_t* result_len) {
    /* Calculate required buffer size */
    size_t len = calculate_expanded_length(dtd, value, state);
    if (len == 0 && *value != '\0') {
        /* Expansion failed (circular reference or error) */
        return NULL;
    }

    /* Allocate result buffer */
    char* result = (char*)malloc(len + 1);
    if (!result) return NULL;

    /* Expand entities */
    char* dest = result;
    const char* p = value;

    while (*p) {
        if (*p == '&' && *(p + 1) != '#') {
            /* Entity reference */
            const char* end = strchr(p, ';');
            if (end) {
                /* Extract entity name */
                size_t name_len = end - p - 1;
                char* name = (char*)malloc(name_len + 1);
                if (name) {
                    memcpy(name, p + 1, name_len);
                    name[name_len] = '\0';

                    /* Check for circular reference */
                    if (expansion_state_contains(state, name)) {
                        free(name);
                        free(result);
                        return NULL; /* Circular reference */
                    }

                    /* Lookup entity */
                    const char* entity_value = lookup_predefined_entity(name);
                    if (!entity_value && dtd) {
                        DTDEntityDecl* entity = ttdtd_lookup_entity(dtd, name);
                        if (entity && entity->type == DTD_ENTITY_INTERNAL && entity->value) {
                            entity_value = entity->value;
                        }
                    }

                    if (entity_value) {
                        /* Recursively expand nested entities */
                        if (state->depth < MAX_ENTITY_DEPTH) {
                            size_t nested_len = 0;
                            expansion_state_push(state, name);
                            char* nested = expand_entity_references(dtd, entity_value, state, &nested_len);
                            expansion_state_pop(state);

                            if (nested) {
                                /* Copy expanded value */
                                size_t copy_len = strlen(nested);
                                memcpy(dest, nested, copy_len);
                                dest += copy_len;
                                free(nested);

                                p = end + 1;
                                free(name);
                                continue;
                            } else {
                                free(name);
                                free(result);
                                return NULL; /* Expansion error */
                            }
                        } else {
                            /* Max depth exceeded - copy literal value */
                            size_t copy_len = strlen(entity_value);
                            memcpy(dest, entity_value, copy_len);
                            dest += copy_len;
                            p = end + 1;
                            free(name);
                            continue;
                        }
                    } else {
                        /* Unknown entity - copy reference as-is */
                        size_t copy_len = end - p + 1;
                        memcpy(dest, p, copy_len);
                        dest += copy_len;
                        p = end + 1;
                        free(name);
                        continue;
                    }
                }
            }
        }
        *dest++ = *p++;
    }

    *dest = '\0';
    if (result_len) *result_len = len;

    return result;
}

/**
 * Expand entities in text content
 *
 * @param dtd DTD container (can be NULL for predefined entities only)
 * @param text Text content with entity references
 * @param len Length of text content
 * @param result_len Output: length of expanded text (can be NULL)
 * @return Expanded text, or NULL on error (caller must free)
 *
 * Example:
 *   Input:  "Hello &world;!"
 *   DTD:    <!ENTITY world "Earth">
 *   Output: "Hello Earth!"
 */
char* leptris_dtd_expand_entities(const LeptrisDTD* dtd,
                                  const char* text,
                                  size_t len,
                                  size_t* result_len) {
    if (!text || len == 0) return NULL;

    /* Create expansion state */
    EntityExpansionState* state = expansion_state_create();
    if (!state) return NULL;

    /* Expand entity references */
    char* result = expand_entity_references(dtd, text, state, result_len);

    /* Cleanup */
    expansion_state_free(state);

    return result;
}

/**
 * Lookup entity by name (two-tier: predefined → custom)
 *
 * @param dtd DTD container (can be NULL for predefined only)
 * @param name Entity name to lookup
 * @return Entity value (predefined or custom), or NULL if not found
 *
 * Note: Returns pointer to internal storage - do not free
 */
const char* leptris_dtd_lookup_entity(const LeptrisDTD* dtd, const char* name) {
    if (!name) return NULL;

    /* First check predefined entities */
    const char* value = lookup_predefined_entity(name);
    if (value) return value;

    /* Then check custom entities */
    if (dtd) {
        DTDEntityDecl* entity = ttdtd_lookup_entity(dtd, name);
        if (entity && entity->type == DTD_ENTITY_INTERNAL) {
            return entity->value;
        }
    }

    return NULL;
}
