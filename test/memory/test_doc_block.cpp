// test/memory/test_doc_block.cpp - the node-layout fork's document
// block (slice 1). Falsifiable contract: offsets stay valid across
// geometric growth, regions stay dense/aligned, boundary inputs
// are refused.

#include <gtest/gtest.h>
#include <cstring>
#include <cstdio>
#include <vector>
#include "../../src/leptris/memory/doc_block.h"

typedef struct {
    uint32_t first_child;
    uint32_t next;
    uint32_t name_off;
} TNode;

TEST(DocBlock, OffsetsSurviveGrowth) {
    LeptrisDocBlock* b = leptris_doc_block_create(0);
    ASSERT_NE(b, nullptr);
    std::vector<uint32_t> offs;
    const int N = 20000;
    for (int i = 0; i < N; i++) {
        size_t off = leptris_doc_block_node_alloc(b, sizeof(TNode));
        ASSERT_NE(off, (size_t)-1);
        ASSERT_EQ(off % 8u, 0u);
        TNode* n = (TNode*)leptris_doc_block_node(b, off);
        ASSERT_NE(n, nullptr);
        n->first_child = 0;
        n->next = i > 0 ? offs[i - 1] : 0;
        n->name_off = 0;
        offs.push_back((uint32_t)off);
        if (i % 1000 == 0) {
            for (size_t k = 1; k < offs.size(); k++) {
                const TNode* p =
                    (const TNode*)leptris_doc_block_node(b, offs[k]);
                ASSERT_NE(p, nullptr);
                ASSERT_EQ(p->next, offs[k - 1]);
            }
        }
    }
    EXPECT_GT(leptris_doc_block_capacity(b), (size_t)0);
    leptris_doc_block_free(b);
}

TEST(DocBlock, StringRegionStableAcrossGrowth) {
    LeptrisDocBlock* b = leptris_doc_block_create(64);
    ASSERT_NE(b, nullptr);
    std::vector<size_t> offs;
    char buf[32];
    for (int i = 0; i < 5000; i++) {
        int n = snprintf(buf, sizeof(buf), "name-%05d", i);
        size_t off = leptris_doc_block_str(b, buf, (size_t)n);
        ASSERT_NE(off, (size_t)-1);
        offs.push_back(off);
    }
    for (int i = 0; i < 5000; i += 500) {
        int n = snprintf(buf, sizeof(buf), "name-%05d", i);
        const char* s = leptris_doc_block_str_at(b, offs[i]);
        ASSERT_NE(s, nullptr);
        EXPECT_EQ(std::memcmp(s, buf, (size_t)n), 0);
        EXPECT_EQ(s[n], '\0');
    }
    leptris_doc_block_free(b);
}

TEST(DocBlock, BoundaryInputsRefused) {
    LeptrisDocBlock* b = leptris_doc_block_create(0);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(leptris_doc_block_node_alloc(b, 0), (size_t)-1);
    EXPECT_EQ(leptris_doc_block_node_alloc(nullptr, 8), (size_t)-1);
    EXPECT_EQ(leptris_doc_block_str(b, nullptr, 3), (size_t)-1);
    size_t off = leptris_doc_block_str(b, "", 0);
    EXPECT_NE(off, (size_t)-1);
    EXPECT_STREQ(leptris_doc_block_str_at(b, off), "");
    EXPECT_EQ(leptris_doc_block_node(b, 1 << 20), nullptr);
    leptris_doc_block_free(b);
    leptris_doc_block_free(nullptr);
}

TEST(DocBlock, NodesAndStringsIndependent) {
    LeptrisDocBlock* b = leptris_doc_block_create(0);
    ASSERT_NE(b, nullptr);
    size_t n1 = leptris_doc_block_node_alloc(b, 16);
    size_t s1 = leptris_doc_block_str(b, "abc", 3);
    size_t n2 = leptris_doc_block_node_alloc(b, 16);
    size_t s2 = leptris_doc_block_str(b, "defg", 4);
    EXPECT_NE(n1, n2);
    EXPECT_NE(s1, s2);
    EXPECT_STREQ(leptris_doc_block_str_at(b, s1), "abc");
    EXPECT_STREQ(leptris_doc_block_str_at(b, s2), "defg");
    EXPECT_EQ(leptris_doc_block_nodes_used(b), 32u);
    leptris_doc_block_free(b);
}
