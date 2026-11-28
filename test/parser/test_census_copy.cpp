// test_census_copy — the copy-fused parse census (#2132 attrparse lever).
//
// The direct-parse entry used to run a standalone whole-document
// text_count3_high pre-scan (arena sizing + UTF-8 gate + Track D
// copy-source census) before the parse re-walked the buffer. The
// bounded lane folds the census INTO the mandatory scratch copy
// (leptris_copy_census) and sizes the arena from len-derived upper
// bounds, so small documents are traversed once for the copy instead
// of twice (copy + pre-scan).
//
// Two contracts are pinned here:
//   1. The kernel: leptris_copy_census must agree with its scalar
//      oracle on every input (the cross-arch law — same protocol as
//      test_utf8_simd.cpp) and the copy must be byte-exact.
//   2. The lane: documents that ride the bounded lane (small, IL-
//      ineligible) must parse with behavior identical to the classic
//      exact-count lane — attribute views, encoding gate, census
//      semantics (pristine skip, value normalization), recover and
//      flag parity.
#include "../../src/leptris/common/simd_text.h"

#include <gtest/gtest.h>
#include <cstring>
#include <cstdint>
#include <cstdlib>
#include <string>
#include <vector>

extern "C" {
#include "leptris.h"
}

namespace {

/* Scalar oracle: copy + the census ANY-flags. The ws/control class is
 * deliberately the conservative "< 0x20" rule (the parser only needs
 * no-false-negatives for {'&', tab, LF, CR}; valid XML carries no
 * other control bytes, so the conservatism is unobservable on
 * parseable documents). The SIMD cores and this oracle must agree
 * exactly. */
void copy_census_oracle(char* dst, const char* src, size_t len,
                        int* saw_high, int* amp_any, int* ctlws_any) {
    int h = 0, a = 0, w = 0;
    for (size_t i = 0; i < len; i++) {
        unsigned char ch = (unsigned char)src[i];
        dst[i] = src[i];
        if (ch >= 0x80) h = 1;
        if (ch == '&') a = 1;
        if (ch < 0x20) w = 1;
    }
    *saw_high = h;
    *amp_any = a;
    *ctlws_any = w;
}

void expect_copy_census(const char* s, size_t len) {
    std::vector<char> got(len + 64, 0xAA);
    std::vector<char> want(len + 64, 0xAA);
    int hi = -1, amp = -1, ws = -1;
    leptris_copy_census(got.data(), s, len, &hi, &amp, &ws);
    int ehi = 0, eamp = 0, ews = 0;
    copy_census_oracle(want.data(), s, len, &ehi, &eamp, &ews);
    EXPECT_EQ(hi, ehi) << "saw_high len=" << len;
    EXPECT_EQ(amp, eamp) << "amp_any len=" << len;
    EXPECT_EQ(ws, ews) << "ctlws_any len=" << len;
    EXPECT_EQ(0, memcmp(got.data(), want.data(), len))
        << "copy mismatch len=" << len;
    /* the guard byte past the copy must stay untouched */
    EXPECT_EQ(got[len], '\xAA') << "tail guard clobbered len=" << len;
}

/* Deterministic PRNG (no <random> — keep the seed stable across
 * platforms so a failure reproduces byte-for-byte). */
uint32_t lcg_next(uint32_t* st) {
    *st = (uint32_t)(1103515245u * (uint64_t)*st + 12345u);
    return *st;
}

std::string make_attr_fixture(int elems, int attrs) {
    std::string xml = "<r>";
    for (int e = 0; e < elems; e++) {
        xml += "<e";
        for (int a = 0; a < attrs; a++)
            xml += " a" + std::to_string(a) + "='value-" +
                   std::to_string(a) + "-xyz'";
        xml += "/>";
    }
    xml += "</r>";
    return xml;
}

/* MSVC has no setenv/unsetenv (the interleaved suite's pattern):
 * _putenv_s(k, "") is the only "unset" the CRT offers, and it
 * leaves an empty value the memo treats as unset. */
extern "C" void leptris_dp_env_cache_reset(void);
#ifdef _WIN32
static void set_env(const char* k, const char* v) { _putenv_s(k, v); leptris_dp_env_cache_reset(); }
static void unset_env(const char* k) { _putenv_s(k, ""); leptris_dp_env_cache_reset(); }
#else
static void set_env(const char* k, const char* v) { setenv(k, v, 1); leptris_dp_env_cache_reset(); }
static void unset_env(const char* k) { unsetenv(k); leptris_dp_env_cache_reset(); }
#endif

/* The lane specs pin the CLASSIC-parse census family (the bounded
 * lane and the exact-count lane — the interleaved lane never
 * consults the census and, being env-gated, is otherwise covered by
 * test_interleaved). The fixture pins that family regardless of the
 * ambient environment a CI leg may force (LEPTRIS_INTERLEAVED=1,
 * LEPTRIS_IL_MIN=0), saving and restoring the ambient IL_MIN so the
 * leg's own setting survives these tests. */
class CensusCopyLane : public ::testing::Test {
protected:
    void SetUp() override {
        const char* cur = std::getenv("LEPTRIS_IL_MIN");
        had_min_ = (cur != NULL);
        old_min_ = had_min_ ? std::string(cur) : std::string();
        set_env("LEPTRIS_IL_MIN", "2147483647");
    }
    void TearDown() override {
        if (had_min_)
            set_env("LEPTRIS_IL_MIN", old_min_.c_str());
        else
            unset_env("LEPTRIS_IL_MIN");
    }
    bool had_min_;
    std::string old_min_;
};

}  // namespace

/* ---- Kernel: SIMD core vs scalar oracle (cross-arch law) ---- */

TEST(CensusCopyKernel, MatchesOracleAtEveryLength) {
    std::string pattern = "<a b=\"c\" d='e' --> &amp;\n\t\x80\xc3\xa9\r";
    for (size_t len = 0; len <= 200; len++) {
        std::string s;
        for (size_t i = 0; i < len; i++) s += pattern[i % pattern.size()];
        expect_copy_census(s.data(), s.size());
    }
}

TEST(CensusCopyKernel, SpecialBytesWalkEveryBoundary) {
    /* '&', tab, LF, CR and high bytes walked across the 16/32-byte
     * chunk edges (the NEON core advances 32/iter, the scalar oracle
     * is byte-wise). */
    const char inserts[] = {'&', '\t', '\n', '\r', '\x80',
                            '\xc3', '\x01', '\x1f'};
    for (char ins : inserts) {
        for (size_t pad = 0; pad < 80; pad++) {
            std::string s(pad, 'y');
            s += ins;
            s += "<x a='1' b=\"2\">t</x>";
            expect_copy_census(s.data(), s.size());
        }
    }
}

TEST(CensusCopyKernel, AllClassesDense) {
    std::string amp(5000, '&');
    expect_copy_census(amp.data(), amp.size());
    std::string nl(70000, '\n');   /* > 32B chunk scale */
    expect_copy_census(nl.data(), nl.size());
    std::string hi(5000, '\xff');
    expect_copy_census(hi.data(), hi.size());
    std::string ctrl(5000, '\x01'); /* conservative class only */
    expect_copy_census(ctrl.data(), ctrl.size());
}

TEST(CensusCopyKernel, CleanMachineGeneratedShapeIsAllZero) {
    /* the class the bounded lane exists for: pure-ASCII, no '&', no
     * controls — all three census flags must come back 0 */
    std::string s = make_attr_fixture(4, 16);
    std::vector<char> dst(s.size() + 64, 0xAA);
    int hi = -1, amp = -1, ws = -1;
    leptris_copy_census(dst.data(), s.data(), s.size(), &hi, &amp, &ws);
    EXPECT_EQ(hi, 0);
    EXPECT_EQ(amp, 0);
    EXPECT_EQ(ws, 0);
}

TEST(CensusCopyKernel, RandomizedCrossCheck) {
    const char pool[] = {'<', '>', '"', '\'', 'a', ' ', '/', '=',
                         '&', '\t', '\n', '\r', '\x80', '\xc3',
                         '\x9f', '\x01', '\x1f', 'z'};
    const size_t lens[] = {0, 1, 15, 16, 17, 31, 32, 33, 63, 64, 65,
                           1023, 1024, 1025, 4096, 70001};
    for (size_t li = 0; li < sizeof(lens) / sizeof(lens[0]); li++) {
        uint32_t st = 0x12345u + (uint32_t)li * 7919u;
        size_t len = lens[li];
        std::string s(len, ' ');
        for (size_t i = 0; i < len; i++)
            s[i] = pool[lcg_next(&st) % (sizeof(pool) / sizeof(pool[0]))];
        expect_copy_census(s.data(), s.size());
    }
}

/* ---- Lane: bounded-census parse behavior (public API) ---- */

TEST_F(CensusCopyLane, AttrFixtureParityBothEntries) {
    std::string xml = make_attr_fixture(4, 16);
    const LeptrisParseFlags door_a = static_cast<LeptrisParseFlags>(
        LEPTRIS_PARSE_SKIP_DUP_DETECTION |
        LEPTRIS_PARSE_SKIP_SOURCE_POSITIONS);
    const LeptrisParseFlags entries[] = {LEPTRIS_PARSE_DEFAULT, door_a};
    for (int ei = 0; ei < 2; ei++) {
        LeptrisDocument d =
            (ei == 0) ? leptris_parse_string(xml.data(), xml.size(), NULL)
                      : leptris_parse_string_flags(xml.data(), xml.size(),
                                                   entries[ei], NULL);
        ASSERT_NE(d, nullptr) << "entry " << ei;
        LeptrisElement r = leptris_document_root(d);
        ASSERT_NE(r, nullptr);
        EXPECT_STREQ(leptris_element_name(r), "r");
        int eidx = 0;
        for (LeptrisElement c = leptris_element_first_child_any(r); c;
             c = leptris_element_next_sibling_any(c), eidx++) {
            EXPECT_STREQ(leptris_element_name(c), "e") << "elem " << eidx;
            const char* names[32];
            const char* vals[32];
            size_t na = leptris_element_attributes_raw(c, names, vals, 32);
            ASSERT_EQ(na, 16u) << "elem " << eidx;
            for (size_t k = 0; k < na; k++) {
                std::string want_name = "a" + std::to_string(k);
                std::string want_val =
                    "value-" + std::to_string(k) + "-xyz";
                EXPECT_STREQ(names[k], want_name.c_str());
                EXPECT_STREQ(vals[k], want_val.c_str());
            }
        }
        EXPECT_EQ(eidx, 4);
        leptris_document_free(d);
    }
}

TEST_F(CensusCopyLane, InvalidUtf8SmallDocStillFails) {
    /* The encoding gate moved inside the bounded lane — the verdict
     * must not move: truncated sequence and stray continuation both
     * reject. */
    const char* bad[] = {
        "<r>\xc3</r>",              /* truncated 2-byte */
        "<r a='\x80'>t</r>",        /* stray continuation */
        "<r>\xe0\x9f\xbf</r>",      /* overlong 3-byte */
        "<r>\xf4\x90\x80\x80</r>",  /* > U+10FFFF */
    };
    for (int i = 0; i < 4; i++) {
        std::string s = bad[i];
        LeptrisDocument d =
            leptris_parse_string(s.data(), s.size(), NULL);
        EXPECT_EQ(d, nullptr) << "case " << i;
    }
}

TEST_F(CensusCopyLane, ValidMultibyteUtf8ParsesAndRoundTrips) {
    std::string s = "<r a=\"caf\xc3\xa9\">na\xc3\xa9ve</r>";
    LeptrisDocument d = leptris_parse_string(s.data(), s.size(), NULL);
    ASSERT_NE(d, nullptr);
    LeptrisElement r = leptris_document_root(d);
    ASSERT_NE(r, nullptr);
    EXPECT_STREQ(leptris_element_attribute(r, "a"), "caf\xc3\xa9");
    char* out = leptris_document_serialize(d, NULL);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(std::strstr(out, "caf\xc3\xa9"), nullptr);
    leptris_free_string(out);
    leptris_document_free(d);
}

TEST_F(CensusCopyLane, TagDenseDocNearElementBound) {
    /* `<a/>` is 4 bytes — the len/4 element density the sizing bound
     * must hold for. All elements must carve (no arena exhaustion,
     * no silent loss). */
    std::string xml = "<r>";
    for (int i = 0; i < 5000; i++) xml += "<a/>";
    xml += "</r>";
    LeptrisDocument d = leptris_parse_string(xml.data(), xml.size(), NULL);
    ASSERT_NE(d, nullptr);
    LeptrisElement r = leptris_document_root(d);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_element_child_count(r), 5000u);
    leptris_document_free(d);
}

TEST_F(CensusCopyLane, AttrDenseDocAtQuoteBound) {
    /* `a=''` is 4 bytes per attribute carrying 2 quotes — the quote
     * density the attr-capacity bound keys on. */
    std::string xml = "<r>";
    for (int i = 0; i < 2000; i++) xml += "<b a=''/>";
    xml += "</r>";
    LeptrisDocument d = leptris_parse_string(xml.data(), xml.size(), NULL);
    ASSERT_NE(d, nullptr);
    LeptrisElement r = leptris_document_root(d);
    ASSERT_NE(r, nullptr);
    ASSERT_EQ(leptris_element_child_count(r), 2000u);
    LeptrisElement c = leptris_element_first_child_any(r);
    ASSERT_NE(c, nullptr);
    const char* names[8];
    const char* vals[8];
    const size_t na = leptris_element_attributes_raw(c, names, vals, 8);
    EXPECT_EQ(na, 1u);
    /* never read the out-slots when the accessor returned none */
    if (na >= 1u) {
        EXPECT_STREQ(names[0], "a");
        EXPECT_STREQ(vals[0], "");
    }
    leptris_document_free(d);
}

TEST_F(CensusCopyLane, CommentHeavyLtByteDensity) {
    /* '<' bytes inside comments inflate the count3 '<' census without
     * creating elements — the bounded lane sizes markup from len
     * bounds instead; comment nodes must still all carve. (Comments
     * are invisible to the element-only child walk — count them in
     * the serialized tree.) */
    std::string xml = "<r>";
    for (int i = 0; i < 500; i++) xml += "<!--<<<<<<-->";
    xml += "</r>";
    LeptrisDocument d = leptris_parse_string(xml.data(), xml.size(), NULL);
    ASSERT_NE(d, nullptr);
    char* out = leptris_document_serialize(d, NULL);
    ASSERT_NE(out, nullptr);
    int n = 0;
    for (const char* p = out; (p = std::strstr(p, "<!--")) != NULL; p += 4)
        n++;
    leptris_free_string(out);
    EXPECT_EQ(n, 500);
    leptris_document_free(d);
}

TEST_F(CensusCopyLane, CdataWithLtBytesParses) {
    std::string xml = "<r><![CDATA[<<<< && ''\"]]><t/></r>";
    LeptrisDocument d = leptris_parse_string(xml.data(), xml.size(), NULL);
    ASSERT_NE(d, nullptr);
    char* out = leptris_document_serialize(d, NULL);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(std::strstr(out, "<<<<"), nullptr) << out;
    leptris_free_string(out);
    leptris_document_free(d);
}

TEST_F(CensusCopyLane, CopySourceDocsKeepValueSemantics) {
    /* '&' and literal tab/LF arm the copy-source census: entity text
     * decodes, attribute values normalize tab/LF/CR to one space
     * (XML 1.0 §3.3.3, CRLF as one break). */
    std::string s =
        "<r a='x\ty' b='p\r\nq'>v&amp;w</r>";
    LeptrisDocument d = leptris_parse_string(s.data(), s.size(), NULL);
    ASSERT_NE(d, nullptr);
    LeptrisElement r = leptris_document_root(d);
    ASSERT_NE(r, nullptr);
    EXPECT_STREQ(leptris_element_attribute(r, "a"), "x y");
    EXPECT_STREQ(leptris_element_attribute(r, "b"), "p q");
    EXPECT_STREQ(leptris_element_text(r), "v&w");
    leptris_document_free(d);
}

TEST_F(CensusCopyLane, MultilineDocRoundTrips) {
    /* valws > 0 keeps the pristine carve alive; the document must
     * round-trip through serialize (quotes normalize to doubles,
     * document-tail newline is not tree content). */
    std::string s = "<r>\n  <a x='1'/>\n  <b/>\n</r>\n";
    LeptrisDocument d = leptris_parse_string(s.data(), s.size(), NULL);
    ASSERT_NE(d, nullptr);
    char* out = leptris_document_serialize(d, NULL);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "<r>\n  <a x=\"1\"/>\n  <b/>\n</r>");
    leptris_free_string(out);
    leptris_document_free(d);
}

TEST_F(CensusCopyLane, EntityInAttrValueLazilyExpands) {
    std::string s = "<r a='x&amp;y&amp;z'/>";
    LeptrisDocument d = leptris_parse_string(s.data(), s.size(), NULL);
    ASSERT_NE(d, nullptr);
    LeptrisElement r = leptris_document_root(d);
    ASSERT_NE(r, nullptr);
    EXPECT_STREQ(leptris_element_attribute(r, "a"), "x&y&z");
    leptris_document_free(d);
}

TEST_F(CensusCopyLane, DifferentialSmallVsLargeSerialize) {
    /* Lane selection is by the document's own size, so the same
     * content cannot ride both lanes. Instead: serialize the fast
     * lane's per-element unit, then verify the classic lane's tree
     * over 10000 of the SAME units reproduces exactly that unit
     * output repeated — per-node parity across the lane boundary. */
    std::string small = make_attr_fixture(1, 16);
    std::string large = make_attr_fixture(10000, 16);
    ASSERT_LT(small.size(), 128u * 1024u);
    ASSERT_GT(large.size(), 128u * 1024u);
    LeptrisDocument ds =
        leptris_parse_string(small.data(), small.size(), NULL);
    LeptrisDocument dl =
        leptris_parse_string(large.data(), large.size(), NULL);
    ASSERT_NE(ds, nullptr);
    ASSERT_NE(dl, nullptr);
    char* os = leptris_document_serialize(ds, NULL);
    char* ol = leptris_document_serialize(dl, NULL);
    ASSERT_NE(os, nullptr);
    ASSERT_NE(ol, nullptr);
    /* unit = the small tree's root children serialization */
    const char* head = "<r>";
    ASSERT_EQ(std::strncmp(os, head, 3), 0);
    const char* tail = "</r>";
    size_t os_len = std::strlen(os);
    ASSERT_GT(os_len, 7u);
    std::string unit(os + 3, os_len - 7);   /* strip <r> and </r> */
    std::string want;
    want.reserve(5 + unit.size() * 10000 + 5);
    want += head;
    for (int i = 0; i < 10000; i++) want += unit;
    want += tail;
    size_t ll = std::strlen(ol);
    ASSERT_EQ(ll, want.size());
    size_t diff = ll;
    for (size_t i = 0; i < ll; i++) {
        if (ol[i] != want[i]) { diff = i; break; }
    }
    EXPECT_EQ(diff, ll)
        << "first divergence at byte " << diff
        << " got['" << std::string(ol + diff, 60).c_str()
        << "'] want['" << std::string(want.c_str() + diff, 60).c_str()
        << "']";
    leptris_free_string(os);
    leptris_free_string(ol);
    leptris_document_free(ds);
    leptris_document_free(dl);
}

TEST_F(CensusCopyLane, RecoverFlagNeverNullOnSmallDoc) {
    std::string s = "<r><a><b></r>";
    LeptrisDocument d = leptris_parse_string_flags(
        s.data(), s.size(), LEPTRIS_PARSE_RECOVER, NULL);
    EXPECT_NE(d, nullptr);
    if (d) leptris_document_free(d);
}

TEST_F(CensusCopyLane, DropWsTextFlagParity) {
    /* Boundary whitespace of NON-blank text is data under
     * DROP_WS_TEXT; whitespace-only runs drop. Assert the fast lane
     * matches the classic lane's verdict on the same content. */
    std::string small_s = "<r>\n  <a/>text  <b/>  </r>";
    LeptrisDocument d = leptris_parse_string_flags(
        small_s.data(), small_s.size(), LEPTRIS_PARSE_DROP_WS_TEXT, NULL);
    ASSERT_NE(d, nullptr);
    LeptrisElement r = leptris_document_root(d);
    ASSERT_NE(r, nullptr);
    /* classic-lane reference: the same content padded past the lane
     * boundary (240KB of empty elements inside the root) */
    std::string big = "<r>";
    for (int i = 0; i < 60000; i++) big += "<z/>";
    big += "\n  <a/>text  <b/>  </r>";
    ASSERT_GT(big.size(), 128u * 1024u);
    LeptrisDocument dbig = leptris_parse_string_flags(
        big.data(), big.size(), LEPTRIS_PARSE_DROP_WS_TEXT, NULL);
    ASSERT_NE(dbig, nullptr);
    LeptrisElement rbig = leptris_document_root(dbig);
    ASSERT_NE(rbig, nullptr);
    EXPECT_STREQ(leptris_element_text(r), leptris_element_text(rbig));
    leptris_document_free(d);
    leptris_document_free(dbig);
}

TEST_F(CensusCopyLane, InplaceSmallDocParses) {
    /* leptris_parse_string_inplace keeps its own entry (no copy pass
     * to fuse with) — behavior must be unchanged. */
    std::string xml = make_attr_fixture(4, 16);
    std::vector<char> buf(xml.begin(), xml.end());
    LeptrisDocument d =
        leptris_parse_string_inplace(buf.data(), buf.size(), NULL);
    ASSERT_NE(d, nullptr);
    LeptrisElement r = leptris_document_root(d);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_element_child_count(r), 4u);
    leptris_document_free(d);
}

TEST_F(CensusCopyLane, DeclarationAndNamespacesRoundTrip) {
    std::string s =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<r xmlns:p=\"urn:t\"><p:a p:x='1'/></r>";
    LeptrisDocument d = leptris_parse_string(s.data(), s.size(), NULL);
    ASSERT_NE(d, nullptr);
    char* out = leptris_document_serialize(d, NULL);
    ASSERT_NE(out, nullptr);
    EXPECT_NE(std::strstr(out, "urn:t"), nullptr) << out;
    EXPECT_NE(std::strstr(out, "p:a"), nullptr) << out;
    leptris_free_string(out);
    leptris_document_free(d);
}
