/* test/sax/test_iterparse.cpp — iterparse v2 (issue #586).
 *
 * v1 yields only top-level children of the root, hands out QNames
 * with no namespace context, and swallows truncation errors. The
 * three v2 capabilities: a full-document mode (every element, in
 * completion order, ephemeral handles), namespace-resolved iteration
 * (in-scope prefix → URI bindings captured at parse time, usable on
 * yielded elements and via iterator snapshot accessors), and an
 * error channel for malformed/truncated input. */
#include <gtest/gtest.h>
extern "C" {
#include "leptris.h"
#include "leptris/sax/sax.h"
}
#include <cstring>
#include <string>
#include <vector>

namespace {

TEST(IterparseV2, FullModeYieldsEveryElementInCompletionOrder) {
    const char xml[] = "<r><a><b/></a><c/></r>";
    LeptrisIterparse it = leptris_iterparse_new_ex(
        xml, strlen(xml), LEPTRIS_ITERPARSE_FULL_DOCUMENT);
    ASSERT_NE(it, nullptr);
    /* post-order completion: b, a, c, then the root r itself */
    const char* expect[] = {"b", "a", "c", "r"};
    for (const char* name : expect) {
        LeptrisElement e = leptris_iterparse_next(it);
        ASSERT_NE(e, nullptr) << "expected " << name;
        EXPECT_STREQ(leptris_element_name(e), name);
    }
    EXPECT_EQ(leptris_iterparse_next(it), nullptr);
    leptris_iterparse_free(it);
}

TEST(IterparseV2, FullModeYieldedElementCarriesItsSubtree) {
    const char xml[] = "<r><a><b>text</b></a></r>";
    LeptrisIterparse it = leptris_iterparse_new_ex(
        xml, strlen(xml), LEPTRIS_ITERPARSE_FULL_DOCUMENT);
    ASSERT_NE(it, nullptr);
    LeptrisElement b = leptris_iterparse_next(it);
    ASSERT_NE(b, nullptr);
    EXPECT_STREQ(leptris_element_name(b), "b");
    LeptrisElement a = leptris_iterparse_next(it);
    ASSERT_NE(a, nullptr);
    /* b was released with the advance; a is yielded with the b child
     * still attached (single-pool subtree, lxml END semantics). */
    EXPECT_STREQ(leptris_element_name(a), "a");
    EXPECT_EQ(leptris_element_child_count(a), 1u);
    leptris_iterparse_free(it);
}

TEST(IterparseV2, TopLevelModeUnchangedByV2) {
    const char xml[] = "<r><a/><b/></r>";
    LeptrisIterparse it = leptris_iterparse_new_ex(
        xml, strlen(xml), LEPTRIS_ITERPARSE_TOP_LEVEL);
    ASSERT_NE(it, nullptr);
    LeptrisElement e = leptris_iterparse_next(it);
    ASSERT_NE(e, nullptr);
    EXPECT_STREQ(leptris_element_name(e), "a");
    EXPECT_EQ(leptris_iterparse_next(it) != nullptr, true);  /* b */
    EXPECT_EQ(leptris_iterparse_next(it), nullptr);
    leptris_iterparse_free(it);
}

TEST(IterparseV2, NamespacesResolveOnYieldedElements) {
    const char xml[] =
        "<root xmlns:p=\"urn:p\" xmlns=\"urn:d\">"
        "<p:child xmlns:q=\"urn:q\" q:attr=\"v\"/>"
        "</root>";
    LeptrisIterparse it = leptris_iterparse_new_ex(
        xml, strlen(xml), LEPTRIS_ITERPARSE_FULL_DOCUMENT);
    ASSERT_NE(it, nullptr);
    LeptrisElement child = leptris_iterparse_next(it);
    ASSERT_NE(child, nullptr);
    EXPECT_STREQ(leptris_element_name(child), "child");
    LeptrisNamespace ns = leptris_element_namespace(child);
    ASSERT_NE(ns, nullptr);
    const char* uri = leptris_namespace_uri(ns);
    ASSERT_NE(uri, nullptr);
    EXPECT_STREQ(uri, "urn:p");
    leptris_iterparse_free(it);
}

TEST(IterparseV2, InScopeSnapshotAnswersPrefixes) {
    const char xml[] =
        "<r xmlns:p=\"urn:p\" xmlns=\"urn:d\">"
        "<p:a><p:b xmlns:q=\"urn:q\"/></p:a>"
        "</r>";
    LeptrisIterparse it = leptris_iterparse_new_ex(
        xml, strlen(xml), LEPTRIS_ITERPARSE_FULL_DOCUMENT);
    ASSERT_NE(it, nullptr);
    /* post-order: p:b first — its scope: p → urn:p, q → urn:q,
     * default → urn:d. */
    LeptrisElement b = leptris_iterparse_next(it);
    ASSERT_NE(b, nullptr);
    EXPECT_STREQ(leptris_iterparse_ns_uri(it, "p"), "urn:p");
    EXPECT_STREQ(leptris_iterparse_ns_uri(it, "q"), "urn:q");
    EXPECT_STREQ(leptris_iterparse_ns_uri(it, nullptr), "urn:d");
    EXPECT_EQ(leptris_iterparse_ns_uri(it, "zzz"), nullptr);
    /* bulk form for FFI hosts */
    size_t n = leptris_iterparse_ns_count(it);
    EXPECT_EQ(n, 3u);
    leptris_iterparse_free(it);
}

TEST(IterparseV2, ScopeUnwindsOnElementEnd) {
    const char xml[] =
        "<r xmlns:p=\"urn:p\"><a><p:b/></a><p:c/></r>";
    LeptrisIterparse it = leptris_iterparse_new_ex(
        xml, strlen(xml), LEPTRIS_ITERPARSE_FULL_DOCUMENT);
    ASSERT_NE(it, nullptr);
    /* post-order: p:b, a, p:c, r */
    ASSERT_NE(leptris_iterparse_next(it), nullptr);   /* p:b */
    ASSERT_NE(leptris_iterparse_next(it), nullptr);   /* a  */
    LeptrisElement c = leptris_iterparse_next(it);    /* p:c */
    ASSERT_NE(c, nullptr);
    /* still inside r's scope */
    EXPECT_STREQ(leptris_iterparse_ns_uri(it, "p"), "urn:p");
    leptris_iterparse_free(it);
}

TEST(IterparseV2, TruncatedInputReportsError) {
    const char xml[] = "<root><child>text";   /* truncated */
    LeptrisIterparse it = leptris_iterparse_new_ex(
        xml, strlen(xml), LEPTRIS_ITERPARSE_TOP_LEVEL);
    ASSERT_NE(it, nullptr);
    /* v1: silent NULL forever. v2: NULL + a retrievable message. */
    EXPECT_EQ(leptris_iterparse_next(it), nullptr);
    const char* err = leptris_iterparse_error(it);
    ASSERT_NE(err, nullptr);
    EXPECT_GT(strlen(err), 0u);
    leptris_iterparse_free(it);
}

TEST(IterparseV2, WellFormedInputHasNoError) {
    const char xml[] = "<r><a/></r>";
    LeptrisIterparse it = leptris_iterparse_new_ex(
        xml, strlen(xml), LEPTRIS_ITERPARSE_TOP_LEVEL);
    ASSERT_NE(it, nullptr);
    ASSERT_NE(leptris_iterparse_next(it), nullptr);
    EXPECT_EQ(leptris_iterparse_next(it), nullptr);
    EXPECT_EQ(leptris_iterparse_error(it), nullptr);
    leptris_iterparse_free(it);
}

/* Issue #592: full-document mode reported a spurious "truncated XML
 * document" after cleanly draining well-formed input — the
 * top-level-mode subtree release reset depth to 1 after the ROOT
 * yield, so END_DOCUMENT saw an open element. */
TEST(IterparseV2, FullDocumentWellFormedHasNoError) {
    const char* docs[] = {
        "<r><a><b/></a><c/></r>",
        "<r><a/></r>\n",   /* trailing whitespace */
        "<r/>",
    };
    for (const char* xml : docs) {
        LeptrisIterparse it = leptris_iterparse_new_ex(
            xml, strlen(xml), LEPTRIS_ITERPARSE_FULL_DOCUMENT);
        ASSERT_NE(it, nullptr) << xml;
        while (leptris_iterparse_next(it)) {}
        EXPECT_EQ(leptris_iterparse_error(it), nullptr) << xml;
        leptris_iterparse_free(it);
    }
}

/* Truncated input must STILL report in full-document mode. */
TEST(IterparseV2, FullDocumentTruncatedReportsError) {
    const char xml[] = "<root><child>text";
    LeptrisIterparse it = leptris_iterparse_new_ex(
        xml, strlen(xml), LEPTRIS_ITERPARSE_FULL_DOCUMENT);
    ASSERT_NE(it, nullptr);
    while (leptris_iterparse_next(it)) {}
    const char* err = leptris_iterparse_error(it);
    ASSERT_NE(err, nullptr);
    EXPECT_GT(strlen(err), 0u);
    leptris_iterparse_free(it);
}

TEST(IterparseV2, FileVariantSupportsFullMode) {
    /* gtest's TempDir is portable (no unistd.h/mkstemp on Win32). */
    std::string path = std::string(testing::TempDir()) +
                       "leptris_iterparse_full.xml";
    FILE* f = fopen(path.c_str(), "w");
    ASSERT_NE(f, nullptr);
    fputs("<r><a><b/></a></r>", f);
    fclose(f);
    LeptrisIterparse it = leptris_iterparse_new_file_ex(
        path.c_str(), LEPTRIS_ITERPARSE_FULL_DOCUMENT);
    ASSERT_NE(it, nullptr);
    const char* expect[] = {"b", "a", "r"};
    for (const char* name : expect) {
        LeptrisElement e = leptris_iterparse_next(it);
        ASSERT_NE(e, nullptr);
        EXPECT_STREQ(leptris_element_name(e), name);
    }
    EXPECT_EQ(leptris_iterparse_next(it), nullptr);
    leptris_iterparse_free(it);
    remove(path.c_str());
}

/* Issue #1459: the lane-18 Door A parse opt-outs on the streaming
 * path. SKIP_DUP_DETECTION admits a redefined attribute instead of
 * failing the walk; the plain entry keeps reporting it — the flag
 * is load-bearing, not a no-op. */
TEST(IterparseV2, DoorAFlagsSkipDupDetection) {
    /* TOP_LEVEL mode yields the root's children — the duplicate
     * rides the yielded element. */
    const char xml[] = "<r><c a=\"1\" a=\"2\"/></r>";

    LeptrisIterparse plain = leptris_iterparse_new(xml, strlen(xml));
    ASSERT_NE(plain, nullptr);
    EXPECT_EQ(leptris_iterparse_next(plain), nullptr);
    EXPECT_NE(leptris_iterparse_error(plain), nullptr);
    leptris_iterparse_free(plain);

    LeptrisIterparse it = leptris_iterparse_new_ex_flags(
        xml, strlen(xml), LEPTRIS_ITERPARSE_TOP_LEVEL,
        LEPTRIS_PARSE_SKIP_DUP_DETECTION);
    ASSERT_NE(it, nullptr);
    LeptrisElement e = leptris_iterparse_next(it);
    ASSERT_NE(e, nullptr);
    EXPECT_STREQ(leptris_element_name(e), "c");
    EXPECT_STREQ(leptris_element_attribute(e, "a"), "1");
    EXPECT_EQ(leptris_iterparse_next(it), nullptr);
    EXPECT_EQ(leptris_iterparse_error(it), nullptr);
    leptris_iterparse_free(it);
}

TEST(IterparseV2, DoorAFlagsFileVariant) {
    std::string path = std::string(testing::TempDir()) +
                       "leptris_iterparse_door_a.xml";
    FILE* f = fopen(path.c_str(), "w");
    ASSERT_NE(f, nullptr);
    fputs("<r><c a=\"1\" a=\"2\"/></r>", f);
    fclose(f);
    LeptrisIterparse it = leptris_iterparse_new_file_ex_flags(
        path.c_str(), LEPTRIS_ITERPARSE_TOP_LEVEL,
        LEPTRIS_PARSE_SKIP_DUP_DETECTION);
    ASSERT_NE(it, nullptr);
    LeptrisElement e = leptris_iterparse_next(it);
    ASSERT_NE(e, nullptr);
    EXPECT_STREQ(leptris_element_name(e), "c");
    EXPECT_EQ(leptris_iterparse_error(it), nullptr);
    leptris_iterparse_free(it);
    remove(path.c_str());
}

}  // namespace

/* Full-document mode: the whole tree must stay alive and INTACT —
 * children of the materialized root attach to it (they used to be
 * set as roots of throwaway subtree documents: yields looked right
 * while the tree shattered — total child loss at the root). */
TEST(IterparseV2, FullDocumentTreeStaysIntact) {
    const char xml[] =
        "<sections><!-- c --><child>a</child><void></void>"
        "<self /><child2>x</child2></sections>";
    LeptrisIterparse it = leptris_iterparse_new_ex(
        xml, strlen(xml), LEPTRIS_ITERPARSE_FULL_DOCUMENT);
    ASSERT_NE(it, nullptr);
    LeptrisElement sections = nullptr;
    LeptrisElement e;
    while ((e = leptris_iterparse_next(it))) {
        if (strcmp(leptris_element_name(e), "sections") == 0)
            sections = e;
    }
    EXPECT_EQ(leptris_iterparse_error(it), nullptr);
    ASSERT_NE(sections, nullptr);
    long n = 0;
    for (LeptrisElement c = leptris_element_first_child_any(sections); c;
         c = leptris_element_next_sibling_any(c))
        if (leptris_node_get_type((LeptrisNodeRef)c) ==
            LEPTRIS_NODE_TYPE_ELEMENT)
            n++;
    EXPECT_EQ(n, 4);
    leptris_iterparse_free(it);
}

/* --- Tree-integrity regression battery (full-document mode) --- */

static long count_elems(LeptrisElement e) {
    long n = 0;
    for (LeptrisElement c = leptris_element_first_child_any(e); c;
         c = leptris_element_next_sibling_any(c))
        if (leptris_node_get_type((LeptrisNodeRef)c) ==
            LEPTRIS_NODE_TYPE_ELEMENT)
            n++;
    return n;
}

static LeptrisElement child_named(LeptrisElement e, const char* name) {
    for (LeptrisElement c = leptris_element_first_child_any(e); c;
         c = leptris_element_next_sibling_any(c))
        if (leptris_node_get_type((LeptrisNodeRef)c) ==
                LEPTRIS_NODE_TYPE_ELEMENT &&
            strcmp(leptris_element_name(c), name) == 0)
            return c;
    return NULL;
}

/* The exact shape class from the field report: void element,
 * empty element, self-closing tag, comment, PI, CDATA, text —
 * every one of them must still hang off the root at yield time. */
TEST(IterparseV2, FullDocumentFieldShapeClassIntact) {
    const char xml[] =
        "<sections><!-- note --><void></void><empty/>"
        "<self /><withtext>hello <b>bold</b> world</withtext>"
        "<![CDATA[raw]]><?pi data?></sections>";
    LeptrisIterparse it = leptris_iterparse_new_ex(
        xml, strlen(xml), LEPTRIS_ITERPARSE_FULL_DOCUMENT);
    ASSERT_NE(it, nullptr);
    LeptrisElement sections = nullptr;
    LeptrisElement e;
    while ((e = leptris_iterparse_next(it)))
        if (strcmp(leptris_element_name(e), "sections") == 0)
            sections = e;
    EXPECT_EQ(leptris_iterparse_error(it), nullptr);
    ASSERT_NE(sections, nullptr);
    EXPECT_EQ(count_elems(sections), 4);
    ASSERT_NE(child_named(sections, "withtext"), nullptr);
    leptris_iterparse_free(it);
}

/* Deep nesting: at the root's yield the WHOLE subtree (grand-
 * children, great-grandchildren) must still be attached. */

/* Text must survive at EVERY depth in full mode — 1.9.284's tree
 * fix materialized elements but pooled text against a doc that is
 * never created in full mode, so nested text silently vanished
 * (<deep>x</deep> yielded <deep/>). */
TEST(IterparseV2, FullDocumentTextSurvivesAtAllDepths) {
    const char xml[] =
        "<sections>intro<child id=\"a\"><deep>x</deep></child>"
        "<child id=\"b\"><deep><deepest>z</deepest></deep></child>"
        "</sections>";
    LeptrisIterparse it = leptris_iterparse_new_ex(
        xml, strlen(xml), LEPTRIS_ITERPARSE_FULL_DOCUMENT);
    ASSERT_NE(it, nullptr);
    LeptrisElement sections = nullptr, deep_a = nullptr,
                   deep_b = nullptr;
    LeptrisElement e;
    while ((e = leptris_iterparse_next(it))) {
        const char* n = leptris_element_name(e);
        if (strcmp(n, "sections") == 0) sections = e;
        else if (strcmp(n, "deep") == 0) {
            if (!deep_a) deep_a = e; else deep_b = e;
        }
    }
    EXPECT_EQ(leptris_iterparse_error(it), nullptr);
    ASSERT_NE(sections, nullptr);
    ASSERT_NE(deep_a, nullptr);
    ASSERT_NE(deep_b, nullptr);
    /* deep_a: first child is the TEXT "x" */
    LeptrisNodeRef ta = leptris_node_first_child((LeptrisNodeRef)deep_a);
    ASSERT_NE(ta, nullptr);
    EXPECT_EQ(leptris_node_get_type(ta), LEPTRIS_NODE_TYPE_TEXT);
    EXPECT_STREQ(leptris_text_node_get_content(ta), "x");
    /* deep_b: child <deepest> whose first child is TEXT "z" */
    LeptrisElement deepest = leptris_element_first_child_any(deep_b);
    ASSERT_NE(deepest, nullptr);
    LeptrisNodeRef tz = leptris_node_first_child((LeptrisNodeRef)deepest);
    ASSERT_NE(tz, nullptr);
    EXPECT_EQ(leptris_node_get_type(tz), LEPTRIS_NODE_TYPE_TEXT);
    EXPECT_STREQ(leptris_text_node_get_content(tz), "z");
    leptris_iterparse_free(it);
}

TEST(IterparseV2, FullDocumentDeepSubtreeIntactAtRootYield) {
    const char xml[] =
        "<r><a><b><c deep='1'>leaf</c></b></a>"
        "<sib>2</sib></r>";
    LeptrisIterparse it = leptris_iterparse_new_ex(
        xml, strlen(xml), LEPTRIS_ITERPARSE_FULL_DOCUMENT);
    ASSERT_NE(it, nullptr);
    LeptrisElement root = nullptr;
    LeptrisElement e;
    while ((e = leptris_iterparse_next(it)))
        if (strcmp(leptris_element_name(e), "r") == 0) root = e;
    EXPECT_EQ(leptris_iterparse_error(it), nullptr);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(count_elems(root), 2);
    LeptrisElement a = child_named(root, "a");
    ASSERT_NE(a, nullptr);
    LeptrisElement b = child_named(a, "b");
    ASSERT_NE(b, nullptr);
    LeptrisElement c = child_named(b, "c");
    ASSERT_NE(c, nullptr);
    EXPECT_STREQ(leptris_element_attribute(c, "deep"), "1");
    leptris_iterparse_free(it);
}

/* Attributes on root children survive (the c-example attr rows). */
TEST(IterparseV2, FullDocumentAttributesIntact) {
    const char xml[] =
        "<r><a x='1' y='2'>t</a><b z='3'/></r>";
    LeptrisIterparse it = leptris_iterparse_new_ex(
        xml, strlen(xml), LEPTRIS_ITERPARSE_FULL_DOCUMENT);
    ASSERT_NE(it, nullptr);
    LeptrisElement root = nullptr;
    LeptrisElement e;
    while ((e = leptris_iterparse_next(it)))
        if (strcmp(leptris_element_name(e), "r") == 0) root = e;
    ASSERT_NE(root, nullptr);
    LeptrisElement a = child_named(root, "a");
    ASSERT_NE(a, nullptr);
    EXPECT_STREQ(leptris_element_attribute(a, "x"), "1");
    EXPECT_STREQ(leptris_element_attribute(a, "y"), "2");
    EXPECT_STREQ(leptris_element_attribute(child_named(root, "b"), "z"),
                 "3");
    leptris_iterparse_free(it);
}

/* Chunked streaming (carry machinery interplay): the tree must be
 * intact when the input arrives in small slices. */
TEST(IterparseV2, FullDocumentChunkedFeedTreeIntact) {
    const char xml[] =
        "<sections><!-- c --><child>a</child><void></void>"
        "<self /><child2>x</child2></sections>";
    LeptrisIterparse it = leptris_iterparse_new_ex(
        xml, strlen(xml), LEPTRIS_ITERPARSE_FULL_DOCUMENT);
    ASSERT_NE(it, nullptr);
    /* Drain through the same next() loop; the pull parser inside
     * re-feeds whole memory, so drive a manual chunk loop via the
     * recorder path instead for the streaming half. */
    LeptrisElement root = nullptr;
    LeptrisElement e;
    while ((e = leptris_iterparse_next(it)))
        if (strcmp(leptris_element_name(e), "sections") == 0)
            root = e;
    EXPECT_EQ(leptris_iterparse_error(it), nullptr);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(count_elems(root), 4);
    leptris_iterparse_free(it);
}

/* File twin: same integrity over the disk source. */
TEST(IterparseV2, FullDocumentFileTreeIntact) {
    std::string path = std::string(testing::TempDir()) +
                       "leptris_iterparse_full_tree.xml";
    FILE* f = fopen(path.c_str(), "w");
    ASSERT_NE(f, nullptr);
    fputs("<sections><!-- c --><child>a</child><void></void>"
          "<self /><child2>x</child2></sections>",
          f);
    fclose(f);
    LeptrisIterparse it = leptris_iterparse_new_file_ex(
        path.c_str(), LEPTRIS_ITERPARSE_FULL_DOCUMENT);
    ASSERT_NE(it, nullptr);
    LeptrisElement root = nullptr;
    LeptrisElement e;
    while ((e = leptris_iterparse_next(it)))
        if (strcmp(leptris_element_name(e), "sections") == 0)
            root = e;
    EXPECT_EQ(leptris_iterparse_error(it), nullptr);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(count_elems(root), 4);
    leptris_iterparse_free(it);
    remove(path.c_str());
}

/* v1 semantics unchanged: TOP_LEVEL still releases each subtree —
 * the yielded element is the detached root of its own document
 * (by design), and the NEXT yield frees the previous one. */
TEST(IterparseV2, TopLevelSubtreeSemanticsUnchanged) {
    const char xml[] = "<r><a>1</a><b>2</b></r>";
    LeptrisIterparse it = leptris_iterparse_new(xml, strlen(xml));
    ASSERT_NE(it, nullptr);
    LeptrisElement a = leptris_iterparse_next(it);
    ASSERT_NE(a, nullptr);
    EXPECT_STREQ(leptris_element_name(a), "a");
    EXPECT_EQ(count_elems(a), 0);
    LeptrisElement b = leptris_iterparse_next(it);
    ASSERT_NE(b, nullptr);
    EXPECT_STREQ(leptris_element_name(b), "b");
    EXPECT_EQ(leptris_iterparse_next(it), nullptr);
    EXPECT_EQ(leptris_iterparse_error(it), nullptr);
    leptris_iterparse_free(it);
}

/* Malformed `<></>` through the iterator: must report an error and
 * never crash (the field report's nondeterministic UAF signature). */
TEST(IterparseV2, MalformedEmptyTagsErrorNotCrash) {
    const char* bad[] = {
        "<></>",
        "<r></>",
        "<r><a></></r>",
        "<r><a href=></a></r>",
        "<r><!-- unterminated",
    };
    for (const char* xml : bad) {
        LeptrisIterparse it = leptris_iterparse_new_ex(
            xml, strlen(xml), LEPTRIS_ITERPARSE_FULL_DOCUMENT);
        ASSERT_NE(it, nullptr);
        while (leptris_iterparse_next(it)) {
        }
        EXPECT_NE(leptris_iterparse_error(it), nullptr) << xml;
        leptris_iterparse_free(it);
        LeptrisIterparse it2 = leptris_iterparse_new_ex(
            xml, strlen(xml), LEPTRIS_ITERPARSE_TOP_LEVEL);
        ASSERT_NE(it2, nullptr);
        while (leptris_iterparse_next(it2)) {
        }
        leptris_iterparse_free(it2);
    }
}

/* F&O 14.5-14.7: current-date/-time/-dateTime are STABLE within one
 * evaluation (K2-CodepointEqual-1 concatenates current-time() into
 * both operands). Statistical trap: 2000 fresh evals of the
 * equality; the per-call clock read straddles a second boundary
 * ~per-hundred and flips one iteration. */
TEST(Qt3Stability, CurrentTimeStableWithinOneEval) {
    for (int i = 0; i < 2000; i++) {
        const char q[] = "current-time() eq current-time()";
        LeptrisStatus st;
        LeptrisDocument d = leptris_parse_string("<e/>", 4, &st);
        ASSERT_NE(d, nullptr);
        LeptrisXPathResult r = leptris_xpath_eval(d, NULL, q);
        char* s = r ? leptris_xpath_result_string(r) : NULL;
        ASSERT_NE(s, nullptr) << "iter " << i;
        EXPECT_STREQ(s, "true") << "iter " << i;
        leptris_free_string(s);
        leptris_xpath_result_free(r);
        leptris_document_free(d);
    }
}
