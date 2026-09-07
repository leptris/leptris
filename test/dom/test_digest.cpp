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

/* Null contract: no node, no digest. */
TEST(Digest, NullNodeIsZero) {
    EXPECT_EQ(leptris_node_digest(nullptr, LEPTRIS_DIGEST_DEFAULT), 0u);
}

}  // namespace
