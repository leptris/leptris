/* test_lexer.c - XPath lexer tests */

#include "../../../src/taurus/xpath/lexer.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) printf("  Testing %s... ", name); fflush(stdout);
#define PASS() printf("✓ PASS\n"); tests_passed++;
#define FAIL(msg) printf("✗ FAIL: %s\n", msg); tests_failed++;

void test_lexer_number(void) {
    TEST("lexer - number token");
    
    XPathLexer* lexer = xpath_lexer_new("123.45", 6);
    XPathToken token = xpath_lexer_next_token(lexer);
    
    if (token.type != TOK_NUMBER) {
        FAIL("wrong token type");
        xpath_lexer_free(lexer);
        return;
    }
    
    if (token.value_len != 6 || strncmp(token.value, "123.45", 6) != 0) {
        FAIL("wrong token value");
        xpath_lexer_free(lexer);
        return;
    }
    
    xpath_lexer_free(lexer);
    PASS();
}

void test_lexer_integer(void) {
    TEST("lexer - integer token");
    
    XPathLexer* lexer = xpath_lexer_new("42", 2);
    XPathToken token = xpath_lexer_next_token(lexer);
    
    if (token.type != TOK_NUMBER) {
        FAIL("wrong token type");
        xpath_lexer_free(lexer);
        return;
    }
    
    if (token.value_len != 2 || strncmp(token.value, "42", 2) != 0) {
        FAIL("wrong token value");
        xpath_lexer_free(lexer);
        return;
    }
    
    xpath_lexer_free(lexer);
    PASS();
}

void test_lexer_decimal(void) {
    TEST("lexer - decimal starting with dot");
    
    XPathLexer* lexer = xpath_lexer_new(".5", 2);
    XPathToken token = xpath_lexer_next_token(lexer);
    
    if (token.type != TOK_NUMBER) {
        FAIL("wrong token type");
        xpath_lexer_free(lexer);
        return;
    }
    
    if (token.value_len != 2 || strncmp(token.value, ".5", 2) != 0) {
        FAIL("wrong token value");
        xpath_lexer_free(lexer);
        return;
    }
    
    xpath_lexer_free(lexer);
    PASS();
}

void test_lexer_string_single(void) {
    TEST("lexer - single-quoted string");
    
    XPathLexer* lexer = xpath_lexer_new("'hello'", 7);
    XPathToken token = xpath_lexer_next_token(lexer);
    
    if (token.type != TOK_STRING) {
        FAIL("wrong token type");
        xpath_lexer_free(lexer);
        return;
    }
    
    if (token.value_len != 7 || strncmp(token.value, "'hello'", 7) != 0) {
        FAIL("wrong token value");
        xpath_lexer_free(lexer);
        return;
    }
    
    xpath_lexer_free(lexer);
    PASS();
}

void test_lexer_string_double(void) {
    TEST("lexer - double-quoted string");
    
    XPathLexer* lexer = xpath_lexer_new("\"world\"", 7);
    XPathToken token = xpath_lexer_next_token(lexer);
    
    if (token.type != TOK_STRING) {
        FAIL("wrong token type");
        xpath_lexer_free(lexer);
        return;
    }
    
    if (token.value_len != 7 || strncmp(token.value, "\"world\"", 7) != 0) {
        FAIL("wrong token value");
        xpath_lexer_free(lexer);
        return;
    }
    
    xpath_lexer_free(lexer);
    PASS();
}

void test_lexer_ncname(void) {
    TEST("lexer - NCName token");
    
    XPathLexer* lexer = xpath_lexer_new("element", 7);
    XPathToken token = xpath_lexer_next_token(lexer);
    
    if (token.type != TOK_NCNAME) {
        FAIL("wrong token type");
        xpath_lexer_free(lexer);
        return;
    }
    
    if (token.value_len != 7 || strncmp(token.value, "element", 7) != 0) {
        FAIL("wrong token value");
        xpath_lexer_free(lexer);
        return;
    }
    
    xpath_lexer_free(lexer);
    PASS();
}

void test_lexer_qname(void) {
    TEST("lexer - QName token");
    
    XPathLexer* lexer = xpath_lexer_new("ns:element", 10);
    XPathToken token = xpath_lexer_next_token(lexer);
    
    if (token.type != TOK_QNAME) {
        FAIL("wrong token type");
        xpath_lexer_free(lexer);
        return;
    }
    
    if (token.value_len != 10 || strncmp(token.value, "ns:element", 10) != 0) {
        FAIL("wrong token value");
        xpath_lexer_free(lexer);
        return;
    }
    
    xpath_lexer_free(lexer);
    PASS();
}

void test_lexer_operators(void) {
    TEST("lexer - logical operators");
    
    XPathLexer* lexer = xpath_lexer_new("and or", 6);
    
    /* Test 'and' */
    XPathToken token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_AND) {
        FAIL("wrong token for 'and'");
        xpath_lexer_free(lexer);
        return;
    }
    
    /* Test 'or' */
    token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_OR) {
        FAIL("wrong token for 'or'");
        xpath_lexer_free(lexer);
        return;
    }
    
    xpath_lexer_free(lexer);
    PASS();
}

void test_lexer_comparison(void) {
    TEST("lexer - comparison operators");
    
    XPathLexer* lexer = xpath_lexer_new("= != < <= > >=", 14);
    
    XPathToken token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_EQUALS) {
        FAIL("wrong token for '='");
        xpath_lexer_free(lexer);
        return;
    }
    
    token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_NOT_EQUALS) {
        FAIL("wrong token for '!='");
        xpath_lexer_free(lexer);
        return;
    }
    
    token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_LT) {
        FAIL("wrong token for '<'");
        xpath_lexer_free(lexer);
        return;
    }
    
    token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_LE) {
        FAIL("wrong token for '<='");
        xpath_lexer_free(lexer);
        return;
    }
    
    token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_GT) {
        FAIL("wrong token for '>'");
        xpath_lexer_free(lexer);
        return;
    }
    
    token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_GE) {
        FAIL("wrong token for '>='");
        xpath_lexer_free(lexer);
        return;
    }
    
    xpath_lexer_free(lexer);
    PASS();
}

void test_lexer_arithmetic(void) {
    TEST("lexer - arithmetic operators");
    
    XPathLexer* lexer = xpath_lexer_new("+ - * div mod", 13);
    
    XPathToken token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_PLUS) {
        FAIL("wrong token for '+'");
        xpath_lexer_free(lexer);
        return;
    }
    
    token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_MINUS) {
        FAIL("wrong token for '-'");
        xpath_lexer_free(lexer);
        return;
    }
    
    token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_STAR) {
        FAIL("wrong token for '*'");
        xpath_lexer_free(lexer);
        return;
    }
    
    token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_DIV) {
        FAIL("wrong token for 'div'");
        xpath_lexer_free(lexer);
        return;
    }
    
    token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_MOD) {
        FAIL("wrong token for 'mod'");
        xpath_lexer_free(lexer);
        return;
    }
    
    xpath_lexer_free(lexer);
    PASS();
}

void test_lexer_path(void) {
    TEST("lexer - path expression");
    
    XPathLexer* lexer = xpath_lexer_new("//book[@id='123']", 17);
    
    /* Test '//' */
    XPathToken token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_DOUBLE_SLASH) {
        FAIL("wrong token for '//'");
        xpath_lexer_free(lexer);
        return;
    }
    
    /* Test 'book' */
    token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_NCNAME) {
        FAIL("wrong token for 'book'");
        xpath_lexer_free(lexer);
        return;
    }
    
    /* Test '[' */
    token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_LBRACKET) {
        FAIL("wrong token for '['");
        xpath_lexer_free(lexer);
        return;
    }
    
    /* Test '@' */
    token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_AT) {
        FAIL("wrong token for '@'");
        xpath_lexer_free(lexer);
        return;
    }
    
    xpath_lexer_free(lexer);
    PASS();
}

void test_lexer_axis_child(void) {
    TEST("lexer - child axis");
    
    XPathLexer* lexer = xpath_lexer_new("child::book", 11);
    
    XPathToken token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_CHILD) {
        FAIL("wrong token for 'child'");
        xpath_lexer_free(lexer);
        return;
    }
    
    token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_DOUBLE_COLON) {
        FAIL("wrong token for '::'");
        xpath_lexer_free(lexer);
        return;
    }
    
    token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_NCNAME) {
        FAIL("wrong token for 'book'");
        xpath_lexer_free(lexer);
        return;
    }
    
    xpath_lexer_free(lexer);
    PASS();
}

void test_lexer_axis_descendant(void) {
    TEST("lexer - descendant axis");
    
    XPathLexer* lexer = xpath_lexer_new("descendant::node", 16);
    
    XPathToken token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_DESCENDANT) {
        FAIL("wrong token for 'descendant'");
        xpath_lexer_free(lexer);
        return;
    }
    
    xpath_lexer_free(lexer);
    PASS();
}

void test_lexer_axis_parent(void) {
    TEST("lexer - parent axis");
    
    XPathLexer* lexer = xpath_lexer_new("parent::*", 9);
    
    XPathToken token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_PARENT) {
        FAIL("wrong token for 'parent'");
        xpath_lexer_free(lexer);
        return;
    }
    
    xpath_lexer_free(lexer);
    PASS();
}

void test_lexer_node_test_text(void) {
    TEST("lexer - text() node test");
    
    XPathLexer* lexer = xpath_lexer_new("text()", 6);
    
    XPathToken token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_TEXT) {
        FAIL("wrong token for 'text'");
        xpath_lexer_free(lexer);
        return;
    }
    
    token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_LPAREN) {
        FAIL("wrong token for '('");
        xpath_lexer_free(lexer);
        return;
    }
    
    xpath_lexer_free(lexer);
    PASS();
}

void test_lexer_node_test_comment(void) {
    TEST("lexer - comment() node test");
    
    XPathLexer* lexer = xpath_lexer_new("comment()", 9);
    
    XPathToken token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_COMMENT) {
        FAIL("wrong token for 'comment'");
        xpath_lexer_free(lexer);
        return;
    }
    
    xpath_lexer_free(lexer);
    PASS();
}

void test_lexer_node_test_node(void) {
    TEST("lexer - node() node test");
    
    XPathLexer* lexer = xpath_lexer_new("node()", 6);
    
    XPathToken token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_NODE) {
        FAIL("wrong token for 'node'");
        xpath_lexer_free(lexer);
        return;
    }
    
    xpath_lexer_free(lexer);
    PASS();
}

void test_lexer_punctuation(void) {
    TEST("lexer - punctuation tokens");
    
    XPathLexer* lexer = xpath_lexer_new("( ) [ ] , @ . ..", 16);
    
    XPathToken token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_LPAREN) {
        FAIL("wrong token for '('");
        xpath_lexer_free(lexer);
        return;
    }
    
    token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_RPAREN) {
        FAIL("wrong token for ')'");
        xpath_lexer_free(lexer);
        return;
    }
    
    token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_LBRACKET) {
        FAIL("wrong token for '['");
        xpath_lexer_free(lexer);
        return;
    }
    
    token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_RBRACKET) {
        FAIL("wrong token for ']'");
        xpath_lexer_free(lexer);
        return;
    }
    
    token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_COMMA) {
        FAIL("wrong token for ','");
        xpath_lexer_free(lexer);
        return;
    }
    
    token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_AT) {
        FAIL("wrong token for '@'");
        xpath_lexer_free(lexer);
        return;
    }
    
    token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_DOT) {
        FAIL("wrong token for '.'");
        xpath_lexer_free(lexer);
        return;
    }
    
    token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_DOUBLE_DOT) {
        FAIL("wrong token for '..'");
        xpath_lexer_free(lexer);
        return;
    }
    
    xpath_lexer_free(lexer);
    PASS();
}

void test_lexer_slash_types(void) {
    TEST("lexer - slash tokens");
    
    XPathLexer* lexer = xpath_lexer_new("/ //", 4);
    
    XPathToken token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_SLASH) {
        FAIL("wrong token for '/'");
        xpath_lexer_free(lexer);
        return;
    }
    
    token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_DOUBLE_SLASH) {
        FAIL("wrong token for '//'");
        xpath_lexer_free(lexer);
        return;
    }
    
    xpath_lexer_free(lexer);
    PASS();
}

void test_lexer_union(void) {
    TEST("lexer - union operator");
    
    XPathLexer* lexer = xpath_lexer_new("book | chapter", 14);
    
    xpath_lexer_next_token(lexer);  /* book */
    
    XPathToken token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_PIPE) {
        FAIL("wrong token for '|'");
        xpath_lexer_free(lexer);
        return;
    }
    
    xpath_lexer_free(lexer);
    PASS();
}

void test_lexer_whitespace(void) {
    TEST("lexer - whitespace handling");
    
    XPathLexer* lexer = xpath_lexer_new("  book  \n  chapter  ", 21);
    
    XPathToken token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_NCNAME || strncmp(token.value, "book", 4) != 0) {
        FAIL("wrong token for 'book'");
        xpath_lexer_free(lexer);
        return;
    }
    
    token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_NCNAME || strncmp(token.value, "chapter", 7) != 0) {
        FAIL("wrong token for 'chapter'");
        xpath_lexer_free(lexer);
        return;
    }
    
    xpath_lexer_free(lexer);
    PASS();
}

void test_lexer_eof(void) {
    TEST("lexer - EOF token");
    
    XPathLexer* lexer = xpath_lexer_new("end", 3);
    
    xpath_lexer_next_token(lexer);  /* 'end' */
    
    XPathToken token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_EOF) {
        FAIL("wrong token for EOF");
        xpath_lexer_free(lexer);
        return;
    }
    
    xpath_lexer_free(lexer);
    PASS();
}

void test_lexer_complex_expression(void) {
    TEST("lexer - complex expression");
    
    const char* expr = "//book[@price > 20 and @category='fiction']/title";
    XPathLexer* lexer = xpath_lexer_new(expr, strlen(expr));
    
    /* Just verify we can tokenize without errors */
    XPathToken token;
    int count = 0;
    do {
        token = xpath_lexer_next_token(lexer);
        count++;
    } while (token.type != TOK_EOF && count < 50);  /* Safety limit */
    
    if (count > 40) {
        FAIL("too many tokens or infinite loop");
        xpath_lexer_free(lexer);
        return;
    }
    
    xpath_lexer_free(lexer);
    PASS();
}

void test_lexer_empty_input(void) {
    TEST("lexer - empty input");
    
    XPathLexer* lexer = xpath_lexer_new("", 0);
    
    XPathToken token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_EOF) {
        FAIL("wrong token for empty input");
        xpath_lexer_free(lexer);
        return;
    }
    
    xpath_lexer_free(lexer);
    PASS();
}

void test_lexer_ancestor_or_self(void) {
    TEST("lexer - ancestor-or-self axis");
    
    XPathLexer* lexer = xpath_lexer_new("ancestor-or-self::*", 19);
    
    XPathToken token = xpath_lexer_next_token(lexer);
    if (token.type != TOK_ANCESTOR_OR_SELF) {
        FAIL("wrong token for 'ancestor-or-self'");
        xpath_lexer_free(lexer);
        return;
    }
    
    xpath_lexer_free(lexer);
    PASS();
}

int main(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║         XPath Lexer Test Suite (Session 86)              ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n");
    printf("\n");
    
    printf("Basic token tests:\n");
    test_lexer_number();
    test_lexer_integer();
    test_lexer_decimal();
    test_lexer_string_single();
    test_lexer_string_double();
    test_lexer_ncname();
    test_lexer_qname();
    printf("\n");
    
    printf("Operator tests:\n");
    test_lexer_operators();
    test_lexer_comparison();
    test_lexer_arithmetic();
    test_lexer_union();
    printf("\n");
    
    printf("Path expression tests:\n");
    test_lexer_path();
    test_lexer_punctuation();
    test_lexer_slash_types();
    printf("\n");
    
    printf("Axis tests:\n");
    test_lexer_axis_child();
    test_lexer_axis_descendant();
    test_lexer_axis_parent();
    test_lexer_ancestor_or_self();
    printf("\n");
    
    printf("Node test tokens:\n");
    test_lexer_node_test_text();
    test_lexer_node_test_comment();
    test_lexer_node_test_node();
    printf("\n");
    
    printf("Edge cases:\n");
    test_lexer_whitespace();
    test_lexer_eof();
    test_lexer_complex_expression();
    test_lexer_empty_input();
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