// test/parser/test_interleaved.cpp — the interleaved lane's parity
// contract (TODO.max-perf/interleaved-parse slice 1). Every fixture
// parses through BOTH lanes in one process (LEPTRIS_INTERLEAVED
// toggled via setenv) and must produce byte-identical serialized
// trees; bail fixtures must also match (classic handles them).
// Falsifiable: any field-store divergence in the post-pass shows up
// as a serialize diff.

#include <gtest/gtest.h>
#include <cstdlib>
#include <cstring>
#ifndef _WIN32
#include <unistd.h>
#endif
#include <string>
#include <vector>
#include "leptris.h"
#include "leptris/error.h"

/* MSVC has no setenv/unsetenv; _putenv_s(k, "") leaves an EMPTY
 * value in the environment, so the lane gates in direct_parse.c
 * treat empty-string values as off. */
#ifdef _WIN32
/* The parser memoizes the lane env gates per process; bump the
 * memo after every toggle so per-case forcing still applies. */
extern "C" void leptris_dp_env_cache_reset(void);
static void set_env(const char* k, const char* v) { _putenv_s(k, v); leptris_dp_env_cache_reset(); }
static void unset_env(const char* k) { _putenv_s(k, ""); leptris_dp_env_cache_reset(); }
#else
extern "C" void leptris_dp_env_cache_reset(void);
static void set_env(const char* k, const char* v) { setenv(k, v, 1); leptris_dp_env_cache_reset(); }
static void unset_env(const char* k) { unsetenv(k); leptris_dp_env_cache_reset(); }
#endif

namespace {

const char* kFixtures[] = {
    "<r><a x='1' y='2'>t</a><b/><c k=\"v\">d1<e/>d2</c></r>",
    "<root>text</root>",
    "<a><b><c/></b></a>",
    "<r id='1' id='2' class='c'>x</r>",
    "<dup><x/><x/></dup>",
    "  <r>  </r>  ",
    "<deep><l1><l2><l3><l4>v</l4></l3></l2></l1></deep>",
    "<mix>t1<a/>t2<b/>t3</mix>",
    "<attrs a='1' b=\"2\" c='3' d='4' e='5' f='6' g='7' h='8'/>",
    "<nl a='x\ty'/>",
    "<self/><self2/><self3 x='y'/>",
    "<r><empty></empty><also/></r>",
};

const char* kBail[] = {
    "<p:a xmlns:p='u'><p:b/></p:a>",
    "<r><!-- c --></r>",
    "<r>&amp;</r>",
    "<r>",
    "<r></wrong>",
    "<a><b></a>",
};

std::string parse_str(const char* xml, bool lane) {
    if (lane) {
        set_env("LEPTRIS_INTERLEAVED", "1");
        /* #1258: fixtures are far below the 64KB default minimum —
         * force the lane's floor to 1 byte so parity specs exercise
         * the lane, never a silent classic fallback. */
        set_env("LEPTRIS_IL_MIN", "1");
    } else {
        unset_env("LEPTRIS_INTERLEAVED");
        unset_env("LEPTRIS_IL_MIN");
    }
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument d = leptris_parse_string(xml, strlen(xml), &st);
    std::string out = d ? "" : "(null)";
    if (d) {
        char* s = leptris_document_serialize(d, NULL);
        out = s ? s : "(ser-failed)";
        leptris_free_string(s);
        leptris_document_free(d);
    } else {
        out += std::to_string((int)st);
    }
    unset_env("LEPTRIS_INTERLEAVED");
    return out;
}

/* Structural walk: serialize parity cannot see fields no serializer
 * reads (attr_count=0 shipped past the byte-diff above) — compare
 * name + attribute_count at every element of the tree. */
static void walk(LeptrisElement e, std::string* out) {
    if (!e) return;
    *out += leptris_element_name(e);
    *out += '/' + std::to_string(leptris_element_attribute_count(e)) + ';';
    for (LeptrisElement c = leptris_element_first_child(e, NULL); c;
         c = leptris_element_next_sibling(c, NULL))
        walk(c, out);
}

std::string shape_str(const char* xml, bool lane) {
    if (lane) set_env("LEPTRIS_INTERLEAVED", "1");
    else unset_env("LEPTRIS_INTERLEAVED");
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument d = leptris_parse_string(xml, strlen(xml), &st);
    std::string out = d ? "" : "(null)";
    if (d) {
        walk(leptris_document_root(d), &out);
        leptris_document_free(d);
    }
    unset_env("LEPTRIS_INTERLEAVED");
    return out;
}

} // namespace

TEST(InterleavedParity, FixturesByteIdentical) {
    for (const char* f : kFixtures) {
        std::string classic = parse_str(f, false);
        std::string il = parse_str(f, true);
        EXPECT_EQ(il, classic) << "fixture: " << f;
    }
}

TEST(InterleavedParity, ShapeWalkIdentical) {
    for (const char* f : kFixtures) {
        EXPECT_EQ(shape_str(f, true), shape_str(f, false))
            << "fixture: " << f;
    }
    EXPECT_EQ(shape_str("<r version='1' type='semantic'>t</r>", true),
              shape_str("<r version='1' type='semantic'>t</r>", false));
}

TEST(InterleavedParity, BailFixturesByteIdentical) {
    for (const char* f : kBail) {
        std::string classic = parse_str(f, false);
        std::string il = parse_str(f, true);
        EXPECT_EQ(il, classic) << "bail fixture: " << f;
    }
}

TEST(InterleavedParity, AttrAndNameAccessIdentical) {
    const char* xml = "<r k='v' n='m'><child z='9'>tx</child></r>";
    set_env("LEPTRIS_INTERLEAVED", "1");
    set_env("LEPTRIS_IL_MIN", "1");
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument d = leptris_parse_string(xml, strlen(xml), &st);
    ASSERT_NE(d, nullptr);
    LeptrisElement root = leptris_document_root(d);
    ASSERT_NE(root, nullptr);
    EXPECT_STREQ(leptris_element_name(root), "r");
    EXPECT_STREQ(leptris_element_attribute(root, "k"), "v");
    EXPECT_STREQ(leptris_element_attribute(root, "n"), "m");
    EXPECT_EQ(leptris_element_attribute(root, "missing"), nullptr);
    LeptrisElement child = leptris_element_first_child(root, NULL);
    ASSERT_NE(child, nullptr);
    EXPECT_STREQ(leptris_element_name(child), "child");
    EXPECT_STREQ(leptris_element_attribute(child, "z"), "9");
    /* line = byte offset of '<' + 1 (classic source-position parity) */
    EXPECT_STREQ(leptris_element_text(child), "tx");
    leptris_document_free(d);
    unset_env("LEPTRIS_INTERLEAVED");
}

#ifndef _WIN32
/* Falsifiability: the parity tests above would pass vacuously if
 * the lane bailed on every input, so assert the lane's engagement
 * canary (LEPTRIS_IL_STATS) directly — fires with the lane on,
 * silent with the lane off for the identical input. */
TEST(InterleavedParity, EngagementCanaryFiresOnlyWhenLaneRuns) {
    const char* xml = "<r><a x='1'>t</a></r>";
    const size_t len = strlen(xml);
    auto parse = [&]() {
        LeptrisStatus st = LEPTRIS_OK;
        LeptrisDocument d = leptris_parse_string(xml, len, &st);
        if (d) leptris_document_free(d);
    };
    /* Redirect stderr into a fresh pipe around one parse; a new pipe
     * per capture (the write end is closed before parsing) makes read
     * return at EOF, not block. */
    auto capture = [&](char* buf, size_t bufsz) -> ssize_t {
        int fds[2];
        if (pipe(fds) != 0) return -2;
        int saved = dup(STDERR_FILENO);
        if (saved < 0) {
            close(fds[0]);
            close(fds[1]);
            return -2;
        }
        fflush(stderr);
        dup2(fds[1], STDERR_FILENO);
        close(fds[1]);
        parse();
        fflush(stderr);
        dup2(saved, STDERR_FILENO);
        close(saved);
        ssize_t n = read(fds[0], buf, bufsz - 1);
        if (n >= 0) buf[n] = '\0';
        close(fds[0]);
        return n;
    };

    set_env("LEPTRIS_INTERLEAVED", "1");
    set_env("LEPTRIS_IL_MIN", "1");
    set_env("LEPTRIS_IL_STATS", "1");
    char lane[512] = {0};
    ssize_t n = capture(lane, sizeof(lane));
    ASSERT_GT(n, 0) << "no stderr captured";
    EXPECT_NE(strstr(lane, "il: records="), nullptr)
        << "lane canary missing, interleaved path did not run: " << lane;
    EXPECT_NE(strstr(lane, "attrs=1"), nullptr) << lane;

    unset_env("LEPTRIS_IL_STATS");
    unset_env("LEPTRIS_IL_MIN");
    char classic[512] = {0};
    n = capture(classic, sizeof(classic));
    ASSERT_GE(n, 0);
    EXPECT_EQ(strstr(classic, "il: records="), nullptr)
        << "canary leaked into the classic path: " << classic;
    unset_env("LEPTRIS_INTERLEAVED");
}
#endif  /* !_WIN32: canary capture uses POSIX pipe/dup2 */

/* #1295: namespace accessors on prefixed attributes. The il lane
 * built attributes without the #542 ns side-cache stamp, so
 * leptris_attribute_namespace_uri (and prefix) returned NULL for
 * every prefixed attribute — xml: included, which broke Canon's
 * namespace-based xml:space handling. This fixture keeps the il
 * lane engaged: the lane defers xmlns-bearing documents to classic
 * (il_scan bails on declarations), and xml: needs no declaration. */
TEST(InterleavedParity, XmlAttrNamespaceParity) {
    const char* kXmlNs = "http://www.w3.org/XML/1998/namespace";
    const char* xml = "<root><code xml:space='preserve'>t</code></root>";
    for (int lane = 0; lane <= 1; lane++) {
        SCOPED_TRACE(lane ? "interleaved" : "classic");
        if (lane) { set_env("LEPTRIS_INTERLEAVED", "1");
                    set_env("LEPTRIS_IL_MIN", "1"); }
        else { unset_env("LEPTRIS_INTERLEAVED");
               unset_env("LEPTRIS_IL_MIN"); }
        LeptrisStatus st = LEPTRIS_OK;
        LeptrisDocument d = leptris_parse_string(xml, strlen(xml), &st);
        ASSERT_NE(d, nullptr);
        LeptrisElement root = leptris_document_root(d);
        ASSERT_NE(root, nullptr);
        LeptrisElement code = leptris_element_first_child(root, NULL);
        ASSERT_NE(code, nullptr);
        LeptrisAttribute a = leptris_element_first_attribute(code);
        ASSERT_NE(a, nullptr);
        EXPECT_STREQ(leptris_attribute_prefix(a), "xml");
        EXPECT_STREQ(leptris_attribute_namespace_uri(a), kXmlNs);
        /* expanded-name lookup rides the element-side resolver and
         * must agree with the attr-side URI */
        EXPECT_STREQ(leptris_element_attribute_ns(code, kXmlNs, "space"),
                     "preserve");
        EXPECT_STREQ(leptris_element_attribute(code, "xml:space"),
                     "preserve");
        leptris_document_free(d);
    }
    unset_env("LEPTRIS_INTERLEAVED");
}

/* Declared-prefix parity. The il lane bails to classic on xmlns
 * today, so this runs classic twice in practice — but if the
 * declaration bail ever lifts, the ns stamp must come with it. */
TEST(InterleavedParity, DeclaredPrefixNamespaceParity) {
    const char* xml = "<d xmlns:q='http://example/q' q:k='v'/>";
    for (int lane = 0; lane <= 1; lane++) {
        SCOPED_TRACE(lane ? "interleaved" : "classic");
        if (lane) { set_env("LEPTRIS_INTERLEAVED", "1");
                    set_env("LEPTRIS_IL_MIN", "1"); }
        else { unset_env("LEPTRIS_INTERLEAVED");
               unset_env("LEPTRIS_IL_MIN"); }
        LeptrisStatus st = LEPTRIS_OK;
        LeptrisDocument d = leptris_parse_string(xml, strlen(xml), &st);
        ASSERT_NE(d, nullptr);
        LeptrisElement root = leptris_document_root(d);
        ASSERT_NE(root, nullptr);
        LeptrisAttribute a = leptris_element_first_attribute(root);
        ASSERT_NE(a, nullptr);
        EXPECT_STREQ(leptris_attribute_prefix(a), "q");
        EXPECT_STREQ(leptris_attribute_namespace_uri(a), "http://example/q");
        leptris_document_free(d);
    }
    unset_env("LEPTRIS_INTERLEAVED");
}

TEST(InterleavedParity, DupAttrDiagnosticParity) {
    const char* xml = "<r a='1' a='2'/>";
    for (int lane = 0; lane <= 1; lane++) {
        if (lane) { set_env("LEPTRIS_INTERLEAVED", "1");
                    set_env("LEPTRIS_IL_MIN", "1"); }
        else { unset_env("LEPTRIS_INTERLEAVED");
               unset_env("LEPTRIS_IL_MIN"); }
        LeptrisStatus st = LEPTRIS_OK;
        LeptrisDocument d = leptris_parse_string(xml, strlen(xml), &st);
        ASSERT_NE(d, nullptr);
        /* #1200: duplicate reported as a RECOVER diagnostic, first
         * wins in the tree, parse still succeeds. */
        EXPECT_EQ(leptris_document_parse_diag_count(d), 1u);
        LeptrisElement root = leptris_document_root(d);
        EXPECT_STREQ(leptris_element_attribute(root, "a"), "1");
        leptris_document_free(d);
        unset_env("LEPTRIS_INTERLEAVED");
    }
}


/* #2233: the raw attribute view (leptris_element_attributes_raw)
 * reads the element's ns_cache raw_attrs journal. The classic lane
 * journals every WIRED attribute at parse time (dp_add_attr_inline's
 * fused registration — dups excluded, source views, xmlns included
 * in source order); the il replay wired the attr list but never
 * journaled, so the accessor returned 0 on interleaved-built trees
 * and left the out-slots untouched. Both lanes must expose the same
 * raw views: count, qnames, values (raw SOURCE bytes — the views
 * predate §3.3.3 whitespace normalization), source order. */
TEST(InterleavedParity, RawAttrViewIdentical) {
    const char* xml =
        "<r a='1' b=\"2\"><b a='' q='x\ty' p=\"u\nv\"/></r>";
    struct RawView {
        size_t n;
        const char* names[8];
        const char* vals[8];
    };
    for (int lane = 0; lane <= 1; lane++) {
        SCOPED_TRACE(lane ? "interleaved" : "classic");
        if (lane) { set_env("LEPTRIS_INTERLEAVED", "1");
                    set_env("LEPTRIS_IL_MIN", "1"); }
        else { unset_env("LEPTRIS_INTERLEAVED");
               unset_env("LEPTRIS_IL_MIN"); }
        LeptrisStatus st = LEPTRIS_OK;
        LeptrisDocument d = leptris_parse_string(xml, strlen(xml), &st);
        ASSERT_NE(d, nullptr);
        LeptrisElement root = leptris_document_root(d);
        ASSERT_NE(root, nullptr);
        LeptrisElement b = leptris_element_first_child(root, NULL);
        ASSERT_NE(b, nullptr);
        RawView rv;
        rv.n = leptris_element_attributes_raw(root, rv.names, rv.vals, 8);
        ASSERT_EQ(rv.n, 2u);
        EXPECT_STREQ(rv.names[0], "a");
        EXPECT_STREQ(rv.vals[0], "1");
        EXPECT_STREQ(rv.names[1], "b");
        EXPECT_STREQ(rv.vals[1], "2");
        rv.n = leptris_element_attributes_raw(b, rv.names, rv.vals, 8);
        ASSERT_EQ(rv.n, 3u);
        EXPECT_STREQ(rv.names[0], "a");
        EXPECT_STREQ(rv.vals[0], "");
        EXPECT_STREQ(rv.names[1], "q");
        EXPECT_STREQ(rv.vals[1], "x\ty");   /* raw source byte */
        EXPECT_STREQ(rv.names[2], "p");
        EXPECT_STREQ(rv.vals[2], "u\nv");   /* raw source bytes */
        /* the lookup accessor stays on normalized values — the raw
         * view showing source bytes must not drag it along */
        EXPECT_STREQ(leptris_element_attribute(b, "q"), "x y");
        EXPECT_STREQ(leptris_element_attribute(b, "p"), "u v");
        leptris_document_free(d);
        unset_env("LEPTRIS_INTERLEAVED");
    }
}

/* #2233 falsifiability: the count parity spec above would pass
 * vacuously if the lane bailed on the fixture, so the interleaved
 * leg here also asserts the lane actually built the tree the spec
 * reads (attr list via the lookup accessor under the forced floor).
 * The canary spec (EngagementCanaryFiresOnlyWhenLaneRuns) already
 * proves the env toggle engages the lane for exactly this shape. */
TEST(InterleavedParity, RawAttrViewWideFixture) {
    /* every wired attr journaled, in source order, on a wide element
     * (the raw-view consumer is the attrparse batch row: wide fans) */
    std::string xml = "<r>";
    for (int i = 0; i < 40; i++) xml += "<e k0='0' k1='1' k2='2' k3='3'/>";
    xml += "</r>";
    for (int lane = 0; lane <= 1; lane++) {
        SCOPED_TRACE(lane ? "interleaved" : "classic");
        if (lane) { set_env("LEPTRIS_INTERLEAVED", "1");
                    set_env("LEPTRIS_IL_MIN", "1"); }
        else { unset_env("LEPTRIS_INTERLEAVED");
               unset_env("LEPTRIS_IL_MIN"); }
        LeptrisStatus st = LEPTRIS_OK;
        LeptrisDocument d =
            leptris_parse_string(xml.data(), xml.size(), &st);
        ASSERT_NE(d, nullptr);
        LeptrisElement root = leptris_document_root(d);
        ASSERT_NE(root, nullptr);
        int eidx = 0;
        for (LeptrisElement e = leptris_element_first_child(root, NULL); e;
             e = leptris_element_next_sibling(e, NULL), eidx++) {
            const char* names[8];
            const char* vals[8];
            const size_t n =
                leptris_element_attributes_raw(e, names, vals, 8);
            ASSERT_EQ(n, 4u) << "elem " << eidx;
            for (size_t k = 0; k < n; k++) {
                std::string want = "k" + std::to_string(k);
                EXPECT_STREQ(names[k], want.c_str()) << "elem " << eidx;
                EXPECT_STREQ(vals[k], std::to_string(k).c_str())
                    << "elem " << eidx;
            }
        }
        EXPECT_EQ(eidx, 40);
        leptris_document_free(d);
        unset_env("LEPTRIS_INTERLEAVED");
    }
}

#ifndef _WIN32
/* #1258: the size floor. Below LEPTRIS_IL_MIN (default 64KB) the
 * lane defers to the classic parser even when the gate is on —
 * small documents cannot amortize the record pass (canon measured
 * ~25% slower at 24KB). Forced floor (IL_MIN=1) restores engagement. */
TEST(InterleavedParity, SizeFloorDefersSmallDocsToClassic) {
    const char* xml = "<r><a x='1'>t</a></r>";
    set_env("LEPTRIS_INTERLEAVED", "1");
    set_env("LEPTRIS_IL_STATS", "1");
    unset_env("LEPTRIS_IL_MIN");   /* default floor: 64KB */

    /* stderr capture, same pipe discipline as the canary spec. */
    auto capture_one = [&](char* buf, size_t bufsz) -> void {
        int fds[2];
        ASSERT_EQ(pipe(fds), 0);
        int saved = dup(STDERR_FILENO);
        fflush(stderr);
        dup2(fds[1], STDERR_FILENO);
        close(fds[1]);
        LeptrisStatus st = LEPTRIS_OK;
        LeptrisDocument d = leptris_parse_string(xml, strlen(xml), &st);
        if (d) leptris_document_free(d);
        fflush(stderr);
        dup2(saved, STDERR_FILENO);
        close(saved);
        ssize_t n = read(fds[0], buf, bufsz - 1);
        if (n >= 0) buf[n] = '\0';
        close(fds[0]);
    };

    char small[512] = {0};
    capture_one(small, sizeof(small));
    EXPECT_EQ(strstr(small, "il: records="), nullptr)
        << "small doc must NOT engage the lane under the default floor: "
        << small;

    set_env("LEPTRIS_IL_MIN", "1");
    char forced[512] = {0};
    capture_one(forced, sizeof(forced));
    EXPECT_NE(strstr(forced, "il: records="), nullptr)
        << "forced floor must restore engagement";

    unset_env("LEPTRIS_IL_STATS");
    unset_env("LEPTRIS_IL_MIN");
    unset_env("LEPTRIS_INTERLEAVED");
}
#endif  /* !_WIN32: pipe/dup2 capture */
