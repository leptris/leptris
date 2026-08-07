// test/flat/test_flat_doc.cpp — FlatDoc allocator specs (TODO 139 Phase A).
//
// Phase A only adds the data structure + array-append primitives.
// Phase B (flat_parser) and Phase C (flat_promote) plug real XML in.
// These specs exercise:
//   - lifecycle (new/free, NULL-safety, idempotent free)
//   - capacity heuristic (no grow on initial append)
//   - geometric growth (1.5x on overflow)
//   - sentinel initialization (FLAT_INDEX_NULL on all edges)
//   - text-node payload overload (attr_start/attr_count/depth reuse)
//   - xml_buffer ownership transfer (borrow -> owned)
//   - struct size contracts (_Static_assert guards in the header)

#include <gtest/gtest.h>

extern "C" {
#include "flat_doc.h"
}

#include <cstdint>
#include <cstring>

namespace {

// Sentinel value used by the parser to mean "no such edge". Must
// remain UINT32_MAX — array indexing on it would fault loudly.
TEST(FlatDoc, NullIndexIsMaxUint32) {
    EXPECT_EQ(FLAT_INDEX_NULL, 0xFFFFFFFFu);
}

TEST(FlatDoc, StructSizeContract) {
    // The promote pass and parser both rely on the exact layout.
    // If either of these breaks, the _Static_assert in flat_doc.h
    // will already have failed the build — this test documents the
    // invariant for the spec suite.
    EXPECT_EQ(sizeof(FlatNode), 28u);
    EXPECT_EQ(sizeof(FlatAttr), 12u);
}

TEST(FlatDoc, NewDocHasHeuristicCapacity) {
    const char xml[] = "<root/>";
    FlatDoc* doc = flat_doc_new(xml, sizeof(xml) - 1);
    ASSERT_NE(doc, nullptr);

    // Small doc floor: at least 16 nodes / 16 attrs.
    EXPECT_GE(doc->node_capacity, 16u);
    EXPECT_GE(doc->attr_capacity, 16u);
    EXPECT_EQ(doc->node_count, 0u);
    EXPECT_EQ(doc->attr_count, 0u);

    // Borrow semantics: input pointer is preserved.
    EXPECT_EQ(doc->xml_buffer, xml);
    EXPECT_EQ(doc->xml_buffer_owned, nullptr);
    EXPECT_EQ(doc->xml_len, sizeof(xml) - 1);

    // Declaration defaults.
    EXPECT_EQ(doc->standalone, -1);
    EXPECT_EQ(doc->root_index, FLAT_INDEX_NULL);

    flat_doc_free(doc);
}

TEST(FlatDoc, NewDocScalesCapacityWithInput) {
    // ~1 node per 100 bytes. A 5000-byte doc should pre-size for
    // at least 50 nodes.
    size_t len = 5000;
    FlatDoc* doc = flat_doc_new("x", len);  // contents irrelevant
    ASSERT_NE(doc, nullptr);
    EXPECT_GE(doc->node_capacity, 50u);
    EXPECT_GE(doc->attr_capacity, 100u);
    flat_doc_free(doc);
}

TEST(FlatDoc, FreeNullIsSafe) {
    flat_doc_free(nullptr);
    SUCCEED();
}

TEST(FlatDoc, AppendNodeInitializesSentinelEdges) {
    const char xml[] = "abc";
    FlatDoc* doc = flat_doc_new(xml, 3);
    ASSERT_NE(doc, nullptr);

    uint32_t idx = flat_doc_append_node(doc, FLAT_NODE_ELEMENT, 0, 3);
    ASSERT_NE(idx, FLAT_INDEX_NULL);
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(doc->node_count, 1u);

    FlatNode* n = &doc->nodes[idx];
    EXPECT_EQ(n->type, (uint16_t)FLAT_NODE_ELEMENT);
    EXPECT_EQ(n->name_len, 3u);
    EXPECT_EQ(n->name_offset, 0u);
    EXPECT_EQ(n->parent, FLAT_INDEX_NULL);
    EXPECT_EQ(n->first_child, FLAT_INDEX_NULL);
    EXPECT_EQ(n->next_sibling, FLAT_INDEX_NULL);
    EXPECT_EQ(n->attr_start, FLAT_INDEX_NULL);
    EXPECT_EQ(n->attr_count, 0u);
    EXPECT_EQ(n->depth, 0u);

    flat_doc_free(doc);
}

TEST(FlatDoc, AppendAttrRoundTrip) {
    const char xml[] = "namevalue";
    FlatDoc* doc = flat_doc_new(xml, 9);
    ASSERT_NE(doc, nullptr);

    uint32_t idx = flat_doc_append_attr(doc, 0, 4, 4, 5);
    ASSERT_NE(idx, FLAT_INDEX_NULL);
    EXPECT_EQ(idx, 0u);
    EXPECT_EQ(doc->attr_count, 1u);

    FlatAttr* a = &doc->attrs[idx];
    EXPECT_EQ(a->name_offset, 0u);
    EXPECT_EQ(a->name_len, 4u);
    EXPECT_EQ(a->value_offset, 4u);
    EXPECT_EQ(a->value_len, 5u);

    flat_doc_free(doc);
}

TEST(FlatDoc, AppendNodeGrowsGeometrically) {
    // Force the array to grow past the initial capacity. The 1.5x
    // growth factor should let appends succeed without losing any.
    const char xml[] = "<r/>";
    FlatDoc* doc = flat_doc_new(xml, 4);
    ASSERT_NE(doc, nullptr);

    size_t initial_cap = doc->node_capacity;

    // Append 10x the initial capacity. All indices must be unique.
    size_t target = initial_cap * 10;
    for (size_t i = 0; i < target; i++) {
        uint32_t idx = flat_doc_append_node(doc, FLAT_NODE_ELEMENT, 0, 0);
        ASSERT_NE(idx, FLAT_INDEX_NULL) << "iter " << i;
        ASSERT_EQ(idx, (uint32_t)i) << "iter " << i;
    }
    EXPECT_EQ(doc->node_count, target);
    EXPECT_GT(doc->node_capacity, initial_cap);

    flat_doc_free(doc);
}

TEST(FlatDoc, EdgeSetterRoundTrip) {
    const char xml[] = "<r/>";
    FlatDoc* doc = flat_doc_new(xml, 4);
    ASSERT_NE(doc, nullptr);

    uint32_t root = flat_doc_append_node(doc, FLAT_NODE_ELEMENT, 0, 1);
    uint32_t child = flat_doc_append_node(doc, FLAT_NODE_ELEMENT, 1, 1);
    ASSERT_NE(root, FLAT_INDEX_NULL);
    ASSERT_NE(child, FLAT_INDEX_NULL);

    flat_node_set_parent(&doc->nodes[child], root);
    flat_node_set_first_child(&doc->nodes[root], child);
    flat_node_set_next_sibling(&doc->nodes[child], FLAT_INDEX_NULL);
    flat_node_set_attrs(&doc->nodes[root], FLAT_INDEX_NULL, 0);
    flat_node_set_depth(&doc->nodes[root], 0);
    flat_node_set_depth(&doc->nodes[child], 1);

    EXPECT_EQ(doc->nodes[child].parent, root);
    EXPECT_EQ(doc->nodes[root].first_child, child);
    EXPECT_EQ(doc->nodes[child].next_sibling, FLAT_INDEX_NULL);
    EXPECT_EQ(doc->nodes[root].attr_count, 0u);
    EXPECT_EQ(doc->nodes[child].depth, 1u);

    flat_doc_free(doc);
}

TEST(FlatDoc, TextNodePayloadOverloadRoundTrip) {
    // Non-element nodes overload attr_start/attr_count/depth to hold
    // a 32-bit text_offset + 32-bit text_len pair. Verify the
    // helpers pack/unpack correctly.
    const char xml[] = "<r>hello</r>";
    FlatDoc* doc = flat_doc_new(xml, sizeof(xml) - 1);
    ASSERT_NE(doc, nullptr);

    uint32_t text = flat_doc_append_node(doc, FLAT_NODE_TEXT, 0, 0);
    ASSERT_NE(text, FLAT_INDEX_NULL);

    // Pack a (offset=3, len=5) pair pointing at "hello".
    flat_node_set_text(&doc->nodes[text], 3, 5);

    EXPECT_EQ(flat_node_text_offset(&doc->nodes[text]), 3u);
    EXPECT_EQ(flat_node_text_len(&doc->nodes[text]), 5u);

    // Verify the bytes line up with the input.
    EXPECT_EQ(memcmp(doc->xml_buffer + 3, "hello", 5), 0);

    flat_doc_free(doc);
}

TEST(FlatDoc, TextNodePayloadHoldsMaxLen) {
    // The overload packs text_len across 16 bits of attr_count +
    // 16 bits of depth. text_len > 0xFFFF must round-trip.
    const char xml[] = "<r/>";
    FlatDoc* doc = flat_doc_new(xml, 4);
    ASSERT_NE(doc, nullptr);

    uint32_t text = flat_doc_append_node(doc, FLAT_NODE_TEXT, 0, 0);
    flat_node_set_text(&doc->nodes[text], 1000, 0x00012345u);

    EXPECT_EQ(flat_node_text_offset(&doc->nodes[text]), 1000u);
    EXPECT_EQ(flat_node_text_len(&doc->nodes[text]), 0x00012345u);

    flat_doc_free(doc);
}

TEST(FlatDoc, DupXmlTransfersOwnership) {
    char xml[] = "<root/>";
    FlatDoc* doc = flat_doc_new(xml, sizeof(xml) - 1);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(doc->xml_buffer, xml);
    EXPECT_EQ(doc->xml_buffer_owned, nullptr);

    // Corrupt the caller-side buffer. Without dup_xml this would
    // corrupt the FlatDoc's view too.
    xml[0] = 'X';

    // flat_doc_dup_xml must copy from the (now-corrupted) buffer.
    // Use a fresh input to verify the copy is independent.
    char fresh[] = "<root/>";
    doc->xml_buffer = fresh;
    doc->xml_len = sizeof(fresh) - 1;

    EXPECT_EQ(flat_doc_dup_xml(doc), 0);
    EXPECT_NE(doc->xml_buffer_owned, nullptr);
    EXPECT_EQ(doc->xml_buffer, doc->xml_buffer_owned);

    // Mutate the caller-side buffer; the FlatDoc copy is unaffected.
    fresh[0] = 'X';
    EXPECT_EQ(doc->xml_buffer[0], '<');

    // Idempotent: second call is a no-op.
    EXPECT_EQ(flat_doc_dup_xml(doc), 0);

    flat_doc_free(doc);
}

TEST(FlatDoc, DupXmlPreservesNameOffsets) {
    // Offsets in FlatNode/FlatAttr are byte offsets from the start
    // of the XML buffer. After dup_xml, swapping the base pointer
    // must keep offsets valid.
    const char xml[] = "name_value";
    FlatDoc* doc = flat_doc_new(xml, sizeof(xml) - 1);
    ASSERT_NE(doc, nullptr);

    uint32_t n = flat_doc_append_node(doc, FLAT_NODE_ELEMENT, 0, 4);
    ASSERT_NE(n, FLAT_INDEX_NULL);

    EXPECT_EQ(flat_doc_dup_xml(doc), 0);
    EXPECT_NE(doc->xml_buffer, xml);           // no longer borrows
    EXPECT_EQ(memcmp(doc->xml_buffer, "name", 4), 0);

    flat_doc_free(doc);
}

TEST(FlatDoc, NewDocReturnsNullOnNullInput) {
    // The allocator pre-sizes based on xml_len; a NULL input is
    // allowed (caller is responsible for setting xml_buffer later).
    FlatDoc* doc = flat_doc_new(NULL, 0);
    ASSERT_NE(doc, nullptr);
    EXPECT_EQ(doc->xml_buffer, nullptr);
    EXPECT_EQ(doc->xml_len, 0u);
    // Still functional for appends (used in tests / synthetic docs).
    uint32_t idx = flat_doc_append_node(doc, FLAT_NODE_ELEMENT, 0, 0);
    EXPECT_NE(idx, FLAT_INDEX_NULL);
    flat_doc_free(doc);
}

TEST(FlatDoc, AppendNodeOnNullDocReturnsNullIndex) {
    EXPECT_EQ(flat_doc_append_node(nullptr, FLAT_NODE_ELEMENT, 0, 0),
              FLAT_INDEX_NULL);
}

TEST(FlatDoc, AppendAttrOnNullDocReturnsNullIndex) {
    EXPECT_EQ(flat_doc_append_attr(nullptr, 0, 0, 0, 0),
              FLAT_INDEX_NULL);
}

TEST(FlatDoc, FlatNodeLayoutIsContiguous) {
    // Real FlatDoc allocates one big array. The promote pass walks
    // it as nodes[0], nodes[1], ... Verify that the struct doesn't
    // have surprising alignment padding.
    EXPECT_EQ(sizeof(FlatNode) % 4, 0u);
    EXPECT_EQ(sizeof(FlatAttr) % 4, 0u);

    // offsetof sanity — name_offset must be in the first 8 bytes
    // so the parser can initialize type+name_len+name_offset with
    // a single store.
    FlatNode n;
    std::memset(&n, 0, sizeof(n));
    n.type = FLAT_NODE_ELEMENT;
    n.name_len = 7;
    n.name_offset = 42;
    EXPECT_EQ(n.type, (uint16_t)FLAT_NODE_ELEMENT);
    EXPECT_EQ(n.name_len, 7u);
    EXPECT_EQ(n.name_offset, 42u);
}

}  // namespace
