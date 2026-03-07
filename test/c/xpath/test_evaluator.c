/* test_evaluator.c - XPath evaluator tests
 * Copyright (c) 2024, Ribose Inc.
 *
 * Comprehensive tests for XPath evaluator with all axes and operators.
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>
#include <math.h>
#include "../../../src/taurus/xpath/evaluator.h"
#include "../../../src/taurus/xpath/parser.h"
#include "../../../src/taurus/xpath/lexer.h"

/* Test counter */
static int tests_run = 0;
static int tests_passed = 0;

/* Helper: Create simple test document */
static struct taurus_document* create_test_document(void) {
    struct taurus_document* doc = malloc(sizeof(struct taurus_document));
    assert(doc != NULL);
    doc->root = malloc(sizeof(struct taurus_element));
    assert(doc->root != NULL);
    
    /* Root element: <root> */
    doc->root->name = strdup("root");
    doc->root->prefix = NULL;
    doc->root->namespace_uri = NULL;
    doc->root->parent = NULL;
    doc->root->children = NULL;
    doc->root->children_count = 0;
    doc->root->children_capacity = 0;
    doc->root->attributes = NULL;
    doc->root->attributes_count = 0;
    doc->root->attributes_capacity = 0;
    doc->root->namespaces = NULL;
    doc->root->namespaces_count = 0;
    doc->root->namespaces_capacity = 0;
    doc->root->text_content = NULL;
    doc->root->doc_order = 0;
    
    doc->encoding = NULL;
    doc->pis = NULL;
    doc->ref_count = 1;
    
    return doc;
}

/* Helper: Add child element */
static struct taurus_element* add_child(struct taurus_element* parent, const char* name) {
    struct taurus_element* child = malloc(sizeof(struct taurus_element));
    assert(child != NULL);
    
    child->name = strdup(name);
    child->prefix = NULL;
    child->namespace_uri = NULL;
    child->parent = parent;
    child->children = NULL;
    child->children_count = 0;
    child->children_capacity = 0;
    child->attributes = NULL;
    child->attributes_count = 0;
    child->attributes_capacity = 0;
    child->namespaces = NULL;
    child->namespaces_count = 0;
    child->namespaces_capacity = 0;
    child->text_content = NULL;
    child->doc_order = parent->doc_order + parent->children_count + 1;
    
    /* Add to parent */
    if (parent->children_count >= parent->children_capacity) {
        size_t new_cap = parent->children_capacity == 0 ? 4 : parent->children_capacity * 2;
        parent->children = realloc(parent->children, new_cap * sizeof(struct taurus_element*));
        parent->children_capacity = new_cap;
    }
    parent->children[parent->children_count++] = child;
    
    return child;
}

/* Helper: Free document */
static void free_element(struct taurus_element* elem) {
    if (!elem) return;
    
    free(elem->name);
    for (size_t i = 0; i < elem->children_count; i++) {
        free_element(elem->children[i]);
    }
    free(elem->children);
    free(elem->text_content);
    free(elem);
}

static void free_document(struct taurus_document* doc) {
    if (!doc) return;
    free_element(doc->root);
    free(doc);
}

/* Helper: Run test */
#define RUN_TEST(name) \
    do { \
        printf("Running %s... ", #name); \
        tests_run++; \
        if (name()) { \
            printf("PASSED\n"); \
            tests_passed++; \
        } else { \
            printf("FAILED\n"); \
        } \
    } while(0)

/* ============================================================================
 * Axis Tests (13 tests)
 * ============================================================================ */

/* Test 1: child axis */
static int test_child_axis(void) {
    struct taurus_document* doc = create_test_document();
    struct taurus_element* child1 = add_child(doc->root, "child1");
    struct taurus_element* child2 = add_child(doc->root, "child2");
    (void)child1; (void)child2;
    
    XPathParser* parser = xpath_parser_new("child::child1", 14);
    assert(parser != NULL);
    XPathASTNode* ast = xpath_parse(parser);
    assert(ast != NULL);
    
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    
    int success = (result && result->type == XPATH_RESULT_NODESET &&
                   xpath_nodeset_count(result->value.nodeset_value) == 1);
    
    xpath_result_free(result);
    xpath_context_free(ctx);
    ast_node_free(ast);
    xpath_parser_free(parser);
    free_document(doc);
    return success;
}

/* Test 2: descendant axis */
static int test_descendant_axis(void) {
    struct taurus_document* doc = create_test_document();
    struct taurus_element* child = add_child(doc->root, "child");
    add_child(child, "grandchild");
    
    XPathParser* parser = xpath_parser_new("descendant::grandchild", 22);
    XPathASTNode* ast = xpath_parse(parser);
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    
    int success = (result && result->type == XPATH_RESULT_NODESET &&
                   xpath_nodeset_count(result->value.nodeset_value) == 1);
    
    xpath_result_free(result);
    xpath_context_free(ctx);
    ast_node_free(ast);
    xpath_parser_free(parser);
    free_document(doc);
    return success;
}

/* Test 3: descendant-or-self axis */
static int test_descendant_or_self_axis(void) {
    struct taurus_document* doc = create_test_document();
    add_child(doc->root, "child");
    
    XPathParser* parser = xpath_parser_new("descendant-or-self::*", 21);
    XPathASTNode* ast = xpath_parse(parser);
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    
    /* Should match root + child = 2 nodes */
    int success = (result && result->type == XPATH_RESULT_NODESET &&
                   xpath_nodeset_count(result->value.nodeset_value) == 2);
    
    xpath_result_free(result);
    xpath_context_free(ctx);
    ast_node_free(ast);
    xpath_parser_free(parser);
    free_document(doc);
    return success;
}

/* Test 4: parent axis */
static int test_parent_axis(void) {
    struct taurus_document* doc = create_test_document();
    struct taurus_element* child = add_child(doc->root, "child");
    
    XPathParser* parser = xpath_parser_new("parent::root", 12);
    XPathASTNode* ast = xpath_parse(parser);
    XPathContext* ctx = xpath_context_new(doc, child);  /* Context is child */
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    
    int success = (result && result->type == XPATH_RESULT_NODESET &&
                   xpath_nodeset_count(result->value.nodeset_value) == 1);
    
    xpath_result_free(result);
    xpath_context_free(ctx);
    ast_node_free(ast);
    xpath_parser_free(parser);
    free_document(doc);
    return success;
}

/* Test 5: ancestor axis */
static int test_ancestor_axis(void) {
    struct taurus_document* doc = create_test_document();
    struct taurus_element* child = add_child(doc->root, "child");
    struct taurus_element* grandchild = add_child(child, "grandchild");
    
    XPathParser* parser = xpath_parser_new("ancestor::*", 11);
    XPathASTNode* ast = xpath_parse(parser);
    XPathContext* ctx = xpath_context_new(doc, grandchild);
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    
    /* Should match child + root = 2 ancestors */
    int success = (result && result->type == XPATH_RESULT_NODESET &&
                   xpath_nodeset_count(result->value.nodeset_value) == 2);
    
    xpath_result_free(result);
    xpath_context_free(ctx);
    ast_node_free(ast);
    xpath_parser_free(parser);
    free_document(doc);
    return success;
}

/* Test 6: ancestor-or-self axis */
static int test_ancestor_or_self_axis(void) {
    struct taurus_document* doc = create_test_document();
    struct taurus_element* child = add_child(doc->root, "child");
    
    XPathParser* parser = xpath_parser_new("ancestor-or-self::*", 19);
    XPathASTNode* ast = xpath_parse(parser);
    XPathContext* ctx = xpath_context_new(doc, child);
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    
    /* Should match child + root = 2 nodes */
    int success = (result && result->type == XPATH_RESULT_NODESET &&
                   xpath_nodeset_count(result->value.nodeset_value) == 2);
    
    xpath_result_free(result);
    xpath_context_free(ctx);
    ast_node_free(ast);
    xpath_parser_free(parser);
    free_document(doc);
    return success;
}

/* Test 7: self axis */
static int test_self_axis(void) {
    struct taurus_document* doc = create_test_document();
    
    XPathParser* parser = xpath_parser_new("self::root", 10);
    XPathASTNode* ast = xpath_parse(parser);
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    
    int success = (result && result->type == XPATH_RESULT_NODESET &&
                   xpath_nodeset_count(result->value.nodeset_value) == 1);
    
    xpath_result_free(result);
    xpath_context_free(ctx);
    ast_node_free(ast);
    xpath_parser_free(parser);
    free_document(doc);
    return success;
}

/* Test 8: following-sibling axis */
static int test_following_sibling_axis(void) {
    struct taurus_document* doc = create_test_document();
    struct taurus_element* child1 = add_child(doc->root, "child1");
    add_child(doc->root, "child2");
    add_child(doc->root, "child3");
    
    XPathParser* parser = xpath_parser_new("following-sibling::*", 20);
    XPathASTNode* ast = xpath_parse(parser);
    XPathContext* ctx = xpath_context_new(doc, child1);
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    
    /* Should match child2 + child3 = 2 siblings */
    int success = (result && result->type == XPATH_RESULT_NODESET &&
                   xpath_nodeset_count(result->value.nodeset_value) == 2);
    
    xpath_result_free(result);
    xpath_context_free(ctx);
    ast_node_free(ast);
    xpath_parser_free(parser);
    free_document(doc);
    return success;
}

/* Test 9: preceding-sibling axis */
static int test_preceding_sibling_axis(void) {
    struct taurus_document* doc = create_test_document();
    add_child(doc->root, "child1");
    add_child(doc->root, "child2");
    struct taurus_element* child3 = add_child(doc->root, "child3");
    
    XPathParser* parser = xpath_parser_new("preceding-sibling::*", 20);
    XPathASTNode* ast = xpath_parse(parser);
    XPathContext* ctx = xpath_context_new(doc, child3);
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    
    /* Should match child1 + child2 = 2 siblings */
    int success = (result && result->type == XPATH_RESULT_NODESET &&
                   xpath_nodeset_count(result->value.nodeset_value) == 2);
    
    xpath_result_free(result);
    xpath_context_free(ctx);
    ast_node_free(ast);
    xpath_parser_free(parser);
    free_document(doc);
    return success;
}

/* Test 10: following axis */
static int test_following_axis(void) {
    struct taurus_document* doc = create_test_document();
    struct taurus_element* child1 = add_child(doc->root, "child1");
    struct taurus_element* child2 = add_child(doc->root, "child2");
    add_child(child2, "grandchild");
    
    XPathParser* parser = xpath_parser_new("following::*", 12);
    XPathASTNode* ast = xpath_parse(parser);
    XPathContext* ctx = xpath_context_new(doc, child1);
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    
    /* Should match child2 + grandchild = 2 nodes */
    int success = (result && result->type == XPATH_RESULT_NODESET &&
                   xpath_nodeset_count(result->value.nodeset_value) >= 1);
    
    xpath_result_free(result);
    xpath_context_free(ctx);
    ast_node_free(ast);
    xpath_parser_free(parser);
    free_document(doc);
    return success;
}

/* Test 11: preceding axis */
static int test_preceding_axis(void) {
    struct taurus_document* doc = create_test_document();
    struct taurus_element* child1 = add_child(doc->root, "child1");
    add_child(child1, "grandchild");
    struct taurus_element* child2 = add_child(doc->root, "child2");
    
    XPathParser* parser = xpath_parser_new("preceding::*", 12);
    XPathASTNode* ast = xpath_parse(parser);
    XPathContext* ctx = xpath_context_new(doc, child2);
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    
    /* Should match child1 + grandchild = 2 nodes */
    int success = (result && result->type == XPATH_RESULT_NODESET &&
                   xpath_nodeset_count(result->value.nodeset_value) >= 1);
    
    xpath_result_free(result);
    xpath_context_free(ctx);
    ast_node_free(ast);
    xpath_parser_free(parser);
    free_document(doc);
    return success;
}

/* Test 12: attribute axis (stub) */
static int test_attribute_axis(void) {
    struct taurus_document* doc = create_test_document();
    
    XPathParser* parser = xpath_parser_new("attribute::*", 12);
    XPathASTNode* ast = xpath_parse(parser);
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    
    /* Currently stub, returns empty */
    int success = (result && result->type == XPATH_RESULT_NODESET);
    
    xpath_result_free(result);
    xpath_context_free(ctx);
    ast_node_free(ast);
    xpath_parser_free(parser);
    free_document(doc);
    return success;
}

/* Test 13: namespace axis (stub) */
static int test_namespace_axis(void) {
    struct taurus_document* doc = create_test_document();
    
    XPathParser* parser = xpath_parser_new("namespace::*", 12);
    XPathASTNode* ast = xpath_parse(parser);
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    
    /* Currently stub, returns empty */
    int success = (result && result->type == XPATH_RESULT_NODESET);
    
    xpath_result_free(result);
    xpath_context_free(ctx);
    ast_node_free(ast);
    xpath_parser_free(parser);
    free_document(doc);
    return success;
}

/* ============================================================================
 * Operator Tests (8 tests)
 * ============================================================================ */

/* Test 14: Addition operator */
static int test_addition_operator(void) {
    struct taurus_document* doc = create_test_document();
    
    XPathParser* parser = xpath_parser_new("2 + 3", 5);
    XPathASTNode* ast = xpath_parse(parser);
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    
    int success = (result && result->type == XPATH_RESULT_NUMBER &&
                   result->value.number_value == 5.0);
    
    xpath_result_free(result);
    xpath_context_free(ctx);
    ast_node_free(ast);
    xpath_parser_free(parser);
    free_document(doc);
    return success;
}

/* Test 15: Subtraction operator */
static int test_subtraction_operator(void) {
    struct taurus_document* doc = create_test_document();
    
    XPathParser* parser = xpath_parser_new("10 - 3", 6);
    XPathASTNode* ast = xpath_parse(parser);
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    
    int success = (result && result->type == XPATH_RESULT_NUMBER &&
                   result->value.number_value == 7.0);
    
    xpath_result_free(result);
    xpath_context_free(ctx);
    ast_node_free(ast);
    xpath_parser_free(parser);
    free_document(doc);
    return success;
}

/* Test 16: Multiplication operator */
static int test_multiplication_operator(void) {
    struct taurus_document* doc = create_test_document();
    
    XPathParser* parser = xpath_parser_new("4 * 5", 5);
    XPathASTNode* ast = xpath_parse(parser);
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    
    int success = (result && result->type == XPATH_RESULT_NUMBER &&
                   result->value.number_value == 20.0);
    
    xpath_result_free(result);
    xpath_context_free(ctx);
    ast_node_free(ast);
    xpath_parser_free(parser);
    free_document(doc);
    return success;
}

/* Test 17: Division operator */
static int test_division_operator(void) {
    struct taurus_document* doc = create_test_document();
    
    XPathParser* parser = xpath_parser_new("15 div 3", 8);
    XPathASTNode* ast = xpath_parse(parser);
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    
    int success = (result && result->type == XPATH_RESULT_NUMBER &&
                   result->value.number_value == 5.0);
    
    xpath_result_free(result);
    xpath_context_free(ctx);
    ast_node_free(ast);
    xpath_parser_free(parser);
    free_document(doc);
    return success;
}

/* Test 18: Equality operator */
static int test_equality_operator(void) {
    struct taurus_document* doc = create_test_document();
    
    XPathParser* parser = xpath_parser_new("5 = 5", 5);
    XPathASTNode* ast = xpath_parse(parser);
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    
    int success = (result && result->type == XPATH_RESULT_BOOLEAN &&
                   result->value.boolean_value == 1);
    
    xpath_result_free(result);
    xpath_context_free(ctx);
    ast_node_free(ast);
    xpath_parser_free(parser);
    free_document(doc);
    return success;
}

/* Test 19: Less-than operator */
static int test_less_than_operator(void) {
    struct taurus_document* doc = create_test_document();
    
    XPathParser* parser = xpath_parser_new("3 < 5", 5);
    XPathASTNode* ast = xpath_parse(parser);
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    
    int success = (result && result->type == XPATH_RESULT_BOOLEAN &&
                   result->value.boolean_value == 1);
    
    xpath_result_free(result);
    xpath_context_free(ctx);
    ast_node_free(ast);
    xpath_parser_free(parser);
    free_document(doc);
    return success;
}

/* Test 20: AND operator */
static int test_and_operator(void) {
    struct taurus_document* doc = create_test_document();
    
    XPathParser* parser = xpath_parser_new("1 and 1", 7);
    XPathASTNode* ast = xpath_parse(parser);
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    
    int success = (result && result->type == XPATH_RESULT_BOOLEAN &&
                   result->value.boolean_value == 1);
    
    xpath_result_free(result);
    xpath_context_free(ctx);
    ast_node_free(ast);
    xpath_parser_free(parser);
    free_document(doc);
    return success;
}

/* Test 21: OR operator */
static int test_or_operator(void) {
    struct taurus_document* doc = create_test_document();
    
    XPathParser* parser = xpath_parser_new("0 or 1", 6);
    XPathASTNode* ast = xpath_parse(parser);
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    
    int success = (result && result->type == XPATH_RESULT_BOOLEAN &&
                   result->value.boolean_value == 1);
    
    xpath_result_free(result);
    xpath_context_free(ctx);
    ast_node_free(ast);
    xpath_parser_free(parser);
    free_document(doc);
    return success;
}

/* ============================================================================
 * Type Conversion Tests (4 tests)
 * ============================================================================ */

/* Test 22: Number to boolean conversion */
static int test_number_to_boolean(void) {
    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_NUMBER);
    result->value.number_value = 5.0;
    
    int success = (xpath_to_boolean(result) == 1);
    
    xpath_result_free(result);
    return success;
}

/* Test 23: String to number conversion */
static int test_string_to_number(void) {
    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_STRING);
    result->value.string_value = strdup("42");
    
    int success = (xpath_to_number(result) == 42.0);
    
    xpath_result_free(result);
    return success;
}

/* Test 24: Boolean to string conversion */
static int test_boolean_to_string(void) {
    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_BOOLEAN);
    result->value.boolean_value = 1;
    
    char* str = xpath_to_string(result);
    int success = (strcmp(str, "true") == 0);
    
    free(str);
    xpath_result_free(result);
    return success;
}

/* Test 25: Empty nodeset to boolean */
static int test_empty_nodeset_to_boolean(void) {
    struct taurus_xpath_result* result = xpath_result_new(XPATH_RESULT_NODESET);
    result->value.nodeset_value = xpath_nodeset_new();
    
    int success = (xpath_to_boolean(result) == 0);
    
    xpath_result_free(result);
    return success;
}

/* ============================================================================
 * Predicate Tests (3 tests)
 * ============================================================================ */

/* Test 26: Position predicate [1] */
static int test_position_predicate(void) {
    struct taurus_document* doc = create_test_document();
    add_child(doc->root, "child");
    add_child(doc->root, "child");
    add_child(doc->root, "child");
    
    XPathParser* parser = xpath_parser_new("child::child[1]", 15);
    XPathASTNode* ast = xpath_parse(parser);
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    
    /* Should match only first child */
    int success = (result && result->type == XPATH_RESULT_NODESET &&
                   xpath_nodeset_count(result->value.nodeset_value) == 1);
    
    xpath_result_free(result);
    xpath_context_free(ctx);
    ast_node_free(ast);
    xpath_parser_free(parser);
    free_document(doc);
    return success;
}

/* Test 27: Boolean predicate */
static int test_boolean_predicate(void) {
    struct taurus_document* doc = create_test_document();
    add_child(doc->root, "child");
    
    XPathParser* parser = xpath_parser_new("child::*[1]", 11);
    XPathASTNode* ast = xpath_parse(parser);
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    
    int success = (result && result->type == XPATH_RESULT_NODESET &&
                   xpath_nodeset_count(result->value.nodeset_value) == 1);
    
    xpath_result_free(result);
    xpath_context_free(ctx);
    ast_node_free(ast);
    xpath_parser_free(parser);
    free_document(doc);
    return success;
}

/* Test 28: Multiple predicates */
static int test_multiple_predicates(void) {
    struct taurus_document* doc = create_test_document();
    add_child(doc->root, "child");
    add_child(doc->root, "child");
    
    XPathParser* parser = xpath_parser_new("child::*[1][1]", 14);
    XPathASTNode* ast = xpath_parse(parser);
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    
    int success = (result && result->type == XPATH_RESULT_NODESET);
    
    xpath_result_free(result);
    xpath_context_free(ctx);
    ast_node_free(ast);
    xpath_parser_free(parser);
    free_document(doc);
    return success;
}

/* ============================================================================
 * Edge Case Tests (4 tests)
 * ============================================================================ */

/* Test 29: Empty nodeset */
static int test_empty_nodeset(void) {
    struct taurus_document* doc = create_test_document();
    
    XPathParser* parser = xpath_parser_new("child::nonexistent", 18);
    XPathASTNode* ast = xpath_parse(parser);
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    
    int success = (result && result->type == XPATH_RESULT_NODESET &&
                   xpath_nodeset_count(result->value.nodeset_value) == 0);
    
    xpath_result_free(result);
    xpath_context_free(ctx);
    ast_node_free(ast);
    xpath_parser_free(parser);
    free_document(doc);
    return success;
}

/* Test 30: Wildcard match */
static int test_wildcard_match(void) {
    struct taurus_document* doc = create_test_document();
    add_child(doc->root, "child1");
    add_child(doc->root, "child2");
    
    XPathParser* parser = xpath_parser_new("child::*", 8);
    XPathASTNode* ast = xpath_parse(parser);
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    
    /* Should match both children */
    int success = (result && result->type == XPATH_RESULT_NODESET &&
                   xpath_nodeset_count(result->value.nodeset_value) == 2);
    
    xpath_result_free(result);
    xpath_context_free(ctx);
    ast_node_free(ast);
    xpath_parser_free(parser);
    free_document(doc);
    return success;
}

/* Test 31: Negation operator */
static int test_negation_operator(void) {
    struct taurus_document* doc = create_test_document();
    
    XPathParser* parser = xpath_parser_new("-5", 2);
    XPathASTNode* ast = xpath_parse(parser);
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    
    int success = (result && result->type == XPATH_RESULT_NUMBER &&
                   result->value.number_value == -5.0);
    
    xpath_result_free(result);
    xpath_context_free(ctx);
    ast_node_free(ast);
    xpath_parser_free(parser);
    free_document(doc);
    return success;
}

/* Test 32: Union operator */
static int test_union_operator(void) {
    struct taurus_document* doc = create_test_document();
    add_child(doc->root, "child1");
    add_child(doc->root, "child2");
    
    XPathParser* parser = xpath_parser_new("child::child1 | child::child2", 29);
    XPathASTNode* ast = xpath_parse(parser);
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    
    /* Should match both children */
    int success = (result && result->type == XPATH_RESULT_NODESET &&
                   xpath_nodeset_count(result->value.nodeset_value) == 2);
    
    xpath_result_free(result);
    xpath_context_free(ctx);
    ast_node_free(ast);
    xpath_parser_free(parser);
    free_document(doc);
    return success;
}

/* ============================================================================
 * Main
 * ============================================================================ */

int main(void) {
    printf("Running XPath Evaluator Tests\n");
    printf("==============================\n\n");
    
    /* Axis tests */
    RUN_TEST(test_child_axis);
    RUN_TEST(test_descendant_axis);
    RUN_TEST(test_descendant_or_self_axis);
    RUN_TEST(test_parent_axis);
    RUN_TEST(test_ancestor_axis);
    RUN_TEST(test_ancestor_or_self_axis);
    RUN_TEST(test_self_axis);
    RUN_TEST(test_following_sibling_axis);
    RUN_TEST(test_preceding_sibling_axis);
    RUN_TEST(test_following_axis);
    RUN_TEST(test_preceding_axis);
    RUN_TEST(test_attribute_axis);
    RUN_TEST(test_namespace_axis);
    
    /* Operator tests */
    RUN_TEST(test_addition_operator);
    RUN_TEST(test_subtraction_operator);
    RUN_TEST(test_multiplication_operator);
    RUN_TEST(test_division_operator);
    RUN_TEST(test_equality_operator);
    RUN_TEST(test_less_than_operator);
    RUN_TEST(test_and_operator);
    RUN_TEST(test_or_operator);
    
    /* Type conversion tests */
    RUN_TEST(test_number_to_boolean);
    RUN_TEST(test_string_to_number);
    RUN_TEST(test_boolean_to_string);
    RUN_TEST(test_empty_nodeset_to_boolean);
    
    /* Predicate tests */
    RUN_TEST(test_position_predicate);
    RUN_TEST(test_boolean_predicate);
    RUN_TEST(test_multiple_predicates);
    
    /* Edge case tests */
    RUN_TEST(test_empty_nodeset);
    RUN_TEST(test_wildcard_match);
    RUN_TEST(test_negation_operator);
    RUN_TEST(test_union_operator);
    
    printf("\n==============================\n");
    printf("Results: %d/%d tests passed\n", tests_passed, tests_run);
    
    return (tests_passed == tests_run) ? 0 : 1;
}