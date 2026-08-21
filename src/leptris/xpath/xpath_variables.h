/* xpath_variables.h - XPath Variables Support
 * Copyright (c) 2025, Ribose Inc.
 *
 * XPath variables allow external values to be passed into XPath queries.
 * This is a core XPath 1.0 feature that enables parameterized queries.
 */

#ifndef XPATH_VARIABLES_H
#define XPATH_VARIABLES_H

#include "../leptris_internal.h"

/* ============================================================================
 * XPath Variable Types
 * ============================================================================ */

/**
 * XPath variable value types
 * Matches XPath 1.0 specification: boolean, number, string, node-set
 */
typedef enum {
    XPATH_VAR_TYPE_NONE = 0,      /* Invalid type */
    XPATH_VAR_TYPE_BOOLEAN,       /* Boolean value */
    XPATH_VAR_TYPE_NUMBER,        /* Floating-point number */
    XPATH_VAR_TYPE_STRING,        /* String value */
    XPATH_VAR_TYPE_NODE_SET       /* Node set (XPathNodeSet*) */
} XPathVariableType;

/**
 * XPath variable value - Union of all possible types
 */
typedef struct xpath_variable_value {
    XPathVariableType type;
    union {
        int boolean_value;        /* XPATH_VAR_TYPE_BOOLEAN */
        double number_value;      /* XPATH_VAR_TYPE_NUMBER */
        char* string_value;       /* XPATH_VAR_TYPE_STRING - allocated */
        XPathNodeSet* nodeset_value; /* XPATH_VAR_TYPE_NODE_SET - owned */
    } v;
} XPathVariableValue;

/**
 * XPath variable - Single named variable with value
 */
typedef struct xpath_variable {
    char* name;                   /* Variable name (allocated) */
    XPathVariableValue value;     /* Variable value */
} XPathVariable;

/**
 * XPath variable set - Collection of variables for XPath evaluation
 */
typedef struct xpath_variable_set {
    XPathVariable** variables;    /* Array of variable pointers */
    size_t count;                 /* Number of variables */
    size_t capacity;             /* Allocated capacity */
} XPathVariableSet;

/* ============================================================================
 * Variable Value Management
 * ============================================================================ */

/**
 * Create a new variable value (boolean)
 */
XPathVariableValue xpath_value_boolean(int value);

/**
 * Create a new variable value (number)
 */
XPathVariableValue xpath_value_number(double value);

/**
 * Create a new variable value (string)
 * Note: String is duplicated, caller keeps ownership of input
 */
XPathVariableValue xpath_value_string(const char* value);

/**
 * Create a new variable value (node set)
 * Note: Node set is referenced, not copied
 */
XPathVariableValue xpath_value_nodeset(XPathNodeSet* nodeset);

/**
 * Free variable value resources
 * For strings and node sets, this frees the internal data
 */
void xpath_value_free(XPathVariableValue* value);

/**
 * Copy variable value
 * Creates a deep copy of the value
 */
XPathVariableValue xpath_value_copy(const XPathVariableValue* value);

/* ============================================================================
 * Variable Management
 * ============================================================================ */

/**
 * Create a new variable
 * Returns NULL on error (invalid type or memory allocation failure)
 */
XPathVariable* xpath_variable_new(const char* name, XPathVariableType type);

/**
 * Free a variable and its resources
 */
void xpath_variable_free(XPathVariable* variable);

/**
 * Get variable name
 */
const char* xpath_variable_name(const XPathVariable* variable);

/**
 * Get variable type
 */
XPathVariableType xpath_variable_type(const XPathVariable* variable);

/**
 * Get variable value as boolean
 * Returns 0 for non-boolean types (or default value)
 */
int xpath_variable_get_boolean(const XPathVariable* variable);

/**
 * Get variable value as number
 * Returns NaN for non-number types
 */
double xpath_variable_get_number(const XPathVariable* variable);

/**
 * Get variable value as string
 * Returns empty string for non-string types
 */
const char* xpath_variable_get_string(const XPathVariable* variable);

/**
 * Get variable value as node set
 * Returns NULL for non-node-set types
 */
XPathNodeSet* xpath_variable_get_nodeset(const XPathVariable* variable);

/**
 * Set variable value (boolean)
 * Returns 1 on success, 0 if type mismatch
 */
int xpath_variable_set_boolean(XPathVariable* variable, int value);

/**
 * Set variable value (number)
 * Returns 1 on success, 0 if type mismatch
 */
int xpath_variable_set_number(XPathVariable* variable, double value);

/**
 * Set variable value (string)
 * Returns 1 on success, 0 if type mismatch
 * Note: String is duplicated
 */
int xpath_variable_set_string(XPathVariable* variable, const char* value);

/**
 * Set variable value (node set)
 * Returns 1 on success, 0 if type mismatch
 * Note: Node set is referenced, not copied
 */
int xpath_variable_set_nodeset(XPathVariable* variable, XPathNodeSet* nodeset);

/* ============================================================================
 * Variable Set Management
 * ============================================================================ */

/**
 * Create a new variable set
 */
XPathVariableSet* xpath_variable_set_new(void);

/**
 * Free a variable set and all its variables
 */
void xpath_variable_set_free(XPathVariableSet* set);

/**
 * Add a variable to the set
 * If a variable with the same name exists, it is replaced if type matches.
 * Returns the variable pointer, or NULL if:
 * - name is NULL or empty
 * - type is invalid (XPATH_VAR_TYPE_NONE)
 * - a variable with the same name has a different type
 * - memory allocation fails
 */
XPathVariable* xpath_variable_set_add(XPathVariableSet* set, const char* name, XPathVariableType type);

/**
 * Get a variable from the set by name
 * Returns NULL if not found
 */
XPathVariable* xpath_variable_set_get(XPathVariableSet* set, const char* name);

/**
 * Get a variable from the set by name (const version)
 */
const XPathVariable* xpath_variable_set_get_const(const XPathVariableSet* set, const char* name);

/**
 * Remove a variable from the set
 * Returns 1 if removed, 0 if not found
 */
int xpath_variable_set_remove(XPathVariableSet* set, const char* name);

/**
 * Get variable count
 */
size_t xpath_variable_set_count(const XPathVariableSet* set);

/**
 * Clear all variables from the set
 */
void xpath_variable_set_clear(XPathVariableSet* set);

/**
 * Copy a variable set
 * Creates a deep copy of all variables
 */
XPathVariableSet* xpath_variable_set_copy(const XPathVariableSet* set);

#endif /* XPATH_VARIABLES_H */
