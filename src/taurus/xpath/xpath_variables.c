/* xpath_variables.c - XPath Variables Implementation
 * Copyright (c) 2025, Ribose Inc.
 */

#include "xpath_variables.h"
#include "evaluator.h"  /* For xpath_nodeset_free */
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdio.h>  /* for debug fprintf — TEMPORARY */
#include <stddef.h>  /* for offsetof — TEMPORARY */

/* ============================================================================
 * Constants
 * ============================================================================ */

#define NAN_VALUE (0.0 / 0.0)  /* NaN value */

/* Initial capacity for variable set */
#define VARIABLE_SET_INITIAL_CAPACITY 8

/* ============================================================================
 * Variable Value Management
 * ============================================================================ */

XPathVariableValue xpath_value_boolean(int value) {
    XPathVariableValue v;
    v.type = XPATH_VAR_TYPE_BOOLEAN;
    v.v.boolean_value = value ? 1 : 0;
    return v;
}

XPathVariableValue xpath_value_number(double value) {
    XPathVariableValue v;
    v.type = XPATH_VAR_TYPE_NUMBER;
    v.v.number_value = value;
    return v;
}

XPathVariableValue xpath_value_string(const char* value) {
    XPathVariableValue v;
    v.type = XPATH_VAR_TYPE_STRING;
    if (value) {
        v.v.string_value = strdup(value);
    } else {
        v.v.string_value = strdup("");
    }
    return v;
}

XPathVariableValue xpath_value_nodeset(XPathNodeSet* nodeset) {
    XPathVariableValue v;
    v.type = XPATH_VAR_TYPE_NODE_SET;
    v.v.nodeset_value = nodeset;
    return v;
}

void xpath_value_free(XPathVariableValue* value) {
    if (!value) return;

    switch (value->type) {
        case XPATH_VAR_TYPE_STRING:
            free(value->v.string_value);
            value->v.string_value = NULL;
            break;
        case XPATH_VAR_TYPE_NODE_SET:
            xpath_nodeset_free(value->v.nodeset_value);
            value->v.nodeset_value = NULL;
            break;
        default:
            break;
    }
    value->type = XPATH_VAR_TYPE_NONE;
}

XPathVariableValue xpath_value_copy(const XPathVariableValue* value) {
    XPathVariableValue copy;
    copy.type = value->type;

    switch (value->type) {
        case XPATH_VAR_TYPE_BOOLEAN:
            copy.v.boolean_value = value->v.boolean_value;
            break;
        case XPATH_VAR_TYPE_NUMBER:
            copy.v.number_value = value->v.number_value;
            break;
        case XPATH_VAR_TYPE_STRING:
            copy.v.string_value = strdup(value->v.string_value);
            break;
        case XPATH_VAR_TYPE_NODE_SET:
            /* Reference the existing nodeset - caller should handle ownership */
            copy.v.nodeset_value = value->v.nodeset_value;
            break;
        default:
            copy.type = XPATH_VAR_TYPE_NONE;
            break;
    }

    return copy;
}

/* ============================================================================
 * Variable Management
 * ============================================================================ */

XPathVariable* xpath_variable_new(const char* name, XPathVariableType type) {
    /* Validate type */
    if (type == XPATH_VAR_TYPE_NONE) {
        return NULL;
    }

    /* Validate name */
    if (!name || name[0] == '\0') {
        return NULL;
    }

    /* Allocate variable */
    XPathVariable* var = (XPathVariable*)calloc(1, sizeof(XPathVariable));
    if (!var) {
        return NULL;
    }

    /* Copy name */
    var->name = strdup(name);
    if (!var->name) {
        free(var);
        return NULL;
    }

    /* Initialize value */
    var->value.type = type;
    switch (type) {
        case XPATH_VAR_TYPE_BOOLEAN:
            var->value.v.boolean_value = 0;
            break;
        case XPATH_VAR_TYPE_NUMBER:
            var->value.v.number_value = NAN_VALUE;
            break;
        case XPATH_VAR_TYPE_STRING:
            var->value.v.string_value = strdup("");
            if (!var->value.v.string_value) {
                free(var->name);
                free(var);
                return NULL;
            }
            break;
        case XPATH_VAR_TYPE_NODE_SET:
            var->value.v.nodeset_value = NULL;
            break;
        default:
            break;
    }

    return var;
}

void xpath_variable_free(XPathVariable* variable) {
    if (!variable) return;
    free(variable->name);
    xpath_value_free(&variable->value);
    free(variable);
}

const char* xpath_variable_name(const XPathVariable* variable) {
    return variable ? variable->name : NULL;
}

XPathVariableType xpath_variable_type(const XPathVariable* variable) {
    return variable ? variable->value.type : XPATH_VAR_TYPE_NONE;
}

int xpath_variable_get_boolean(const XPathVariable* variable) {
    if (!variable || variable->value.type != XPATH_VAR_TYPE_BOOLEAN) {
        return 0;
    }
    return variable->value.v.boolean_value;
}

double xpath_variable_get_number(const XPathVariable* variable) {
    if (!variable || variable->value.type != XPATH_VAR_TYPE_NUMBER) {
        return NAN_VALUE;
    }
    return variable->value.v.number_value;
}

const char* xpath_variable_get_string(const XPathVariable* variable) {
    if (!variable || variable->value.type != XPATH_VAR_TYPE_STRING) {
        return "";
    }
    return variable->value.v.string_value;
}

XPathNodeSet* xpath_variable_get_nodeset(const XPathVariable* variable) {
    if (!variable || variable->value.type != XPATH_VAR_TYPE_NODE_SET) {
        return NULL;
    }
    return variable->value.v.nodeset_value;
}

int xpath_variable_set_boolean(XPathVariable* variable, int value) {
    if (!variable || variable->value.type != XPATH_VAR_TYPE_BOOLEAN) {
        return 0;
    }
    variable->value.v.boolean_value = value ? 1 : 0;
    return 1;
}

int xpath_variable_set_number(XPathVariable* variable, double value) {
    if (!variable || variable->value.type != XPATH_VAR_TYPE_NUMBER) {
        return 0;
    }
    variable->value.v.number_value = value;
    return 1;
}

int xpath_variable_set_string(XPathVariable* variable, const char* value) {
    if (!variable || variable->value.type != XPATH_VAR_TYPE_STRING) {
        return 0;
    }

    /* Free old value */
    free(variable->value.v.string_value);

    /* Set new value */
    if (value) {
        variable->value.v.string_value = strdup(value);
    } else {
        variable->value.v.string_value = strdup("");
    }

    return (variable->value.v.string_value != NULL) ? 1 : 0;
}

int xpath_variable_set_nodeset(XPathVariable* variable, XPathNodeSet* nodeset) {
    if (!variable || variable->value.type != XPATH_VAR_TYPE_NODE_SET) {
        return 0;
    }
    variable->value.v.nodeset_value = nodeset;
    return 1;
}

/* ============================================================================
 * Variable Set Management
 * ============================================================================ */

XPathVariableSet* xpath_variable_set_new(void) {
    XPathVariableSet* set = (XPathVariableSet*)calloc(1, sizeof(XPathVariableSet));
    if (!set) {
        return NULL;
    }

    set->capacity = VARIABLE_SET_INITIAL_CAPACITY;
    set->variables = (XPathVariable**)calloc(set->capacity, sizeof(XPathVariable*));
    if (!set->variables) {
        free(set);
        return NULL;
    }

    return set;
}

void xpath_variable_set_free(XPathVariableSet* set) {
    if (!set) return;

    /* Free all variables */
    for (size_t i = 0; i < set->count; i++) {
        xpath_variable_free(set->variables[i]);
    }

    free(set->variables);
    free(set);
}

XPathVariable* xpath_variable_set_add(XPathVariableSet* set, const char* name, XPathVariableType type) {
    if (!set || !name || name[0] == '\0' || type == XPATH_VAR_TYPE_NONE) {
        return NULL;
    }

    /* Check if variable already exists */
    XPathVariable* existing = xpath_variable_set_get(set, name);
    if (existing) {
        /* Variable exists - check if type matches */
        if (existing->value.type != type) {
            return NULL;  /* Type mismatch */
        }
        return existing;  /* Return existing variable */
    }

    /* Expand capacity if needed */
    if (set->count >= set->capacity) {
        size_t new_capacity = set->capacity * 2;
        XPathVariable** new_vars = (XPathVariable**)realloc(set->variables,
            new_capacity * sizeof(XPathVariable*));
        if (!new_vars) {
            return NULL;
        }
        set->variables = new_vars;
        set->capacity = new_capacity;
    }

    /* Create new variable */
    XPathVariable* var = xpath_variable_new(name, type);
    if (!var) {
        return NULL;
    }

    /* Add to set */
    set->variables[set->count++] = var;
    return var;
}

XPathVariable* xpath_variable_set_get(XPathVariableSet* set, const char* name) {
    if (!set || !name) return NULL;

    for (size_t i = 0; i < set->count; i++) {
        if (strcmp(set->variables[i]->name, name) == 0) {
            return set->variables[i];
        }
    }
    return NULL;
}

const XPathVariable* xpath_variable_set_get_const(const XPathVariableSet* set, const char* name) {
    if (!set || !name) return NULL;

    static int once = 0;
    if (!once++) {
        fprintf(stderr, "DEBUG sizeof(XPathVariable)=%zu offsetof(name)=%zu offsetof(value)=%zu\n",
                sizeof(XPathVariable), offsetof(XPathVariable, name), offsetof(XPathVariable, value));
        fprintf(stderr, "DEBUG sizeof(XPathVariableValue)=%zu sizeof(XPathVariableSet)=%zu sizeof(XPathVariableType)=%zu\n",
                sizeof(XPathVariableValue), sizeof(XPathVariableSet), sizeof(XPathVariableType));
    }
    fprintf(stderr, "DEBUG get_const: set=%p count=%zu variables=%p name=%s\n",
            (void*)set, set->count, (void*)set->variables, name);
    for (size_t i = 0; i < set->count; i++) {
        XPathVariable* v = set->variables[i];
        fprintf(stderr, "DEBUG [%zu]: var=%p name=%p\n", i, (void*)v, v ? (void*)v->name : NULL);
        if (!v || !v->name) continue;
        if (strcmp(v->name, name) == 0) {
            return v;
        }
    }
    return NULL;
}

int xpath_variable_set_remove(XPathVariableSet* set, const char* name) {
    if (!set || !name) return 0;

    for (size_t i = 0; i < set->count; i++) {
        if (strcmp(set->variables[i]->name, name) == 0) {
            /* Free variable */
            xpath_variable_free(set->variables[i]);

            /* Shift remaining variables */
            for (size_t j = i; j < set->count - 1; j++) {
                set->variables[j] = set->variables[j + 1];
            }
            set->count--;
            return 1;
        }
    }
    return 0;
}

size_t xpath_variable_set_count(const XPathVariableSet* set) {
    return set ? set->count : 0;
}

void xpath_variable_set_clear(XPathVariableSet* set) {
    if (!set) return;

    /* Free all variables */
    for (size_t i = 0; i < set->count; i++) {
        xpath_variable_free(set->variables[i]);
    }
    set->count = 0;
}

XPathVariableSet* xpath_variable_set_copy(const XPathVariableSet* set) {
    if (!set) return NULL;

    XPathVariableSet* copy = xpath_variable_set_new();
    if (!copy) return NULL;

    for (size_t i = 0; i < set->count; i++) {
        XPathVariable* orig = set->variables[i];
        XPathVariable* var = xpath_variable_set_add(copy, orig->name, orig->value.type);
        if (!var) {
            xpath_variable_set_free(copy);
            return NULL;
        }

        /* Copy value */
        switch (orig->value.type) {
            case XPATH_VAR_TYPE_BOOLEAN:
                var->value.v.boolean_value = orig->value.v.boolean_value;
                break;
            case XPATH_VAR_TYPE_NUMBER:
                var->value.v.number_value = orig->value.v.number_value;
                break;
            case XPATH_VAR_TYPE_STRING:
                var->value.v.string_value = strdup(orig->value.v.string_value);
                if (!var->value.v.string_value) {
                    xpath_variable_set_free(copy);
                    return NULL;
                }
                break;
            case XPATH_VAR_TYPE_NODE_SET:
                /* Just reference, don't deep copy nodeset */
                var->value.v.nodeset_value = orig->value.v.nodeset_value;
                break;
            default:
                break;
        }
    }

    return copy;
}
