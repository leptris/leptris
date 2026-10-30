// leptris#1905: one call answering what this installed engine
// serves. The descriptor is a versioned bitset — new capabilities
// APPEND bits, never reorder or repack (a reorder bumps
// LEPTRIS_CAPS_DESCRIPTOR_VERSION).
#include <leptris.h>
#include <leptris/descriptor.h>
#include <leptris/dtd.h>
#include <leptris/html.h>
#include <leptris/sax/sax.h>
#include <leptris/xpath/xpath.h>
#include <leptris/xquery/xquery.h>

#include <gtest/gtest.h>

// Anchor law: each unconditional bit's face symbol is REFERENCED
// here — a descriptor bit without a real, linked face fails the
// BUILD of this test, not just the assertion.
namespace caps_anchors {
void* const kParse = (void*)&leptris_parse_string;
void* const kDom = (void*)&leptris_document_root;
void* const kXpath = (void*)&leptris_xpath_eval;
void* const kXquery = (void*)&leptris_xquery_eval;
void* const kDtd = (void*)&leptris_dtd_parse;
void* const kSax = (void*)&leptris_sax_parse;
void* const kHtml = (void*)&leptris_parse_html_string;
void* const kXinclude = (void*)&leptris_xinclude_process;
void* const kDiff = (void*)&leptris_diff;
void* const kDigest = (void*)&leptris_node_digest;
void* const kXsd = (void*)&leptris_xsd_compile;
void* const kXsdSet = (void*)&leptris_xsd_compile_set;
void* const kRelaxng = (void*)&leptris_rng_parse;
void* const kSchematron = (void*)&leptris_schematron_parse;
void* const kXslt = (void*)&leptris_xslt_parse;
void* const kDescriptor = (void*)&leptris_plan_result_build_packed;
void* const kNsSnapshot = (void*)&leptris_element_namespace_declarations_snapshot;
void* const kChildrenBatch = (void*)&leptris_element_children_batch;
void* const kNameCompare = (void*)&leptris_node_name_compare;
void* const kVersion = (void*)&leptris_version;
void* const kEntityRefs = (void*)&leptris_entity_ref_node_name;
}  // namespace caps_anchors

TEST(LeptrisCapabilities, DescriptorVersionIsCurrent) {
    LeptrisCapabilities c = leptris_capabilities();
    EXPECT_EQ(c.descriptor_version, LEPTRIS_CAPS_DESCRIPTOR_VERSION);
}

TEST(LeptrisCapabilities, UnconditionalFacesAreSet) {
    LeptrisCapabilities c = leptris_capabilities();
    EXPECT_NE(c.caps & LEPTRIS_CAP_PARSE, 0ULL);
    EXPECT_NE(c.caps & LEPTRIS_CAP_DOM, 0ULL);
    EXPECT_NE(c.caps & LEPTRIS_CAP_XPATH, 0ULL);
    EXPECT_NE(c.caps & LEPTRIS_CAP_XQUERY, 0ULL);
    EXPECT_NE(c.caps & LEPTRIS_CAP_DTD, 0ULL);
    EXPECT_NE(c.caps & LEPTRIS_CAP_SAX, 0ULL);
    EXPECT_NE(c.caps & LEPTRIS_CAP_HTML, 0ULL);
    EXPECT_NE(c.caps & LEPTRIS_CAP_XINCLUDE, 0ULL);
    EXPECT_NE(c.caps & LEPTRIS_CAP_DIFF, 0ULL);
    EXPECT_NE(c.caps & LEPTRIS_CAP_DIGEST, 0ULL);
    EXPECT_NE(c.caps & LEPTRIS_CAP_XSD, 0ULL);
    EXPECT_NE(c.caps & LEPTRIS_CAP_XSD_SET, 0ULL);
    EXPECT_NE(c.caps & LEPTRIS_CAP_RELAXNG, 0ULL);
    EXPECT_NE(c.caps & LEPTRIS_CAP_SCHEMATRON, 0ULL);
    EXPECT_NE(c.caps & LEPTRIS_CAP_XSLT, 0ULL);
    EXPECT_NE(c.caps & LEPTRIS_CAP_DESCRIPTOR, 0ULL);
    EXPECT_NE(c.caps & LEPTRIS_CAP_NS_SNAPSHOT_BULK, 0ULL);
    EXPECT_NE(c.caps & LEPTRIS_CAP_CHILDREN_BATCH, 0ULL);
    EXPECT_NE(c.caps & LEPTRIS_CAP_NAME_COMPARE, 0ULL);
    EXPECT_NE(c.caps & LEPTRIS_CAP_VERSION, 0ULL);
    EXPECT_NE(c.caps & LEPTRIS_CAP_ENTITY_REF_NODES, 0ULL);
}

TEST(LeptrisCapabilities, BuildConditionalBitsMatchTheBuild) {
    LeptrisCapabilities c = leptris_capabilities();
#if defined(LEPTRIS_HAS_DUCET)
    EXPECT_NE(c.caps & LEPTRIS_CAP_COLLATION_DUCET, 0ULL);
#else
    EXPECT_EQ(c.caps & LEPTRIS_CAP_COLLATION_DUCET, 0ULL);
#endif
#if defined(LEPTRIS_HAS_UTF8PROC)
    EXPECT_NE(c.caps & LEPTRIS_CAP_UNICODE_UTF8PROC, 0ULL);
#else
    EXPECT_EQ(c.caps & LEPTRIS_CAP_UNICODE_UTF8PROC, 0ULL);
#endif
#if defined(LEPTRIS_HAS_ICONV)
    EXPECT_NE(c.caps & LEPTRIS_CAP_ENCODING_ICONV, 0ULL);
#else
    EXPECT_EQ(c.caps & LEPTRIS_CAP_ENCODING_ICONV, 0ULL);
#endif
#if defined(LEPTRIS_HAS_AVX2_BUILD)
    EXPECT_NE(c.caps & LEPTRIS_CAP_SIMD_AVX2, 0ULL);
#else
    EXPECT_EQ(c.caps & LEPTRIS_CAP_SIMD_AVX2, 0ULL);
#endif
}

TEST(LeptrisCapabilities, NoUnknownBits) {
    LeptrisCapabilities c = leptris_capabilities();
    constexpr unsigned long long kKnown =
        LEPTRIS_CAP_PARSE | LEPTRIS_CAP_DOM | LEPTRIS_CAP_XPATH |
        LEPTRIS_CAP_XQUERY | LEPTRIS_CAP_DTD | LEPTRIS_CAP_SAX |
        LEPTRIS_CAP_HTML | LEPTRIS_CAP_XINCLUDE | LEPTRIS_CAP_DIFF |
        LEPTRIS_CAP_DIGEST | LEPTRIS_CAP_XSD | LEPTRIS_CAP_XSD_SET |
        LEPTRIS_CAP_RELAXNG | LEPTRIS_CAP_SCHEMATRON | LEPTRIS_CAP_XSLT |
        LEPTRIS_CAP_DESCRIPTOR | LEPTRIS_CAP_NS_SNAPSHOT_BULK |
        LEPTRIS_CAP_CHILDREN_BATCH | LEPTRIS_CAP_NAME_COMPARE |
        LEPTRIS_CAP_VERSION | LEPTRIS_CAP_COLLATION_DUCET |
        LEPTRIS_CAP_UNICODE_UTF8PROC | LEPTRIS_CAP_ENCODING_ICONV |
        LEPTRIS_CAP_SIMD_AVX2 | LEPTRIS_CAP_ENTITY_REF_NODES;
    EXPECT_EQ(c.caps & ~kKnown, 0ULL)
        << "an unknown bit is set — the descriptor grew without "
           "bumping LEPTRIS_CAPS_DESCRIPTOR_VERSION";
}
