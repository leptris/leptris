// test/dom/test_digest.cpp — subtree structural digest specs (#869).
//
// leptris_node_digest returns a content-defined Merkle hash of a
// subtree. The soundness contract the comparator relies on: digest
// equality implies subtree equivalence under the flag semantics.
// Inequality implies nothing (the consumer descends and decides).

#include <gtest/gtest.h>

#include "leptris.h"

#include <cstring>
#include <string>
#include <cstdlib>
#ifdef _WIN32
static void td_setenv(const char* k, const char* v) { _putenv_s(k, v); }
static void td_unsetenv(const char* k) { _putenv_s(k, ""); }
#else
static void td_setenv(const char* k, const char* v) { setenv(k, v, 1); }
static void td_unsetenv(const char* k) { unsetenv(k); }
#endif

namespace {

LeptrisElement parse_root(const char* xml) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    if (!doc) return nullptr;
    return leptris_document_root(doc);
}

uint64_t digest_of(const char* xml, LeptrisDigestFlags flags) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    if (!doc) return 0;
    LeptrisElement root = leptris_document_root(doc);
    uint64_t d = root ? leptris_node_digest((LeptrisNodeRef)root, flags) : 0;
    leptris_document_free(doc);
    return d;
}

/* Equality side of the contract: identical content parses to equal
 * digests, with and without the whitespace-drop flag. */
TEST(Digest, EqualAcrossIdenticalDocuments) {
    const char xml[] = "<r a='1'><b x='2'>t</b><!--c--><?p d?><![CDATA[q]]></r>";
    EXPECT_EQ(digest_of(xml, LEPTRIS_DIGEST_DEFAULT),
              digest_of(xml, LEPTRIS_DIGEST_DEFAULT));
    EXPECT_EQ(digest_of(xml, LEPTRIS_DIGEST_DROP_WS_TEXT),
              digest_of(xml, LEPTRIS_DIGEST_DROP_WS_TEXT));
}

TEST(Digest, StableAcrossCalls) {
    const char xml[] = "<r><a/></r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    EXPECT_EQ(leptris_node_digest((LeptrisNodeRef)root, LEPTRIS_DIGEST_DEFAULT),
              leptris_node_digest((LeptrisNodeRef)root, LEPTRIS_DIGEST_DEFAULT));
    leptris_document_free(doc);
}

/* Violation cases: every content difference the comparator relies
 * on must move the digest. */
TEST(Digest, DetectsNameChange) {
    EXPECT_NE(digest_of("<r><a/></r>", LEPTRIS_DIGEST_DEFAULT),
              digest_of("<r><b/></r>", LEPTRIS_DIGEST_DEFAULT));
}

TEST(Digest, DetectsAttrValueChange) {
    EXPECT_NE(digest_of("<r a='1'/>", LEPTRIS_DIGEST_DEFAULT),
              digest_of("<r a='2'/>", LEPTRIS_DIGEST_DEFAULT));
}

TEST(Digest, DetectsTextChange) {
    EXPECT_NE(digest_of("<r><a>x</a></r>", LEPTRIS_DIGEST_DEFAULT),
              digest_of("<r><a>y</a></r>", LEPTRIS_DIGEST_DEFAULT));
}

TEST(Digest, DetectsChildOrderChange) {
    EXPECT_NE(digest_of("<r><a/><b/></r>", LEPTRIS_DIGEST_DEFAULT),
              digest_of("<r><b/><a/></r>", LEPTRIS_DIGEST_DEFAULT));
}

TEST(Digest, DetectsNamespaceChange) {
    /* Bare name vs namespaced name: resolved URI differs. */
    EXPECT_NE(digest_of("<r/>", LEPTRIS_DIGEST_DEFAULT),
              digest_of("<x:r xmlns:x='urn:u'/>", LEPTRIS_DIGEST_DEFAULT));
}

TEST(Digest, DetectsPrefixChange) {
    /* Same resolved URI, different prefix: the spec hashes the
     * prefix, so these are (deliberately) distinguishable. */
    EXPECT_NE(digest_of("<x:r xmlns:x='urn:u'/>", LEPTRIS_DIGEST_DEFAULT),
              digest_of("<y:r xmlns:y='urn:u'/>", LEPTRIS_DIGEST_DEFAULT));
}

/* Attribute ORDER must not matter (sorted by URI+local at hash
 * time) — the comparator treats reordered attributes as equal. */
/* #1297: hosts whose binding has no flags carrier (moxml's
 * Node#digest takes no flags parameter) opt in through the
 * LEPTRIS_DIGEST_ATTR_ORDER env var — per-call read, empty string
 * counts as unset (MSVC _putenv_s(k, "")), "0" disables. The env
 * path must produce exactly what the explicit flag produces. */
TEST(Digest, AttrOrderEnvOptInMatchesExplicitFlag) {
    td_unsetenv("LEPTRIS_DIGEST_ATTR_ORDER");
    /* Order-differing docs: equal by default (sorted), unequal with
     * the flag — repro shape from #1297. */
    EXPECT_EQ(digest_of("<r x='1' y='2'/>", LEPTRIS_DIGEST_DEFAULT),
              digest_of("<r y='2' x='1'/>", LEPTRIS_DIGEST_DEFAULT));
    EXPECT_NE(digest_of("<r x='1' y='2'/>", LEPTRIS_DIGEST_ATTR_ORDER),
              digest_of("<r y='2' x='1'/>", LEPTRIS_DIGEST_ATTR_ORDER));

    td_setenv("LEPTRIS_DIGEST_ATTR_ORDER", "1");
    EXPECT_NE(digest_of("<r x='1' y='2'/>", LEPTRIS_DIGEST_DEFAULT),
              digest_of("<r y='2' x='1'/>", LEPTRIS_DIGEST_DEFAULT))
        << "env opt-in must activate order sensitivity with flagless calls";
    EXPECT_EQ(digest_of("<r x='1' y='2'/>", LEPTRIS_DIGEST_DEFAULT),
              digest_of("<r x='1' y='2'/>", LEPTRIS_DIGEST_ATTR_ORDER))
        << "env opt-in must match the explicit flag exactly";

    /* "0" disables; empty counts as unset. */
    td_setenv("LEPTRIS_DIGEST_ATTR_ORDER", "0");
    EXPECT_EQ(digest_of("<r x='1' y='2'/>", LEPTRIS_DIGEST_DEFAULT),
              digest_of("<r y='2' x='1'/>", LEPTRIS_DIGEST_DEFAULT));
    td_setenv("LEPTRIS_DIGEST_ATTR_ORDER", "");
    EXPECT_EQ(digest_of("<r x='1' y='2'/>", LEPTRIS_DIGEST_DEFAULT),
              digest_of("<r y='2' x='1'/>", LEPTRIS_DIGEST_DEFAULT));

    /* Explicit flags still compose with the env OR: DROP_WS
     * explicit + env ATTR_ORDER => ws-only text dropped AND order
     * sensitivity active (the two docs differ only in attr order;
     * their ws-only text is dropped by the explicit flag). */
    td_setenv("LEPTRIS_DIGEST_ATTR_ORDER", "1");
    EXPECT_NE(digest_of("<r x='1' y='2'> </r>", LEPTRIS_DIGEST_DROP_WS_TEXT),
              digest_of("<r y='2' x='1'>  </r>", LEPTRIS_DIGEST_DROP_WS_TEXT))
        << "env flag must OR with explicitly passed flags";
    td_unsetenv("LEPTRIS_DIGEST_ATTR_ORDER");
}

TEST(Digest, AttrOrderInsensitive) {
    EXPECT_EQ(digest_of("<r a='1' b='2'/>", LEPTRIS_DIGEST_DEFAULT),
              digest_of("<r b='2' a='1'/>", LEPTRIS_DIGEST_DEFAULT));
}

/* The DROP_WS_TEXT flag: whitespace-only text nodes vanish from
 * the hash. Without the flag they participate. */
TEST(Digest, DropWsTextSkipsWhitespaceOnlyNodes) {
    EXPECT_EQ(digest_of("<r><a/><b/></r>", LEPTRIS_DIGEST_DROP_WS_TEXT),
              digest_of("<r><a/> <b/></r>", LEPTRIS_DIGEST_DROP_WS_TEXT));
    EXPECT_NE(digest_of("<r><a/><b/></r>", LEPTRIS_DIGEST_DEFAULT),
              digest_of("<r><a/> <b/></r>", LEPTRIS_DIGEST_DEFAULT));
}

/* #1200: ATTR_ORDER keeps document order in the hash — reordered
 * attributes digest DIFFERENT, same-order documents stay equal,
 * and the flag composes with DROP_WS_TEXT. The dedup (first-wins
 * on the resolved key) still applies. */
TEST(Digest, AttrOrderSensitiveVariant) {
    EXPECT_NE(digest_of("<r a='1' b='2'/>", LEPTRIS_DIGEST_ATTR_ORDER),
              digest_of("<r b='2' a='1'/>", LEPTRIS_DIGEST_ATTR_ORDER));
    EXPECT_EQ(digest_of("<r a='1' b='2'/>", LEPTRIS_DIGEST_ATTR_ORDER),
              digest_of("<r a='1' b='2'/>", LEPTRIS_DIGEST_ATTR_ORDER));
}

TEST(Digest, AttrOrderComposesWithDropWs) {
    EXPECT_EQ(
        digest_of("<r a='1' b='2'><a/><b/></r>",
                  (LeptrisDigestFlags)(LEPTRIS_DIGEST_ATTR_ORDER |
                                       LEPTRIS_DIGEST_DROP_WS_TEXT)),
        digest_of("<r a='1' b='2'><a/> <b/></r>",
                  (LeptrisDigestFlags)(LEPTRIS_DIGEST_ATTR_ORDER |
                                       LEPTRIS_DIGEST_DROP_WS_TEXT)));
    EXPECT_NE(
        digest_of("<r a='1' b='2'><a/></r>",
                  (LeptrisDigestFlags)(LEPTRIS_DIGEST_ATTR_ORDER |
                                       LEPTRIS_DIGEST_DROP_WS_TEXT)),
        digest_of("<r b='2' a='1'><a/></r>",
                  (LeptrisDigestFlags)(LEPTRIS_DIGEST_ATTR_ORDER |
                                       LEPTRIS_DIGEST_DROP_WS_TEXT)));
}

TEST(Digest, AttrOrderRecursesIntoChildren) {
    /* The child's attribute order participates too: the Merkle
     * combine feeds each child's digest (with the flag) upward. */
    EXPECT_NE(
        digest_of("<r><c x='1' y='2'/></r>", LEPTRIS_DIGEST_ATTR_ORDER),
        digest_of("<r><c y='2' x='1'/></r>", LEPTRIS_DIGEST_ATTR_ORDER));
}

/* Null contract: no node, no digest. */
TEST(Digest, NullNodeIsZero) {
    EXPECT_EQ(leptris_node_digest(nullptr, LEPTRIS_DIGEST_DEFAULT), 0u);
}

}  // namespace
