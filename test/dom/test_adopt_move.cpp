// test/dom/test_adopt_move.cpp — #1548 move/steal node adoption.
//
// leptris_document_adopt_move(dst, src, element, &moved) transfers a
// subtree's storage from src into dst: the source handle becomes void,
// the destination receives a fully re-homed subtree (interned names,
// re-stamped document backreferences), and the source document stays
// valid minus the moved subtree — free(src) before or after dst is
// both safe. Nothing in dst references src after the move, so this is
// the transfer face that does NOT pin the source pool into dst
// (absorb's lifetime) and does NOT re-duplicate every string instance
// (the #1529 deep copy's live-set doubling).
//
// DESIGN LAW (owner directive, #1548): adoption/transfer paths are
// MOVE or COPY-ON-WRITE — never eager deep copy. A recursive per-node
// copy that doubles the live set is a design defect.
//
// Falsifiability: every spec either walks every view after a source
// free (the #1528 detonation class) or asserts a rejection contract.

#include <gtest/gtest.h>

extern "C" {
#include "leptris.h"
}

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

LeptrisDocument ParseDoc(const char* xml) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc =
        leptris_parse_string(xml, std::strlen(xml), &st);
    EXPECT_EQ(st, LEPTRIS_OK);
    return doc;
}

/* Walk every readable view of the subtree — the #1646 ReadAllViews
 * pattern: any dangling reference (a stale name slot, a buffer-
 * resident attr view, a src-pool ns cache) detonates here, not at
 * the user. */
void ReadAllViews(LeptrisElement e) {
    ASSERT_NE(e, nullptr);
    const char* name = leptris_element_name(e);
    if (name) EXPECT_FALSE(std::string(name).empty());

    size_t ac = leptris_element_attribute_count(e);
    if (ac > 0) {
        size_t need = leptris_element_attributes_snapshot(
            e, nullptr, 0, nullptr, nullptr, 0, nullptr, nullptr, 0,
            nullptr, nullptr, 0, nullptr);
        std::vector<char> nb(need), vb(need), pb(need), ub(need);
        std::vector<size_t> no(ac + 1), vo(ac + 1), po(ac + 1),
            uo(ac + 1);
        ASSERT_EQ(leptris_element_attributes_snapshot(
                      e, nb.data(), need, no.data(), vb.data(), need,
                      vo.data(), pb.data(), need, po.data(), ub.data(),
                      need, uo.data()),
                  0u);
    }

    for (LeptrisNodeRef c = leptris_node_first_child(
             leptris_element_as_node(e));
         c; c = leptris_node_next_sibling(c)) {
        switch (leptris_node_get_type(c)) {
            case LEPTRIS_NODE_TYPE_ELEMENT:
                ReadAllViews(leptris_node_as_element(c));
                break;
            case LEPTRIS_NODE_TYPE_TEXT:
                if (leptris_text_node_get_content(c))
                    (void)std::string(leptris_text_node_get_content(c));
                break;
            case LEPTRIS_NODE_TYPE_COMMENT:
                (void)leptris_comment_node_get_content(c);
                break;
            case LEPTRIS_NODE_TYPE_CDATA:
                (void)leptris_cdata_node_get_content(c);
                break;
            case LEPTRIS_NODE_TYPE_PI:
                (void)leptris_pi_node_get_target(c);
                (void)leptris_pi_node_get_data(c);
                break;
            default:
                break;
        }
    }
}

const char* kFragmentXml =
    "<frag xmlns:p='urn:p' id='f1' p:kind='top'>"
    "<meta id='m1' xmi:type='uml:Class' guid='aaaaaaaa-1111'>"
    "head text</meta>"
    "<body><item id='i1'>one</item><item id='i2'>two</item>"
    "<![CDATA[raw <stuff>]]><!-- a comment -->"
    "<?proc instr?></body>"
    "</frag>";

const char* kDstXml = "<main><existing id='x'/></main>";

/* Countdown allocator (the OOM-injection harness pattern): the
 * allocation numbered `fail_at` (1-based) returns NULL. */
long g_adopt_countdown = -1;

void* adopt_count_alloc(size_t size) {
    if (g_adopt_countdown == 0) return nullptr;
    if (g_adopt_countdown > 0) g_adopt_countdown--;
    return std::malloc(size);
}

void adopt_count_free(void* p) { std::free(p); }

}  // namespace

/* The core contract: move a fragment out of a scratch document into
 * the destination, splice it, free the source FIRST, and everything
 * reachable from dst must read and serialize exactly. */
TEST(AdoptMove, MovesSubtreeSurvivesSourceFree) {
    LeptrisDocument dst = ParseDoc(kDstXml);
    ASSERT_NE(dst, nullptr);
    LeptrisDocument src = ParseDoc(kFragmentXml);
    ASSERT_NE(src, nullptr);

    LeptrisElement frag = leptris_document_root(src);
    ASSERT_NE(frag, nullptr);
    LeptrisElement moved = nullptr;
    ASSERT_EQ(leptris_document_adopt_move(dst, src, frag, &moved),
              LEPTRIS_OK);
    ASSERT_NE(moved, nullptr);

    /* Splice: same-document now — the append must not re-copy. */
    LeptrisElement droot = leptris_document_root(dst);
    ASSERT_EQ(leptris_element_append_child(droot, moved), LEPTRIS_OK);

    /* Source dies BEFORE any moved read: the #1528 ordering. */
    leptris_document_free(src);

    char* out = leptris_document_serialize(dst, nullptr);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out,
                 "<main><existing id=\"x\"/><frag xmlns:p=\"urn:p\" "
                 "id=\"f1\" p:kind=\"top\"><meta id=\"m1\" "
                 "xmi:type=\"uml:Class\" "
                 "guid=\"aaaaaaaa-1111\">head text</meta><body>"
                 "<item id=\"i1\">one</item><item id=\"i2\">two</item>"
                 "<![CDATA[raw <stuff>]]><!-- a comment -->"
                 "<?proc instr?></body></frag></main>");
    leptris_free_string(out);

    ReadAllViews(droot);
    leptris_document_free(dst);
}

/* The other ordering: dst (and the moved subtree) die before the
 * source. The move must not have left any dst->src or src->dst
 * linkage that detonates at either free. */
TEST(AdoptMove, DestinationFreeBeforeSourceFree) {
    LeptrisDocument dst = ParseDoc(kDstXml);
    LeptrisDocument src = ParseDoc(kFragmentXml);
    ASSERT_NE(dst, nullptr);
    ASSERT_NE(src, nullptr);
    LeptrisElement moved = nullptr;
    ASSERT_EQ(leptris_document_adopt_move(
                  dst, src, leptris_document_root(src), &moved),
              LEPTRIS_OK);
    ASSERT_EQ(leptris_element_append_child(leptris_document_root(dst),
                                           moved),
              LEPTRIS_OK);
    leptris_document_free(dst);
    leptris_document_free(src);
}

/* The source remains a live, usable document minus the moved
 * subtree: siblings keep their order, and a second move of the same
 * element is rejected (it now belongs to dst). */
TEST(AdoptMove, SourceSurvivesMinusSubtree) {
    static const char kSrc[] =
        "<w><keep1/><frag><deep/></frag><keep2/></w>";
    LeptrisDocument dst = ParseDoc(kDstXml);
    LeptrisDocument src = ParseDoc(kSrc);
    ASSERT_NE(dst, nullptr);
    ASSERT_NE(src, nullptr);

    /* Move the middle child. */
    LeptrisElement w = leptris_document_root(src);
    LeptrisNodeRef c = leptris_node_first_child(leptris_element_as_node(w));
    ASSERT_NE(c, nullptr);                        /* keep1 */
    c = leptris_node_next_sibling(c);
    ASSERT_NE(c, nullptr);                        /* frag */
    LeptrisElement moved = nullptr;
    ASSERT_EQ(leptris_document_adopt_move(dst, src, (LeptrisElement)c,
                                          &moved),
              LEPTRIS_OK);

    /* Source serializes its remaining children in order. */
    char* out = leptris_document_serialize(src, nullptr);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "<w><keep1/><keep2/></w>");
    leptris_free_string(out);

    /* The moved handle belongs to dst now: moving it again must be
     * rejected. */
    LeptrisElement again = nullptr;
    EXPECT_EQ(leptris_document_adopt_move(dst, src, moved, &again),
              LEPTRIS_ERROR_INVALID_ARG);

    leptris_document_free(dst);
    leptris_document_free(src);
}

/* Moving the source's ROOT element: src becomes rootless but stays
 * valid (document-children chain coherent); the moved root splices
 * into dst. */
TEST(AdoptMove, MovesRootElement) {
    LeptrisDocument dst = ParseDoc(kDstXml);
    LeptrisDocument src = ParseDoc(kFragmentXml);
    ASSERT_NE(dst, nullptr);
    ASSERT_NE(src, nullptr);

    LeptrisElement moved = nullptr;
    ASSERT_EQ(leptris_document_adopt_move(
                  dst, src, leptris_document_root(src), &moved),
              LEPTRIS_OK);
    ASSERT_EQ(leptris_element_append_child(leptris_document_root(dst),
                                           moved),
              LEPTRIS_OK);

    /* Src is empty: no root; serialize must not walk stale nodes. */
    EXPECT_EQ(leptris_document_root(src), nullptr);
    char* srot = leptris_document_serialize(src, nullptr);
    if (srot) leptris_free_string(srot);

    char* out = leptris_document_serialize(dst, nullptr);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(std::strstr(out, "<frag"), nullptr);
    leptris_free_string(out);

    leptris_document_free(dst);
    leptris_document_free(src);
}

/* Fidelity: attributes (incl. prefixed with side cache), xmlns
 * declarations, entity-bearing values, and every child node kind
 * survive the move; prefixed-attribute namespace URIs resolve after
 * the source is gone. */
TEST(AdoptMove, AttributeNamespaceAndChildFidelity) {
    static const char kSrc[] =
        "<?xml version=\"1.0\"?>"
        "<r xmlns:a='urn:alpha' xmlns:b='urn:beta'>"
        "<f a:att='v1' plain='2 &amp; 3' b:extra='v3'>"
        "text0<!--c1--><?p d?><![CDATA[cd1]]><d>dd</d>"
        "</f></r>";
    LeptrisDocument dst = ParseDoc(kDstXml);
    LeptrisDocument src = ParseDoc(kSrc);
    ASSERT_NE(dst, nullptr);
    ASSERT_NE(src, nullptr);

    LeptrisNodeRef f = leptris_node_first_child(
        leptris_element_as_node(leptris_document_root(src)));
    ASSERT_NE(f, nullptr);
    LeptrisElement moved = nullptr;
    ASSERT_EQ(leptris_document_adopt_move(dst, src, (LeptrisElement)f,
                                          &moved),
              LEPTRIS_OK);
    ASSERT_EQ(leptris_element_append_child(leptris_document_root(dst),
                                           moved),
              LEPTRIS_OK);
    leptris_document_free(src);

    EXPECT_EQ(leptris_element_attribute_count(moved), 3u);
    EXPECT_STREQ(leptris_element_attribute(moved, "a:att"), "v1");
    EXPECT_STREQ(leptris_element_attribute(moved, "plain"), "2 & 3");
    EXPECT_STREQ(leptris_element_attribute(moved, "b:extra"), "v3");

    /* Prefixed attribute lanes (snapshot face): the ns side cache
     * rebuilt into dst must answer after the source is gone. */
    size_t ac = leptris_element_attribute_count(moved);
    size_t need = leptris_element_attributes_snapshot(
        moved, nullptr, 0, nullptr, nullptr, 0, nullptr, nullptr, 0,
        nullptr, nullptr, 0, nullptr);
    ASSERT_GT(need, 0u);
    std::vector<char> nb(need), vb(need), pb(need), ub(need);
    std::vector<size_t> no(ac + 1), vo(ac + 1), po(ac + 1), uo(ac + 1);
    ASSERT_EQ(leptris_element_attributes_snapshot(
                  moved, nb.data(), need, no.data(), vb.data(), need,
                  vo.data(), pb.data(), need, po.data(), ub.data(),
                  need, uo.data()),
              0u);
    for (size_t i = 0; i < ac; i++) {
        const char* nm = nb.data() + no[i];
        if (std::strcmp(nm, "a:att") == 0) {
            EXPECT_STREQ(pb.data() + po[i], "a");
            EXPECT_STREQ(ub.data() + uo[i], "urn:alpha");
        } else if (std::strcmp(nm, "b:extra") == 0) {
            EXPECT_STREQ(pb.data() + po[i], "b");
            EXPECT_STREQ(ub.data() + uo[i], "urn:beta");
        } else {
            EXPECT_EQ(po[i], (size_t)-1) << "for " << nm;
            EXPECT_EQ(uo[i], (size_t)-1) << "for " << nm;
        }
    }

    /* Child kinds and order. */
    struct Expected {
        int type;
        const char* payload;
    } const want[] = {
        {LEPTRIS_NODE_TYPE_TEXT, "text0"},
        {LEPTRIS_NODE_TYPE_COMMENT, "c1"},
        {LEPTRIS_NODE_TYPE_PI, "p"},
        {LEPTRIS_NODE_TYPE_CDATA, "cd1"},
        {LEPTRIS_NODE_TYPE_ELEMENT, "d"},
    };
    size_t i = 0;
    for (LeptrisNodeRef c = leptris_node_first_child(
             leptris_element_as_node(moved));
         c; c = leptris_node_next_sibling(c), i++) {
        ASSERT_LT(i, sizeof(want) / sizeof(want[0]));
        EXPECT_EQ(leptris_node_get_type(c), want[i].type);
        if (want[i].type == LEPTRIS_NODE_TYPE_TEXT) {
            EXPECT_STREQ(leptris_text_node_get_content(c),
                         want[i].payload);
        } else if (want[i].type == LEPTRIS_NODE_TYPE_COMMENT) {
            EXPECT_STREQ(leptris_comment_node_get_content(c),
                         want[i].payload);
        } else if (want[i].type == LEPTRIS_NODE_TYPE_CDATA) {
            EXPECT_STREQ(leptris_cdata_node_get_content(c),
                         want[i].payload);
        } else if (want[i].type == LEPTRIS_NODE_TYPE_PI) {
            EXPECT_STREQ(leptris_pi_node_get_target(c), want[i].payload);
        }
    }
    EXPECT_EQ(i, sizeof(want) / sizeof(want[0]));

    /* XPath over the moved tree in dst — names resolve through
     * dst's interned name slots after the source is gone. */
    LeptrisXPathResult res = leptris_xpath_eval(
        dst, leptris_document_root(dst), "//main/f[@plain='2 & 3']");
    ASSERT_NE(res, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(res), 1u);
    leptris_xpath_result_free(res);

    leptris_document_free(dst);
}

/* Deep subtree + a large text node: content integrity after the
 * source is freed (the cg3 shape: big semantic trees, mixed
 * content). */
TEST(AdoptMove, DeepTreeAndLargeTextIntegrity) {
    std::string src_xml = "<root><branch>";
    for (int i = 0; i < 60; i++) {
        src_xml += "<l" + std::to_string(i) + " id='x" +
                   std::to_string(i) + "' kind='k" +
                   std::to_string(i % 7) + "'>leaf" +
                   std::to_string(i) + "</l" + std::to_string(i) + ">";
    }
    std::string big(200000, 'Z');
    src_xml += "<tail>" + big + "</tail>";
    src_xml += "</branch></root>";

    LeptrisDocument dst = ParseDoc(kDstXml);
    LeptrisDocument src = ParseDoc(src_xml.c_str());
    ASSERT_NE(dst, nullptr);
    ASSERT_NE(src, nullptr);

    LeptrisElement moved = nullptr;
    ASSERT_EQ(leptris_document_adopt_move(
                  dst, src, leptris_document_root(src), &moved),
              LEPTRIS_OK);
    ASSERT_EQ(leptris_element_append_child(leptris_document_root(dst),
                                           moved),
              LEPTRIS_OK);
    leptris_document_free(src);

    /* Spot-check a deep leaf and the big text tail. */
    LeptrisXPathResult res = leptris_xpath_eval(
        dst, leptris_document_root(dst), "//l59[@id='x59']");
    ASSERT_NE(res, nullptr);
    EXPECT_EQ(leptris_xpath_result_count(res), 1u);
    leptris_xpath_result_free(res);

    LeptrisXPathResult tail = leptris_xpath_eval(
        dst, leptris_document_root(dst), "//tail");
    ASSERT_NE(tail, nullptr);
    ASSERT_EQ(leptris_xpath_result_count(tail), 1u);
    LeptrisNodeRef tnode = leptris_xpath_result_get_node(tail, 0);
    ASSERT_NE(tnode, nullptr);
    const char* tc = leptris_text_node_get_content(tnode);
    ASSERT_NE(tc, nullptr);
    EXPECT_EQ(std::strlen(tc), 200000u);
    leptris_xpath_result_free(tail);

    ReadAllViews(leptris_document_root(dst));
    leptris_document_free(dst);
}

/* Absorbed sources move ZERO-COPY: the moved handle IS the source
 * handle (identity proves no re-encode), and the splice stays O(1). */
TEST(AdoptMove, AbsorbedSourceMovesByIdentity) {
    LeptrisDocument dst = ParseDoc(kDstXml);
    LeptrisDocument src = ParseDoc(kFragmentXml);
    ASSERT_NE(dst, nullptr);
    ASSERT_NE(src, nullptr);
    LeptrisElement frag = leptris_document_root(src);
    ASSERT_EQ(leptris_document_absorb(dst, src), LEPTRIS_OK);

    LeptrisElement moved = nullptr;
    ASSERT_EQ(leptris_document_adopt_move(dst, src, frag, &moved),
              LEPTRIS_OK);
    /* Identity, not a copy: absorbed storage already lives with
     * dst, so the move is a detach. */
    EXPECT_EQ(moved, frag);

    ASSERT_EQ(leptris_element_append_child(leptris_document_root(dst),
                                           moved),
              LEPTRIS_OK);
    leptris_document_free(src);

    char* out = leptris_document_serialize(dst, nullptr);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(std::strstr(out, "<frag"), nullptr);
    leptris_free_string(out);
    leptris_document_free(dst);
}

/* Argument contracts. */
TEST(AdoptMove, ArgumentValidation) {
    LeptrisDocument dst = ParseDoc(kDstXml);
    LeptrisDocument src = ParseDoc(kFragmentXml);
    LeptrisDocument third = ParseDoc(kFragmentXml);
    ASSERT_NE(dst, nullptr);
    ASSERT_NE(src, nullptr);
    ASSERT_NE(third, nullptr);
    LeptrisElement out = nullptr;

    EXPECT_EQ(leptris_document_adopt_move(nullptr, src,
                                          leptris_document_root(src), &out),
              LEPTRIS_ERROR_NULL_ARG);
    EXPECT_EQ(leptris_document_adopt_move(dst, nullptr,
                                          leptris_document_root(src), &out),
              LEPTRIS_ERROR_NULL_ARG);
    EXPECT_EQ(leptris_document_adopt_move(dst, src, nullptr, &out),
              LEPTRIS_ERROR_NULL_ARG);
    EXPECT_EQ(leptris_document_adopt_move(dst, src,
                                          leptris_document_root(src), nullptr),
              LEPTRIS_ERROR_INVALID_ARG);

    /* Element belongs to a THIRD document. */
    EXPECT_EQ(leptris_document_adopt_move(dst, src,
                                          leptris_document_root(third), &out),
              LEPTRIS_ERROR_INVALID_ARG);

    /* src == dst: there is nothing to transfer. */
    EXPECT_EQ(leptris_document_adopt_move(dst, dst,
                                          leptris_document_root(dst), &out),
              LEPTRIS_ERROR_INVALID_ARG);

    /* A src element absorbed by some OTHER document is anchored
     * there — not movable. */
    LeptrisDocument holder = ParseDoc(kDstXml);
    ASSERT_EQ(leptris_document_absorb(holder, third), LEPTRIS_OK);
    EXPECT_EQ(leptris_document_adopt_move(dst, third,
                                          leptris_document_root(third), &out),
              LEPTRIS_ERROR_INVALID_ARG);
    leptris_document_free(holder);

    leptris_document_free(dst);
    leptris_document_free(src);
    leptris_document_free(third);
}

/* Allocation failure at any point of the move must leave the source
 * subtree INTACT (build-then-detach atomicity) — the #1528 class
 * guard: a half-moved tree is worse than a failed move. */
TEST(AdoptMove, OomMidMoveLeavesSourceIntact) {
    static const char kSrc[] =
        "<w><frag id='f' p:k='v' xmlns:p='urn:p'>"
        "<d>text</d><e>more</e></frag><keep/></w>";
    for (int k = 1; k <= 96; k++) {
        LeptrisDocument dst = ParseDoc(kDstXml);
        LeptrisDocument src = ParseDoc(kSrc);
        ASSERT_NE(dst, nullptr);
        ASSERT_NE(src, nullptr);
        SCOPED_TRACE(std::string("adopt_move fail at alloc #") +
                     std::to_string(k));
        g_adopt_countdown = k;
        leptris_set_memory_management_functions(adopt_count_alloc,
                                                adopt_count_free);
        LeptrisElement moved = nullptr;
        LeptrisStatus rc = leptris_document_adopt_move(
            dst, src, leptris_document_root(src), &moved);
        g_adopt_countdown = -1;
        leptris_set_memory_management_functions(nullptr, nullptr);
        if (rc == LEPTRIS_OK) {
            /* The failure landed in parse/setup, not the move: the
             * pair must still be consistent. */
            ASSERT_NE(moved, nullptr);
        } else {
            EXPECT_EQ(moved, nullptr)
                << "failed move must not report a moved tree";
            /* Source intact: serialize and verify content. */
            char* out = leptris_document_serialize(src, nullptr);
            ASSERT_NE(out, nullptr);
            EXPECT_STREQ(out,
                         "<w><frag id=\"f\" xmlns:p=\"urn:p\" "
                         "p:k=\"v\"><d>text</d><e>more</e></frag>"
                         "<keep/></w>");
            leptris_free_string(out);
        }
        leptris_document_free(dst);
        leptris_document_free(src);
    }
}
