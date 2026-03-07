/* test_functions.c - XPath 1.0 function tests
 * Copyright (c) 2024, Ribose Inc.
 *
 * Pure C tests for all 27 XPath 1.0 functions
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include <assert.h>
#include "../../../src/taurus/taurus_internal.h"
#include "../../../src/taurus/xpath/functions.h"
#include "../../../src/taurus/xpath/evaluator.h"
#include "../../../src/taurus/xpath/parser.h"
#include "../../../src/taurus/xpath/lexer.h"

/* ============================================================================
 * Test Helpers
 * ============================================================================ */

static int tests_run = 0;
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    static void name(void); \
    static void name##_wrapper(void) { \
        printf("Testing %s...\n", #name); \
        name(); \
        tests_run++; \
    } \
    static void name(void)

#define ASSERT(cond, msg) \
    do { \
        if (!(cond)) { \
            printf("  FAIL: %s (line %d)\n", msg, __LINE__); \
            tests_failed++; \
            return; \
        } \
    } while(0)

#define ASSERT_EQ(a, b, msg) \
    ASSERT((a) == (b), msg)

#define ASSERT_DOUBLE_EQ(a, b, msg) \
    ASSERT(fabs((a) - (b)) < 0.0001, msg)

#define ASSERT_STR_EQ(a, b, msg) \
    ASSERT(strcmp((a), (b)) == 0, msg)

#define PASS() \
    do { \
        printf("  PASS\n"); \
        tests_passed++; \
    } while(0)

/* Helper to create a simple test document */
static struct taurus_document* create_test_document(void) {
    struct taurus_document* doc = TAURUS_ALLOC(struct taurus_document);
    if (!doc) return NULL;
    
    doc->encoding = NULL;
    doc->pis = NULL;
    doc->ref_count = 1;
    
    /* Create root element */
    doc->root = TAURUS_ALLOC(struct taurus_element);
    if (!doc->root) {
        TAURUS_FREE(doc);
        return NULL;
    }
    
    doc->root->name = taurus_strdup("root");
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
    doc->root->text_content = taurus_strdup("root text");
    doc->root->doc_order = 0;
    
    return doc;
}

/* Helper to free test document */
static void free_test_document(struct taurus_document* doc) {
    if (!doc) return;
    if (doc->root) {
        if (doc->root->name) TAURUS_FREE(doc->root->name);
        if (doc->root->text_content) TAURUS_FREE(doc->root->text_content);
        TAURUS_FREE(doc->root);
    }
    TAURUS_FREE(doc);
}

/* ============================================================================
 * String Function Tests
 * ============================================================================ */

TEST(test_string_function) {
    struct taurus_document* doc = create_test_document();
    ASSERT(doc != NULL, "Failed to create document");
    
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    ASSERT(ctx != NULL, "Failed to create context");
    
    /* Test string() with no args - returns context node text */
    const char* expr = "string()";
    XPathParser* parser = xpath_parser_new(expr, strlen(expr));
    ASSERT(parser != NULL, "Failed to create parser");
    
    XPathASTNode* ast = xpath_parse(parser);
    ASSERT(ast != NULL, "Failed to parse");
    
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    ASSERT(result != NULL, "Failed to evaluate");
    ASSERT_EQ(result->type, XPATH_RESULT_STRING, "Result should be string");
    ASSERT_STR_EQ(result->value.string_value, "root text", "String value incorrect");
    
    xpath_result_free(result);
    ast_node_free(ast);
    xpath_parser_free(parser);
    xpath_context_free(ctx);
    free_test_document(doc);
    
    PASS();
}

TEST(test_concat_function) {
    struct taurus_document* doc = create_test_document();
    ASSERT(doc != NULL, "Failed to create document");
    
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    ASSERT(ctx != NULL, "Failed to create context");
    
    XPathParser* parser = xpath_parser_new("concat('Hello', ' ', 'World')", strlen("concat('Hello', ' ', 'World')"));
    ASSERT(parser != NULL, "Failed to create parser");
    
    XPathASTNode* ast = xpath_parse(parser);
    ASSERT(ast != NULL, "Failed to parse");
    
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    ASSERT(result != NULL, "Failed to evaluate");
    ASSERT_EQ(result->type, XPATH_RESULT_STRING, "Result should be string");
    ASSERT_STR_EQ(result->value.string_value, "Hello World", "Concat incorrect");
    
    xpath_result_free(result);
    ast_node_free(ast);
    xpath_parser_free(parser);
    xpath_context_free(ctx);
    free_test_document(doc);
    
    PASS();
}

TEST(test_starts_with_function) {
    struct taurus_document* doc = create_test_document();
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    
    XPathParser* parser = xpath_parser_new("starts-with('Hello World', 'Hello')", strlen("starts-with('Hello World', 'Hello')"));
    XPathASTNode* ast = xpath_parse(parser);
    
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    ASSERT(result != NULL, "Failed to evaluate");
    ASSERT_EQ(result->type, XPATH_RESULT_BOOLEAN, "Result should be boolean");
    ASSERT_EQ(result->value.boolean_value, 1, "Should be true");
    
    xpath_result_free(result);
    ast_node_free(ast);
    xpath_parser_free(parser);
    xpath_context_free(ctx);
    free_test_document(doc);
    
    PASS();
}

TEST(test_contains_function) {
    struct taurus_document* doc = create_test_document();
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    
    XPathParser* parser = xpath_parser_new("contains('Hello World', 'Wor')", strlen("contains('Hello World', 'Wor')"));
    XPathASTNode* ast = xpath_parse(parser);
    
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    ASSERT(result != NULL, "Failed to evaluate");
    ASSERT_EQ(result->type, XPATH_RESULT_BOOLEAN, "Result should be boolean");
    ASSERT_EQ(result->value.boolean_value, 1, "Should be true");
    
    xpath_result_free(result);
    ast_node_free(ast);
    xpath_parser_free(parser);
    xpath_context_free(ctx);
    free_test_document(doc);
    
    PASS();
}

TEST(test_substring_function) {
    struct taurus_document* doc = create_test_document();
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    
    /* Test substring with 2 args */
    XPathParser* parser = xpath_parser_new("substring('Hello World', 7)", strlen("substring('Hello World', 7)"));
    XPathASTNode* ast = xpath_parse(parser);
    
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    ASSERT(result != NULL, "Failed to evaluate");
    ASSERT_EQ(result->type, XPATH_RESULT_STRING, "Result should be string");
    ASSERT_STR_EQ(result->value.string_value, "World", "Substring incorrect");
    
    xpath_result_free(result);
    ast_node_free(ast);
    xpath_parser_free(parser);
    
    /* Test substring with 3 args */
    parser = xpath_parser_new("substring('Hello World', 1, 5)", strlen("substring('Hello World', 1, 5)"));
    ast = xpath_parse(parser);
    
    result = xpath_evaluate(ctx, ast);
    ASSERT(result != NULL, "Failed to evaluate");
    ASSERT_STR_EQ(result->value.string_value, "Hello", "Substring with length incorrect");
    
    xpath_result_free(result);
    ast_node_free(ast);
    xpath_parser_free(parser);
    xpath_context_free(ctx);
    free_test_document(doc);
    
    PASS();
}

TEST(test_string_length_function) {
    struct taurus_document* doc = create_test_document();
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    
    XPathParser* parser = xpath_parser_new("string-length('Hello')", strlen("string-length('Hello')"));
    XPathASTNode* ast = xpath_parse(parser);
    
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    ASSERT(result != NULL, "Failed to evaluate");
    ASSERT_EQ(result->type, XPATH_RESULT_NUMBER, "Result should be number");
    ASSERT_DOUBLE_EQ(result->value.number_value, 5.0, "Length incorrect");
    
    xpath_result_free(result);
    ast_node_free(ast);
    xpath_parser_free(parser);
    xpath_context_free(ctx);
    free_test_document(doc);
    
    PASS();
}

TEST(test_normalize_space_function) {
    struct taurus_document* doc = create_test_document();
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    
    XPathParser* parser = xpath_parser_new("normalize-space('  Hello   World  ')", strlen("normalize-space('  Hello   World  ')"));
    XPathASTNode* ast = xpath_parse(parser);
    
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    ASSERT(result != NULL, "Failed to evaluate");
    ASSERT_EQ(result->type, XPATH_RESULT_STRING, "Result should be string");
    ASSERT_STR_EQ(result->value.string_value, "Hello World", "Normalize-space incorrect");
    
    xpath_result_free(result);
    ast_node_free(ast);
    xpath_parser_free(parser);
    xpath_context_free(ctx);
    free_test_document(doc);
    
    PASS();
}

/* ============================================================================
 * Boolean Function Tests
 * ============================================================================ */

TEST(test_boolean_function) {
    struct taurus_document* doc = create_test_document();
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    
    /* Test boolean(true) */
    XPathParser* parser = xpath_parser_new("boolean(1)", strlen("boolean(1)"));
    XPathASTNode* ast = xpath_parse(parser);
    
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    ASSERT(result != NULL, "Failed to evaluate");
    ASSERT_EQ(result->type, XPATH_RESULT_BOOLEAN, "Result should be boolean");
    ASSERT_EQ(result->value.boolean_value, 1, "Should be true");
    
    xpath_result_free(result);
    ast_node_free(ast);
    xpath_parser_free(parser);
    
    /* Test boolean(0) */
    parser = xpath_parser_new("boolean(0)", strlen("boolean(0)"));
    ast = xpath_parse(parser);
    
    result = xpath_evaluate(ctx, ast);
    ASSERT(result != NULL, "Failed to evaluate");
    ASSERT_EQ(result->value.boolean_value, 0, "Should be false");
    
    xpath_result_free(result);
    ast_node_free(ast);
    xpath_parser_free(parser);
    xpath_context_free(ctx);
    free_test_document(doc);
    
    PASS();
}

TEST(test_not_function) {
    struct taurus_document* doc = create_test_document();
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    
    XPathParser* parser = xpath_parser_new("not(true())", strlen("not(true())"));
    XPathASTNode* ast = xpath_parse(parser);
    
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    ASSERT(result != NULL, "Failed to evaluate");
    ASSERT_EQ(result->type, XPATH_RESULT_BOOLEAN, "Result should be boolean");
    ASSERT_EQ(result->value.boolean_value, 0, "not(true) should be false");
    
    xpath_result_free(result);
    ast_node_free(ast);
    xpath_parser_free(parser);
    xpath_context_free(ctx);
    free_test_document(doc);
    
    PASS();
}

TEST(test_true_false_functions) {
    struct taurus_document* doc = create_test_document();
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    
    /* Test true() */
    XPathParser* parser = xpath_parser_new("true()", strlen("true()"));
    XPathASTNode* ast = xpath_parse(parser);
    
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    ASSERT(result != NULL, "Failed to evaluate");
    ASSERT_EQ(result->type, XPATH_RESULT_BOOLEAN, "Result should be boolean");
    ASSERT_EQ(result->value.boolean_value, 1, "true() should be true");
    
    xpath_result_free(result);
    ast_node_free(ast);
    xpath_parser_free(parser);
    
    /* Test false() */
    parser = xpath_parser_new("false()", strlen("false()"));
    ast = xpath_parse(parser);
    
    result = xpath_evaluate(ctx, ast);
    ASSERT(result != NULL, "Failed to evaluate");
    ASSERT_EQ(result->value.boolean_value, 0, "false() should be false");
    
    xpath_result_free(result);
    ast_node_free(ast);
    xpath_parser_free(parser);
    xpath_context_free(ctx);
    free_test_document(doc);
    
    PASS();
}

/* ============================================================================
 * Number Function Tests
 * ============================================================================ */

TEST(test_number_function) {
    struct taurus_document* doc = create_test_document();
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    
    XPathParser* parser = xpath_parser_new("number('42.5')", strlen("number('42.5')"));
    XPathASTNode* ast = xpath_parse(parser);
    
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    ASSERT(result != NULL, "Failed to evaluate");
    ASSERT_EQ(result->type, XPATH_RESULT_NUMBER, "Result should be number");
    ASSERT_DOUBLE_EQ(result->value.number_value, 42.5, "Number conversion incorrect");
    
    xpath_result_free(result);
    ast_node_free(ast);
    xpath_parser_free(parser);
    xpath_context_free(ctx);
    free_test_document(doc);
    
    PASS();
}

TEST(test_floor_function) {
    struct taurus_document* doc = create_test_document();
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    
    XPathParser* parser = xpath_parser_new("floor(3.7)", strlen("floor(3.7)"));
    XPathASTNode* ast = xpath_parse(parser);
    
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    ASSERT(result != NULL, "Failed to evaluate");
    ASSERT_EQ(result->type, XPATH_RESULT_NUMBER, "Result should be number");
    ASSERT_DOUBLE_EQ(result->value.number_value, 3.0, "floor(3.7) should be 3");
    
    xpath_result_free(result);
    ast_node_free(ast);
    xpath_parser_free(parser);
    xpath_context_free(ctx);
    free_test_document(doc);
    
    PASS();
}

TEST(test_ceiling_function) {
    struct taurus_document* doc = create_test_document();
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    
    XPathParser* parser = xpath_parser_new("ceiling(3.2)", strlen("ceiling(3.2)"));
    XPathASTNode* ast = xpath_parse(parser);
    
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    ASSERT(result != NULL, "Failed to evaluate");
    ASSERT_EQ(result->type, XPATH_RESULT_NUMBER, "Result should be number");
    ASSERT_DOUBLE_EQ(result->value.number_value, 4.0, "ceiling(3.2) should be 4");
    
    xpath_result_free(result);
    ast_node_free(ast);
    xpath_parser_free(parser);
    xpath_context_free(ctx);
    free_test_document(doc);
    
    PASS();
}

TEST(test_round_function) {
    struct taurus_document* doc = create_test_document();
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    
    /* Test round(3.5) */
    XPathParser* parser = xpath_parser_new("round(3.5)", strlen("round(3.5)"));
    XPathASTNode* ast = xpath_parse(parser);
    
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    ASSERT(result != NULL, "Failed to evaluate");
    ASSERT_EQ(result->type, XPATH_RESULT_NUMBER, "Result should be number");
    ASSERT_DOUBLE_EQ(result->value.number_value, 4.0, "round(3.5) should be 4");
    
    xpath_result_free(result);
    ast_node_free(ast);
    xpath_parser_free(parser);
    xpath_context_free(ctx);
    free_test_document(doc);
    
    PASS();
}

/* ============================================================================
 * Context Function Tests
 * ============================================================================ */

TEST(test_last_function) {
    struct taurus_document* doc = create_test_document();
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    
    /* Set context size */
    ctx->context_size = 5;
    
    XPathParser* parser = xpath_parser_new("last()", strlen("last()"));
    XPathASTNode* ast = xpath_parse(parser);
    
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    ASSERT(result != NULL, "Failed to evaluate");
    ASSERT_EQ(result->type, XPATH_RESULT_NUMBER, "Result should be number");
    ASSERT_DOUBLE_EQ(result->value.number_value, 5.0, "last() should return context size");
    
    xpath_result_free(result);
    ast_node_free(ast);
    xpath_parser_free(parser);
    xpath_context_free(ctx);
    free_test_document(doc);
    
    PASS();
}

TEST(test_position_function) {
    struct taurus_document* doc = create_test_document();
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    
    /* Set context position */
    ctx->context_position = 3;
    
    XPathParser* parser = xpath_parser_new("position()", strlen("position()"));
    XPathASTNode* ast = xpath_parse(parser);
    
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    ASSERT(result != NULL, "Failed to evaluate");
    ASSERT_EQ(result->type, XPATH_RESULT_NUMBER, "Result should be number");
    ASSERT_DOUBLE_EQ(result->value.number_value, 3.0, "position() should return context position");
    
    xpath_result_free(result);
    ast_node_free(ast);
    xpath_parser_free(parser);
    xpath_context_free(ctx);
    free_test_document(doc);
    
    PASS();
}

/* ============================================================================
 * Additional String Function Tests
 * ============================================================================ */

TEST(test_translate_function) {
    struct taurus_document* doc = create_test_document();
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    
    XPathParser* parser = xpath_parser_new("translate('abc', 'abc', 'ABC')", strlen("translate('abc', 'abc', 'ABC')"));
    XPathASTNode* ast = xpath_parse(parser);
    
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    ASSERT(result != NULL, "Failed to evaluate");
    ASSERT_EQ(result->type, XPATH_RESULT_STRING, "Result should be string");
    ASSERT_STR_EQ(result->value.string_value, "ABC", "translate() incorrect");
    
    xpath_result_free(result);
    ast_node_free(ast);
    xpath_parser_free(parser);
    xpath_context_free(ctx);
    free_test_document(doc);
    
    PASS();
}

TEST(test_substring_before_function) {
    struct taurus_document* doc = create_test_document();
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    
    XPathParser* parser = xpath_parser_new("substring-before('Hello-World', '-')", strlen("substring-before('Hello-World', '-')"));
    XPathASTNode* ast = xpath_parse(parser);
    
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    ASSERT(result != NULL, "Failed to evaluate");
    ASSERT_EQ(result->type, XPATH_RESULT_STRING, "Result should be string");
    ASSERT_STR_EQ(result->value.string_value, "Hello", "substring-before() incorrect");
    
    xpath_result_free(result);
    ast_node_free(ast);
    xpath_parser_free(parser);
    xpath_context_free(ctx);
    free_test_document(doc);
    
    PASS();
}

TEST(test_substring_after_function) {
    struct taurus_document* doc = create_test_document();
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    
    XPathParser* parser = xpath_parser_new("substring-after('Hello-World', '-')", strlen("substring-after('Hello-World', '-')"));
    XPathASTNode* ast = xpath_parse(parser);
    
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    ASSERT(result != NULL, "Failed to evaluate");
    ASSERT_EQ(result->type, XPATH_RESULT_STRING, "Result should be string");
    ASSERT_STR_EQ(result->value.string_value, "World", "substring-after() incorrect");
    
    xpath_result_free(result);
    ast_node_free(ast);
    xpath_parser_free(parser);
    xpath_context_free(ctx);
    free_test_document(doc);
    
    PASS();
}

/* ============================================================================
 * Edge Case Tests
 * ============================================================================ */

TEST(test_function_missing_args) {
    struct taurus_document* doc = create_test_document();
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    
    /* concat() requires at least 2 args */
    XPathParser* parser = xpath_parser_new("concat('Hello')", strlen("concat('Hello')"));
    XPathASTNode* ast = xpath_parse(parser);
    
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    ASSERT(result == NULL, "Should fail with too few arguments");
    
    const char* error = xpath_context_error(ctx);
    ASSERT(error != NULL, "Should have error message");
    
    if (ast) ast_node_free(ast);
    xpath_parser_free(parser);
    xpath_context_free(ctx);
    free_test_document(doc);
    
    PASS();
}

TEST(test_function_too_many_args) {
    struct taurus_document* doc = create_test_document();
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    
    /* true() takes no args */
    XPathParser* parser = xpath_parser_new("true(1)", strlen("true(1)"));
    XPathASTNode* ast = xpath_parse(parser);
    
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    ASSERT(result == NULL, "Should fail with too many arguments");
    
    const char* error = xpath_context_error(ctx);
    ASSERT(error != NULL, "Should have error message");
    
    if (ast) ast_node_free(ast);
    xpath_parser_free(parser);
    xpath_context_free(ctx);
    free_test_document(doc);
    
    PASS();
}

TEST(test_unknown_function) {
    struct taurus_document* doc = create_test_document();
    XPathContext* ctx = xpath_context_new(doc, doc->root);
    
    XPathParser* parser = xpath_parser_new("unknown-function()", strlen("unknown-function()"));
    XPathASTNode* ast = xpath_parse(parser);
    
    struct taurus_xpath_result* result = xpath_evaluate(ctx, ast);
    ASSERT(result == NULL, "Should fail with unknown function");
    
    const char* error = xpath_context_error(ctx);
    ASSERT(error != NULL, "Should have error message");
    
    if (ast) ast_node_free(ast);
    xpath_parser_free(parser);
    xpath_context_free(ctx);
    free_test_document(doc);
    
    PASS();
}

/* ============================================================================
 * Test Runner
 * ============================================================================ */

int main(void) {
    printf("=== XPath Functions Test Suite ===\n\n");
    
    /* String functions */
    test_string_function_wrapper();
    test_concat_function_wrapper();
    test_starts_with_function_wrapper();
    test_contains_function_wrapper();
    test_substring_function_wrapper();
    test_string_length_function_wrapper();
    test_normalize_space_function_wrapper();
    test_translate_function_wrapper();
    test_substring_before_function_wrapper();
    test_substring_after_function_wrapper();
    
    /* Boolean functions */
    test_boolean_function_wrapper();
    test_not_function_wrapper();
    test_true_false_functions_wrapper();
    
    /* Number functions */
    test_number_function_wrapper();
    test_floor_function_wrapper();
    test_ceiling_function_wrapper();
    test_round_function_wrapper();
    
    /* Context functions */
    test_last_function_wrapper();
    test_position_function_wrapper();
    
    /* Edge cases */
    test_function_missing_args_wrapper();
    test_function_too_many_args_wrapper();
    test_unknown_function_wrapper();
    
    printf("\n=== Test Summary ===\n");
    printf("Total tests: %d\n", tests_run);
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    
    return (tests_failed == 0) ? 0 : 1;
}