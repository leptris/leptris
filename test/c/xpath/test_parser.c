/* test_parser.c - XPath parser test suite
 * Copyright (c) 2024, Ribose Inc.
 *
 * Comprehensive tests for XPath parser - Session 87
 */

#include "../../../src/taurus/xpath/parser.h"
#include "../../../src/taurus/xpath/lexer.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) printf("  Testing %s... ", name); fflush(stdout);
#define PASS() printf("✓ PASS\n"); tests_passed++;
#define FAIL(msg) printf("✗ FAIL: %s\n", msg); tests_failed++;

/* ============================================================================
 * Literal Expression Tests
 * ============================================================================ */

void test_parser_number(void) {
    TEST("parser - integer literal");
    
    XPathParser* parser = xpath_parser_new("123", 3);
    XPathASTNode* ast = xpath_parse(parser);
    
    if (!ast || ast->type != XPATH_AST_NUMBER) {
        FAIL("wrong AST type");
        if (ast) ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    if (ast->number_value != 123.0) {
        FAIL("wrong number value");
        ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    ast_node_free(ast);
    xpath_parser_free(parser);
    PASS();
}

void test_parser_decimal(void) {
    TEST("parser - decimal literal");
    
    XPathParser* parser = xpath_parser_new("123.45", 6);
    XPathASTNode* ast = xpath_parse(parser);
    
    if (!ast || ast->type != XPATH_AST_NUMBER) {
        FAIL("wrong AST type");
        if (ast) ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    if (ast->number_value < 123.44 || ast->number_value > 123.46) {
        FAIL("wrong number value");
        ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    ast_node_free(ast);
    xpath_parser_free(parser);
    PASS();
}

void test_parser_string_single(void) {
    TEST("parser - single-quoted string");
    
    XPathParser* parser = xpath_parser_new("'hello'", 7);
    XPathASTNode* ast = xpath_parse(parser);
    
    if (!ast || ast->type != XPATH_AST_STRING) {
        FAIL("wrong AST type");
        if (ast) ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    if (!ast->value || strcmp(ast->value, "hello") != 0) {
        FAIL("wrong string value");
        ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    ast_node_free(ast);
    xpath_parser_free(parser);
    PASS();
}

void test_parser_string_double(void) {
    TEST("parser - double-quoted string");
    
    XPathParser* parser = xpath_parser_new("\"world\"", 7);
    XPathASTNode* ast = xpath_parse(parser);
    
    if (!ast || ast->type != XPATH_AST_STRING) {
        FAIL("wrong AST type");
        if (ast) ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    if (!ast->value || strcmp(ast->value, "world") != 0) {
        FAIL("wrong string value");
        ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    ast_node_free(ast);
    xpath_parser_free(parser);
    PASS();
}

void test_parser_string_empty(void) {
    TEST("parser - empty string");
    
    XPathParser* parser = xpath_parser_new("''", 2);
    XPathASTNode* ast = xpath_parse(parser);
    
    if (!ast || ast->type != XPATH_AST_STRING) {
        FAIL("wrong AST type");
        if (ast) ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    if (!ast->value || ast->value[0] != '\0') {
        FAIL("wrong string value");
        ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    ast_node_free(ast);
    xpath_parser_free(parser);
    PASS();
}

/* ============================================================================
 * Path Expression Tests
 * ============================================================================ */

void test_parser_slash(void) {
    TEST("parser - absolute path /");
    
    XPathParser* parser = xpath_parser_new("/", 1);
    XPathASTNode* ast = xpath_parse(parser);
    
    if (!ast || ast->type != XPATH_AST_ABSOLUTE_PATH) {
        FAIL("wrong AST type");
        if (ast) ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    if (ast->child_count != 0) {
        FAIL("should have no children");
        ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    ast_node_free(ast);
    xpath_parser_free(parser);
    PASS();
}

void test_parser_absolute_path(void) {
    TEST("parser - absolute path /book");
    
    XPathParser* parser = xpath_parser_new("/book", 5);
    XPathASTNode* ast = xpath_parse(parser);
    
    if (!ast || ast->type != XPATH_AST_ABSOLUTE_PATH) {
        FAIL("wrong AST type");
        if (ast) ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    if (ast->child_count != 1) {
        FAIL("should have 1 child");
        ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    ast_node_free(ast);
    xpath_parser_free(parser);
    PASS();
}

void test_parser_relative_path(void) {
    TEST("parser - relative path book/title");
    
    XPathParser* parser = xpath_parser_new("book/title", 10);
    XPathASTNode* ast = xpath_parse(parser);
    
    if (!ast || ast->type != XPATH_AST_RELATIVE_PATH) {
        FAIL("wrong AST type");
        if (ast) ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    if (ast->child_count != 2) {
        FAIL("should have 2 children");
        ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    ast_node_free(ast);
    xpath_parser_free(parser);
    PASS();
}

void test_parser_double_slash(void) {
    TEST("parser - descendant path //book");
    
    XPathParser* parser = xpath_parser_new("//book", 6);
    XPathASTNode* ast = xpath_parse(parser);
    
    if (!ast || ast->type != XPATH_AST_ABSOLUTE_PATH) {
        FAIL("wrong AST type");
        if (ast) ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    if (ast->child_count != 2) {
        FAIL("should have 2 children (desc-or-self + relative)");
        ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    ast_node_free(ast);
    xpath_parser_free(parser);
    PASS();
}

void test_parser_complex_path(void) {
    TEST("parser - complex path /library/book/author");
    
    XPathParser* parser = xpath_parser_new("/library/book/author", 21);
    XPathASTNode* ast = xpath_parse(parser);
    
    if (!ast || ast->type != XPATH_AST_ABSOLUTE_PATH) {
        FAIL("wrong AST type");
        if (ast) ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    if (ast->child_count != 1) {
        FAIL("should have 1 child (relative path)");
        ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    XPathASTNode* rel = ast->children[0];
    if (rel->type != XPATH_AST_RELATIVE_PATH || rel->child_count != 3) {
        FAIL("relative path should have 3 steps");
        ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    ast_node_free(ast);
    xpath_parser_free(parser);
    PASS();
}

/* ============================================================================
 * Step Expression Tests
 * ============================================================================ */

void test_parser_axis_child(void) {
    TEST("parser - axis child::book");
    
    XPathParser* parser = xpath_parser_new("child::book", 11);
    XPathASTNode* ast = xpath_parse(parser);
    
    if (!ast || ast->type != XPATH_AST_STEP) {
        FAIL("wrong AST type");
        if (ast) ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    if (!ast->value || strcmp(ast->value, "child") != 0) {
        FAIL("wrong axis");
        ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    ast_node_free(ast);
    xpath_parser_free(parser);
    PASS();
}

void test_parser_axis_descendant(void) {
    TEST("parser - axis descendant::node");
    
    XPathParser* parser = xpath_parser_new("descendant::node", 16);
    XPathASTNode* ast = xpath_parse(parser);
    
    if (!ast || ast->type != XPATH_AST_STEP) {
        FAIL("wrong AST type");
        if (ast) ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    if (!ast->value || strcmp(ast->value, "descendant") != 0) {
        FAIL("wrong axis");
        ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    ast_node_free(ast);
    xpath_parser_free(parser);
    PASS();
}

void test_parser_abbreviated_dot(void) {
    TEST("parser - abbreviated .");
    
    XPathParser* parser = xpath_parser_new(".", 1);
    XPathASTNode* ast = xpath_parse(parser);
    
    if (!ast || ast->type != XPATH_AST_STEP) {
        FAIL("wrong AST type");
        if (ast) ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    if (!ast->value || strcmp(ast->value, "self") != 0) {
        FAIL("should be self axis");
        ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    ast_node_free(ast);
    xpath_parser_free(parser);
    PASS();
}

void test_parser_abbreviated_double_dot(void) {
    TEST("parser - abbreviated ..");
    
    XPathParser* parser = xpath_parser_new("..", 2);
    XPathASTNode* ast = xpath_parse(parser);
    
    if (!ast || ast->type != XPATH_AST_STEP) {
        FAIL("wrong AST type");
        if (ast) ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    if (!ast->value || strcmp(ast->value, "parent") != 0) {
        FAIL("should be parent axis");
        ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    ast_node_free(ast);
    xpath_parser_free(parser);
    PASS();
}

/* ============================================================================
 * Predicate Tests
 * ============================================================================ */

void test_parser_predicate_position(void) {
    TEST("parser - predicate [1]");
    
    XPathParser* parser = xpath_parser_new("book[1]", 7);
    XPathASTNode* ast = xpath_parse(parser);
    
    if (!ast || ast->type != XPATH_AST_STEP) {
        FAIL("wrong AST type");
        if (ast) ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    if (ast->child_count != 2) {
        FAIL("should have node test + predicate");
        ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    XPathASTNode* pred = ast->children[1];
    if (pred->type != XPATH_AST_NUMBER) {
        FAIL("predicate should be number");
        ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    ast_node_free(ast);
    xpath_parser_free(parser);
    PASS();
}

void test_parser_predicate_boolean(void) {
    TEST("parser - predicate [@id]");
    
    XPathParser* parser = xpath_parser_new("book[@id]", 9);
    XPathASTNode* ast = xpath_parse(parser);
    
    if (!ast || ast->type != XPATH_AST_STEP) {
        FAIL("wrong AST type");
        if (ast) ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    if (ast->child_count != 2) {
        FAIL("should have node test + predicate");
        ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    ast_node_free(ast);
    xpath_parser_free(parser);
    PASS();
}

void test_parser_predicate_expression(void) {
    TEST("parser - predicate [@price > 20]");
    
    XPathParser* parser = xpath_parser_new("book[@price > 20]", 17);
    XPathASTNode* ast = xpath_parse(parser);
    
    if (!ast || ast->type != XPATH_AST_STEP) {
        FAIL("wrong AST type");
        if (ast) ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    if (ast->child_count != 2) {
        FAIL("should have node test + predicate");
        ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    XPathASTNode* pred = ast->children[1];
    if (pred->type != XPATH_AST_OPERATOR) {
        FAIL("predicate should be operator");
        ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    ast_node_free(ast);
    xpath_parser_free(parser);
    PASS();
}

void test_parser_multiple_predicates(void) {
    TEST("parser - multiple predicates [1][@id]");
    
    XPathParser* parser = xpath_parser_new("book[1][@id]", 12);
    XPathASTNode* ast = xpath_parse(parser);
    
    if (!ast || ast->type != XPATH_AST_STEP) {
        FAIL("wrong AST type");
        if (ast) ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    if (ast->child_count != 3) {
        FAIL("should have node test + 2 predicates");
        ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    ast_node_free(ast);
    xpath_parser_free(parser);
    PASS();
}

/* ============================================================================
 * Operator Tests
 * ============================================================================ */

void test_parser_logical_and(void) {
    TEST("parser - logical AND (a and b)");
    
    XPathParser* parser = xpath_parser_new("a and b", 7);
    XPathASTNode* ast = xpath_parse(parser);
    
    if (!ast || ast->type != XPATH_AST_OPERATOR) {
        FAIL("wrong AST type");
        if (ast) ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    if ((int)ast->number_value != XPATH_OP_AND) {
        FAIL("wrong operator");
        ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    if (ast->child_count != 2) {
        FAIL("should have 2 children");
        ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    ast_node_free(ast);
    xpath_parser_free(parser);
    PASS();
}

void test_parser_comparison(void) {
    TEST("parser - comparison (a < b)");
    
    XPathParser* parser = xpath_parser_new("a < b", 5);
    XPathASTNode* ast = xpath_parse(parser);
    
    if (!ast || ast->type != XPATH_AST_OPERATOR) {
        FAIL("wrong AST type");
        if (ast) ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    if ((int)ast->number_value != XPATH_OP_LESS) {
        FAIL("wrong operator");
        ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    ast_node_free(ast);
    xpath_parser_free(parser);
    PASS();
}

void test_parser_arithmetic(void) {
    TEST("parser - arithmetic (a + b)");
    
    XPathParser* parser = xpath_parser_new("a + b", 5);
    XPathASTNode* ast = xpath_parse(parser);
    
    if (!ast || ast->type != XPATH_AST_OPERATOR) {
        FAIL("wrong AST type");
        if (ast) ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    if ((int)ast->number_value != XPATH_OP_PLUS) {
        FAIL("wrong operator");
        ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    ast_node_free(ast);
    xpath_parser_free(parser);
    PASS();
}

void test_parser_union(void) {
    TEST("parser - union (a | b)");
    
    XPathParser* parser = xpath_parser_new("a | b", 5);
    XPathASTNode* ast = xpath_parse(parser);
    
    if (!ast || ast->type != XPATH_AST_OPERATOR) {
        FAIL("wrong AST type");
        if (ast) ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    if ((int)ast->number_value != XPATH_OP_UNION) {
        FAIL("wrong operator");
        ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    ast_node_free(ast);
    xpath_parser_free(parser);
    PASS();
}

/* ============================================================================
 * Function Call Tests
 * ============================================================================ */

void test_parser_function_no_args(void) {
    TEST("parser - function last()");
    
    XPathParser* parser = xpath_parser_new("last()", 6);
    XPathASTNode* ast = xpath_parse(parser);
    
    if (!ast || ast->type != XPATH_AST_FUNCTION_CALL) {
        FAIL("wrong AST type");
        if (ast) ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    if (!ast->value || strcmp(ast->value, "last") != 0) {
        FAIL("wrong function name");
        ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    if (ast->child_count != 0) {
        FAIL("should have no arguments");
        ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    ast_node_free(ast);
    xpath_parser_free(parser);
    PASS();
}

void test_parser_function_one_arg(void) {
    TEST("parser - function count(book)");
    
    XPathParser* parser = xpath_parser_new("count(book)", 11);
    XPathASTNode* ast = xpath_parse(parser);
    
    if (!ast || ast->type != XPATH_AST_FUNCTION_CALL) {
        FAIL("wrong AST type");
        if (ast) ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    if (!ast->value || strcmp(ast->value, "count") != 0) {
        FAIL("wrong function name");
        ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    if (ast->child_count != 1) {
        FAIL("should have 1 argument");
        ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    ast_node_free(ast);
    xpath_parser_free(parser);
    PASS();
}

void test_parser_function_multiple_args(void) {
    TEST("parser - function substring('hello', 2, 3)");

    XPathParser* parser = xpath_parser_new("substring('hello', 2, 3)", 24);
    XPathASTNode* ast = xpath_parse(parser);

    /* With constant folding, literal-only substring() is pre-computed to a STRING */
    if (!ast) {
        FAIL("AST is NULL");
        xpath_parser_free(parser);
        return;
    }

    /* Constant folding transforms substring('hello', 2, 3) -> 'ell' */
    if (ast->type == XPATH_AST_STRING) {
        if (!ast->value || strcmp(ast->value, "ell") != 0) {
            FAIL("wrong constant-folded value");
            ast_node_free(ast);
            xpath_parser_free(parser);
            return;
        }
    } else if (ast->type == XPATH_AST_FUNCTION_CALL) {
        /* Without constant folding, check function structure */
        if (!ast->value || strcmp(ast->value, "substring") != 0) {
            FAIL("wrong function name");
            ast_node_free(ast);
            xpath_parser_free(parser);
            return;
        }

        if (ast->child_count != 3) {
            FAIL("should have 3 arguments");
            ast_node_free(ast);
            xpath_parser_free(parser);
            return;
        }
    } else {
        FAIL("wrong AST type");
        ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }

    ast_node_free(ast);
    xpath_parser_free(parser);
    PASS();
}

/* ============================================================================
 * Additional Tests
 * ============================================================================ */

void test_parser_parenthesized(void) {
    TEST("parser - parenthesized expression");
    
    XPathParser* parser = xpath_parser_new("(1 + 2)", 7);
    XPathASTNode* ast = xpath_parse(parser);
    
    if (!ast || ast->type != XPATH_AST_OPERATOR) {
        FAIL("wrong AST type");
        if (ast) ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    ast_node_free(ast);
    xpath_parser_free(parser);
    PASS();
}

void test_parser_operator_precedence(void) {
    TEST("parser - operator precedence (1 + 2 * 3)");
    
    XPathParser* parser = xpath_parser_new("1 + 2 * 3", 9);
    XPathASTNode* ast = xpath_parse(parser);
    
    if (!ast || ast->type != XPATH_AST_OPERATOR) {
        FAIL("wrong AST type");
        if (ast) ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    /* Top should be PLUS */
    if ((int)ast->number_value != XPATH_OP_PLUS) {
        FAIL("top should be PLUS");
        ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    /* Right child should be MULTIPLY */
    if (ast->child_count < 2 || ast->children[1]->type != XPATH_AST_OPERATOR ||
        (int)ast->children[1]->number_value != XPATH_OP_MULTIPLY) {
        FAIL("precedence wrong");
        ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    ast_node_free(ast);
    xpath_parser_free(parser);
    PASS();
}

void test_parser_unary_minus(void) {
    TEST("parser - unary minus (-5)");
    
    XPathParser* parser = xpath_parser_new("-5", 2);
    XPathASTNode* ast = xpath_parse(parser);
    
    if (!ast || ast->type != XPATH_AST_OPERATOR) {
        FAIL("wrong AST type");
        if (ast) ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    if ((int)ast->number_value != XPATH_OP_NEGATION) {
        FAIL("should be negation operator");
        ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    ast_node_free(ast);
    xpath_parser_free(parser);
    PASS();
}

void test_parser_attribute_abbreviated(void) {
    TEST("parser - attribute abbreviated @id");
    
    XPathParser* parser = xpath_parser_new("@id", 3);
    XPathASTNode* ast = xpath_parse(parser);
    
    if (!ast || ast->type != XPATH_AST_STEP) {
        FAIL("wrong AST type");
        if (ast) ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    if (!ast->value || strcmp(ast->value, "attribute") != 0) {
        FAIL("should be attribute axis");
        ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    ast_node_free(ast);
    xpath_parser_free(parser);
    PASS();
}

void test_parser_wildcard(void) {
    TEST("parser - wildcard *");
    
    XPathParser* parser = xpath_parser_new("*", 1);
    XPathASTNode* ast = xpath_parse(parser);
    
    if (!ast || ast->type != XPATH_AST_STEP) {
        FAIL("wrong AST type");
        if (ast) ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    /* Should have wildcard node test */
    if (ast->child_count < 1 || ast->children[0]->type != XPATH_AST_NODE_TEST_ALL) {
        FAIL("should have wildcard node test");
        ast_node_free(ast);
        xpath_parser_free(parser);
        return;
    }
    
    ast_node_free(ast);
    xpath_parser_free(parser);
    PASS();
}

/* ============================================================================
 * Main Test Runner
 * ============================================================================ */

int main(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║         XPath Parser Test Suite (Session 87)             ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    printf("Literal expression tests:\n");
    test_parser_number();
    test_parser_decimal();
    test_parser_string_single();
    test_parser_string_double();
    test_parser_string_empty();
    
    printf("\nPath expression tests:\n");
    test_parser_slash();
    test_parser_absolute_path();
    test_parser_relative_path();
    test_parser_double_slash();
    test_parser_complex_path();
    
    printf("\nStep expression tests:\n");
    test_parser_axis_child();
    test_parser_axis_descendant();
    test_parser_abbreviated_dot();
    test_parser_abbreviated_double_dot();
    
    printf("\nPredicate tests:\n");
    test_parser_predicate_position();
    test_parser_predicate_boolean();
    test_parser_predicate_expression();
    test_parser_multiple_predicates();
    
    printf("\nOperator tests:\n");
    test_parser_logical_and();
    test_parser_comparison();
    test_parser_arithmetic();
    test_parser_union();
    
    printf("\nFunction call tests:\n");
    test_parser_function_no_args();
    test_parser_function_one_arg();
    test_parser_function_multiple_args();
    
    printf("\nAdditional tests:\n");
    test_parser_parenthesized();
    test_parser_operator_precedence();
    test_parser_unary_minus();
    test_parser_attribute_abbreviated();
    test_parser_wildcard();
    
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║                      Test Results                         ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("  Passed:  %2d / %2d\n", tests_passed, tests_passed + tests_failed);
    printf("  Failed:  %2d / %2d\n", tests_failed, tests_passed + tests_failed);
    printf("\n");
    
    return tests_failed == 0 ? 0 : 1;
}