// test/dom/test_vtable.cpp — Node vtable specs (TODO 23/29/30).

#include <gtest/gtest.h>
#include <cstring>
#include "taurus.h"

extern "C" {
/* Internal headers — required to inspect the vtable registry. */
#include "node.h"
#include "text.h"
#include "comment.h"
}

namespace {

// ---- Contract: enum values are stable across ABI changes -------------------

constexpr int kNodeTypeElement = 0;
constexpr int kNodeTypeText    = 1;

TEST(NodeVTableContract, TextTypeEnumIsLocked) {
    EXPECT_EQ(TAURUS_NODE_TYPE_TEXT, kNodeTypeText);
}

TEST(NodeVTableContract, ElementTypeEnumIsLocked) {
    EXPECT_EQ(TAURUS_NODE_TYPE_ELEMENT, kNodeTypeElement);
}

// ---- Registry: every concrete node type has a registered vtable -----------

TEST(NodeVTableRegistry, EveryConcreteTypeHasVtable) {
    EXPECT_NE(taurus_node_vtable_for(TAURUS_NODE_TYPE_ELEMENT), nullptr);
    EXPECT_NE(taurus_node_vtable_for(TAURUS_NODE_TYPE_TEXT),    nullptr);
    EXPECT_NE(taurus_node_vtable_for(TAURUS_NODE_TYPE_COMMENT), nullptr);
    EXPECT_NE(taurus_node_vtable_for(TAURUS_NODE_TYPE_CDATA),   nullptr);
    EXPECT_NE(taurus_node_vtable_for(TAURUS_NODE_TYPE_PI),      nullptr);
    EXPECT_NE(taurus_node_vtable_for(TAURUS_NODE_TYPE_DOCTYPE), nullptr);
}

TEST(NodeVTableRegistry, AttributeTypeHasNoVtable) {
    /* XPath-internal node type, not serialized via the registry. */
    EXPECT_EQ(taurus_node_vtable_for(TAURUS_NODE_TYPE_ATTRIBUTE), nullptr);
}

TEST(NodeVTableRegistry, OutOfRangeReturnsNull) {
    EXPECT_EQ(taurus_node_vtable_for((TaurusNodeTypeEnum) 99), nullptr);
    EXPECT_EQ(taurus_node_vtable_for((TaurusNodeTypeEnum) -1), nullptr);
}

TEST(NodeVTableRegistry, TypeNameIsHumanReadable) {
    EXPECT_STREQ(taurus_node_vtable_for(TAURUS_NODE_TYPE_ELEMENT)->type_name, "element");
    EXPECT_STREQ(taurus_node_vtable_for(TAURUS_NODE_TYPE_TEXT)->type_name,    "text");
    EXPECT_STREQ(taurus_node_vtable_for(TAURUS_NODE_TYPE_COMMENT)->type_name, "comment");
    EXPECT_STREQ(taurus_node_vtable_for(TAURUS_NODE_TYPE_CDATA)->type_name,   "cdata");
    EXPECT_STREQ(taurus_node_vtable_for(TAURUS_NODE_TYPE_PI)->type_name,      "pi");
    EXPECT_STREQ(taurus_node_vtable_for(TAURUS_NODE_TYPE_DOCTYPE)->type_name, "doctype");
}

TEST(NodeVTableRegistry, TypeEnumMatchesIndex) {
    /* Sanity: each vtable's type_enum matches its slot in the registry. */
    for (int i = 0; i < TAURUS_NODE_TYPE_COUNT; i++) {
        const TaurusNodeVTable* vt = taurus_node_vtable_for((TaurusNodeTypeEnum)i);
        if (vt) {
            EXPECT_EQ(vt->type_enum, (TaurusNodeTypeEnum)i);
        }
    }
}

// ---- Dispatch: serializer routes through the registry ---------------------

TEST(NodeVTableDispatch, EveryNodeTypeSerializesViaVtable) {
    /* Document containing one of every node type.  The point is to
     * exercise every per-type serialize callback — not to assert
     * exact byte-for-byte output (the serializer normalizes quote
     * style and adds an XML declaration by default; that's
     * pre-existing behavior, not vtable-related). */
    const char xml[] =
        "<?xml version='1.0'?>"
        "<!-- doc-level comment -->"
        "<r attr='v'><!-- nested -->text<![CDATA[raw]]><?pi data?>"
        "<child/></r>";

    TaurusStatus st;
    TaurusDocument doc = taurus_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    /* taurus_document_serialize returns NULL only on allocation failure.
     * Each vtable's serialize callback must run without crashing. */
    char* out = taurus_document_serialize(doc, NULL);
    ASSERT_NE(out, nullptr);
    EXPECT_GT(std::strlen(out), 0u);

    /* Spot-check that every node type's output appears:
     * element/attr/text/comment/cdata/pi are all represented. */
    std::string s(out);
    EXPECT_NE(s.find("<r"),            std::string::npos);  // element
    EXPECT_NE(s.find("attr="),         std::string::npos);  // attribute
    EXPECT_NE(s.find("text"),          std::string::npos);  // text
    EXPECT_NE(s.find("<!--"),          std::string::npos);  // comment
    EXPECT_NE(s.find("<![CDATA["),     std::string::npos);  // cdata
    EXPECT_NE(s.find("<?pi"),          std::string::npos);  // pi
    EXPECT_NE(s.find("<child"),        std::string::npos);  // child element

    taurus_free_string(out);
    taurus_document_free(doc);
}

}  // namespace
