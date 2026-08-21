// test/dom/test_dom.cpp — DOM creation/traversal/modification specs.

#include <gtest/gtest.h>

#include "leptris.h"

#include <cstring>
#include <string>

namespace {

// Public API documents these as integers (see leptris_node_get_type docs).
constexpr int kNodeTypeElement  = 0;
constexpr int kNodeTypeText     = 1;
constexpr int kNodeTypeComment  = 2;
constexpr int kNodeTypeCDATA    = 3;
constexpr int kNodeTypePI       = 4;
constexpr int kNodeTypeDoctype  = 5;
constexpr int kNodeTypeAttribute = 6;

TEST(DomBasics, EmptyDocumentRoundTrips) {
    const char xml[] = "<r/>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);
    EXPECT_STREQ(leptris_element_name(root), "r");
    leptris_document_free(doc);
}

TEST(DomBasics, AttributeLookupByName) {
    const char xml[] = "<r a='1' b='2' c='3'/>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    EXPECT_STREQ(leptris_element_attribute(root, "a"), "1");
    EXPECT_STREQ(leptris_element_attribute(root, "b"), "2");
    EXPECT_STREQ(leptris_element_attribute(root, "c"), "3");
    EXPECT_EQ(leptris_element_attribute(root, "missing"), nullptr);

    leptris_document_free(doc);
}

TEST(DomBasics, ManyAttributesPerElementAreAllReachable) {
    /* Regression for TODO 159 Phase G: parser-local last-attr cache
     * must wire every attr into the list, not just the first. */
    const char xml[] =
        "<e a0='0' a1='1' a2='2' a3='3' a4='4' a5='5' a6='6' a7='7' "
        "   a8='8' a9='9' a10='10' a11='11' a12='12' a13='13' "
        "   a14='14' a15='15'/>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    EXPECT_EQ(leptris_element_attribute_count(root), 16);
    for (int i = 0; i < 16; i++) {
        char name[8];
        std::snprintf(name, sizeof(name), "a%d", i);
        char value[8];
        std::snprintf(value, sizeof(value), "%d", i);
        EXPECT_STREQ(leptris_element_attribute(root, name), value)
            << "attr " << name << " should be reachable";
    }

    leptris_document_free(doc);
}

TEST(DomBasics, AttributeHandleIterationWalksAll) {
    /* TODO.remaining/06: handle-based iteration — O(n) total where
     * the _at(index) accessors re-walk from the head per call. */
    const char xml[] = "<r a='1' b='2' c='3'/>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    const char* names[4] = {0};
    const char* values[4] = {0};
    size_t n = 0;
    for (LeptrisAttribute a = leptris_element_first_attribute(root);
         a && n < 4; a = leptris_attribute_next(a), n++) {
        names[n] = leptris_attribute_get_name(a);
        values[n] = leptris_attribute_get_value(root, a);
    }

    EXPECT_EQ(n, 3);
    EXPECT_STREQ(names[0], "a");
    EXPECT_STREQ(values[0], "1");
    EXPECT_STREQ(names[1], "b");
    EXPECT_STREQ(values[1], "2");
    EXPECT_STREQ(names[2], "c");
    EXPECT_STREQ(values[2], "3");

    leptris_document_free(doc);
}

TEST(DomBasics, AttributeHandleIterationEmptyAndNullContracts) {
    const char xml[] = "<r/>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    EXPECT_EQ(leptris_element_first_attribute(root), nullptr);
    EXPECT_EQ(leptris_attribute_next(nullptr), nullptr);
    EXPECT_STREQ(leptris_attribute_get_name(nullptr), "");
    EXPECT_STREQ(leptris_attribute_get_value(root, nullptr), "");

    leptris_document_free(doc);
}

TEST(DomBasics, AttributeHandleValueExpandsEntities) {
    const char xml[] = "<r t='a &amp; b &lt; c'/>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    LeptrisAttribute a = leptris_element_first_attribute(root);
    ASSERT_NE(a, nullptr);
    EXPECT_STREQ(leptris_attribute_get_name(a), "t");
    EXPECT_STREQ(leptris_attribute_get_value(root, a), "a & b < c");
    /* Matches the by-name accessor's expansion contract. */
    EXPECT_STREQ(leptris_element_attribute(root, "t"), "a & b < c");

    leptris_document_free(doc);
}

TEST(DomBasics, AttributeHandleIterationSeesMutationAppends) {
    const char xml[] = "<r a='1'/>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    EXPECT_EQ(leptris_element_set_attribute(root, "b", "2"), LEPTRIS_OK);

    size_t n = 0;
    const char* last_name = nullptr;
    for (LeptrisAttribute a = leptris_element_first_attribute(root);
         a; a = leptris_attribute_next(a)) {
        last_name = leptris_attribute_get_name(a);
        n++;
    }
    EXPECT_EQ(n, 2);
    EXPECT_STREQ(last_name, "b");

    leptris_document_free(doc);
}

TEST(DomBasics, MultipleNamespacesPerElementAreAllReachable) {
    /* Regression for TODO 159 Phase G: parser-local last-ns cache
     * must wire every xmlns into the list. We verify by parsing an
     * element with three xmlns declarations and a prefixed attribute
     * that depends on one of them; if the parser dropped any ns the
     * prefix lookup would fail. The document must parse cleanly. */
    const char xml[] =
        "<e xmlns:a='urn:a' xmlns:b='urn:b' xmlns:c='urn:c' a:x='1'/>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);

    /* Prefixed attribute should round-trip. */
    EXPECT_EQ(leptris_element_attribute_count(root), 1);

    leptris_document_free(doc);
}

TEST(DomBasics, TraversesChildrenInDocumentOrder) {
    const char xml[] = "<r><a/><b/><c/></r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    LeptrisElement child = leptris_element_first_child_any(root);
    ASSERT_NE(child, nullptr);
    EXPECT_STREQ(leptris_element_name(child), "a");

    child = leptris_element_next_sibling_any(child);
    ASSERT_NE(child, nullptr);
    EXPECT_STREQ(leptris_element_name(child), "b");

    child = leptris_element_next_sibling_any(child);
    ASSERT_NE(child, nullptr);
    EXPECT_STREQ(leptris_element_name(child), "c");

    child = leptris_element_next_sibling_any(child);
    EXPECT_EQ(child, nullptr);

    leptris_document_free(doc);
}

TEST(DomBasics, NodeRefTraversalCoversAllNodeTypes) {
    // LeptrisNodeRef traversal exposes every child regardless of type;
    // LeptrisElement-only traversal skips text/comment/cdata siblings.
    const char xml[] = "<r><!--c-->text<x/></r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    LeptrisNodeRef n = leptris_node_first_child(leptris_element_as_node(root));
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(leptris_node_get_type(n), kNodeTypeComment);

    n = leptris_node_next_sibling(n);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(leptris_node_get_type(n), kNodeTypeText);

    n = leptris_node_next_sibling(n);
    ASSERT_NE(n, nullptr);
    EXPECT_EQ(leptris_node_get_type(n), kNodeTypeElement);

    leptris_document_free(doc);
}

}  // namespace

// ---- Freeze API (TODO 88) ------------------------------------------------

TEST(DocumentFreeze, FreshDocumentIsFrozenAfterParse) {
    /* The parser calls leptris_document_freeze_tree internally. */
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root><child/></root>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(leptris_document_is_frozen(doc), 1);
    leptris_document_free(doc);
}

TEST(DocumentFreeze, ExplicitFreezeSetsFlag) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    /* Already frozen by parser, but explicit freeze should still work. */
    EXPECT_EQ(leptris_document_freeze(doc), LEPTRIS_OK);
    EXPECT_EQ(leptris_document_is_frozen(doc), 1);
    leptris_document_free(doc);
}

TEST(DocumentFreeze, NullDocReturnsSafe) {
    EXPECT_EQ(leptris_document_freeze(nullptr), LEPTRIS_ERROR_NULL_ARG);
    EXPECT_EQ(leptris_document_is_frozen(nullptr), 0);
}

// The freeze flag is advisory only — it does NOT cause mutations to
// reject.  See leptris.h:leptris_document_freeze for the rationale.
// This spec pins the current contract so future "freeze = read-only"
// enforcement has to update the spec deliberately.
TEST(DocumentFreeze, MutationsSucceedOnFrozenDocument) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root><child/></root>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    ASSERT_EQ(leptris_document_is_frozen(doc), 1);

    /* These mutation calls succeed even though the doc is frozen.
     * If we ever want true read-only enforcement, this is the spec
     * that would need to flip. */
    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(leptris_element_set_attribute(root, "x", "1"), LEPTRIS_OK);
    EXPECT_EQ(leptris_element_set_name(root, "renamed"), LEPTRIS_OK);
    EXPECT_STREQ(leptris_element_attribute(root, "x"), "1");
    EXPECT_STREQ(leptris_element_name(root), "renamed");

    /* The flag is still set — mutating didn't unfreeze. */
    EXPECT_EQ(leptris_document_is_frozen(doc), 1);

    leptris_document_free(doc);
}

// ---- Node content accessors (TODO: specs coverage) -----------------------

TEST(NodeContentAccessors, CdataNodeReturnsContent) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r><![CDATA[raw <content> & stuff]]></r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisElement root = leptris_document_root(doc);
    LeptrisNodeRef child = leptris_node_first_child((LeptrisNodeRef)root);
    ASSERT_NE(child, nullptr);

    const char* cdata = leptris_cdata_node_get_content(child);
    EXPECT_NE(cdata, nullptr);
    EXPECT_STREQ(cdata, "raw <content> & stuff");

    leptris_document_free(doc);
}

TEST(NodeContentAccessors, CommentNodeReturnsContent) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r><!-- a comment --></r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisElement root = leptris_document_root(doc);
    LeptrisNodeRef child = leptris_node_first_child((LeptrisNodeRef)root);
    ASSERT_NE(child, nullptr);

    const char* comment = leptris_comment_node_get_content(child);
    EXPECT_NE(comment, nullptr);
    EXPECT_STREQ(comment, " a comment ");

    leptris_document_free(doc);
}

// ---- Attribute type accessors -------------------------------------------

TEST(AttributeAccessors, IntAttributeReturnsValue) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r count='42'/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    EXPECT_EQ(leptris_element_attribute_int(root, "count", 0), 42);
    EXPECT_EQ(leptris_element_attribute_int(root, "missing", 99), 99);
    leptris_document_free(doc);
}

TEST(AttributeAccessors, DoubleAttributeReturnsValue) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r pi='3.14'/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    EXPECT_DOUBLE_EQ(leptris_element_attribute_double(root, "pi", 0.0), 3.14);
    leptris_document_free(doc);
}

// ---- Text content type accessors ----------------------------------------

TEST(TextContentAccessors, IntTextReturnsValue) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r>123</r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    EXPECT_EQ(leptris_element_text_int(root, 0), 123);
    EXPECT_EQ(leptris_element_text_int(nullptr, -1), -1);
    leptris_document_free(doc);
}

TEST(TextContentAccessors, DoubleTextReturnsValue) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r>45.67</r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    EXPECT_DOUBLE_EQ(leptris_element_text_double(root, 0.0), 45.67);
    leptris_document_free(doc);
}

// leptris_element_text promises document-owned storage ("String is owned by
// element"), so neither path may hand back a malloc'd buffer — ASAN/LSan in CI
// is what enforces this.
TEST(TextContentAccessors, TextIsDocumentOwnedForSingleTextChild) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r>plain</r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    const char* first = leptris_element_text(root);
    EXPECT_STREQ(first, "plain");
    /* A lone text child needs no concatenation, so repeated calls return the
     * text node's own storage — same pointer, no allocation at all. */
    EXPECT_EQ(leptris_element_text(root), first);

    leptris_document_free(doc);
}

TEST(TextContentAccessors, TextIsDocumentOwnedForMixedContent) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r>a<b>c</b>d</r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    /* Mixed content is concatenated into the document pool; it must stay
     * readable until leptris_document_free and must not leak. */
    const char* text = leptris_element_text(root);
    EXPECT_STREQ(text, "acd");

    leptris_document_free(doc);
}

TEST(TextContentAccessors, EmptyElementYieldsEmptyString) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    EXPECT_STREQ(leptris_element_text(root), "");
    EXPECT_STREQ(leptris_element_text(nullptr), "");
    leptris_document_free(doc);
}

// ---- Serialize document ---------------------------------------------------

TEST(SerializeDocument, RoundTripPreservesStructure) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<root><child>text</child></root>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    char* output = leptris_document_serialize(doc, nullptr);
    EXPECT_NE(output, nullptr);
    if (output) {
        /* The serialized output should contain the element names. */
        std::string s(output);
        EXPECT_NE(s.find("root"), std::string::npos);
        EXPECT_NE(s.find("child"), std::string::npos);
        EXPECT_NE(s.find("text"), std::string::npos);
        leptris_free_string(output);
    }
    leptris_document_free(doc);
}

// ---- Element mutation -----------------------------------------------------

TEST(ElementMutation, AppendChildMovesElement) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r><a/><b/></r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement a = leptris_element_first_child_any(root);
    ASSERT_NE(a, nullptr);
    EXPECT_STREQ(leptris_element_name(a), "a");

    LeptrisElement b = leptris_element_next_sibling_any(a);
    ASSERT_NE(b, nullptr);
    EXPECT_STREQ(leptris_element_name(b), "b");

    /* Move b under a. */
    LeptrisStatus rc = leptris_element_append_child(a, b);
    EXPECT_EQ(rc, LEPTRIS_OK);

    /* a should now have b as a child. */
    LeptrisElement child = leptris_element_first_child_any(a);
    ASSERT_NE(child, nullptr);
    EXPECT_STREQ(leptris_element_name(child), "b");

    leptris_document_free(doc);
}

// ---- Element mutation: comprehensive coverage ---------------------------
//
// The element-modification API surface (set_name, set_text, set_attribute
// and its typed variants, remove_attribute, append/prepend/insert/remove)
// was almost entirely uncovered — only AppendChildMovesElement existed.
// Each function gets at minimum a happy path and a NULL/error path.

TEST(ElementSetName, RenamesTag) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<old/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    EXPECT_EQ(leptris_element_set_name(root, "new"), LEPTRIS_OK);
    EXPECT_STREQ(leptris_element_name(root), "new");
    EXPECT_EQ(leptris_element_set_name(nullptr, "x"), LEPTRIS_ERROR_NULL_ARG);
    /* NULL new_name is rejected; the function path picks INVALID_ARG. */
    LeptrisStatus null_name_rc = leptris_element_set_name(root, nullptr);
    EXPECT_NE(null_name_rc, LEPTRIS_OK);
    leptris_document_free(doc);
}

TEST(ElementSetText, ReplacesTextContent) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r>old</r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    EXPECT_EQ(leptris_element_set_text(root, "new"), LEPTRIS_OK);
    EXPECT_STREQ(leptris_element_text(root), "new");
    EXPECT_EQ(leptris_element_set_text(nullptr, "x"), LEPTRIS_ERROR_NULL_ARG);
    leptris_document_free(doc);
}

/* Regression (v0.26.4 bug + round 21 hazard): set_name never updated
 * name_len, so a renamed element serialized the first N bytes of its
 * new name (N = old length). Round 21 additionally replaced the name
 * storage: the doc must be resolved BEFORE the mutation name
 * backpointer bit is cleared, or every later mutation on the element
 * loses its document. Pins both. */
TEST(ElementSetName, UpdatesLengthAndKeepsDocReachable) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    LeptrisElement c = leptris_element_create(doc, "orig");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(leptris_element_set_name(c, "renamed"), LEPTRIS_OK);

    /* Mutation after rename must still reach the document. */
    EXPECT_EQ(leptris_element_set_attribute(c, "after", "v"), LEPTRIS_OK);

    EXPECT_EQ(leptris_element_append_child(root, c), LEPTRIS_OK);
    char* out = leptris_document_serialize(doc, nullptr);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "<r><renamed after=\"v\"/></r>");
    leptris_free_string(out);

    leptris_document_free(doc);
}

/* Shorter new name: name_len must shrink too (0xFF sentinel logic). */
TEST(ElementSetName, ShorterNameAlsoTruncatesCorrectly) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    LeptrisElement c = leptris_element_create(doc, "longername");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(leptris_element_set_name(c, "short"), LEPTRIS_OK);
    EXPECT_EQ(leptris_element_append_child(root, c), LEPTRIS_OK);

    char* out = leptris_document_serialize(doc, nullptr);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "<r><short/></r>");
    leptris_free_string(out);

    leptris_document_free(doc);
}

/* Regression (#450): sibling links from text nodes to element
 * siblings are cp16-encoded (±256 KB). On large documents the
 * element block sits farther than that from the text block, and the
 * parser's raw store silently truncated — serialize then decoded
 * into stale arena memory and segfaulted (heap-layout dependent,
 * reliably reproducible around 90 KB of pretty-printed mixed
 * content). The wiring must go through the encoder's overflow path.
 * This spec builds a document large enough to cross the boundary. */
TEST(LargeMixedContent, TextToElementSiblingLinksSurviveSerialize) {
    std::string xml = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n<users>\n";
    xml.reserve(220 * 1024);
    for (int i = 0; i < 1200; i++) {
        char buf[160];
        std::snprintf(buf, sizeof buf,
            "<user id=\"%d\"><name>User %d</name>"
            "<created>2023-01-%02dT10:00:00Z</created></user>\n",
            i, i, (i % 28) + 1);
        xml += buf;
    }
    xml += "</users>\n";

    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml.data(), xml.size(), &st);
    ASSERT_NE(doc, nullptr);

    /* Every element must remain reachable through the sibling chain
     * (the truncated cp16 links broke exactly this walk). */
    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);
    size_t users = 0;
    for (LeptrisElement u = leptris_element_first_child(root, "user"); u;
         u = leptris_element_next_sibling(u, "user")) {
        users++;
    }
    EXPECT_EQ(users, 1200u);

    char* out = leptris_document_serialize(doc, nullptr);
    ASSERT_NE(out, nullptr);
    /* Round trip modulo the serializer's declaration handling: the
     * newline after the declaration is not part of the tree. */
    std::string expected = xml;
    expected.erase(strlen("<?xml version=\"1.0\" encoding=\"UTF-8\"?>"), 1);
    ASSERT_EQ(expected.back(), '\n');
    expected.pop_back(); /* top-level trailing ws is not in the tree */
    EXPECT_EQ(std::string(out), expected);
    leptris_free_string(out);
    leptris_document_free(doc);
}

TEST(ElementSetAttribute, CreatesAndUpdates) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r a='1'/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    /* Update existing. */
    EXPECT_EQ(leptris_element_set_attribute(root, "a", "2"), LEPTRIS_OK);
    EXPECT_STREQ(leptris_element_attribute(root, "a"), "2");

    /* Create new. */
    EXPECT_EQ(leptris_element_set_attribute(root, "b", "hello"), LEPTRIS_OK);
    EXPECT_STREQ(leptris_element_attribute(root, "b"), "hello");

    /* NULL handling. */
    EXPECT_EQ(leptris_element_set_attribute(nullptr, "x", "y"), LEPTRIS_ERROR_NULL_ARG);
    EXPECT_EQ(leptris_element_set_attribute(root, nullptr, "y"), LEPTRIS_ERROR_NULL_ARG);

    leptris_document_free(doc);
}

/* Regression (v0.26.2 bug): set_attribute on a name it had itself
 * created inserted a DUPLICATE attribute instead of updating. The
 * doc-level attr index stored hash 0 for every mutation-created
 * attr (an overzealous name_hash clear wiped the eager value), so
 * the duplicate probe never matched. XML forbids duplicate
 * attribute names — this must stay pinned. */
TEST(ElementSetAttribute, UpdatesMutationCreatedAttrInPlace) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    EXPECT_EQ(leptris_element_set_attribute(root, "x", "1"), LEPTRIS_OK);
    EXPECT_EQ(leptris_element_set_attribute(root, "x", "2"), LEPTRIS_OK);
    EXPECT_EQ(leptris_element_attribute_count(root), 1u);
    EXPECT_STREQ(leptris_element_attribute(root, "x"), "2");

    /* Same via serialize: exactly one x attribute on output. */
    char* out = leptris_document_serialize(doc, nullptr);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "<r x=\"2\"/>");
    leptris_free_string(out);

    /* Third write and a distinct name alongside. */
    EXPECT_EQ(leptris_element_set_attribute(root, "x", "3"), LEPTRIS_OK);
    EXPECT_EQ(leptris_element_set_attribute(root, "y", "9"), LEPTRIS_OK);
    EXPECT_EQ(leptris_element_attribute_count(root), 2u);
    EXPECT_STREQ(leptris_element_attribute(root, "x"), "3");

    leptris_document_free(doc);
}

TEST(ElementSetAttributeTyped, RoundTripViaStringAccessor) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    EXPECT_EQ(leptris_element_set_attribute_int(root, "n", 42), LEPTRIS_OK);
    EXPECT_EQ(leptris_element_attribute_int(root, "n", 0), 42);

    EXPECT_EQ(leptris_element_set_attribute_uint(root, "u", 123456u), LEPTRIS_OK);
    EXPECT_EQ(leptris_element_attribute_int(root, "u", 0), 123456);

    EXPECT_EQ(leptris_element_set_attribute_double(root, "f", 3.14), LEPTRIS_OK);
    EXPECT_DOUBLE_EQ(leptris_element_attribute_double(root, "f", 0.0), 3.14);

    EXPECT_EQ(leptris_element_set_attribute_bool(root, "flag", 1), LEPTRIS_OK);
    /* bool is stored as "true"/"false" string; int accessor parses it back. */
    EXPECT_EQ(leptris_element_attribute_int(root, "flag", 0), 0);  /* "true" is not a number */

    leptris_document_free(doc);
}

TEST(ElementRemoveAttribute, DeletesByName) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r a='1' b='2' c='3'/>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    EXPECT_EQ(leptris_element_remove_attribute(root, "b"), LEPTRIS_OK);
    EXPECT_EQ(leptris_element_attribute(root, "b"), nullptr);
    /* Other attributes survive. */
    EXPECT_STREQ(leptris_element_attribute(root, "a"), "1");
    EXPECT_STREQ(leptris_element_attribute(root, "c"), "3");

    /* Removing a missing attribute should not crash; status code reflects it. */
    LeptrisStatus rc = leptris_element_remove_attribute(root, "missing");
    EXPECT_NE(rc, LEPTRIS_OK);

    leptris_document_free(doc);
}

TEST(ElementChildMutation, PrependChildAddsAtBeginning) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r><a/></r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement new_child = leptris_element_create(doc, "new");
    ASSERT_NE(new_child, nullptr);

    EXPECT_EQ(leptris_element_prepend_child(root, new_child), LEPTRIS_OK);

    /* new_child should be first, a second. */
    LeptrisElement first = leptris_element_first_child_any(root);
    ASSERT_NE(first, nullptr);
    EXPECT_STREQ(leptris_element_name(first), "new");
    LeptrisElement second = leptris_element_next_sibling_any(first);
    ASSERT_NE(second, nullptr);
    EXPECT_STREQ(leptris_element_name(second), "a");

    leptris_document_free(doc);
}

TEST(ElementChildMutation, InsertBeforeAndAfter) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r><b/></r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement b = leptris_element_first_child_any(root);
    ASSERT_NE(b, nullptr);

    LeptrisElement a = leptris_element_create(doc, "a");
    LeptrisElement c = leptris_element_create(doc, "c");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(c, nullptr);

    EXPECT_EQ(leptris_element_insert_before(b, a), LEPTRIS_OK);
    EXPECT_EQ(leptris_element_insert_after(b, c), LEPTRIS_OK);

    /* Expected order: a, b, c. */
    LeptrisElement cur = leptris_element_first_child_any(root);
    ASSERT_NE(cur, nullptr);
    EXPECT_STREQ(leptris_element_name(cur), "a");
    cur = leptris_element_next_sibling_any(cur);
    ASSERT_NE(cur, nullptr);
    EXPECT_STREQ(leptris_element_name(cur), "b");
    cur = leptris_element_next_sibling_any(cur);
    ASSERT_NE(cur, nullptr);
    EXPECT_STREQ(leptris_element_name(cur), "c");
    EXPECT_EQ(leptris_element_next_sibling_any(cur), nullptr);

    leptris_document_free(doc);
}

TEST(ElementChildMutation, RemoveChildUnlinksSpecificChild) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r><a/><b/><c/></r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement a = leptris_element_first_child_any(root);
    LeptrisElement b = leptris_element_next_sibling_any(a);

    EXPECT_EQ(leptris_element_remove_child(root, b), LEPTRIS_OK);

    /* Remaining: a, c. */
    LeptrisElement cur = leptris_element_first_child_any(root);
    ASSERT_NE(cur, nullptr);
    EXPECT_STREQ(leptris_element_name(cur), "a");
    cur = leptris_element_next_sibling_any(cur);
    ASSERT_NE(cur, nullptr);
    EXPECT_STREQ(leptris_element_name(cur), "c");
    EXPECT_EQ(leptris_element_next_sibling_any(cur), nullptr);

    leptris_document_free(doc);
}

TEST(ElementChildMutation, RemoveChildrenClearsAll) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r><a/><b/><c/></r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisElement root = leptris_document_root(doc);
    EXPECT_EQ(leptris_element_remove_children(root), LEPTRIS_OK);

    /* No children remain. */
    EXPECT_EQ(leptris_element_first_child_any(root), nullptr);

    /* Idempotent: removing again should still succeed. */
    EXPECT_EQ(leptris_element_remove_children(root), LEPTRIS_OK);

    leptris_document_free(doc);
}

// ---- Status contract (TODO 98) -----------------------------------------
//
// leptris_parse_string's status out-param must use the public LeptrisStatus
// enum.  An earlier build wrote the internal leptris_error_code value
// LEPTRIS_ERROR_MEMORY_ALLOCATION (=1) on allocation failure, which is
// outside the public enum range and broke every caller that compared
// against LEPTRIS_ERROR_MEMORY (=-1).  The negative-XML and NULL-input
// paths are the only failure modes reachable from a unit test without
// an allocator hook; both must produce documented public codes.

TEST(ParseStatusContract, NullInputYieldsPublicError) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(nullptr, 0, &st);
    EXPECT_EQ(doc, nullptr);
    EXPECT_NE(st, LEPTRIS_OK);
    /* st must be one of the public LeptrisStatus error codes — never
     * a positive internal leptris_error_code value. */
    EXPECT_LT(st, 0)
        << "status=" << static_cast<int>(st) << " is not a public error code";
}

TEST(ParseStatusContract, EmptyInputYieldsPublicError) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "";
    LeptrisDocument doc = leptris_parse_string(xml, 0, &st);
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
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] =
        "<root attr1='a'>"
        "  <child id='1'>alpha</child>"
        "  <child id='2'>beta</child>"
        "  <child id='3'>gamma</child>"
        "</root>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    // XPath descendant axis uses leptris_elem_first_child + leptris_node_get_next_sibling
    // — the offset-encoded traversal. 3 child elements must be visible.
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "//child");
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(r), 3u);
    leptris_xpath_result_free(r);

    // Mutation exercises leptris_elem_set_*, which encodes offsets.
    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(leptris_element_set_attribute(root, "new", "value"), LEPTRIS_OK);

    // Append a 4th child — exercises set_last_child + set_next_sibling.
    LeptrisElement new_child = leptris_element_create(doc, "child");
    ASSERT_NE(new_child, nullptr);
    EXPECT_EQ(leptris_element_append_child(root, new_child), LEPTRIS_OK);

    // Re-walk via XPath — must see 4 children now.
    LeptrisXPathResult r2 = leptris_xpath_eval(doc, nullptr, "//child");
    ASSERT_NE(r2, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(r2), 4u);
    leptris_xpath_result_free(r2);

    // Serialize — exercises the same edges in reverse.
    char* serialized = leptris_document_serialize(doc, nullptr);
    ASSERT_NE(serialized, nullptr);
    EXPECT_STRNE(serialized, "");
    EXPECT_NE(std::string(serialized).find("new=\"value\""), std::string::npos);
    leptris_free_string(serialized);

    leptris_document_free(doc);
}

TEST(CompactPointerIntegration, MixedContentTreeWalksCorrectly) {
    // The walker visits every child regardless of type — TODO 109 + Phase 2c.
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r>text1<!-- comment -->text2<?pi data?></r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    // count(node()) walks the offset-encoded sibling chain through every
    // node type (text, comment, text, PI).
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "count(/r/node())");
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(leptris_xpath_result_number(r), 4.0);
    leptris_xpath_result_free(r);

    leptris_document_free(doc);
}

TEST(CompactPointerIntegration, DeepNestingTraversesViaOffsets) {
    // 50-level deep nesting — exercises offset arithmetic at every depth.
    // Pool-allocated elements stay within int32_t range; no overflow.
    std::string xml = "<a0>";
    for (int i = 1; i < 50; i++) xml += "<a" + std::to_string(i) + ">";
    for (int i = 49; i >= 0; i--) xml += "</a" + std::to_string(i) + ">";

    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml.data(), xml.size(), &st);
    ASSERT_NE(doc, nullptr);

    // count(//*) walks every element via the offset-encoded edges.
    // The deeply-nested tree has 50 elements; if any offset corrupts,
    // the count would differ.
    LeptrisXPathResult r = leptris_xpath_eval(doc, nullptr, "count(//*)");
    ASSERT_NE(r, nullptr);
    EXPECT_DOUBLE_EQ(leptris_xpath_result_number(r), 50.0);
    leptris_xpath_result_free(r);

    // count(//a49) finds the deepest element via 49 chained first_child
    // offset decodes.
    LeptrisXPathResult r2 = leptris_xpath_eval(doc, nullptr, "count(//a49)");
    ASSERT_NE(r2, nullptr);
    EXPECT_DOUBLE_EQ(leptris_xpath_result_number(r2), 1.0);
    leptris_xpath_result_free(r2);

    leptris_document_free(doc);
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
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);
    EXPECT_STREQ(leptris_element_name(root), "root");
    EXPECT_STREQ(leptris_element_attribute(root, "attr"), "value");
    EXPECT_STREQ(leptris_element_attribute(root, "empty"), "");

    LeptrisElement child = leptris_element_first_child_any(root);
    ASSERT_NE(child, nullptr);
    EXPECT_STREQ(leptris_element_name(child), "child");
    EXPECT_STREQ(leptris_element_attribute(child, "id"), "1");

    // Self-closing — name was NUL-terminated at '/'.
    LeptrisElement sc = leptris_element_next_sibling_any(child);
    ASSERT_NE(sc, nullptr);
    EXPECT_STREQ(leptris_element_name(sc), "self-closing");
    EXPECT_STREQ(leptris_element_attribute(sc, "enabled"), "yes");

    leptris_document_free(doc);
}

TEST(ZeroCopyParse, NamespacedElementExposesLocalName) {
    const char xml[] =
        "<root xmlns:xi='http://www.w3.org/2001/XInclude'>"
        "<xi:include href='chapter.xml'/>"
        "</root>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement include = leptris_element_first_child_any(root);
    ASSERT_NE(include, nullptr);
    // Local name only — prefix "xi:" was stripped during parse.
    EXPECT_STREQ(leptris_element_name(include), "include");
    EXPECT_STREQ(leptris_element_attribute(include, "href"), "chapter.xml");

    leptris_document_free(doc);
}

TEST(ZeroCopyParse, AttributeValueWithEntitiesDecodesCorrectly) {
    const char xml[] =
        "<root encoded='a&amp;b&lt;c&gt;d' normal='plain'/>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisElement root = leptris_document_root(doc);
    EXPECT_STREQ(leptris_element_attribute(root, "encoded"), "a&b<c>d");
    EXPECT_STREQ(leptris_element_attribute(root, "normal"), "plain");

    leptris_document_free(doc);
}

TEST(ZeroCopyParse, EmptyElementRoundTrips) {
    const char xml[] = "<root><child></child><empty/></root>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement child = leptris_element_first_child_any(root);
    ASSERT_NE(child, nullptr);
    EXPECT_STREQ(leptris_element_name(child), "child");
    EXPECT_EQ(leptris_element_first_child_any(child), nullptr);

    LeptrisElement empty = leptris_element_next_sibling_any(child);
    ASSERT_NE(empty, nullptr);
    EXPECT_STREQ(leptris_element_name(empty), "empty");
    EXPECT_EQ(leptris_element_first_child_any(empty), nullptr);

    leptris_document_free(doc);
}

namespace {

struct TraverseCollector {
    std::string names_pre;
    std::string names_post;
    int stop_after;
    int calls;
};

static int collect_pre(LeptrisNodeRef node, void* ud) {
    auto* c = static_cast<TraverseCollector*>(ud);
    c->calls++;
    if (c->stop_after > 0 && c->calls > c->stop_after) return 1;
    if (leptris_node_get_type(node) == kNodeTypeElement) {
        if (!c->names_pre.empty()) c->names_pre += ",";
        c->names_pre += leptris_element_name((LeptrisElement)node);
    }
    return 0;
}

static int collect_post(LeptrisNodeRef node, void* ud) {
    auto* c = static_cast<TraverseCollector*>(ud);
    c->calls++;
    if (c->stop_after > 0 && c->calls > c->stop_after) return 1;
    if (leptris_node_get_type(node) == kNodeTypeElement) {
        if (!c->names_post.empty()) c->names_post += ",";
        c->names_post += leptris_element_name((LeptrisElement)node);
    }
    return 0;
}

}  // namespace

TEST(NodeTraverse, PreOrderVisitsParentBeforeChildren) {
    const char xml[] =
        "<a><b><d/></b><c/></a>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    TraverseCollector col{ "", "", 0, 0 };
    int n = leptris_node_traverse(leptris_element_as_node(root),
                                 LEPTRIS_TRAVERSE_PRE_ORDER,
                                 collect_pre, &col);
    EXPECT_EQ(n, 4);
    EXPECT_EQ(col.names_pre, "a,b,d,c");
    leptris_document_free(doc);
}

TEST(NodeTraverse, PostOrderVisitsChildrenBeforeParent) {
    const char xml[] =
        "<a><b><d/></b><c/></a>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    TraverseCollector col{ "", "", 0, 0 };
    int n = leptris_node_traverse(leptris_element_as_node(root),
                                 LEPTRIS_TRAVERSE_POST_ORDER,
                                 collect_post, &col);
    EXPECT_EQ(n, 4);
    EXPECT_EQ(col.names_post, "d,b,c,a");
    leptris_document_free(doc);
}

TEST(NodeTraverse, IncludesTextAndElementChildren) {
    const char xml[] = "<r>hello<x/>world</r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    int calls = 0;
    leptris_node_traverse(leptris_element_as_node(root),
                         LEPTRIS_TRAVERSE_PRE_ORDER,
                         [](LeptrisNodeRef, void* p) -> int {
                             (*static_cast<int*>(p))++;
                             return 0;
                         }, &calls);
    /* root + text("hello") + x + text("world") = 4 */
    EXPECT_EQ(calls, 4);
    leptris_document_free(doc);
}

TEST(NodeTraverse, EarlyStopReturnsCountVisited) {
    const char xml[] = "<a><b/><c/><d/></a>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    TraverseCollector col{ "", "", 2, 0 };
    int n = leptris_node_traverse(leptris_element_as_node(root),
                                 LEPTRIS_TRAVERSE_PRE_ORDER,
                                 collect_pre, &col);
    /* stop_after=2 → callback returns non-zero on its 3rd invocation */
    EXPECT_EQ(n, 2);
    leptris_document_free(doc);
}

TEST(NodeTraverse, NullArgsReturnNegativeOne) {
    const char xml[] = "<r/>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    EXPECT_EQ(leptris_node_traverse(nullptr,
                                   LEPTRIS_TRAVERSE_PRE_ORDER,
                                   collect_pre, nullptr), -1);
    EXPECT_EQ(leptris_node_traverse(leptris_element_as_node(root),
                                   LEPTRIS_TRAVERSE_PRE_ORDER,
                                   nullptr, nullptr), -1);
    leptris_document_free(doc);
}
