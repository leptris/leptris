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

/* --- #2132 append-uniq lever guards -------------------------------------
 * The fused create-path scan (one pass: length + colon + hash) must be
 * byte-identical to the three separate passes it replaces: the same
 * names hash the same, the same QNames split the same, the same cap
 * overflow carves the same. These specs pin the carve-path behaviors
 * through the public surface (find_child rides the name_hash
 * prefilter, so a stale or divergent hash breaks lookups). ---------- */

/* Past the intern cap every create takes the carve fallback. The
 * carve path recomputes the canonical name hash — a divergence would
 * silently break every hash-prefiltered child lookup. */
TEST(InternNames, CarvePathNameHashPrefilterStillFinds) {
    LeptrisDocument d = leptris_document_create();
    ASSERT_NE(d, nullptr);
    LeptrisElement root = leptris_element_create(d, "root");
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(leptris_document_set_root(d, root), LEPTRIS_OK);

    const size_t kOver = (size_t)DOM_INTERN_MAX_DISTINCT + 256;
    for (size_t i = 0; i < kOver; i++) {
        std::string n = "n" + std::to_string(i);
        LeptrisElement e = leptris_element_create(d, n.c_str());
        ASSERT_NE(e, nullptr);
        ASSERT_EQ(leptris_element_append_child(root, e), LEPTRIS_OK);
    }
    /* the table latched overflow — every lookup below resolves through
     * the carve path's recomputed hashes */
    EXPECT_EQ(leptris_name_intern_distinct(d),
              (size_t)DOM_INTERN_MAX_DISTINCT);
    /* head, mid, and tail children all findable by name */
    const size_t probes[] = {0, 1, kOver / 2, kOver - 2, kOver - 1};
    for (size_t idx : probes) {
        std::string n = "n" + std::to_string(idx);
        LeptrisElement found = leptris_element_find_child(root, n.c_str());
        ASSERT_NE(found, nullptr) << "probe " << n;
        EXPECT_STREQ(leptris_element_get_name(found), n.c_str());
    }
    /* a name that was never created must miss */
    EXPECT_EQ(leptris_element_find_child(root, "n_no_such"),
              nullptr);
    leptris_document_free(d);
}

/* A trailing colon is NOT a QName split (html5lib 'rdar:' semantics):
 * the whole token stays the local name, no prefix materializes. */
TEST(InternNames, TrailingColonKeepsWholeToken) {
    LeptrisDocument d = leptris_document_create();
    ASSERT_NE(d, nullptr);
    LeptrisElement root = leptris_element_create(d, "root");
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(leptris_document_set_root(d, root), LEPTRIS_OK);

    LeptrisElement e = leptris_element_create(d, "rdar:");
    ASSERT_NE(e, nullptr);
    ASSERT_EQ(leptris_element_append_child(root, e), LEPTRIS_OK);
    EXPECT_STREQ(leptris_element_get_name(e), "rdar:");
    /* still a full QName token: lookup by the whole name hits */
    EXPECT_EQ(leptris_element_find_child(root, "rdar:"), e);
    char* xml = leptris_serialize_document(d);
    ASSERT_NE(xml, nullptr);
    EXPECT_NE(strstr(xml, "<rdar:"), nullptr);
    leptris_free_string(xml);
    leptris_document_free(d);
}

/* A prefixed create splits into prefix + local: the local name reads
 * back, the hash/len prefilter is recomputed for the LOCAL part, and
 * the child lookup by local name still resolves (the p: prefix rides
 * the ns_cache — a bare prefixed create has no URI, serialization
 * keeps the lexical prefix). */
TEST(InternNames, PrefixedCreateSplitsAndPrefiltersLocal) {
    LeptrisDocument d = leptris_document_create();
    ASSERT_NE(d, nullptr);
    LeptrisElement root = leptris_element_create(d, "root");
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(leptris_document_set_root(d, root), LEPTRIS_OK);

    LeptrisElement e = leptris_element_create(d, "p:local");
    ASSERT_NE(e, nullptr);
    ASSERT_EQ(leptris_element_append_child(root, e), LEPTRIS_OK);
    EXPECT_STREQ(leptris_element_get_name(e), "local");
    LeptrisElement found = leptris_element_find_child(root, "local");
    EXPECT_EQ(found, e);
    /* name_hash must be the canonical hash of the LOCAL part — a
     * mismatch here is the regression the fused scan must not cause */
    for (LeptrisElement c = leptris_element_get_first_child(root); c;
         c = leptris_element_get_next_sibling(c)) {
        if (c == e)
            EXPECT_STREQ(leptris_element_get_name(c), "local");
    }
    char* xml = leptris_serialize_document(d);
    ASSERT_NE(xml, nullptr);
    EXPECT_NE(strstr(xml, "<p:local"), nullptr);
    leptris_free_string(xml);
    leptris_document_free(d);
}

/* Long prefixed names (17+ bytes: the memchr colon-probe tier today,
 * the inline fused-scan tier after) split identically. */
TEST(InternNames, LongPrefixedNameSplitsIdentically) {
    LeptrisDocument d = leptris_document_create();
    ASSERT_NE(d, nullptr);
    LeptrisElement root = leptris_element_create(d, "root");
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(leptris_document_set_root(d, root), LEPTRIS_OK);

    const char* qname = "verylongprefixname:local";
    LeptrisElement e = leptris_element_create(d, qname);
    ASSERT_NE(e, nullptr);
    ASSERT_EQ(leptris_element_append_child(root, e), LEPTRIS_OK);
    EXPECT_STREQ(leptris_element_get_name(e), "local");
    EXPECT_NE(leptris_element_find_child(root, "local"), nullptr);
    char* xml = leptris_serialize_document(d);
    ASSERT_NE(xml, nullptr);
    EXPECT_NE(strstr(xml, "<verylongprefixname:local"), nullptr);
    leptris_free_string(xml);
    leptris_document_free(d);
}

/* S8 carve boundary: the 8-byte word-store fast path must produce
 * exactly the same bytes as the general path — names of 1, 7, 8, and
 * 9 bytes all read back intact (and no trailing-garbage leak into
 * the name). */
TEST(InternNames, CarveBoundaryLengthsReadBackIntact) {
    LeptrisDocument d = leptris_document_create();
    ASSERT_NE(d, nullptr);
    LeptrisElement root = leptris_element_create(d, "root");
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(leptris_document_set_root(d, root), LEPTRIS_OK);

    const char* names[] = {"a", "abc1234" /* 7 */, "abc12345" /* 8 */,
                           "abc123456" /* 9 */, "x" /* repeat of 1 */};
    for (const char* n : names) {
        LeptrisElement e = leptris_element_create(d, n);
        ASSERT_NE(e, nullptr) << n;
        ASSERT_EQ(leptris_element_append_child(root, e), LEPTRIS_OK);
        EXPECT_STREQ(leptris_element_get_name(e), n);
    }
    /* the repeat 1-byte name shares the intern slot with the first */
    LeptrisElement dup = leptris_element_create(d, "a");
    ASSERT_NE(dup, nullptr);
    ASSERT_EQ(leptris_element_append_child(root, dup), LEPTRIS_OK);
    EXPECT_STREQ(leptris_element_get_name(dup), "a");
    leptris_document_free(d);
}

/* >254-byte names never intern and never carve — the pool fallback
 * reads the WHOLE token back (today's contract: no split on this
 * path). The fused scan must route these identically. */
TEST(InternNames, Over254NamePoolPathUnchanged) {
    std::string longname(300, 'q');
    LeptrisDocument d = leptris_document_create();
    ASSERT_NE(d, nullptr);
    LeptrisElement root = leptris_element_create(d, "root");
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(leptris_document_set_root(d, root), LEPTRIS_OK);

    LeptrisElement e = leptris_element_create(d, longname.c_str());
    ASSERT_NE(e, nullptr);
    ASSERT_EQ(leptris_element_append_child(root, e), LEPTRIS_OK);
    EXPECT_STREQ(leptris_element_get_name(e), longname.c_str());
    EXPECT_NE(leptris_element_find_child(root, longname.c_str()),
              nullptr);
    leptris_document_free(d);
}

/* --- #2132 uniq-intern sizing lever guards -------------------------------
 * The intern table grows as distinct names arrive (open addressing:
 * every growth re-probes every live entry into a fresh slot array).
 * These specs pin the growth-time contracts independent of the growth
 * POLICY (doubling cadence or final-size jump):
 *   - an entry interned early is still sharable after the table has
 *     grown (same name bytes, same cached hash) — no stale entries
 *   - the distinct count never double-counts across a rehash
 *   - lookups (hash-prefiltered find_child) resolve across growth
 * and one spec pins the carve-path zero-init contract the block
 * memset used to provide (fresh elements carry no recycled state).
 * ---------------------------------------------------------------------- */

/* Names interned BEFORE a growth must still share their original
 * slot bytes AFTER the table has grown past several rehashes, and
 * the distinct count must not double-count. 1200 distinct names
 * crosses multiple growth boundaries under any geometric policy. */
TEST(InternNames, EarlyEntriesStaySharableAcrossGrowth) {
    LeptrisDocument d = leptris_document_create();
    ASSERT_NE(d, nullptr);
    LeptrisElement root = leptris_element_create(d, "root");
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(leptris_document_set_root(d, root), LEPTRIS_OK);

    /* seed names interned at the smallest table size */
    const char* seeds[] = {"k0", "k1", "k2", "k3", "k4", "k5", "k6", "k7"};
    const char* seed_slots[8];
    for (int i = 0; i < 8; i++) {
        LeptrisElement e = leptris_element_create(d, seeds[i]);
        ASSERT_NE(e, nullptr) << seeds[i];
        seed_slots[i] = elem_name(e);
    }

    /* distinct stream interleaved with seed re-creates: crosses the
     * growth boundaries mid-stream (rehashes happen while earlier
     * entries are live — the stale-entry hazard). */
    const int kDistinct = 1200;
    for (int i = 0; i < kDistinct; i++) {
        std::string n = "g" + std::to_string(i);
        LeptrisElement e = leptris_element_create(d, n.c_str());
        ASSERT_NE(e, nullptr) << n;
        /* every ~100 names re-create a seed: must share the ORIGINAL
         * slot bytes — a rehash must not move or orphan them */
        if (i % 100 == 0) {
            LeptrisElement s =
                leptris_element_create(d, seeds[i % 8]);
            ASSERT_NE(s, nullptr) << seeds[i % 8];
            EXPECT_EQ(elem_name(s), seed_slots[i % 8])
                << "seed slot moved at distinct " << i;
            EXPECT_STREQ(leptris_element_get_name(s), seeds[i % 8]);
        }
    }
    /* seeds share after the FULL growth too */
    for (int i = 0; i < 8; i++) {
        LeptrisElement s = leptris_element_create(d, seeds[i]);
        ASSERT_NE(s, nullptr);
        EXPECT_EQ(elem_name(s), seed_slots[i]) << seeds[i];
    }
    /* exact distinct count: root + 8 seeds + 1200 stream (seed
     * re-creates must not double-count through any rehash) */
    EXPECT_EQ(leptris_name_intern_distinct(d), 1u + 8u + (size_t)kDistinct);

    leptris_document_free(d);
}

/* The cached 16-bit hash and the slot bytes of an entry interned at
 * the initial table size must be identical after the table reaches
 * its largest size — XPath child-axis prefilters ride this hash. */
TEST(InternNames, HashStableAcrossGrowth) {
    LeptrisDocument d = leptris_document_create();
    ASSERT_NE(d, nullptr);
    LeptrisElement root = leptris_element_create(d, "root");
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(leptris_document_set_root(d, root), LEPTRIS_OK);

    LeptrisElement keeper = leptris_element_create(d, "keeper");
    ASSERT_NE(keeper, nullptr);
    const char* keeper_slot = elem_name(keeper);
    uint16_t keeper_hash = keeper->name_hash;
    EXPECT_EQ(keeper_hash, leptris_name_hash_compute("keeper"));

    const int kGrow = 3000;
    for (int i = 0; i < kGrow; i++) {
        std::string n = "h" + std::to_string(i);
        LeptrisElement e = leptris_element_create(d, n.c_str());
        ASSERT_NE(e, nullptr) << n;
        EXPECT_EQ(e->name_hash, leptris_name_hash_compute(n.c_str()))
            << n;
    }

    /* keeper after growth: same slot, same hash */
    LeptrisElement again = leptris_element_create(d, "keeper");
    ASSERT_NE(again, nullptr);
    EXPECT_EQ(elem_name(again), keeper_slot);
    EXPECT_EQ(again->name_hash, keeper_hash);
    EXPECT_EQ(again->name_hash, leptris_name_hash_compute("keeper"));
    /* the hash prefilter still resolves keeper from the grown table */
    EXPECT_EQ(leptris_element_find_child(root, "keeper"), nullptr);
    ASSERT_EQ(leptris_element_append_child(root, again), LEPTRIS_OK);
    EXPECT_EQ(leptris_element_find_child(root, "keeper"), again);

    leptris_document_free(d);
}

/* No stale entries: after growth, a fresh pass over EVERY name must
 * share (each name resolves to exactly one slot), the distinct count
 * must be unchanged, and head/mid/tail names must stay findable. */
TEST(InternNames, NoStaleEntriesAfterGrowth) {
    LeptrisDocument d = leptris_document_create();
    ASSERT_NE(d, nullptr);
    LeptrisElement root = leptris_element_create(d, "root");
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(leptris_document_set_root(d, root), LEPTRIS_OK);

    const int kNames = 1500;
    std::vector<std::string> names;
    std::vector<const char*> slots;
    names.reserve(kNames);
    slots.reserve(kNames);
    for (int i = 0; i < kNames; i++) {
        names.push_back("s" + std::to_string(i));
        LeptrisElement e = leptris_element_create(d, names.back().c_str());
        ASSERT_NE(e, nullptr) << names.back();
        slots.push_back(elem_name(e));
    }
    ASSERT_EQ(leptris_name_intern_distinct(d), 1u + (size_t)kNames);

    /* full re-probe pass: every name must share its slot; the
     * distinct count must not move */
    for (int i = 0; i < kNames; i++) {
        LeptrisElement e = leptris_element_create(d, names[i].c_str());
        ASSERT_NE(e, nullptr) << names[i];
        EXPECT_EQ(elem_name(e), slots[i]) << names[i];
        EXPECT_STREQ(leptris_element_get_name(e), names[i].c_str());
    }
    EXPECT_EQ(leptris_name_intern_distinct(d), 1u + (size_t)kNames);

    /* hash-prefiltered lookups resolve across the growth (attach a
     * sample spread over head/mid/tail and find them) */
    const int probes[] = {0, 1, kNames / 3, kNames / 2,
                          (2 * kNames) / 3, kNames - 2, kNames - 1};
    for (int idx : probes) {
        LeptrisElement e =
            leptris_element_create(d, names[idx].c_str());
        ASSERT_NE(e, nullptr) << names[idx];
        ASSERT_EQ(leptris_element_append_child(root, e), LEPTRIS_OK);
        LeptrisElement found =
            leptris_element_find_child(root, names[idx].c_str());
        EXPECT_EQ(found, e) << names[idx];
        EXPECT_STREQ(leptris_element_get_name(found), names[idx].c_str());
    }

    leptris_document_free(d);
}

/* Deep growth up to near the distinct cap stays coherent: 8000
 * distinct names (under DOM_INTERN_MAX_DISTINCT) with seed sharing
 * and exact counts at the end — "table growth across the cap". */
TEST(InternNames, GrowthUpToNearCapStaysCoherent) {
    LeptrisDocument d = leptris_document_create();
    ASSERT_NE(d, nullptr);
    LeptrisElement root = leptris_element_create(d, "root");
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(leptris_document_set_root(d, root), LEPTRIS_OK);

    const char* seed = "anchor";
    LeptrisElement seed_elem = leptris_element_create(d, seed);
    ASSERT_NE(seed_elem, nullptr);
    const char* seed_slot = elem_name(seed_elem);

    const size_t kDeep = (size_t)DOM_INTERN_MAX_DISTINCT - 192;
    for (size_t i = 0; i < kDeep; i++) {
        std::string n = "d" + std::to_string(i);
        LeptrisElement e = leptris_element_create(d, n.c_str());
        ASSERT_NE(e, nullptr) << n;
    }
    EXPECT_EQ(leptris_name_intern_distinct(d), 1u + 1u + kDeep);
    /* the seed still shares after the deep growth */
    LeptrisElement again = leptris_element_create(d, seed);
    ASSERT_NE(again, nullptr);
    EXPECT_EQ(elem_name(again), seed_slot);
    EXPECT_EQ(leptris_name_intern_distinct(d), 1u + 1u + kDeep);

    leptris_document_free(d);
}

/* The carve path's zero-init contract: fresh elements carry NO
 * recycled state. Round 1 dirties the mutation caches (attrs,
 * children, prefixed splits); round 2 creates bare elements and
 * asserts every field the create path does not assign reads as
 * zero/empty — the block memset this spec guards used to provide
 * this (assign-not-clear must not resurrect a previous document's
 * attribute/child/namespace state). */
TEST(InternNames, FreshElementCarriesNoRecycledState) {
    /* round 1: dirty doc — attrs, children, QNames — then free so
     * the recycle caches hold fully-populated element blocks */
    for (int round = 0; round < 2; round++) {
        LeptrisDocument dirty = leptris_document_create();
        ASSERT_NE(dirty, nullptr);
        LeptrisElement droot = leptris_element_create(dirty, "root");
        ASSERT_NE(droot, nullptr);
        ASSERT_EQ(leptris_document_set_root(dirty, droot), LEPTRIS_OK);
        for (int i = 0; i < 600; i++) {
            LeptrisElement e =
                leptris_element_create(dirty, "rich");
            ASSERT_NE(e, nullptr);
            ASSERT_EQ(leptris_element_set_attribute(e, "a", "v"),
                      LEPTRIS_OK);
            ASSERT_EQ(leptris_element_set_attribute(e, "b", "w"),
                      LEPTRIS_OK);
            LeptrisElement kid =
                leptris_element_create(dirty, "p:child");
            ASSERT_NE(kid, nullptr);
            ASSERT_EQ(leptris_element_append_child(e, kid), LEPTRIS_OK);
            ASSERT_EQ(leptris_element_append_child(droot, e),
                      LEPTRIS_OK);
        }
        leptris_document_free(dirty);
    }

    /* round 2: bare elements from the recycled blocks */
    LeptrisDocument d = leptris_document_create();
    ASSERT_NE(d, nullptr);
    LeptrisElement root = leptris_element_create(d, "root");
    ASSERT_NE(root, nullptr);
    ASSERT_EQ(leptris_document_set_root(d, root), LEPTRIS_OK);
    for (int i = 0; i < 600; i++) {
        LeptrisElement e = leptris_element_create(d, "bare");
        ASSERT_NE(e, nullptr);
        /* detached, pre-append: nothing may be set */
        const char* anames[8];
        const char* avals[8];
        EXPECT_EQ(leptris_element_attributes_raw(e, anames, avals, 8),
                  0u)
            << "recycled attrs leaked at " << i;
        EXPECT_EQ(leptris_element_child_count(e), 0u) << i;
        EXPECT_EQ(leptris_element_get_first_child(e), nullptr) << i;
        /* name/hash coherent for a fresh create */
        EXPECT_STREQ(leptris_element_get_name(e), "bare");
        EXPECT_EQ(e->name_hash, leptris_name_hash_compute("bare"));
        ASSERT_EQ(leptris_element_append_child(root, e), LEPTRIS_OK);
    }
    /* serialization carries exactly the bare tree — no phantom
     * attributes, children, or xmlns from the recycled state */
    char* xml = leptris_serialize_document(d);
    ASSERT_NE(xml, nullptr);
    EXPECT_NE(strstr(xml, "<bare"), nullptr);
    EXPECT_EQ(strstr(xml, "a="), nullptr);
    EXPECT_EQ(strstr(xml, "b="), nullptr);
    EXPECT_EQ(strstr(xml, "xmlns"), nullptr);
    EXPECT_EQ(strstr(xml, "p:child"), nullptr);
    leptris_free_string(xml);

    leptris_document_free(d);
}

}  // namespace
