/* test_helpers.c - Test suite for parse helpers
 * Copyright (c) 2024, Ribose Inc.
 * All rights reserved.
 *
 * Tests character classification, attribute stack, and string interning
 */

#include "parse_helpers.h"
#include <stdio.h>
#include <assert.h>
#include <string.h>

/* Test counter */
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    printf("Testing %s... ", name); \
    fflush(stdout);

#define PASS() \
    printf("PASS\n"); \
    tests_passed++;

#define FAIL(msg) \
    printf("FAIL: %s\n", msg); \
    tests_failed++;

/* ==================================================================
 * CHARACTER CLASSIFICATION TESTS
 * ================================================================= */

void test_character_classification(void) {
    TEST("character classification");
    
    /* Whitespace */
    assert(taurus_is_whitespace(' '));
    assert(taurus_is_whitespace('\t'));
    assert(taurus_is_whitespace('\n'));
    assert(taurus_is_whitespace('\r'));
    assert(!taurus_is_whitespace('a'));
    assert(!taurus_is_whitespace('0'));
    
    /* Name characters */
    assert(taurus_is_name_char('a'));
    assert(taurus_is_name_char('Z'));
    assert(taurus_is_name_char('0'));
    assert(taurus_is_name_char('_'));
    assert(taurus_is_name_char('-'));
    assert(taurus_is_name_char('.'));
    assert(taurus_is_name_char(':'));
    assert(!taurus_is_name_char(' '));
    assert(!taurus_is_name_char('<'));
    
    PASS();
}

void test_whitespace_skipping(void) {
    TEST("whitespace skipping");
    
    const char *text = "   \t\n\rabc";
    const char *pos = text;
    const char *end = text + strlen(text);
    
    taurus_skip_whitespace(&pos, end);
    assert(pos == text + 6);  /* Should skip to 'a' */
    assert(*pos == 'a');
    
    PASS();
}

void test_char_helpers(void) {
    TEST("character access helpers");
    
    const char *text = "hello";
    const char *pos = text;
    const char *end = text + strlen(text);
    
    /* Peek without advancing */
    assert(taurus_peek_char(pos, end) == 'h');
    assert(pos == text);  /* Should not advance */
    
    /* Next char advances */
    assert(taurus_next_char(&pos, end) == 'h');
    assert(pos == text + 1);
    assert(taurus_next_char(&pos, end) == 'e');
    assert(pos == text + 2);
    
    /* Past end returns '\0' */
    pos = end;
    assert(taurus_next_char(&pos, end) == '\0');
    assert(taurus_peek_char(pos, end) == '\0');
    
    PASS();
}

/* ==================================================================
 * ATTRIBUTE STACK TESTS
 * ================================================================= */

void test_attr_stack_basic(void) {
    TEST("attribute stack basic operations");
    
    TaurusAttrStack stack;
    taurus_attr_stack_init(&stack);
    
    assert(taurus_attr_stack_size(&stack) == 0);
    
    /* Add attributes */
    taurus_attr_stack_push(&stack, "name", "value");
    assert(taurus_attr_stack_size(&stack) == 1);
    
    taurus_attr_stack_push(&stack, "id", "123");
    assert(taurus_attr_stack_size(&stack) == 2);
    
    /* Get attributes */
    ParseAttribute *attr0 = taurus_attr_stack_at(&stack, 0);
    assert(attr0 != NULL);
    assert(strcmp(attr0->name, "name") == 0);
    assert(strcmp(attr0->value, "value") == 0);
    
    ParseAttribute *attr1 = taurus_attr_stack_at(&stack, 1);
    assert(attr1 != NULL);
    assert(strcmp(attr1->name, "id") == 0);
    assert(strcmp(attr1->value, "123") == 0);
    
    /* Out of bounds returns NULL */
    assert(taurus_attr_stack_at(&stack, 10) == NULL);
    
    taurus_attr_stack_cleanup(&stack);
    PASS();
}

void test_attr_stack_growth(void) {
    TEST("attribute stack growth beyond base size");
    
    TaurusAttrStack stack;
    taurus_attr_stack_init(&stack);
    
    /* Add more than TAURUS_ATTR_STACK_BASE_SIZE (8) attributes */
    for (int i = 0; i < 20; i++) {
        char name[32], value[32];
        snprintf(name, sizeof(name), "attr%d", i);
        snprintf(value, sizeof(value), "value%d", i);
        taurus_attr_stack_push(&stack, name, value);
    }
    
    assert(taurus_attr_stack_size(&stack) == 20);
    
    /* Verify all attributes are accessible */
    for (int i = 0; i < 20; i++) {
        ParseAttribute *attr = taurus_attr_stack_at(&stack, i);
        assert(attr != NULL);
    }
    
    taurus_attr_stack_cleanup(&stack);
    PASS();
}

/* ==================================================================
 * STRING INTERNING TESTS
 * ================================================================= */

void test_string_interning_fast_path(void) {
    TEST("string interning fast path (common attributes)");
    
    /* Fast path should return pre-interned strings */
    const char *id1 = string_intern_get_fast("id", 2);
    const char *id2 = string_intern_get_fast("id", 2);
    assert(id1 == id2);  /* Same pointer */
    assert(id1 == g_intern_id);
    
    const char *class1 = string_intern_get_fast("class", 5);
    const char *class2 = string_intern_get_fast("class", 5);
    assert(class1 == class2);
    assert(class1 == g_intern_class);
    
    const char *href = string_intern_get_fast("href", 4);
    assert(href == g_intern_href);
    
    /* Non-common attributes return NULL */
    assert(string_intern_get_fast("custom", 6) == NULL);
    
    PASS();
}

void test_string_interning_table(void) {
    TEST("string interning table");
    
    StringInternTable table;
    string_intern_table_init(&table);
    
    /* Common attributes use fast path */
    const char *id1 = string_intern_get(&table, "id", 2);
    const char *id2 = string_intern_get(&table, "id", 2);
    assert(id1 == id2);  /* Same pointer */
    assert(id1 == g_intern_id);
    
    /* Custom attributes go through hash table */
    const char *custom1 = string_intern_get(&table, "custom", 6);
    const char *custom2 = string_intern_get(&table, "custom", 6);
    assert(custom1 == custom2);  /* Same pointer (interned) */
    assert(strcmp(custom1, "custom") == 0);
    
    /* Different strings get different pointers */
    const char *other = string_intern_get(&table, "other", 5);
    assert(other != custom1);
    assert(strcmp(other, "other") == 0);
    
    string_intern_table_free(&table);
    PASS();
}

void test_string_interning_multiple(void) {
    TEST("string interning with multiple strings");
    
    StringInternTable table;
    string_intern_table_init(&table);
    
    /* Intern many strings */
    const char *strings[10];
    for (int i = 0; i < 10; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "attr%d", i);
        strings[i] = string_intern_get(&table, buf, strlen(buf));
    }
    
    /* Each should be unique */
    for (int i = 0; i < 10; i++) {
        for (int j = i + 1; j < 10; j++) {
            assert(strings[i] != strings[j]);
        }
    }
    
    /* Re-interning same strings returns same pointers */
    for (int i = 0; i < 10; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "attr%d", i);
        const char *interned = string_intern_get(&table, buf, strlen(buf));
        assert(interned == strings[i]);
    }
    
    string_intern_table_free(&table);
    PASS();
}

/* ==================================================================
 * PARSED ELEMENT TESTS
 * ================================================================= */

void test_parsed_element(void) {
    TEST("parsed element initialization");
    
    TaurusAttrStack stack;
    taurus_attr_stack_init(&stack);
    
    ParsedElement elem;
    parsed_element_init(&elem, &stack);
    
    assert(elem.name == NULL);
    assert(elem.prefix == NULL);
    assert(elem.attrs == &stack);
    assert(elem.has_children == 0);
    assert(elem.is_self_closing == 0);
    
    /* Set some values */
    elem.name = "element";
    elem.prefix = "ns";
    elem.is_self_closing = 1;
    
    assert(strcmp(elem.name, "element") == 0);
    assert(strcmp(elem.prefix, "ns") == 0);
    assert(elem.is_self_closing == 1);
    
    taurus_attr_stack_cleanup(&stack);
    PASS();
}

/* ==================================================================
 * MAIN TEST RUNNER
 * ================================================================= */

int main(void) {
    printf("=== Parse Helpers Test Suite ===\n\n");
    
    /* Character classification tests */
    printf("--- Character Classification ---\n");
    test_character_classification();
    test_whitespace_skipping();
    test_char_helpers();
    
    /* Attribute stack tests */
    printf("\n--- Attribute Stack ---\n");
    test_attr_stack_basic();
    test_attr_stack_growth();
    
    /* String interning tests */
    printf("\n--- String Interning ---\n");
    test_string_interning_fast_path();
    test_string_interning_table();
    test_string_interning_multiple();
    
    /* Parsed element tests */
    printf("\n--- Parsed Element ---\n");
    test_parsed_element();
    
    /* Summary */
    printf("\n=== Test Results ===\n");
    printf("Passed: %d\n", tests_passed);
    printf("Failed: %d\n", tests_failed);
    
    if (tests_failed == 0) {
        printf("\nAll tests passed! ✅\n");
        return 0;
    } else {
        printf("\nSome tests failed! ❌\n");
        return 1;
    }
}