// test/dom/test_intern_names.cpp — #2132 phase 2: per-document name
// interning as the default create path.
//
// leptris_element_create interns COLON-FREE names in a per-document
// open-addressed table (shared mut-block slot + cached 16-bit hash),
// with a last-name memo covering sequential same-name creates. These
// specs pin the behavioral contract:
//   - repeated names serialize and read exactly like distinct names
//   - same-name elements share ONE name allocation (same doc only)
//   - set_name routes through the same intern
//   - colon'd names NEVER intern (#846 splits in place — disjoint)
//   - the distinct-name cap falls back to carve, bounded table
//   - no case folding
//   - populated tables die with the document (ASAN/leaks validate)

#include <gtest/gtest.h>

#include <string.h>

#include <string>
#include <vector>

#include "leptris.h"

extern "C" {
#include "element.h"      /* elem_name — name storage identity */
#include "name_intern.h"  /* table internals + distinct-count hook */
}

namespace {

/* Repeated-name creates must surface exactly like distinct-name
 * creates: every child reads back its name and the serialization
 * carries every element. */
TEST(InternNames, RepeatNameSerializesLikeDistinctNames) {
    LeptrisDocument d = leptris_document_create();
    ASSERT_NE(d, nullptr);
    LeptrisElement root = leptris_element_create(d, "root");
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(leptris_document_set_root(d, root), LEPTRIS_OK);

    for (int i = 0; i < 50; i++) {
        LeptrisElement c = leptris_element_create(d, "item");
        ASSERT_NE(c, nullptr);
        ASSERT_EQ(leptris_element_append_child(root, c), LEPTRIS_OK);
    }
    /* every child reads back the repeated name */
    int seen = 0;
    for (LeptrisElement c = leptris_element_get_first_child(root); c;
         c = leptris_element_get_next_sibling(c), seen++) {
        EXPECT_STREQ(leptris_element_get_name(c), "item");
    }
    EXPECT_EQ(seen, 50);

    char* xml = leptris_serialize_document(d);
    ASSERT_NE(xml, nullptr);
    size_t hits = 0;
    for (const char* p = xml; (p = strstr(p, "<item")) != NULL; p++)
        hits++;
    EXPECT_EQ(hits, 50u);
    leptris_free_string(xml);
    leptris_document_free(d);
}

/* The sharing core: two elements created with the same name point at
 * the same name allocation; a distinct name does not. Same-doc only
 * by construction (the table is per-document). */
TEST(InternNames, SameNameElementsShareNameAllocation) {
    LeptrisDocument d = leptris_document_create();
    ASSERT_NE(d, nullptr);
    LeptrisElement root = leptris_element_create(d, "root");
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(leptris_document_set_root(d, root), LEPTRIS_OK);

    LeptrisElement a = leptris_element_create(d, "dup");
    LeptrisElement b = leptris_element_create(d, "dup");
    LeptrisElement c = leptris_element_create(d, "other");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_NE(c, nullptr);

    EXPECT_EQ(elem_name(a), elem_name(b));
    EXPECT_NE(elem_name(a), elem_name(c));
    /* root + dup + other */
    EXPECT_EQ(leptris_name_intern_distinct(d), 3u);

    EXPECT_STREQ(leptris_element_get_name(a), "dup");
    EXPECT_STREQ(leptris_element_get_name(b), "dup");
    EXPECT_STREQ(leptris_element_get_name(c), "other");
    /* cached hash must equal the canonical compute — XPath child-axis
     * and the parser's close-tag check pre-filter on it. */
    EXPECT_EQ(a->name_hash, leptris_name_hash_compute("dup"));
    EXPECT_EQ(b->name_hash, a->name_hash);

    leptris_document_free(d);
}

/* The memo covers the sequential shape; an interleaved stream of
 * distinct names must still share through the table probe. */
TEST(InternNames, InterleavedSameNameStillShares) {
    LeptrisDocument d = leptris_document_create();
    ASSERT_NE(d, nullptr);
    LeptrisElement root = leptris_element_create(d, "root");
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(leptris_document_set_root(d, root), LEPTRIS_OK);

    LeptrisElement a1 = leptris_element_create(d, "a");
    LeptrisElement b1 = leptris_element_create(d, "b");
    LeptrisElement a2 = leptris_element_create(d, "a");
    LeptrisElement c1 = leptris_element_create(d, "c");
    LeptrisElement a3 = leptris_element_create(d, "a");
    ASSERT_NE(a1, nullptr);
    ASSERT_NE(b1, nullptr);
    ASSERT_NE(a2, nullptr);
    ASSERT_NE(c1, nullptr);
    ASSERT_NE(a3, nullptr);

    EXPECT_EQ(elem_name(a1), elem_name(a2));
    EXPECT_EQ(elem_name(a2), elem_name(a3));
    EXPECT_NE(elem_name(a1), elem_name(b1));
    EXPECT_NE(elem_name(a1), elem_name(c1));
    /* root + a + b + c */
    EXPECT_EQ(leptris_name_intern_distinct(d), 4u);

    leptris_document_free(d);
}

/* set_name routes through the intern: renaming two elements to the
 * SAME new name shares the slot; renaming to distinct names does not. */
TEST(InternNames, SetNameRoutesThroughIntern) {
    LeptrisDocument d = leptris_document_create();
    ASSERT_NE(d, nullptr);
    LeptrisElement root = leptris_element_create(d, "root");
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(leptris_document_set_root(d, root), LEPTRIS_OK);
    LeptrisElement a = leptris_element_create(d, "one");
    LeptrisElement b = leptris_element_create(d, "two");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    ASSERT_EQ(leptris_element_set_name(a, "shared"), LEPTRIS_OK);
    ASSERT_EQ(leptris_element_set_name(b, "shared"), LEPTRIS_OK);
    EXPECT_EQ(elem_name(a), elem_name(b));
    EXPECT_STREQ(leptris_element_get_name(a), "shared");
    EXPECT_STREQ(leptris_element_get_name(b), "shared");
    EXPECT_EQ(a->name_hash, leptris_name_hash_compute("shared"));

    /* distinct renames: fresh slots, coherent reads */
    ASSERT_EQ(leptris_element_set_name(a, "px"), LEPTRIS_OK);
    ASSERT_EQ(leptris_element_set_name(b, "py"), LEPTRIS_OK);
    EXPECT_NE(elem_name(a), elem_name(b));
    EXPECT_STREQ(leptris_element_get_name(a), "px");
    EXPECT_STREQ(leptris_element_get_name(b), "py");

    /* renames to names that were interned earlier still share */
    ASSERT_EQ(leptris_element_set_name(a, "one"), LEPTRIS_OK);
    ASSERT_EQ(leptris_element_set_name(b, "one"), LEPTRIS_OK);
    EXPECT_EQ(elem_name(a), elem_name(b));

    leptris_document_free(d);
}

/* Colon'd names keep today's carve+split path (#846): two creates of
 * the same prefixed name must NOT share (the split makes fresh
 * stamped slots) and the QName shape must hold. */
TEST(InternNames, ColonNamesNeverIntern) {
    LeptrisDocument d = leptris_document_create();
    ASSERT_NE(d, nullptr);
    LeptrisElement root = leptris_element_create(d, "root");
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(leptris_document_set_root(d, root), LEPTRIS_OK);

    LeptrisElement a = leptris_element_create(d, "p:local");
    LeptrisElement b = leptris_element_create(d, "p:local");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);

    /* split mutated in place — the slots cannot be shared */
    EXPECT_NE(elem_name(a), elem_name(b));
    EXPECT_STREQ(leptris_element_get_name(a), "local");
    EXPECT_STREQ(leptris_element_get_name(b), "local");
    EXPECT_STREQ(leptris_element_get_prefix(a), "p");
    EXPECT_STREQ(leptris_element_get_prefix(b), "p");
    /* the split re-stamps the backpointer (existing #846 pin) */
    EXPECT_TRUE(leptris_elem_has_namebp(a));
    EXPECT_EQ(leptris_elem_namebp_doc(a), d);
    /* colon'd names are not interned entries */
    EXPECT_EQ(leptris_name_intern_distinct(d), 1u); /* just "root" */

    /* QName shape survives serialization */
    ASSERT_EQ(leptris_element_append_child(root, a), LEPTRIS_OK);
    ASSERT_EQ(leptris_element_append_child(root, b), LEPTRIS_OK);
    char* xml = leptris_serialize_document(d);
    ASSERT_NE(xml, nullptr);
    EXPECT_NE(strstr(xml, "p:local"), nullptr);
    leptris_free_string(xml);

    leptris_document_free(d);
}

/* Past the distinct-name cap the table stops interning (bounded) and
 * every create behaves exactly like the carve path. */
TEST(InternNames, CapFallsBackToCarveAndStaysBounded) {
    LeptrisDocument d = leptris_document_create();
    ASSERT_NE(d, nullptr);
    LeptrisElement root = leptris_element_create(d, "root");
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(leptris_document_set_root(d, root), LEPTRIS_OK);

    const size_t kOver = (size_t)DOM_INTERN_MAX_DISTINCT + 512;
    std::vector<std::string> names;
    names.reserve(kOver);
    for (size_t i = 0; i < kOver; i++) {
        names.push_back("n" + std::to_string(i));
        LeptrisElement e = leptris_element_create(d, names.back().c_str());
        ASSERT_NE(e, nullptr);
        ASSERT_EQ(leptris_element_append_child(root, e), LEPTRIS_OK);
        /* post-cap creates are indistinguishable: name reads back */
        EXPECT_STREQ(leptris_element_get_name(e), names.back().c_str());
    }
    EXPECT_LE(leptris_name_intern_distinct(d),
              (size_t)DOM_INTERN_MAX_DISTINCT);

    /* full-tree serialization still carries every distinct element */
    char* xml = leptris_serialize_document(d);
    ASSERT_NE(xml, nullptr);
    size_t elems = 0;
    for (const char* p = xml; (p = strstr(p, "<n")) != NULL; p++) elems++;
    EXPECT_EQ(elems, kOver);
    leptris_free_string(xml);

    leptris_document_free(d);
}

/* No case folding: names differing only in case are distinct
 * entries with distinct storage. */
TEST(InternNames, CaseDistinctNoFolding) {
    LeptrisDocument d = leptris_document_create();
    ASSERT_NE(d, nullptr);
    LeptrisElement root = leptris_element_create(d, "root");
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(leptris_document_set_root(d, root), LEPTRIS_OK);

    LeptrisElement a = leptris_element_create(d, "Tag");
    LeptrisElement b = leptris_element_create(d, "tag");
    LeptrisElement c = leptris_element_create(d, "TAG");
    ASSERT_NE(a, nullptr);
    ASSERT_NE(b, nullptr);
    ASSERT_NE(c, nullptr);

    EXPECT_NE(elem_name(a), elem_name(b));
    EXPECT_NE(elem_name(a), elem_name(c));
    EXPECT_NE(elem_name(b), elem_name(c));
    EXPECT_EQ(leptris_name_intern_distinct(d), 4u); /* + root */
    EXPECT_STREQ(leptris_element_get_name(a), "Tag");
    EXPECT_STREQ(leptris_element_get_name(b), "tag");
    EXPECT_STREQ(leptris_element_get_name(c), "TAG");

    leptris_document_free(d);
}

/* Populated table + churned names must die with the document without
 * touching freed storage (the ASAN/leaks run validates the release;
 * a follow-up document exercises the recycled-block path). */
TEST(InternNames, PopulatedTableFreesCleanWithDocument) {
    for (int round = 0; round < 3; round++) {
        LeptrisDocument d = leptris_document_create();
        ASSERT_NE(d, nullptr);
        LeptrisElement root = leptris_element_create(d, "root");
        ASSERT_NE(root, nullptr);
        ASSERT_EQ(leptris_document_set_root(d, root), LEPTRIS_OK);
        for (int i = 0; i < 100; i++) {
            std::string uniq = "u" + std::to_string(i);
            LeptrisElement e = leptris_element_create(d, "rep");
            ASSERT_NE(e, nullptr);
            LeptrisElement f = leptris_element_create(d, uniq.c_str());
            ASSERT_NE(f, nullptr);
            ASSERT_EQ(leptris_element_append_child(root, e), LEPTRIS_OK);
            ASSERT_EQ(leptris_element_append_child(root, f), LEPTRIS_OK);
        }
        /* rename churn through the intern */
        LeptrisElement g = leptris_element_create(d, "victim");
        ASSERT_NE(g, nullptr);
        for (int i = 0; i < 20; i++) {
            std::string rn = "rn" + std::to_string(i);
            ASSERT_EQ(leptris_element_set_name(g, rn.c_str()), LEPTRIS_OK);
        }
        leptris_document_free(d);
    }
    SUCCEED();
}

/* Cross-document adoption copies fresh names (#1528/#358 family):
 * the adopted copy must NOT point into the source document's intern
 * slots, and must stay valid after the source document dies. */
TEST(InternNames, CrossDocAdoptCopiesFreshName) {
    LeptrisDocument a = leptris_document_create();
    ASSERT_NE(a, nullptr);
    LeptrisElement ra = leptris_element_create(a, "ra");
    ASSERT_NE(ra, nullptr);
    ASSERT_EQ(leptris_document_set_root(a, ra), LEPTRIS_OK);
    LeptrisElement child = leptris_element_create(a, "item");
    ASSERT_NE(child, nullptr);
    ASSERT_EQ(leptris_element_append_child(ra, child), LEPTRIS_OK);
    const char* child_slot = elem_name(child);

    LeptrisDocument b = leptris_document_create();
    ASSERT_NE(b, nullptr);
    LeptrisElement rb = leptris_element_create(b, "rb");
    ASSERT_NE(rb, nullptr);
    ASSERT_EQ(leptris_document_set_root(b, rb), LEPTRIS_OK);
    /* foreign child → adopted BY COPY */
    ASSERT_EQ(leptris_element_append_child(rb, child), LEPTRIS_OK);
    LeptrisElement copy = leptris_element_get_first_child(rb);
    ASSERT_NE(copy, nullptr);
    EXPECT_NE(elem_name(copy), child_slot);
    EXPECT_STREQ(leptris_element_get_name(copy), "item");
    /* the source's intern table is untouched by the adoption */
    EXPECT_EQ(leptris_name_intern_distinct(a), 2u); /* ra + item */

    leptris_document_free(a);
    /* the copy's name is b-owned storage — still valid */
    EXPECT_STREQ(leptris_element_get_name(copy), "item");
    leptris_document_free(b);
}

/* Interned elements keep the mut-name backpointer: a detached
 * (never-attached) element still resolves its document — the create
 * path relies on namebp instead of root-map registration. */
TEST(InternNames, DetachedInternedElementResolvesDoc) {
    LeptrisDocument d = leptris_document_create();
    ASSERT_NE(d, nullptr);
    LeptrisElement e = leptris_element_create(d, "solo");
    ASSERT_NE(e, nullptr);
    EXPECT_TRUE(leptris_elem_has_namebp(e));
    EXPECT_EQ(leptris_elem_namebp_doc(e), d);
    EXPECT_EQ(leptris_element_get_document(e), d);
    leptris_document_free(d);
}

}  // namespace
