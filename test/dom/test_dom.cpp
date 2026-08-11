// test/dom/test_dom.cpp — DOM creation/traversal/modification specs.

#include <gtest/gtest.h>

#include "taurus.h"

#include <cstring>
#include <string>

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

// ---- Compact-pointer integration (TODO 90 Phase 2 + TODO 109) ----------
//
// The element struct now stores parent/child/sibling/attribute edges as
// int32_t byte-offsets relative to the hosting node's address. This test
// exercises the full cycle (parse → tree-walk via XPath → mutate →
// serialize) to catch any regression in the offset encoding/decoding.

TEST(CompactPointerIntegration, ParseWalkMutateSerializeRoundTrip) {
    TaurusStatus st = TAURUS_OK;
    const char xml[] =
        "<root attr1='a'>"
        "  <child id='1'>alpha</child>"
        "  <child id='2'>beta</child>"
        "  <child id='3'>gamma</child>"
        "</root>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    // XPath descendant axis uses taurus_elem_first_child + taurus_node_get_next_sibling
    // — the offset-encoded traversal. 3 child elements must be visible.
    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, "//child");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(taurus_xpath_result_count(r), 3u);
    taurus_xpath_result_free(r);

    // Mutation exercises taurus_elem_set_*, which encodes offsets.
    TaurusElement root = taurus_document_root(doc);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(taurus_element_set_attribute(root, "new", "value"), TAURUS_OK);

    // Append a 4th child — exercises set_last_child + set_next_sibling.
    TaurusElement new_child = taurus_element_create(doc, "child");
    ASSERT_NE(new_child, nullptr);
    EXPECT_EQ(taurus_element_append_child(root, new_child), TAURUS_OK);

    // Re-walk via XPath — must see 4 children now.
    TaurusXPathResult r2 = taurus_xpath_eval(doc, nullptr, "//child");
    ASSERT_NE(r2, nullptr);
    EXPECT_EQ(taurus_xpath_result_count(r2), 4u);
    taurus_xpath_result_free(r2);

    // Serialize — exercises the same edges in reverse.
    char* serialized = taurus_document_serialize(doc, nullptr);
    ASSERT_NE(serialized, nullptr);
    EXPECT_STRNE(serialized, "");
    EXPECT_NE(std::string(serialized).find("new=\"value\""), std::string::npos);
    taurus_free_string(serialized);

    taurus_document_free(doc);
}

TEST(CompactPointerIntegration, MixedContentTreeWalksCorrectly) {
    // The walker visits every child regardless of type — TODO 109 + Phase 2c.
    TaurusStatus st = TAURUS_OK;
    const char xml[] = "<r>text1<!-- comment -->text2<?pi data?></r>";
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    // count(node()) walks the offset-encoded sibling chain through every
    // node type (text, comment, text, PI).
    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, "count(/r/node())");
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r), 4.0);
    taurus_xpath_result_free(r);

    taurus_document_free(doc);
}

TEST(CompactPointerIntegration, DeepNestingTraversesViaOffsets) {
    // 50-level deep nesting — exercises offset arithmetic at every depth.
    // Pool-allocated elements stay within int32_t range; no overflow.
    std::string xml = "<a0>";
    for (int i = 1; i < 50; i++) xml += "<a" + std::to_string(i) + ">";
    for (int i = 49; i >= 0; i--) xml += "</a" + std::to_string(i) + ">";

    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml.data(), xml.size(), &st);
    ASSERT_NE(doc, nullptr);

    // count(//*) walks every element via the offset-encoded edges.
    // The deeply-nested tree has 50 elements; if any offset corrupts,
    // the count would differ.
    TaurusXPathResult r = taurus_xpath_eval(doc, nullptr, "count(//*)");
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r), 50.0);
    taurus_xpath_result_free(r);

    // count(//a49) finds the deepest element via 49 chained first_child
    // offset decodes.
    TaurusXPathResult r2 = taurus_xpath_eval(doc, nullptr, "count(//a49)");
    ASSERT_NE(r2, nullptr);
    EXPECT_DOUBLE_EQ(taurus_xpath_result_number(r2), 1.0);
    taurus_xpath_result_free(r2);

    taurus_document_free(doc);
}

// Zero-copy deferred NUL-termination (TODO 113 Phase 5). The parser
// writes NUL terminators into the writable XML buffer to avoid one
// pool_strdup per element/attribute name and per attribute value.
// These specs exercise the edge cases where that path could corrupt
// state: prefix splitting, entity-bearing values, self-closing tags,
// empty elements, and namespaced names.
TEST(ZeroCopyParse, ElementAndAttributeNamesRoundTrip) {
    const char xml[] =
        "<root attr='value' empty=''>"
        "<child id='1' name='first'>text</child>"
        "<self-closing enabled='yes'/>"
        "</root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    ASSERT_NE(root, nullptr);
    EXPECT_STREQ(taurus_element_name(root), "root");
    EXPECT_STREQ(taurus_element_attribute(root, "attr"), "value");
    EXPECT_STREQ(taurus_element_attribute(root, "empty"), "");

    TaurusElement child = taurus_element_first_child_any(root);
    ASSERT_NE(child, nullptr);
    EXPECT_STREQ(taurus_element_name(child), "child");
    EXPECT_STREQ(taurus_element_attribute(child, "id"), "1");

    // Self-closing — name was NUL-terminated at '/'.
    TaurusElement sc = taurus_element_next_sibling_any(child);
    ASSERT_NE(sc, nullptr);
    EXPECT_STREQ(taurus_element_name(sc), "self-closing");
    EXPECT_STREQ(taurus_element_attribute(sc, "enabled"), "yes");

    taurus_document_free(doc);
}

TEST(ZeroCopyParse, NamespacedElementExposesLocalName) {
    const char xml[] =
        "<root xmlns:xi='http://www.w3.org/2001/XInclude'>"
        "<xi:include href='chapter.xml'/>"
        "</root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    TaurusElement include = taurus_element_first_child_any(root);
    ASSERT_NE(include, nullptr);
    // Local name only — prefix "xi:" was stripped during parse.
    EXPECT_STREQ(taurus_element_name(include), "include");
    EXPECT_STREQ(taurus_element_attribute(include, "href"), "chapter.xml");

    taurus_document_free(doc);
}

TEST(ZeroCopyParse, AttributeValueWithEntitiesDecodesCorrectly) {
    const char xml[] =
        "<root encoded='a&amp;b&lt;c&gt;d' normal='plain'/>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    EXPECT_STREQ(taurus_element_attribute(root, "encoded"), "a&b<c>d");
    EXPECT_STREQ(taurus_element_attribute(root, "normal"), "plain");

    taurus_document_free(doc);
}

TEST(ZeroCopyParse, EmptyElementRoundTrips) {
    const char xml[] = "<root><child></child><empty/></root>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    TaurusElement root = taurus_document_root(doc);
    TaurusElement child = taurus_element_first_child_any(root);
    ASSERT_NE(child, nullptr);
    EXPECT_STREQ(taurus_element_name(child), "child");
    EXPECT_EQ(taurus_element_first_child_any(child), nullptr);

    TaurusElement empty = taurus_element_next_sibling_any(child);
    ASSERT_NE(empty, nullptr);
    EXPECT_STREQ(taurus_element_name(empty), "empty");
    EXPECT_EQ(taurus_element_first_child_any(empty), nullptr);

    taurus_document_free(doc);
}

namespace {

struct TraverseCollector {
    std::string names_pre;
    std::string names_post;
    int stop_after;
    int calls;
};

static int collect_pre(TaurusNodeRef node, void* ud) {
    auto* c = static_cast<TraverseCollector*>(ud);
    c->calls++;
    if (c->stop_after > 0 && c->calls > c->stop_after) return 1;
    if (taurus_node_get_type(node) == kNodeTypeElement) {
        if (!c->names_pre.empty()) c->names_pre += ",";
        c->names_pre += taurus_element_name((TaurusElement)node);
    }
    return 0;
}

static int collect_post(TaurusNodeRef node, void* ud) {
    auto* c = static_cast<TraverseCollector*>(ud);
    c->calls++;
    if (c->stop_after > 0 && c->calls > c->stop_after) return 1;
    if (taurus_node_get_type(node) == kNodeTypeElement) {
        if (!c->names_post.empty()) c->names_post += ",";
        c->names_post += taurus_element_name((TaurusElement)node);
    }
    return 0;
}

}  // namespace

TEST(NodeTraverse, PreOrderVisitsParentBeforeChildren) {
    const char xml[] =
        "<a><b><d/></b><c/></a>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);

    TraverseCollector col{ "", "", 0, 0 };
    int n = taurus_node_traverse(taurus_element_as_node(root),
                                 TAURUS_TRAVERSE_PRE_ORDER,
                                 collect_pre, &col);
    EXPECT_EQ(n, 4);
    EXPECT_EQ(col.names_pre, "a,b,d,c");
    taurus_document_free(doc);
}

TEST(NodeTraverse, PostOrderVisitsChildrenBeforeParent) {
    const char xml[] =
        "<a><b><d/></b><c/></a>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);

    TraverseCollector col{ "", "", 0, 0 };
    int n = taurus_node_traverse(taurus_element_as_node(root),
                                 TAURUS_TRAVERSE_POST_ORDER,
                                 collect_post, &col);
    EXPECT_EQ(n, 4);
    EXPECT_EQ(col.names_post, "d,b,c,a");
    taurus_document_free(doc);
}

TEST(NodeTraverse, IncludesTextAndElementChildren) {
    const char xml[] = "<r>hello<x/>world</r>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);

    int calls = 0;
    taurus_node_traverse(taurus_element_as_node(root),
                         TAURUS_TRAVERSE_PRE_ORDER,
                         [](TaurusNodeRef, void* p) -> int {
                             (*static_cast<int*>(p))++;
                             return 0;
                         }, &calls);
    /* root + text("hello") + x + text("world") = 4 */
    EXPECT_EQ(calls, 4);
    taurus_document_free(doc);
}

TEST(NodeTraverse, EarlyStopReturnsCountVisited) {
    const char xml[] = "<a><b/><c/><d/></a>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);

    TraverseCollector col{ "", "", 2, 0 };
    int n = taurus_node_traverse(taurus_element_as_node(root),
                                 TAURUS_TRAVERSE_PRE_ORDER,
                                 collect_pre, &col);
    /* stop_after=2 → callback returns non-zero on its 3rd invocation */
    EXPECT_EQ(n, 2);
    taurus_document_free(doc);
}

TEST(NodeTraverse, NullArgsReturnNegativeOne) {
    const char xml[] = "<r/>";
    TaurusStatus st = TAURUS_OK;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    TaurusElement root = taurus_document_root(doc);

    EXPECT_EQ(taurus_node_traverse(nullptr,
                                   TAURUS_TRAVERSE_PRE_ORDER,
                                   collect_pre, nullptr), -1);
    EXPECT_EQ(taurus_node_traverse(taurus_element_as_node(root),
                                   TAURUS_TRAVERSE_PRE_ORDER,
                                   nullptr, nullptr), -1);
    taurus_document_free(doc);
}
