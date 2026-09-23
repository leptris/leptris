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
static void set_env(const char* k, const char* v) { _putenv_s(k, v); }
static void unset_env(const char* k) { _putenv_s(k, ""); }
#else
static void set_env(const char* k, const char* v) { setenv(k, v, 1); }
static void unset_env(const char* k) { unsetenv(k); }
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
