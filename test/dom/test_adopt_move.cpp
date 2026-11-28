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
#include "pool.h" /* leptris_pool_used_size — the live-set counters */
}

#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

/* Internal document layout (transitively complete via element.h's
 * node.h include): the mut-block chains are part of a document's
 * live set and MUST be counted — moved elements, attributes, and
 * interned names live there, not in the pool. */
#include "element.h"

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
        dst, leptris_document_root(dst), "//tail/text()");
    ASSERT_NE(tail, nullptr);
    ASSERT_EQ(leptris_xpath_result_count(tail), 1u);
    LeptrisNodeRef tnode = leptris_xpath_result_get_node(tail, 0);
    ASSERT_NE(tnode, nullptr);
    ASSERT_EQ(leptris_node_get_type(tnode), LEPTRIS_NODE_TYPE_TEXT);
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

/* ===========================================================================
 * AdoptionLiveSet — the permanent CI gate for the owner's law
 * (issue #1548): adoption/transfer paths are MOVE or COPY-ON-WRITE,
 * never eager deep copy, and the resource bound is proven by a test
 * that runs forever, not by a one-off measurement.
 *
 * The cg3 shape: parse a scratch document at real scale, splice one
 * fragment into a growing output document, free the scratch. Under
 * absorb (the pre-#1548 face) the scratch pool is PINNED into the
 * output forever — compile pipelines that absorbed several inputs
 * held every input's full pool. Under the move the scratch is really
 * freed and the output holds only the spliced fragment.
 *
 * Live set is measured in DOCUMENT BYTES (pool bytes handed out +
 * every mutation-block allocation + the doc/pool structs) —
 * deterministic, no allocator-hook ledger needed. A moved fragment's
 * elements/attrs/names live in mut blocks; its strings and leaf
 * nodes in the pool. Both sides count the same way.
 *
 * Falsifiability (what this test fails on):
 *   - regressing the move to pin-the-source (absorb semantics):
 *     live_move ~ live_absorb  ->  2x gate fails
 *   - regressing to eager deep copy WITH a pinned source (the #1529
 *     doubling): live ~ build + copy       ->  2x gate fails
 *   - a silent content drop "winning" the bound: the spliced tree is
 *     element-counted and serialization-asserted before measuring
 * Deep-copy baseline for this exact shape (measured 2026-11-28,
 * arm64-darwin, v1.9.451 + this face): eager copy with a pinned
 * source (the #1529 doubling) lands at 18.90 MB live; the absorb
 * pin at 18.89 MB. The move measured 1.62 MB — 8.6% of either old
 * face. The move must stay under the pinned MOVE_LIVESET bound
 * below.
 * =========================================================================== */
namespace {

/* One quarter-subtree of the scratch document: 12500 elements with
 * a plain + a prefixed attribute and text each (repeat-heavy names —
 * the interning shape). */
const int kLsPerQuarter = 12500;
const char* kLsPrefixes[4] = {"q1", "q2", "q3", "q4"};

std::string LsQuarterXml(int q) {
    std::string s = "<";
    s += kLsPrefixes[q];
    s += " xmlns:m='urn:m' build='2026'>";
    for (int i = 0; i < kLsPerQuarter; i++) {
        s += "<e" + std::to_string(i % 7) + " id='i" +
             std::to_string(i) + "' m:k='v" + std::to_string(i % 13) +
             "'>t" + std::to_string(i % 31) + "</e" +
             std::to_string(i % 7) + ">";
    }
    s += "</";
    s += kLsPrefixes[q];
    s += ">";
    return s;
}

std::string LsScratchXml() {
    std::string s = "<root>";
    for (int q = 0; q < 4; q++) s += LsQuarterXml(q);
    s += "</root>";
    return s;
}

/* A document's TOTAL live footprint: pool bytes handed out + every
 * mut-block allocation (whole blocks — they are malloc'd whole) +
 * the document struct + the pool struct. */
long LsDocLiveBytes(LeptrisDocument d) {
    EXPECT_NE(d, nullptr);
    long total = (long)sizeof(*d);
    if (d->pool) {
        total += (long)sizeof(*d->pool);
        total += (long)leptris_pool_used_size(d->pool);
    }
    for (struct leptris_mut_elem_block* b = d->mut_elem_blocks; b;
         b = b->next) {
        total += (long)(sizeof(*b) +
                        (size_t)MUT_ELEM_BLOCK_COUNT *
                            sizeof(struct leptris_element));
    }
    for (struct leptris_mut_attr_block* b = d->mut_attr_blocks; b;
         b = b->next) {
        total += (long)(sizeof(*b) + (size_t)MUT_ATTR_BLOCK_COUNT *
                                          sizeof(struct leptris_attribute));
    }
    for (struct leptris_mut_name_block* b = d->mut_name_blocks; b;
         b = b->next) {
        total += (long)(sizeof(*b) + MUT_NAME_BLOCK_BYTES);
    }
    return total;
}

/* Positive content assertion (the release law: the rendered tree
 * must CONTAIN its expected elements — silent drop is the failure
 * mode a low live-set number would otherwise reward). */
void LsAssertQuarterIntact(LeptrisDocument doc, int q) {
    std::string x = "//" + std::string(kLsPrefixes[q]) + "/e3";
    LeptrisXPathResult res = leptris_xpath_eval(
        doc, leptris_document_root(doc), x.c_str());
    ASSERT_NE(res, nullptr);
    /* every e3 (one of the 7 repeating names) appears (N+3)/7 times
     * for i in [0, N) with i%7 == 3 */
    EXPECT_EQ(leptris_xpath_result_count(res),
              (size_t)((kLsPerQuarter + 3) / 7));
    leptris_xpath_result_free(res);
    char* out = leptris_document_serialize(doc, nullptr);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(std::strstr(out, "<e0 id=\"i0\" m:k=\"v0\">t0</e0>"),
              nullptr);
    leptris_free_string(out);
}

}  // namespace

/* THE GATE. Both scenarios run the identical cg3 shape; the move
 * must hold under half the absorb pin's live set, and under the
 * pinned absolute bound. */
TEST(AdoptionLiveSet, MoveHalvesTheSpliceCleanupLiveSet) {
    /* ---- Scenario A: absorb (the pre-#1548 face) ---- */
    LeptrisDocument dst_a = ParseDoc(kDstXml);
    LeptrisDocument scratch_a = ParseDoc(LsScratchXml().c_str());
    ASSERT_NE(dst_a, nullptr);
    ASSERT_NE(scratch_a, nullptr);
    LeptrisElement q1_a = nullptr;
    {
        /* Locate quarter q1: root's first child. */
        LeptrisNodeRef c = leptris_node_first_child(
            leptris_element_as_node(leptris_document_root(scratch_a)));
        ASSERT_NE(c, nullptr);
        q1_a = leptris_node_as_element(c);
    }
    ASSERT_EQ(leptris_document_absorb(dst_a, scratch_a), LEPTRIS_OK);
    /* Splice q1 by reference into the output (the compile shape). */
    ASSERT_EQ(leptris_element_append_child(leptris_document_root(dst_a),
                                           q1_a),
              LEPTRIS_OK);
    LsAssertQuarterIntact(dst_a, 0);
    leptris_document_free(scratch_a); /* handle only — pool pinned */
    long live_absorb = LsDocLiveBytes(dst_a) + LsDocLiveBytes(scratch_a);
    leptris_document_free(dst_a);

    /* ---- Scenario B: adopt_move (the #1548 face) ---- */
    LeptrisDocument dst_b = ParseDoc(kDstXml);
    LeptrisDocument scratch_b = ParseDoc(LsScratchXml().c_str());
    ASSERT_NE(dst_b, nullptr);
    ASSERT_NE(scratch_b, nullptr);
    LeptrisNodeRef c = leptris_node_first_child(
        leptris_element_as_node(leptris_document_root(scratch_b)));
    ASSERT_NE(c, nullptr);
    LeptrisElement moved = nullptr;
    ASSERT_EQ(leptris_document_adopt_move(dst_b, scratch_b,
                                          leptris_node_as_element(c),
                                          &moved),
              LEPTRIS_OK);
    ASSERT_NE(moved, nullptr);
    ASSERT_EQ(leptris_element_append_child(leptris_document_root(dst_b),
                                           moved),
              LEPTRIS_OK);
    LsAssertQuarterIntact(dst_b, 0);
    leptris_document_free(scratch_b); /* REAL free — pool released */
    long live_move = LsDocLiveBytes(dst_b);
    leptris_document_free(dst_b);

    RecordProperty("live_absorb_bytes", live_absorb);
    RecordProperty("live_move_bytes", live_move);

    /* Gate 1 — the halving: the move holds a quarter-fragment while
     * absorb pins the WHOLE scratch, so the move must land well under
     * half of the absorb pin. A pin-the-source regression lands at
     * ~1.0x (fails 2x gate); the eager-copy-with-pinned-source
     * regression (#1529) lands at ~1.2x (fails 2x gate). */
    EXPECT_LT(live_move * 2, live_absorb)
        << "move live " << live_move << " vs absorb live " << live_absorb
        << " — adoption must not pin or double the source";

    /* Gate 2 — the pinned absolute bound. Measured 2026-11-28
     * (arm64-darwin): live_move = 1,621,440 bytes (1.55 MB); the
     * deep-copy-with-pinned-source baseline 18,901,288; the absorb
     * pin 18,893,840. Bound = measured + ~60% headroom for arch
     * variance (ILP32 node/name slots are smaller; page rounding
     * differs). ANY regression that pushes the moved fragment past
     * 2.5 MB is a live-set defect regardless of the ratio gate —
     * e.g. losing name interning (+0.3 MB) or forcing heap-dup of
     * inline attribute values (+1 MB) both cross it. */
    const long kMoveLiveBoundBytes = (long)2.5 * 1024 * 1024;
    EXPECT_LT(live_move, kMoveLiveBoundBytes)
        << "moved fragment live set " << live_move
        << " exceeded the pinned bound " << kMoveLiveBoundBytes
        << " (deep-copy baseline 18901288, absorb pin 18893840)";
}

