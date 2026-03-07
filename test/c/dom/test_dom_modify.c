/* test/c/dom/test_dom_modify.c - DOM Modification API Tests
 * Copyright (c) 2024, Ribose Inc.
 */

#include <taurus.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include <stdbool.h>

/* Test counter */
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) \
    printf("  Testing %s... ", name); \
    fflush(stdout);

#define PASS() \
    printf("✓ PASS\n"); \
    tests_passed++;

#define FAIL(msg) \
    printf("✗ FAIL: %s\n", msg); \
    tests_failed++;

/* ==================================================================
 * Element Creation Tests
 * ================================================================== */

void test_element_create_basic(void) {
    TEST("taurus_element_create - basic creation");

    const char* xml = "<root/>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);

    if (!doc) {
        FAIL("failed to parse document");
        return;
    }

    TaurusElement elem = taurus_element_create(doc, "item");
    if (!elem) {
        FAIL("taurus_element_create returned NULL");
        taurus_document_free(doc);
        return;
    }

    const char* name = taurus_element_name(elem);
    if (!name || strcmp(name, "item") != 0) {
        FAIL("wrong element name");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_element_create_null_inputs(void) {
    TEST("taurus_element_create - NULL inputs");

    TaurusElement elem = taurus_element_create(NULL, "test");
    if (elem != NULL) {
        FAIL("should return NULL for NULL doc");
        return;
    }

    const char* xml = "<root/>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);

    elem = taurus_element_create(doc, NULL);
    if (elem != NULL) {
        FAIL("should return NULL for NULL name");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

/* ==================================================================
 * Append Child Tests
 * ================================================================== */

void test_append_child_basic(void) {
    TEST("taurus_element_append_child - basic append");

    const char* xml = "<root/>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TaurusElement root = taurus_document_root(doc);

    TaurusElement child = taurus_element_create(doc, "child");
    TaurusStatus status = taurus_element_append_child(root, child);

    if (status != TAURUS_OK) {
        FAIL("append_child returned error");
        taurus_document_free(doc);
        return;
    }

    size_t count = taurus_element_child_count(root);
    if (count != 1) {
        FAIL("wrong child count");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_append_child_multiple(void) {
    TEST("taurus_element_append_child - multiple children");

    const char* xml = "<root/>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TaurusElement root = taurus_document_root(doc);

    for (int i = 0; i < 5; i++) {
        TaurusElement child = taurus_element_create(doc, "child");
        taurus_element_append_child(root, child);
    }

    size_t count = taurus_element_child_count(root);
    if (count != 5) {
        FAIL("wrong child count");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

/* ==================================================================
 * Prepend Child Tests
 * ================================================================== */

void test_prepend_child_empty_parent(void) {
    TEST("taurus_element_prepend_child - empty parent");

    TaurusDocument doc = taurus_parse_string("<root/>", strlen("<root/>"), NULL);
    TaurusElement root = taurus_document_root(doc);

    TaurusElement child = taurus_element_create(doc, "first");
    TaurusStatus status = taurus_element_prepend_child(root, child);

    if (status != TAURUS_OK) {
        FAIL("prepend_child returned error");
        taurus_document_free(doc);
        return;
    }

    if (taurus_element_child_count(root) != 1) {
        FAIL("wrong child count");
        taurus_document_free(doc);
        return;
    }

    TaurusElement first_child = taurus_element_child(root, 0);
    const char* name = taurus_element_name(first_child);
    if (!name || strcmp(name, "first") != 0) {
        FAIL("wrong child name");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_prepend_child_with_existing(void) {
    TEST("taurus_element_prepend_child - with existing children");

    TaurusDocument doc = taurus_parse_string("<root><child1/><child2/></root>", strlen("<root><child1/><child2/></root>"), NULL);
    TaurusElement root = taurus_document_root(doc);

    TaurusElement new_child = taurus_element_create(doc, "first");
    taurus_element_prepend_child(root, new_child);

    if (taurus_element_child_count(root) != 3) {
        FAIL("wrong child count");
        taurus_document_free(doc);
        return;
    }

    /* Verify order: first, child1, child2 */
    TaurusElement child0 = taurus_element_child(root, 0);
    const char* name0 = taurus_element_name(child0);
    if (!name0 || strcmp(name0, "first") != 0) {
        FAIL("prepended child not first");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_prepend_child_multiple(void) {
    TEST("taurus_element_prepend_child - multiple prepends");

    TaurusDocument doc = taurus_parse_string("<root/>", strlen("<root/>"), NULL);
    TaurusElement root = taurus_document_root(doc);

    /* Prepend three children */
    for (int i = 1; i <= 3; i++) {
        char name[16];
        snprintf(name, sizeof(name), "child%d", i);
        TaurusElement child = taurus_element_create(doc, name);
        taurus_element_prepend_child(root, child);
    }

    if (taurus_element_child_count(root) != 3) {
        FAIL("wrong child count");
        taurus_document_free(doc);
        return;
    }

    /* Verify order: child3, child2, child1 (reverse of insertion) */
    TaurusElement child0 = taurus_element_child(root, 0);
    const char* name0 = taurus_element_name(child0);
    if (!name0 || strcmp(name0, "child3") != 0) {
        FAIL("wrong order");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

/* ==================================================================
 * Insert Before Tests
 * ================================================================== */

void test_insert_before_first_child(void) {
    TEST("taurus_element_insert_before - before first child");

    TaurusDocument doc = taurus_parse_string("<root><child1/><child2/></root>", strlen("<root><child1/><child2/></root>"), NULL);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement first_child = taurus_element_child(root, 0);

    TaurusElement new_child = taurus_element_create(doc, "new");
    TaurusStatus status = taurus_element_insert_before(first_child, new_child);

    if (status != TAURUS_OK) {
        FAIL("insert_before returned error");
        taurus_document_free(doc);
        return;
    }

    if (taurus_element_child_count(root) != 3) {
        FAIL("wrong child count");
        taurus_document_free(doc);
        return;
    }

    /* Verify order: new, child1, child2 */
    TaurusElement child0 = taurus_element_child(root, 0);
    const char* name0 = taurus_element_name(child0);
    if (!name0 || strcmp(name0, "new") != 0) {
        FAIL("not inserted at correct position");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_insert_before_middle_child(void) {
    TEST("taurus_element_insert_before - before middle child");

    TaurusDocument doc = taurus_parse_string("<root><child1/><child2/><child3/></root>", strlen("<root><child1/><child2/><child3/></root>"), NULL);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement middle_child = taurus_element_child(root, 1);

    TaurusElement new_child = taurus_element_create(doc, "new");
    taurus_element_insert_before(middle_child, new_child);

    if (taurus_element_child_count(root) != 4) {
        FAIL("wrong child count");
        taurus_document_free(doc);
        return;
    }

    /* Verify order: child1, new, child2, child3 */
    TaurusElement child1 = taurus_element_child(root, 1);
    const char* name1 = taurus_element_name(child1);
    if (!name1 || strcmp(name1, "new") != 0) {
        FAIL("not inserted at correct position");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_insert_before_null_inputs(void) {
    TEST("taurus_element_insert_before - NULL inputs");

    TaurusDocument doc = taurus_parse_string("<root><child/></root>", strlen("<root><child/></root>"), NULL);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement child = taurus_element_child(root, 0);
    TaurusElement new_child = taurus_element_create(doc, "new");

    TaurusStatus status = taurus_element_insert_before(NULL, new_child);
    if (status != TAURUS_ERROR_NULL_ARG) {
        FAIL("should return NULL_ARG for NULL sibling");
        taurus_document_free(doc);
        return;
    }

    status = taurus_element_insert_before(child, NULL);
    if (status != TAURUS_ERROR_NULL_ARG) {
        FAIL("should return NULL_ARG for NULL new_node");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

/* ==================================================================
 * Insert After Tests
 * ================================================================== */

void test_insert_after_first_child(void) {
    TEST("taurus_element_insert_after - after first child");

    TaurusDocument doc = taurus_parse_string("<root><child1/><child2/></root>", strlen("<root><child1/><child2/></root>"), NULL);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement first_child = taurus_element_child(root, 0);

    TaurusElement new_child = taurus_element_create(doc, "new");
    TaurusStatus status = taurus_element_insert_after(first_child, new_child);

    if (status != TAURUS_OK) {
        FAIL("insert_after returned error");
        taurus_document_free(doc);
        return;
    }

    if (taurus_element_child_count(root) != 3) {
        FAIL("wrong child count");
        taurus_document_free(doc);
        return;
    }

    /* Verify order: child1, new, child2 */
    TaurusElement child1 = taurus_element_child(root, 1);
    const char* name1 = taurus_element_name(child1);
    if (!name1 || strcmp(name1, "new") != 0) {
        FAIL("not inserted at correct position");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_insert_after_last_child(void) {
    TEST("taurus_element_insert_after - after last child");

    TaurusDocument doc = taurus_parse_string("<root><child1/><child2/></root>", strlen("<root><child1/><child2/></root>"), NULL);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement last_child = taurus_element_child(root, 1);

    TaurusElement new_child = taurus_element_create(doc, "new");
    taurus_element_insert_after(last_child, new_child);

    if (taurus_element_child_count(root) != 3) {
        FAIL("wrong child count");
        taurus_document_free(doc);
        return;
    }

    /* Verify order: child1, child2, new */
    TaurusElement child2 = taurus_element_child(root, 2);
    const char* name2 = taurus_element_name(child2);
    if (!name2 || strcmp(name2, "new") != 0) {
        FAIL("not inserted at correct position");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_insert_after_null_inputs(void) {
    TEST("taurus_element_insert_after - NULL inputs");

    TaurusDocument doc = taurus_parse_string("<root><child/></root>", strlen("<root><child/></root>"), NULL);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement child = taurus_element_child(root, 0);
    TaurusElement new_child = taurus_element_create(doc, "new");

    TaurusStatus status = taurus_element_insert_after(NULL, new_child);
    if (status != TAURUS_ERROR_NULL_ARG) {
        FAIL("should return NULL_ARG for NULL sibling");
        taurus_document_free(doc);
        return;
    }

    status = taurus_element_insert_after(child, NULL);
    if (status != TAURUS_ERROR_NULL_ARG) {
        FAIL("should return NULL_ARG for NULL new_node");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

/* ==================================================================
 * Remove Child Tests
 * ================================================================== */

void test_remove_child_basic(void) {
    TEST("taurus_element_remove_child - basic removal");

    const char* xml = "<root><child1/><child2/></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TaurusElement root = taurus_document_root(doc);

    TaurusElement child = taurus_element_child(root, 0);
    TaurusStatus status = taurus_element_remove_child(root, child);

    if (status != TAURUS_OK) {
        FAIL("remove_child returned error");
        taurus_document_free(doc);
        return;
    }

    size_t count = taurus_element_child_count(root);
    if (count != 1) {
        FAIL("wrong child count after removal");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_remove_child_invalid(void) {
    TEST("taurus_element_remove_child - invalid child");

    const char* xml = "<root><child1/></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TaurusElement root = taurus_document_root(doc);

    TaurusElement other = taurus_element_create(doc, "other");
    TaurusStatus status = taurus_element_remove_child(root, other);

    if (status == TAURUS_OK) {
        FAIL("should return error for non-child element");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

/* ==================================================================
 * Set Text Tests
 * ================================================================== */

void test_set_text_basic(void) {
    TEST("taurus_element_set_text - basic text");

    const char* xml = "<root/>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TaurusElement root = taurus_document_root(doc);

    TaurusStatus status = taurus_element_set_text(root, "Hello World");
    if (status != TAURUS_OK) {
        FAIL("set_text returned error");
        taurus_document_free(doc);
        return;
    }

    const char* text = taurus_element_text(root);
    if (!text || strcmp(text, "Hello World") != 0) {
        FAIL("text not set correctly");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_set_text_replaces_children(void) {
    TEST("taurus_element_set_text - replaces existing children");

    const char* xml = "<root><child1/><child2/>Original text</root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TaurusElement root = taurus_document_root(doc);

    taurus_element_set_text(root, "New text");

    const char* text = taurus_element_text(root);
    if (!text || strcmp(text, "New text") != 0) {
        FAIL("text not replaced correctly");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

/* ==================================================================
 * Set Attribute tests
 * ================================================================== */

void test_set_attribute_new(void) {
    TEST("taurus_element_set_attribute - new attribute");

    const char* xml = "<root/>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TaurusElement root = taurus_document_root(doc);

    TaurusStatus status = taurus_element_set_attribute(root, "id", "123");
    if (status != TAURUS_OK) {
        FAIL("set_attribute returned error");
        taurus_document_free(doc);
        return;
    }

    const char* value = taurus_element_attribute(root, "id");
    if (!value || strcmp(value, "123") != 0) {
        FAIL("attribute not set correctly");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_set_attribute_update(void) {
    TEST("taurus_element_set_attribute - update existing");

    const char* xml = "<root id=\"old\"/>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TaurusElement root = taurus_document_root(doc);

    TaurusStatus status = taurus_element_set_attribute(root, "id", "new");
    if (status != TAURUS_OK) {
        FAIL("set_attribute returned error");
        taurus_document_free(doc);
        return;
    }

    const char* value = taurus_element_attribute(root, "id");
    if (!value || strcmp(value, "new") != 0) {
        FAIL("attribute not updated correctly");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_set_attribute_multiple(void) {
    TEST("taurus_element_set_attribute - multiple attributes");

    const char* xml = "<root/>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TaurusElement root = taurus_document_root(doc);

    taurus_element_set_attribute(root, "id", "1");
    taurus_element_set_attribute(root, "name", "test");
    taurus_element_set_attribute(root, "value", "abc");

    if (strcmp(taurus_element_attribute(root, "id"), "1") != 0 ||
        strcmp(taurus_element_attribute(root, "name"), "test") != 0 ||
        strcmp(taurus_element_attribute(root, "value"), "abc") != 0) {
        FAIL("not all attributes set correctly");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

/* ==================================================================
 * Remove Attribute Tests
 * ================================================================== */

void test_remove_attribute_exists(void) {
    TEST("taurus_element_remove_attribute - existing attribute");

    const char* xml = "<root id=\"123\" name=\"test\"/>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TaurusElement root = taurus_document_root(doc);

    TaurusStatus status = taurus_element_remove_attribute(root, "id");
    if (status != TAURUS_OK) {
        FAIL("remove_attribute returned error");
        taurus_document_free(doc);
        return;
    }

    const char* value = taurus_element_attribute(root, "id");
    if (value != NULL) {
        FAIL("attribute not removed");
        taurus_document_free(doc);
        return;
    }

    /* Other attribute should still exist */
    value = taurus_element_attribute(root, "name");
    if (!value || strcmp(value, "test") != 0) {
        FAIL("other attribute affected");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_remove_attribute_not_found(void) {
    TEST("taurus_element_remove_attribute - non-existent attribute");

    const char* xml = "<root id=\"123\"/>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TaurusElement root = taurus_document_root(doc);

    TaurusStatus status = taurus_element_remove_attribute(root, "nonexistent");
    if (status != TAURUS_ERROR_NOT_FOUND) {
        FAIL("should return NOT_FOUND error");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

/* ==================================================================
 * Remove All Attributes Tests
 * ================================================================== */

void test_remove_all_attributes_basic(void) {
    TEST("taurus_element_remove_all_attributes - basic removal");

    const char* xml = "<root id=\"123\" name=\"test\" value=\"abc\"/>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TaurusElement root = taurus_document_root(doc);

    TaurusStatus status = taurus_element_remove_all_attributes(root);
    if (status != TAURUS_OK) {
        FAIL("remove_all_attributes returned error");
        taurus_document_free(doc);
        return;
    }

    /* All attributes should be gone */
    if (taurus_element_attribute(root, "id") != NULL ||
        taurus_element_attribute(root, "name") != NULL ||
        taurus_element_attribute(root, "value") != NULL) {
        FAIL("attributes not removed");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_remove_all_attributes_empty(void) {
    TEST("taurus_element_remove_all_attributes - no attributes");

    const char* xml = "<root/>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TaurusElement root = taurus_document_root(doc);

    TaurusStatus status = taurus_element_remove_all_attributes(root);
    if (status != TAURUS_OK) {
        FAIL("should succeed even with no attributes");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

/* ==================================================================
 * Find Child Tests
 * ================================================================== */

void test_find_child_basic(void) {
    TEST("taurus_element_find_child - find existing child");

    const char* xml = "<root><child1/><child2/><child3/></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TaurusElement root = taurus_document_root(doc);

    TaurusElement found = taurus_element_find_child(root, "child2");
    if (!found) {
        FAIL("child not found");
        taurus_document_free(doc);
        return;
    }

    const char* name = taurus_element_name(found);
    if (!name || strcmp(name, "child2") != 0) {
        FAIL("wrong child returned");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_find_child_not_found(void) {
    TEST("taurus_element_find_child - non-existent child");

    const char* xml = "<root><child1/><child2/></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TaurusElement root = taurus_document_root(doc);

    TaurusElement found = taurus_element_find_child(root, "nonexistent");
    if (found != NULL) {
        FAIL("should return NULL for non-existent child");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_find_child_first_match(void) {
    TEST("taurus_element_find_child - returns first match");

    const char* xml = "<root><item>1</item><item>2</item><item>3</item></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TaurusElement root = taurus_document_root(doc);

    TaurusElement found = taurus_element_find_child(root, "item");
    if (!found) {
        FAIL("child not found");
        taurus_document_free(doc);
        return;
    }

    const char* text = taurus_element_text(found);
    if (!text || strcmp(text, "1") != 0) {
        FAIL("should return first match");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

/* ==================================================================
 * Find Child By Attribute Tests
 * ================================================================== */

void test_find_child_by_attr_basic(void) {
    TEST("taurus_element_find_child_by_attr - basic search");

    const char* xml = "<root><item id=\"1\"/><item id=\"2\"/><item id=\"3\"/></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TaurusElement root = taurus_document_root(doc);

    TaurusElement found = taurus_element_find_child_by_attr(root, "item", "id", "2");
    if (!found) {
        FAIL("child not found");
        taurus_document_free(doc);
        return;
    }

    const char* id = taurus_element_attribute(found, "id");
    if (!id || strcmp(id, "2") != 0) {
        FAIL("wrong child returned");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_find_child_by_attr_any_name(void) {
    TEST("taurus_element_find_child_by_attr - any child name");

    const char* xml = "<root><user id=\"123\"/><item id=\"456\"/></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TaurusElement root = taurus_document_root(doc);

    TaurusElement found = taurus_element_find_child_by_attr(root, NULL, "id", "456");
    if (!found) {
        FAIL("child not found");
        taurus_document_free(doc);
        return;
    }

    const char* name = taurus_element_name(found);
    if (!name || strcmp(name, "item") != 0) {
        FAIL("wrong child returned");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_find_child_by_attr_not_found(void) {
    TEST("taurus_element_find_child_by_attr - not found");

    const char* xml = "<root><item id=\"1\"/><item id=\"2\"/></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TaurusElement root = taurus_document_root(doc);

    TaurusElement found = taurus_element_find_child_by_attr(root, "item", "id", "999");
    if (found != NULL) {
        FAIL("should return NULL for non-matching attribute");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

/* ==================================================================
 * Next Sibling Tests
 * ================================================================== */

void test_next_sibling_with_name(void) {
    TEST("taurus_element_next_sibling - with name");

    const char* xml = "<root><item/><other/><item/><final/></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement first_item = taurus_element_child(root, 0);

    TaurusElement next_item = taurus_element_next_sibling(first_item, "item");
    if (!next_item) {
        FAIL("should find second item");
        taurus_document_free(doc);
        return;
    }

    const char* name = taurus_element_name(next_item);
    if (!name || strcmp(name, "item") != 0) {
        FAIL("wrong element returned");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_next_sibling_any(void) {
    TEST("taurus_element_next_sibling - any name (NULL)");

    TaurusDocument doc = taurus_parse_string(
        "<root><first/><second/><third/></root>", 38, NULL);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement first = taurus_element_child(root, 0);

    TaurusElement next = taurus_element_next_sibling(first, NULL);
    if (!next) {
        FAIL("should find next sibling");
        taurus_document_free(doc);
        return;
    }

    const char* name = taurus_element_name(next);
    if (!name || strcmp(name, "second") != 0) {
        FAIL("wrong element returned");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_next_sibling_not_found(void) {
    TEST("taurus_element_next_sibling - not found");

    TaurusDocument doc = taurus_parse_string(
        "<root><item/><other/></root>", 28, NULL);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement item = taurus_element_child(root, 0);

    TaurusElement next = taurus_element_next_sibling(item, "missing");
    if (next != NULL) {
        FAIL("should return NULL when not found");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

/* ==================================================================
 * Previous Sibling Tests
 * ================================================================== */

void test_previous_sibling_with_name(void) {
    TEST("taurus_element_previous_sibling - with name");

    const char* xml = "<root><item/><other/><item/><final/></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement last_item = taurus_element_child(root, 2);

    TaurusElement prev_item = taurus_element_previous_sibling(last_item, "item");
    if (!prev_item) {
        FAIL("should find first item");
        taurus_document_free(doc);
        return;
    }

    const char* name = taurus_element_name(prev_item);
    if (!name || strcmp(name, "item") != 0) {
        FAIL("wrong element returned");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_previous_sibling_any(void) {
    TEST("taurus_element_previous_sibling - any name (NULL)");

    const char* xml = "<root><first/><second/><third/></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement third = taurus_element_child(root, 2);

    TaurusElement prev = taurus_element_previous_sibling(third, NULL);
    if (!prev) {
        FAIL("should find previous sibling");
        taurus_document_free(doc);
        return;
    }

    const char* name = taurus_element_name(prev);
    if (!name || strcmp(name, "second") != 0) {
        FAIL("wrong element returned");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_previous_sibling_not_found(void) {
    TEST("taurus_element_previous_sibling - not found");

    TaurusDocument doc = taurus_parse_string(
        "<root><other/><item/></root>", 28, NULL);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement item = taurus_element_child(root, 1);

    TaurusElement prev = taurus_element_previous_sibling(item, "missing");
    if (prev != NULL) {
        FAIL("should return NULL when not found");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

/* ==================================================================
 * First Child Tests
 * ================================================================== */

void test_first_child_with_name(void) {
    TEST("taurus_element_first_child - with name");

    TaurusDocument doc = taurus_parse_string(
        "<root><other/><item/><item/></root>", 35, NULL);
    TaurusElement root = taurus_document_root(doc);

    TaurusElement first_item = taurus_element_first_child(root, "item");
    if (!first_item) {
        FAIL("should find first item");
        taurus_document_free(doc);
        return;
    }

    /* Verify it's the second child (index 1) */
    TaurusElement second_child = taurus_element_child(root, 1);
    if (first_item != second_child) {
        FAIL("should return correct child");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_first_child_any(void) {
    TEST("taurus_element_first_child - any name (NULL)");

    TaurusDocument doc = taurus_parse_string(
        "<root><first/><second/><third/></root>", 38, NULL);
    TaurusElement root = taurus_document_root(doc);

    TaurusElement first = taurus_element_first_child(root, NULL);
    if (!first) {
        FAIL("should find first child");
        taurus_document_free(doc);
        return;
    }

    const char* name = taurus_element_name(first);
    if (!name || strcmp(name, "first") != 0) {
        FAIL("wrong element returned");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_first_child_not_found(void) {
    TEST("taurus_element_first_child - not found");

    TaurusDocument doc = taurus_parse_string(
        "<root><other/><another/></root>", 31, NULL);
    TaurusElement root = taurus_document_root(doc);

    TaurusElement first = taurus_element_first_child(root, "missing");
    if (first != NULL) {
        FAIL("should return NULL when not found");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_first_child_empty_parent(void) {
    TEST("taurus_element_first_child - empty parent");

    TaurusDocument doc = taurus_parse_string("<root/>", strlen("<root/>"), NULL);
    TaurusElement root = taurus_document_root(doc);

    TaurusElement first = taurus_element_first_child(root, "item");
    if (first != NULL) {
        FAIL("should return NULL for empty parent");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

/* ==================================================================
 * Last Child Tests
 * ================================================================== */

void test_last_child_with_name(void) {
    TEST("taurus_element_last_child - with name");

    TaurusDocument doc = taurus_parse_string(
        "<root><item/><item/><other/></root>", 35, NULL);
    TaurusElement root = taurus_document_root(doc);

    TaurusElement last_item = taurus_element_last_child(root, "item");
    if (!last_item) {
        FAIL("should find last item");
        taurus_document_free(doc);
        return;
    }

    /* Verify it's the second child (index 1), not the third */
    TaurusElement second_child = taurus_element_child(root, 1);
    if (last_item != second_child) {
        FAIL("should return correct child");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_last_child_any(void) {
    TEST("taurus_element_last_child - any name (NULL)");

    TaurusDocument doc = taurus_parse_string(
        "<root><first/><second/><third/></root>", 38, NULL);
    TaurusElement root = taurus_document_root(doc);

    TaurusElement last = taurus_element_last_child(root, NULL);
    if (!last) {
        FAIL("should find last child");
        taurus_document_free(doc);
        return;
    }

    const char* name = taurus_element_name(last);
    if (!name || strcmp(name, "third") != 0) {
        FAIL("wrong element returned");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_last_child_not_found(void) {
    TEST("taurus_element_last_child - not found");

    TaurusDocument doc = taurus_parse_string(
        "<root><other/><another/></root>", 31, NULL);
    TaurusElement root = taurus_document_root(doc);

    TaurusElement last = taurus_element_last_child(root, "missing");
    if (last != NULL) {
        FAIL("should return NULL when not found");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_last_child_empty_parent(void) {
    TEST("taurus_element_last_child - empty parent");

    TaurusDocument doc = taurus_parse_string("<root/>", strlen("<root/>"), NULL);
    TaurusElement root = taurus_document_root(doc);

    TaurusElement last = taurus_element_last_child(root, "item");
    if (last != NULL) {
        FAIL("should return NULL for empty parent");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

/* ==================================================================
 * Set Name Tests
 * ================================================================== */

void test_set_name_basic(void) {
    TEST("taurus_element_set_name - basic rename");

    TaurusDocument doc = taurus_parse_string("<old>text</old>", strlen("<old>text</old>"), NULL);
    TaurusElement root = taurus_document_root(doc);

    const char* old_name = taurus_element_name(root);
    if (!old_name || strcmp(old_name, "old") != 0) {
        FAIL("initial name wrong");
        taurus_document_free(doc);
        return;
    }

    TaurusStatus status = taurus_element_set_name(root, "new");
    if (status != TAURUS_OK) {
        FAIL("set_name returned error");
        taurus_document_free(doc);
        return;
    }

    const char* new_name = taurus_element_name(root);
    if (!new_name || strcmp(new_name, "new") != 0) {
        FAIL("name not changed");
        taurus_document_free(doc);
        return;
    }

    /* Verify text preserved */
    const char* text = taurus_element_text(root);
    if (!text || strcmp(text, "text") != 0) {
        FAIL("text not preserved");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_set_name_preserves_attributes(void) {
    TEST("taurus_element_set_name - preserves attributes");

    const char* xml = "<old id=\"123\">text</old>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TaurusElement root = taurus_document_root(doc);

    taurus_element_set_name(root, "new");

    const char* id = taurus_element_attribute(root, "id");
    if (!id || strcmp(id, "123") != 0) {
        FAIL("attribute not preserved");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_set_name_error_handling(void) {
    TEST("taurus_element_set_name - error handling");

    TaurusDocument doc = taurus_parse_string("<root/>", strlen("<root/>"), NULL);
    TaurusElement root = taurus_document_root(doc);

    TaurusStatus status = taurus_element_set_name(NULL, "new");
    if (status != TAURUS_ERROR_NULL_ARG) {
        FAIL("should return NULL_ARG for NULL element");
        taurus_document_free(doc);
        return;
    }

    status = taurus_element_set_name(root, NULL);
    if (status != TAURUS_ERROR_NULL_ARG) {
        FAIL("should return NULL_ARG for NULL name");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_set_name_with_namespaces(void) {
    TEST("taurus_element_set_name - with namespaces");

    const char* xml = "<ns:old xmlns:ns=\"http://example.com\"/>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TaurusElement root = taurus_document_root(doc);

    taurus_element_set_name(root, "ns:new");
    const char* name = taurus_element_name(root);
    if (!name || strcmp(name, "ns:new") != 0) {
        FAIL("name not changed");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

/* ==================================================================
 * Append Copy Tests
 * ================================================================== */

void test_append_copy_simple(void) {
    TEST("taurus_element_append_copy - simple element");

    const char* xml = "<root><template id=\"t1\">text</template></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement template = taurus_element_find_child(root, "template");

    TaurusElement copy = taurus_element_append_copy(root, template);
    if (!copy) {
        FAIL("append_copy returned NULL");
        taurus_document_free(doc);
        return;
    }

    /* Verify copy exists */
    if (taurus_element_child_count(root) != 2) {
        FAIL("wrong child count");
        taurus_document_free(doc);
        return;
    }

    /* Verify copy has same structure */
    const char* name = taurus_element_name(copy);
    if (!name || strcmp(name, "template") != 0) {
        FAIL("copy has wrong name");
        taurus_document_free(doc);
        return;
    }

    const char* id = taurus_element_attribute(copy, "id");
    if (!id || strcmp(id, "t1") != 0) {
        FAIL("copy missing attribute");
        taurus_document_free(doc);
        return;
    }

    const char* text = taurus_element_text(copy);
    if (!text || strcmp(text, "text") != 0) {
        FAIL("copy missing text");
        taurus_document_free(doc);
        return;
    }

    /* Verify original unchanged */
    const char* orig_id = taurus_element_attribute(template, "id");
    if (!orig_id || strcmp(orig_id, "t1") != 0) {
        FAIL("original modified");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_append_copy_nested(void) {
    TEST("taurus_element_append_copy - with nested children");

    const char* xml = "<root><template><child1/><child2/></template></root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement template = taurus_element_find_child(root, "template");

    TaurusElement copy = taurus_element_append_copy(root, template);
    if (!copy) {
        FAIL("append_copy returned NULL");
        taurus_document_free(doc);
        return;
    }

    /* Verify children copied */
    if (taurus_element_child_count(copy) != 2) {
        FAIL("children not copied");
        taurus_document_free(doc);
        return;
    }

    TaurusElement child1 = taurus_element_find_child(copy, "child1");
    if (!child1) {
        FAIL("child1 not found in copy");
        taurus_document_free(doc);
        return;
    }

    TaurusElement child2 = taurus_element_find_child(copy, "child2");
    if (!child2) {
        FAIL("child2 not found in copy");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_append_copy_attributes(void) {
    TEST("taurus_element_append_copy - preserves attributes");

    TaurusDocument doc = taurus_parse_string(
        "<root><template id=\"t1\" class=\"test\"/></root>", 45, NULL);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement template = taurus_element_find_child(root, "template");

    TaurusElement copy = taurus_element_append_copy(root, template);

    const char* id = taurus_element_attribute(copy, "id");
    if (!id || strcmp(id, "t1") != 0) {
        FAIL("id attribute not copied");
        taurus_document_free(doc);
        return;
    }

    const char* class = taurus_element_attribute(copy, "class");
    if (!class || strcmp(class, "test") != 0) {
        FAIL("class attribute not copied");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_append_copy_error_handling(void) {
    TEST("taurus_element_append_copy - error handling");

    TaurusDocument doc = taurus_parse_string("<root><child/></root>", strlen("<root><child/></root>"), NULL);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement child = taurus_element_find_child(root, "child");

    TaurusElement copy = taurus_element_append_copy(NULL, child);
    if (copy != NULL) {
        FAIL("should return NULL for NULL parent");
        taurus_document_free(doc);
        return;
    }

    copy = taurus_element_append_copy(root, NULL);
    if (copy != NULL) {
        FAIL("should return NULL for NULL source");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_append_copy_cross_document(void) {
    TEST("taurus_element_append_copy - from different document");

    const char* xml1 = "<root><item id=\"1\"/></root>";
    TaurusDocument doc1 = taurus_parse_string(xml1, strlen(xml1), NULL);
    TaurusDocument doc2 = taurus_parse_string("<target/>", strlen("<target/>"), NULL);

    TaurusElement item = taurus_element_find_child(taurus_document_root(doc1), "item");
    TaurusElement target = taurus_document_root(doc2);

    TaurusElement copy = taurus_element_append_copy(target, item);
    if (!copy) {
        FAIL("cross-document copy failed");
        taurus_document_free(doc1);
        taurus_document_free(doc2);
        return;
    }

    const char* name = taurus_element_name(copy);
    if (!name || strcmp(name, "item") != 0) {
        FAIL("copy has wrong name");
        taurus_document_free(doc1);
        taurus_document_free(doc2);
        return;
    }

    const char* id = taurus_element_attribute(copy, "id");
    if (!id || strcmp(id, "1") != 0) {
        FAIL("copy missing attribute");
        taurus_document_free(doc1);
        taurus_document_free(doc2);
        return;
    }

    taurus_document_free(doc1);
    taurus_document_free(doc2);
    PASS();
}

void test_append_copy_deep(void) {
    TEST("taurus_element_append_copy - deep recursion");

    TaurusDocument doc = taurus_parse_string(
        "<root><a><b><c>deep</c></b></a></root>", 38, NULL);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement a = taurus_element_find_child(root, "a");

    TaurusElement copy = taurus_element_append_copy(root, a);
    if (!copy) {
        FAIL("append_copy returned NULL");
        taurus_document_free(doc);
        return;
    }

    /* Verify deep structure copied */
    TaurusElement b = taurus_element_find_child(copy, "b");
    if (!b) {
        FAIL("b element not found in copy");
        taurus_document_free(doc);
        return;
    }

    TaurusElement c = taurus_element_find_child(b, "c");
    if (!c) {
        FAIL("c element not found in copy");
        taurus_document_free(doc);
        return;
    }

    const char* text = taurus_element_text(c);
    if (!text || strcmp(text, "deep") != 0) {
        FAIL("deep text not preserved");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

/* ==================================================================
 * Prepend Copy Tests
 * ================================================================== */

void test_prepend_copy_simple(void) {
    TEST("taurus_element_prepend_copy - simple element");

    TaurusDocument doc = taurus_parse_string("<root><existing/></root>", strlen("<root><existing/></root>"), NULL);
    TaurusElement root = taurus_document_root(doc);

    /* Create template to copy */
    TaurusElement template = taurus_element_create(doc, "template");
    taurus_element_set_attribute(template, "id", "t1");
    taurus_element_append_child(root, template);

    /* Prepend copy */
    TaurusElement copy = taurus_element_prepend_copy(root, template);
    if (!copy) {
        FAIL("prepend_copy returned NULL");
        taurus_document_free(doc);
        return;
    }

    /* Verify copy is first child */
    if (taurus_element_child_count(root) != 3) {
        FAIL("wrong child count");
        taurus_document_free(doc);
        return;
    }

    TaurusElement first = taurus_element_child(root, 0);
    if (first != copy) {
        FAIL("copy not first child");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_prepend_copy_nested(void) {
    TEST("taurus_element_prepend_copy - with nested children");

    TaurusDocument doc = taurus_parse_string(
        "<root><existing/><template><child1/><child2/></template></root>", 63, NULL);
    TaurusElement root =

 taurus_document_root(doc);
    TaurusElement template = taurus_element_find_child(root, "template");

    TaurusElement copy = taurus_element_prepend_copy(root, template);
    if (!copy) {
        FAIL("prepend_copy returned NULL");
        taurus_document_free(doc);
        return;
    }

    /* Verify children copied */
    if (taurus_element_child_count(copy) != 2) {
        FAIL("children not copied");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_prepend_copy_error_handling(void) {
    TEST("taurus_element_prepend_copy - error handling");

    TaurusDocument doc = taurus_parse_string("<root><child/></root>", strlen("<root><child/></root>"), NULL);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement child = taurus_element_find_child(root, "child");

    TaurusElement copy = taurus_element_prepend_copy(NULL, child);
    if (copy != NULL) {
        FAIL("should return NULL for NULL parent");
        taurus_document_free(doc);
        return;
    }

    copy = taurus_element_prepend_copy(root, NULL);
    if (copy != NULL) {
        FAIL("should return NULL for NULL source");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

/* ==================================================================
 * Insert Copy Before Tests
 * ================================================================== */

void test_insert_copy_before_simple(void) {
    TEST("taurus_element_insert_copy_before - simple element");

    TaurusDocument doc = taurus_parse_string("<root><sibling/></root>", strlen("<root><sibling/></root>"), NULL);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement sibling = taurus_element_find_child(root, "sibling");

    /* Create template to copy */
    TaurusElement template = taurus_element_create(doc, "template");
    taurus_element_set_attribute(template, "id", "t1");
    taurus_element_append_child(root, template);

    /* Insert copy before sibling */
    TaurusElement copy = taurus_element_insert_copy_before(sibling, template);
    if (!copy) {
        FAIL("insert_copy_before returned NULL");
        taurus_document_free(doc);
        return;
    }

    /* Verify copy is before sibling */
    TaurusElement first = taurus_element_child(root, 0);
    if (first != copy) {
        FAIL("copy not inserted before sibling");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_insert_copy_before_nested(void) {
    TEST("taurus_element_insert_copy_before - with nested children");

    TaurusDocument doc = taurus_parse_string(
        "<root><sibling/><template><child1/><child2/></template></root>", 62, NULL);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement sibling = taurus_element_find_child(root, "sibling");
    TaurusElement template = taurus_element_find_child(root, "template");

    TaurusElement copy = taurus_element_insert_copy_before(sibling, template);
    if (!copy) {
        FAIL("insert_copy_before returned NULL");
        taurus_document_free(doc);
        return;
    }

    /* Verify children copied */
    if (taurus_element_child_count(copy) != 2) {
        FAIL("children not copied");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_insert_copy_before_error_handling(void) {
    TEST("taurus_element_insert_copy_before - error handling");

    TaurusDocument doc = taurus_parse_string("<root><child/></root>", strlen("<root><child/></root>"), NULL);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement child = taurus_element_find_child(root, "child");

    TaurusElement copy = taurus_element_insert_copy_before(NULL, child);
    if (copy != NULL) {
        FAIL("should return NULL for NULL sibling");
        taurus_document_free(doc);
        return;
    }

    copy = taurus_element_insert_copy_before(child, NULL);
    if (copy != NULL) {
        FAIL("should return NULL for NULL source");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

/* ==================================================================
 * Insert Copy After Tests
 * ================================================================== */

void test_insert_copy_after_simple(void) {
    TEST("taurus_element_insert_copy_after - simple element");

    TaurusDocument doc = taurus_parse_string("<root><sibling/></root>", strlen("<root><sibling/></root>"), NULL);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement sibling = taurus_element_find_child(root, "sibling");

    /* Create template to copy */
    TaurusElement template = taurus_element_create(doc, "template");
    taurus_element_set_attribute(template, "id", "t1");
    taurus_element_append_child(root, template);

    /* Insert copy after sibling */
    TaurusElement copy = taurus_element_insert_copy_after(sibling, template);
    if (!copy) {
        FAIL("insert_copy_after returned NULL");
        taurus_document_free(doc);
        return;
    }

    /* Verify copy is after sibling */
    TaurusElement second = taurus_element_child(root, 1);
    if (second != copy) {
        FAIL("copy not inserted after sibling");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_insert_copy_after_nested(void) {
    TEST("taurus_element_insert_copy_after - with nested children");

    TaurusDocument doc = taurus_parse_string(
        "<root><sibling/><template><child1/><child2/></template></root>", 62, NULL);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement sibling = taurus_element_find_child(root, "sibling");
    TaurusElement template = taurus_element_find_child(root, "template");

    TaurusElement copy = taurus_element_insert_copy_after(sibling, template);
    if (!copy) {
        FAIL("insert_copy_after returned NULL");
        taurus_document_free(doc);
        return;
    }

    /* Verify children copied */
    if (taurus_element_child_count(copy) != 2) {
        FAIL("children not copied");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

void test_insert_copy_after_error_handling(void) {
    TEST("taurus_element_insert_copy_after - error handling");

    TaurusDocument doc = taurus_parse_string("<root><child/></root>", strlen("<root><child/></root>"), NULL);
    TaurusElement root = taurus_document_root(doc);
    TaurusElement child = taurus_element_find_child(root, "child");

    TaurusElement copy = taurus_element_insert_copy_after(NULL, child);
    if (copy != NULL) {
        FAIL("should return NULL for NULL sibling");
        taurus_document_free(doc);
        return;
    }

    copy = taurus_element_insert_copy_after(child, NULL);
    if (copy != NULL) {
        FAIL("should return NULL for NULL source");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}


/* ==================================================================
 * Complex Scenarios
 * ================================================================== */

void test_complex_tree_modification(void) {
    TEST("complex tree modification");

    /* Create a complex tree with multiple levels of children */
    const char* xml = "<root>"
                      "<a><a1/><a2/></a>"
                      "<b><b1/><b2><b2a/><b2b/></b2></b>"
                      "<c><c1/><c2/><c3/></c>"
                      "<d><d1/><d2><d2a/></d2></d>"
                      "<e/>"
                      "</root>";
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    TaurusElement root = taurus_document_root(doc);

    /* Total Nodes Check - 5 direct children (a, b, c, d, e) */
    size_t total_nodes = taurus_element_child_count(root);
    if (total_nodes != 5) {
        char msg[100];
        snprintf(msg, sizeof(msg), "wrong total node count: expected 5, got %zu", total_nodes);
        FAIL(msg);
        taurus_document_free(doc);
        return;
    }

    /* Verify nested structure */
    TaurusElement a = taurus_element_find_child(root, "a");
    if (!a || taurus_element_child_count(a) != 2) {
        FAIL("wrong child count for <a>");
        taurus_document_free(doc);
        return;
    }

    TaurusElement b = taurus_element_find_child(root, "b");
    if (!b || taurus_element_child_count(b) != 2) {
        FAIL("wrong child count for <b>");
        taurus_document_free(doc);
        return;
    }

    taurus_document_free(doc);
    PASS();
}

/* ==================================================================
 * MAIN TEST RUNNER
 * ================================================================== */

int main(void) {
    printf("\n");
    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║           DOM Modification API Test Suite                ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n");
    printf("\n");

    printf("Element Creation Tests:\n");
    test_element_create_basic();
    test_element_create_null_inputs();
    printf("\n");

    printf("Append Child Tests:\n");
    test_append_child_basic();
    test_append_child_multiple();
    printf("\n");

    printf("Prepend Child Tests:\n");
    test_prepend_child_empty_parent();
    test_prepend_child_with_existing();
    test_prepend_child_multiple();
    printf("\n");

    printf("Insert Before Tests:\n");
    test_insert_before_first_child();
    test_insert_before_middle_child();
    test_insert_before_null_inputs();
    printf("\n");

    printf("Insert After Tests:\n");
    test_insert_after_first_child();
    test_insert_after_last_child();
    test_insert_after_null_inputs();
    printf("\n");

    printf("Remove Child Tests:\n");
    test_remove_child_basic();
    test_remove_child_invalid();
    printf("\n");

    printf("Set Text Tests:\n");
    test_set_text_basic();
    test_set_text_replaces_children();
    printf("\n");

    printf("Set Attribute Tests:\n");
    test_set_attribute_new();
    test_set_attribute_update();
    test_set_attribute_multiple();
    printf("\n");

    printf("Remove Attribute Tests:\n");
    test_remove_attribute_exists();
    test_remove_attribute_not_found();
    printf("\n");

    printf("Remove All Attributes Tests:\n");
    test_remove_all_attributes_basic();
    test_remove_all_attributes_empty();
    printf("\n");

    printf("Find Child Tests:\n");
    test_find_child_basic();
    test_find_child_not_found();
    test_find_child_first_match();
    printf("\n");

    printf("Find Child By Attribute Tests:\n");
    test_find_child_by_attr_basic();
    test_find_child_by_attr_any_name();
    test_find_child_by_attr_not_found();
    printf("\n");

    printf("Next Sibling Tests:\n");
    test_next_sibling_with_name();
    test_next_sibling_any();
    test_next_sibling_not_found();
    printf("\n");

    printf("Previous Sibling Tests:\n");
    test_previous_sibling_with_name();
    test_previous_sibling_any();
    test_previous_sibling_not_found();
    printf("\n");

    printf("First Child Tests:\n");
    test_first_child_with_name();
    test_first_child_any();
    test_first_child_not_found();
    test_first_child_empty_parent();
    printf("\n");

    printf("Last Child Tests:\n");
    test_last_child_with_name();
    test_last_child_any();
    test_last_child_not_found();
    test_last_child_empty_parent();
    printf("\n");

    printf("Set Name Tests:\n");
    test_set_name_basic();
    test_set_name_preserves_attributes();
    test_set_name_error_handling();
    test_set_name_with_namespaces();
    printf("\n");

    printf("Append Copy Tests:\n");
    test_append_copy_simple();
    test_append_copy_nested();
    test_append_copy_attributes();
    test_append_copy_error_handling();
    test_append_copy_cross_document();
    test_append_copy_deep();
    printf("\n");

    printf("\n");



    printf("Prepend Copy Tests:\n");

    test_prepend_copy_simple();

    test_prepend_copy_nested();

    test_prepend_copy_error_handling();

    printf("\n");



    printf("Insert Copy Before Tests:\n");

    test_insert_copy_before_simple();

    test_insert_copy_before_nested();

    test_insert_copy_before_error_handling();

    printf("\n");



    printf("Insert Copy After Tests:\n");

    test_insert_copy_after_simple();

    test_insert_copy_after_nested();

    test_insert_copy_after_error_handling();

    printf("Complex Scenarios:\n");
    test_complex_tree_modification();
    printf("\n");

    /* Summary */
    printf("╔═══════════════════════════════════════════════════════════╗\n");
    printf("║                      Test Results                         ║\n");
    printf("╚═══════════════════════════════════════════════════════════╝\n");
    printf("\n");
    printf("  Passed:  %2d / %2d\n", tests_passed, tests_passed + tests_failed);
    printf("  Failed:  %2d / %2d\n", tests_failed, tests_passed + tests_failed);
    printf("\n");

    if (tests_failed == 0) {
        printf("  ✓ All tests passed!\n\n");
        return 0;
    } else {
        printf("  ✗ Some tests failed.\n\n");
        return 1;
    }}
