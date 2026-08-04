// test/dom/test_dom.cpp — DOM creation/traversal/modification specs.

#include <gtest/gtest.h>

#include "taurus.h"

#include <cstring>

namespace {

// Public API documents these as integers (see taurus_node_get_type docs).
constexpr int kNodeTypeElement  = 0;
constexpr int kNodeTypeText     = 1;
constexpr int kNodeTypeComment  = 2;
constexpr int kNodeTypeCDATA    = 3;
constexpr int kNodeTypePI       = 4;
constexpr int kNodeTypeDoctype  = 5;
constexpr int kNodeTypeAttribute = 6;

TEST(DomBasics, EmptyDocumentRoundTrips) {
    const char xml[] = "<r/>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    ASSERT_NE(root, nullptr);
    EXPECT_STREQ(taurus_element_name(root), "r");
    taurus_document_free(doc);
}

TEST(DomBasics, AttributeLookupByName) {
    const char xml[] = "<r a='1' b='2' c='3'/>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);

    EXPECT_STREQ(taurus_element_attribute(root, "a"), "1");
    EXPECT_STREQ(taurus_element_attribute(root, "b"), "2");
    EXPECT_STREQ(taurus_element_attribute(root, "c"), "3");
    EXPECT_EQ(taurus_element_attribute(root, "missing"), nullptr);

    taurus_document_free(doc);
}

TEST(DomBasics, TraversesChildrenInDocumentOrder) {
    const char xml[] = "<r><a/><b/><c/></r>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);

    TaurusElement child = taurus_element_first_child_any(root);
    ASSERT_NE(child, nullptr);
    EXPECT_STREQ(taurus_element_name(child), "a");

    child = taurus_element_next_sibling_any(child);
    ASSERT_NE(child, nullptr);
    EXPECT_STREQ(taurus_element_name(child), "b");

    child = taurus_element_next_sibling_any(child);
    ASSERT_NE(child, nullptr);
    EXPECT_STREQ(taurus_element_name(child), "c");

    child = taurus_element_next_sibling_any(child);
    EXPECT_EQ(child, nullptr);

    taurus_document_free(doc);
}

TEST(DomBasics, NodeRefTraversalCoversAllNodeTypes) {
    // TaurusNodeRef traversal exposes every child regardless of type;
    // TaurusElement-only traversal skips text/comment/cdata siblings.
    const char xml[] = "<r><!--c-->text<x/></r>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);

    TaurusNodeRef n = taurus_node_first_child(taurus_element_as_node(root));
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(taurus_node_get_type(n), kNodeTypeComment);

    n = taurus_node_next_sibling(n);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(taurus_node_get_type(n), kNodeTypeText);

    n = taurus_node_next_sibling(n);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(taurus_node_get_type(n), kNodeTypeElement);

    taurus_document_free(doc);
}

}  // namespace

// ---- Freeze API (TODO 88) ------------------------------------------------

TEST(DocumentFreeze, FreshDocumentIsFrozenAfterParse) {
    /* The parser calls taurus_document_freeze_tree internally. */
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<root><child/></root>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(taurus_document_is_frozen(doc), 1);
    taurus_document_free(doc);
}

TEST(DocumentFreeze, ExplicitFreezeSetsFlag) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<root/>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    /* Already frozen by parser, but explicit freeze should still work. */
    EXPECT_EQ(taurus_document_freeze(doc), TAURUS_OK);
    EXPECT_EQ(taurus_document_is_frozen(doc), 1);
    taurus_document_free(doc);
}

TEST(DocumentFreeze, NullDocReturnsSafe) {
    EXPECT_EQ(taurus_document_freeze(nullptr), TAURUS_ERROR_NULL_ARG);
    EXPECT_EQ(taurus_document_is_frozen(nullptr), 0);
}

// The freeze flag is advisory only — it does NOT cause mutations to
// reject.  See taurus.h:taurus_document_freeze for the rationale.
// This spec pins the current contract so future "freeze = read-only"
// enforcement has to update the spec deliberately.
TEST(DocumentFreeze, MutationsSucceedOnFrozenDocument) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<root><child/></root>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    ASSERT_EQ(taurus_document_is_frozen(doc), 1);

    /* These mutation calls succeed even though the doc is frozen.
     * If we ever want true read-only enforcement, this is the spec
     * that would need to flip. */
    TaurusElement root = taurus_document_root(doc);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(taurus_element_set_attribute(root, "x", "1"), TAURUS_OK);
    EXPECT_EQ(taurus_element_set_name(root, "renamed"), TAURUS_OK);
    EXPECT_STREQ(taurus_element_attribute(root, "x"), "1");
    EXPECT_STREQ(taurus_element_name(root), "renamed");

    /* The flag is still set — mutating didn't unfreeze. */
    EXPECT_EQ(taurus_document_is_frozen(doc), 1);

    taurus_document_free(doc);
}

// ---- Node content accessors (TODO: specs coverage) -----------------------

TEST(NodeContentAccessors, CdataNodeReturnsContent) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r><![CDATA[raw <content> & stuff]]></r>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    TaurusNodeRef child = taurus_node_first_child((TaurusNodeRef)root);
    ASSERT_NE(child, nullptr);

    const char* cdata = taurus_cdata_node_get_content(child);
    EXPECT_NE(cdata, nullptr);
    EXPECT_STREQ(cdata, "raw <content> & stuff");

    taurus_document_free(doc);
}

TEST(NodeContentAccessors, CommentNodeReturnsContent) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r><!-- a comment --></r>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    TaurusNodeRef child = taurus_node_first_child((TaurusNodeRef)root);
    ASSERT_NE(child, nullptr);

    const char* comment = taurus_comment_node_get_content(child);
    EXPECT_NE(comment, nullptr);
    EXPECT_STREQ(comment, " a comment ");

    taurus_document_free(doc);
}

// ---- Attribute type accessors -------------------------------------------

TEST(AttributeAccessors, IntAttributeReturnsValue) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r count='42'/>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    EXPECT_EQ(taurus_element_attribute_int(root, "count", 0), 42);
    EXPECT_EQ(taurus_element_attribute_int(root, "missing", 99), 99);
    taurus_document_free(doc);
}

TEST(AttributeAccessors, DoubleAttributeReturnsValue) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r pi='3.14'/>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    EXPECT_DOUBLE_EQ(taurus_element_attribute_double(root, "pi", 0.0), 3.14);
    taurus_document_free(doc);
}

// ---- Text content type accessors ----------------------------------------

TEST(TextContentAccessors, IntTextReturnsValue) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r>123</r>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    EXPECT_EQ(taurus_element_text_int(root, 0), 123);
    EXPECT_EQ(taurus_element_text_int(nullptr, -1), -1);
    taurus_document_free(doc);
}

TEST(TextContentAccessors, DoubleTextReturnsValue) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r>45.67</r>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    EXPECT_DOUBLE_EQ(taurus_element_text_double(root, 0.0), 45.67);
    taurus_document_free(doc);
}

// taurus_element_text promises document-owned storage ("String is owned by
// element"), so neither path may hand back a malloc'd buffer — ASAN/LSan in CI
// is what enforces this.
TEST(TextContentAccessors, TextIsDocumentOwnedForSingleTextChild) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r>plain</r>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);

    const char* first = taurus_element_text(root);
    EXPECT_STREQ(first, "plain");
    /* A lone text child needs no concatenation, so repeated calls return the
     * text node's own storage — same pointer, no allocation at all. */
    EXPECT_EQ(taurus_element_text(root), first);

    taurus_document_free(doc);
}

TEST(TextContentAccessors, TextIsDocumentOwnedForMixedContent) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r>a<b>c</b>d</r>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);

    /* Mixed content is concatenated into the document pool; it must stay
     * readable until taurus_document_free and must not leak. */
    const char* text = taurus_element_text(root);
    EXPECT_STREQ(text, "acd");

    taurus_document_free(doc);
}

TEST(TextContentAccessors, EmptyElementYieldsEmptyString) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r/>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    EXPECT_STREQ(taurus_element_text(root), "");
    EXPECT_STREQ(taurus_element_text(nullptr), "");
    taurus_document_free(doc);
}

// ---- Serialize document ---------------------------------------------------

TEST(SerializeDocument, RoundTripPreservesStructure) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<root><child>text</child></root>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* output = taurus_document_serialize(doc, nullptr);
    EXPECT_NE(output, nullptr);
    if (output) {
        /* The serialized output should contain the element names. */
        std::string s(output);
        EXPECT_NE(s.find("root"), std::string::npos);
        EXPECT_NE(s.find("child"), std::string::npos);
        EXPECT_NE(s.find("text"), std::string::npos);
        taurus_free_string(output);
    }
    taurus_document_free(doc);
}

// ---- Element mutation -----------------------------------------------------

TEST(ElementMutation, AppendChildMovesElement) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r><a/><b/></r>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    TaurusElement a = taurus_element_first_child_any(root);
    ASSERT_NE(a, nullptr);
    EXPECT_STREQ(taurus_element_name(a), "a");

    TaurusElement b = taurus_element_next_sibling_any(a);
    ASSERT_NE(b, nullptr);
    EXPECT_STREQ(taurus_element_name(b), "b");

    /* Move b under a. */
    TaurusStatus rc = taurus_element_append_child(a, b);
    EXPECT_EQ(rc, TAURUS_OK);

    /* a should now have b as a child. */
    TaurusElement child = taurus_element_first_child_any(a);
    ASSERT_NE(child, nullptr);
    EXPECT_STREQ(taurus_element_name(child), "b");

    taurus_document_free(doc);
}

// ---- Element mutation: comprehensive coverage ---------------------------
//
// The element-modification API surface (set_name, set_text, set_attribute
// and its typed variants, remove_attribute, append/prepend/insert/remove)
// was almost entirely uncovered — only AppendChildMovesElement existed.
// Each function gets at minimum a happy path and a NULL/error path.

TEST(ElementSetName, RenamesTag) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<old/>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    EXPECT_EQ(taurus_element_set_name(root, "new"), TAURUS_OK);
    EXPECT_STREQ(taurus_element_name(root), "new");
    EXPECT_EQ(taurus_element_set_name(nullptr, "x"), TAURUS_ERROR_NULL_ARG);
    /* NULL new_name is rejected; the function path picks INVALID_ARG. */
    TaurusStatus null_name_rc = taurus_element_set_name(root, nullptr);
    EXPECT_NE(null_name_rc, TAURUS_OK);
    taurus_document_free(doc);
}

TEST(ElementSetText, ReplacesTextContent) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r>old</r>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);
    EXPECT_EQ(taurus_element_set_text(root, "new"), TAURUS_OK);
    EXPECT_STREQ(taurus_element_text(root), "new");
    EXPECT_EQ(taurus_element_set_text(nullptr, "x"), TAURUS_ERROR_NULL_ARG);
    taurus_document_free(doc);
}

TEST(ElementSetAttribute, CreatesAndUpdates) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r a='1'/>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);

    /* Update existing. */
    EXPECT_EQ(taurus_element_set_attribute(root, "a", "2"), TAURUS_OK);
    EXPECT_STREQ(taurus_element_attribute(root, "a"), "2");

    /* Create new. */
    EXPECT_EQ(taurus_element_set_attribute(root, "b", "hello"), TAURUS_OK);
    EXPECT_STREQ(taurus_element_attribute(root, "b"), "hello");

    /* NULL handling. */
    EXPECT_EQ(taurus_element_set_attribute(nullptr, "x", "y"), TAURUS_ERROR_NULL_ARG);
    EXPECT_EQ(taurus_element_set_attribute(root, nullptr, "y"), TAURUS_ERROR_NULL_ARG);

    taurus_document_free(doc);
}

TEST(ElementSetAttributeTyped, RoundTripViaStringAccessor) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r/>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);

    EXPECT_EQ(taurus_element_set_attribute_int(root, "n", 42), TAURUS_OK);
    EXPECT_EQ(taurus_element_attribute_int(root, "n", 0), 42);

    EXPECT_EQ(taurus_element_set_attribute_uint(root, "u", 123456u), TAURUS_OK);
    EXPECT_EQ(taurus_element_attribute_int(root, "u", 0), 123456);

    EXPECT_EQ(taurus_element_set_attribute_double(root, "f", 3.14), TAURUS_OK);
    EXPECT_DOUBLE_EQ(taurus_element_attribute_double(root, "f", 0.0), 3.14);

    EXPECT_EQ(taurus_element_set_attribute_bool(root, "flag", 1), TAURUS_OK);
    /* bool is stored as "true"/"false" string; int accessor parses it back. */
    EXPECT_EQ(taurus_element_attribute_int(root, "flag", 0), 0);  /* "true" is not a number */

    taurus_document_free(doc);
}

TEST(ElementRemoveAttribute, DeletesByName) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r a='1' b='2' c='3'/>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);

    EXPECT_EQ(taurus_element_remove_attribute(root, "b"), TAURUS_OK);
    EXPECT_EQ(taurus_element_attribute(root, "b"), nullptr);
    /* Other attributes survive. */
    EXPECT_STREQ(taurus_element_attribute(root, "a"), "1");
    EXPECT_STREQ(taurus_element_attribute(root, "c"), "3");

    /* Removing a missing attribute should not crash; status code reflects it. */
    TaurusStatus rc = taurus_element_remove_attribute(root, "missing");
    EXPECT_NE(rc, TAURUS_OK);

    taurus_document_free(doc);
}

TEST(ElementChildMutation, PrependChildAddsAtBeginning) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r><a/></r>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    TaurusElement new_child = taurus_element_create(doc, "new");
    ASSERT_NE(new_child, nullptr);

    EXPECT_EQ(taurus_element_prepend_child(root, new_child), TAURUS_OK);

    /* new_child should be first, a second. */
    TaurusElement first = taurus_element_first_child_any(root);
    ASSERT_NE(first, nullptr);
    EXPECT_STREQ(taurus_element_name(first), "new");
    TaurusElement second = taurus_element_next_sibling_any(first);
    ASSERT_NE(second, nullptr);
    EXPECT_STREQ(taurus_element_name(second), "a");

    taurus_document_free(doc);
}

TEST(ElementChildMutation, InsertBeforeAndAfter) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r><b/></r>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    TaurusElement b = taurus_element_first_child_any(root);
    ASSERT_NE(b, nullptr);

    TaurusElement a = taurus_element_create(doc, "a");
    TaurusElement c = taurus_element_create(doc, "c");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(c, nullptr);

    EXPECT_EQ(taurus_element_insert_before(b, a), TAURUS_OK);
    EXPECT_EQ(taurus_element_insert_after(b, c), TAURUS_OK);

    /* Expected order: a, b, c. */
    TaurusElement cur = taurus_element_first_child_any(root);
    ASSERT_NE(cur, nullptr);
    EXPECT_STREQ(taurus_element_name(cur), "a");
    cur = taurus_element_next_sibling_any(cur);
    ASSERT_NE(cur, nullptr);
    EXPECT_STREQ(taurus_element_name(cur), "b");
    cur = taurus_element_next_sibling_any(cur);
    ASSERT_NE(cur, nullptr);
    EXPECT_STREQ(taurus_element_name(cur), "c");
    EXPECT_EQ(taurus_element_next_sibling_any(cur), nullptr);

    taurus_document_free(doc);
}

TEST(ElementChildMutation, RemoveChildUnlinksSpecificChild) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r><a/><b/><c/></r>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    TaurusElement a = taurus_element_first_child_any(root);
    TaurusElement b = taurus_element_next_sibling_any(a);

    EXPECT_EQ(taurus_element_remove_child(root, b), TAURUS_OK);

    /* Remaining: a, c. */
    TaurusElement cur = taurus_element_first_child_any(root);
    ASSERT_NE(cur, nullptr);
    EXPECT_STREQ(taurus_element_name(cur), "a");
    cur = taurus_element_next_sibling_any(cur);
    ASSERT_NE(cur, nullptr);
    EXPECT_STREQ(taurus_element_name(cur), "c");
    EXPECT_EQ(taurus_element_next_sibling_any(cur), nullptr);

    taurus_document_free(doc);
}

TEST(ElementChildMutation, RemoveChildrenClearsAll) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r><a/><b/><c/></r>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    EXPECT_EQ(taurus_element_remove_children(root), TAURUS_OK);

    /* No children remain. */
    EXPECT_EQ(taurus_element_first_child_any(root), nullptr);

    /* Idempotent: removing again should still succeed. */
    EXPECT_EQ(taurus_element_remove_children(root), TAURUS_OK);

    taurus_document_free(doc);
}

// ---- Status contract (TODO 98) -----------------------------------------
//
// taurus_parse_string's status out-param must use the public TaurusStatus
// enum.  An earlier build wrote the internal taurus_error_code value
// TAURUS_ERROR_MEMORY_ALLOCATION (=1) on allocation failure, which is
// outside the public enum range and broke every caller that compared
// against TAURUS_ERROR_MEMORY (=-1).  The negative-XML and NULL-input
// paths are the only failure modes reachable from a unit test without
// an allocator hook; both must produce documented public codes.

TEST(ParseStatusContract, NullInputYieldsPublicError) {
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(nullptr, 0, &st);
    EXPECT_EQ(doc, nullptr);
    EXPECT_NE(st, TAURUS_OK);
    /* st must be one of the public TaurusStatus error codes — never
     * a positive internal taurus_error_code value. */
    EXPECT_LT(st, 0)
        << "status=" << static_cast<int>(st) << " is not a public error code";
}

TEST(ParseStatusContract, EmptyInputYieldsPublicError) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "";
    TaurusDocument doc = taurus_parse_string(xml, 0, &st);
    EXPECT_EQ(doc, nullptr);
    EXPECT_LT(st, 0)
        << "status=" << static_cast<int>(st) << " is not a public error code";
}
