/* test/c/dom/test_serialization.c - Serialization Tests
 * Copyright (c) 2024, Ribose Inc.
 */

#include "taurus.h"
#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) printf("  Testing %s... ", name); fflush(stdout);
#define PASS() printf("✓ PASS\n"); tests_passed++;
#define FAIL(msg) printf("✗ FAIL: %s\n", msg); tests_failed++;

/* Test 1: Empty element (self-closing tag) */
void test_serialize_empty_element(void) {
    TEST("empty element serialization");

    const char* xml = "<empty/>";
    TaurusStatus status;
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), &status);
    assert(doc != NULL);

    char* result = taurus_document_serialize(doc, NULL);
    assert(result != NULL);
    printf("DEBUG result='%s'\n", result);
    assert(strcmp(result, "<empty/>") == 0);

    taurus_free_string(result);
    taurus_document_free(doc);
    PASS();
}

/* Test 2: Element with text content */
void test_serialize_with_text(void) {
    TEST("element with text");

    const char* xml = "<root>Hello World</root>";
    TaurusStatus status;
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), &status);
    assert(doc != NULL);

    char* result = taurus_document_serialize(doc, NULL);
    assert(result != NULL);
    printf("DEBUG result='%s'\n", result);
    if (strcmp(result, "<root>Hello World</root>") != 0) {
        printf("Expected: '<root>Hello World</root>'\n");
        printf("Got:      '%s'\n", result);
        FAIL("Output mismatch");
        taurus_free_string(result);
        taurus_document_free(doc);
        return;
    }

    taurus_free_string(result);
    taurus_document_free(doc);
    PASS();
}

/* Test 3: Element with attributes */
void test_serialize_with_attributes(void) {
    TEST("element with attributes");

    const char* xml = "<item id=\"1\" name=\"test\"/>";
    TaurusStatus status;
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), &status);
    assert(doc != NULL);

    char* result = taurus_document_serialize(doc, NULL);
    assert(result != NULL);
    assert(strstr(result, "id=\"1\"") != NULL);
    assert(strstr(result, "name=\"test\"") != NULL);

    taurus_free_string(result);
    taurus_document_free(doc);
    PASS();
}

/* Test 4: Nested elements */
void test_serialize_nested(void) {
    TEST("nested elements");

    const char* xml = "<root><child1><grandchild/></child1><child2/></root>";
    TaurusStatus status;
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), &status);
    assert(doc != NULL);

    char* result = taurus_document_serialize(doc, NULL);
    assert(result != NULL);
    assert(strstr(result, "<root>") != NULL);
    assert(strstr(result, "<child1>") != NULL);
    assert(strstr(result, "<grandchild/>") != NULL);
    assert(strstr(result, "<child2/>") != NULL);

    taurus_free_string(result);
    taurus_document_free(doc);
    PASS();
}

/* Test 5: XML with namespaces */
void test_serialize_with_namespaces(void) {
    TEST("namespaces");

    const char* xml = "<root xmlns=\"http://example.org\" xmlns:ns=\"http://ns.example.org\"><ns:item/></root>";
    TaurusStatus status;
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), &status);
    assert(doc != NULL);

    char* result = taurus_document_serialize(doc, NULL);
    assert(result != NULL);
    assert(strstr(result, "xmlns=\"http://example.org\"") != NULL);

    taurus_free_string(result);
    taurus_document_free(doc);
    PASS();
}

/* Test 6: Compact mode (default) */
void test_serialize_compact(void) {
    TEST("compact mode");

    const char* xml = "<root><child>text</child></root>";
    TaurusStatus status;
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), &status);
    assert(doc != NULL);

    char* result = taurus_document_serialize(doc, NULL);
    assert(result != NULL);
    /* Should not contain newlines or indentation */
    assert(strchr(result, '\n') == NULL);

    taurus_free_string(result);
    taurus_document_free(doc);
    PASS();
}

/* Test 7: Pretty-print mode */
void test_serialize_pretty(void) {
    TEST("pretty-print mode");

    const char* xml = "<root><child>text</child></root>";
    TaurusStatus status;
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), &status);
    assert(doc != NULL);

    TaurusSerializeOptions opts = { .indent = 2, .xml_declaration = 0, .encoding = NULL };
    char* result = taurus_document_serialize(doc, &opts);
    assert(result != NULL);
    /* Should contain newlines */
    assert(strchr(result, '\n') != NULL);

    taurus_free_string(result);
    taurus_document_free(doc);
    PASS();
}

/* Test 8: XML declaration */
void test_serialize_with_declaration(void) {
    TEST("XML declaration");

    const char* xml = "<root/>";
    TaurusStatus status;
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), &status);
    assert(doc != NULL);

    TaurusSerializeOptions opts = { .indent = 0, .xml_declaration = 1, .encoding = "UTF-8" };
    char* result = taurus_document_serialize(doc, &opts);
    assert(result != NULL);
    assert(strstr(result, "<?xml version=\"1.0\" encoding=\"UTF-8\"?>") != NULL);

    taurus_free_string(result);
    taurus_document_free(doc);
    PASS();
}

/* Test 9: Escape special characters */
void test_serialize_escape(void) {
    TEST("escape special characters");

    const char* xml = "<root attr=\"&lt;&gt;&quot;\">&amp;&lt;&gt;</root>";
    TaurusStatus status;
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), &status);
    assert(doc != NULL);

    char* result = taurus_document_serialize(doc, NULL);
    assert(result != NULL);
    /* Text should be escaped */
    assert(strstr(result, "&amp;") != NULL);
    assert(strstr(result, "&lt;") != NULL);
    assert(strstr(result, "&gt;") != NULL);
    /* Attribute should be escaped */
    assert(strstr(result, "&quot;") != NULL);

    taurus_free_string(result);
    taurus_document_free(doc);
    PASS();
}

/* Test 10: Roundtrip (parse -> serialize -> parse) */
void test_serialize_roundtrip(void) {
    TEST("roundtrip parse-serialize-parse");

    const char* xml = "<root><item id=\"1\">Hello</item><item id=\"2\">World</item></root>";

    /* First parse */
    printf("[1] First parse...\n"); fflush(stdout);
    TaurusStatus status1;
    TaurusDocument doc1 = taurus_parse_string(xml, strlen(xml), &status1);
    assert(doc1 != NULL);
    printf("[2] First parse OK\n"); fflush(stdout);

    /* Serialize */
    printf("[3] Serializing...\n"); fflush(stdout);
    char* serialized = taurus_document_serialize(doc1, NULL);
    assert(serialized != NULL);
    printf("[4] Serialized: %s\n", serialized); fflush(stdout);

    /* Second parse */
    printf("[5] Second parse...\n"); fflush(stdout);
    TaurusStatus status2;
    TaurusDocument doc2 = taurus_parse_string(serialized, strlen(serialized), &status2);
    assert(doc2 != NULL);
    printf("[6] Second parse OK\n"); fflush(stdout);

    /* Verify structure is preserved */
    printf("[7] Getting roots...\n"); fflush(stdout);
    TaurusElement root1 = taurus_document_root(doc1);
    TaurusElement root2 = taurus_document_root(doc2);
    assert(!taurus_element_is_null(root1) && !taurus_element_is_null(root2));
    printf("[8] Roots OK\n"); fflush(stdout);

    printf("[9] Getting names...\n"); fflush(stdout);
    const char* name1 = taurus_element_name(root1);
    const char* name2 = taurus_element_name(root2);
    printf("[10] Names: %s, %s\n", name1, name2); fflush(stdout);
    assert(strcmp(name1, name2) == 0);

    printf("[11] Getting child counts...\n"); fflush(stdout);
    assert(taurus_element_child_count(root1) == taurus_element_child_count(root2));
    printf("[12] Child counts OK\n"); fflush(stdout);

    printf("[13] Cleanup...\n"); fflush(stdout);
    taurus_free_string(serialized);
    taurus_document_free(doc1);
    taurus_document_free(doc2);
    printf("[14] Done\n"); fflush(stdout);
    PASS();
}

/* Test 11: Serialize subtree (element only) */
void test_serialize_subtree(void) {
    TEST("serialize element subtree");

    const char* xml = "<root><child id=\"1\">Text</child></root>";
    TaurusStatus status;
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), &status);
    assert(doc != NULL);

    TaurusElement root = taurus_document_root(doc);
    assert(!taurus_element_is_null(root));

    TaurusElement child = taurus_element_child(root, 0);
    assert(!taurus_element_is_null(child));

    /* Serialize just the child element */
    char* result = taurus_element_serialize(child, NULL);
    assert(result != NULL);
    assert(strstr(result, "<child") != NULL);
    assert(strstr(result, "id=\"1\"") != NULL);
    assert(strstr(result, "Text") != NULL);
    assert(strstr(result, "<root>") == NULL); /* Should not contain root */

    taurus_free_string(result);
    taurus_document_free(doc);
    PASS();
}

/* Test 12: Memory leak check (multiple serialize/free cycles) */
void test_serialize_memory(void) {
    TEST("memory leak check");

    const char* xml = "<root><item>Test</item><item>Data</item></root>";

    for (int i = 0; i < 100; i++) {
        TaurusStatus status;
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), &status);
        assert(doc != NULL);

        char* result = taurus_document_serialize(doc, NULL);
        assert(result != NULL);

        taurus_free_string(result);
        taurus_document_free(doc);
    }

    PASS();
}

int main(void) {
    printf("\n╔═══════════════════════════════════════════════╗\n");
    printf("║       XML Serialization Test Suite           ║\n");
    printf("╚═══════════════════════════════════════════════╝\n\n");

    test_serialize_empty_element();
    test_serialize_with_text();
    test_serialize_with_attributes();
    test_serialize_nested();
    test_serialize_with_namespaces();
    test_serialize_compact();
    test_serialize_pretty();
    test_serialize_with_declaration();
    test_serialize_escape();
    test_serialize_roundtrip();
    test_serialize_subtree();
    test_serialize_memory();

    printf("\n╔═══════════════════════════════════════════════╗\n");
    printf("║                Test Results                   ║\n");
    printf("╚═══════════════════════════════════════════════╝\n\n");
    printf("  Passed: %2d / %2d\n", tests_passed, tests_passed + tests_failed);
    printf("  Failed: %2d / %2d\n\n", tests_failed, tests_passed + tests_failed);

    return tests_failed == 0 ? 0 : 1;
}