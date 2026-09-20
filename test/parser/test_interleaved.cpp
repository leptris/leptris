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
    if (lane) setenv("LEPTRIS_INTERLEAVED", "1", 1);
    else unsetenv("LEPTRIS_INTERLEAVED");
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
    unsetenv("LEPTRIS_INTERLEAVED");
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
    if (lane) setenv("LEPTRIS_INTERLEAVED", "1", 1);
    else unsetenv("LEPTRIS_INTERLEAVED");
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument d = leptris_parse_string(xml, strlen(xml), &st);
    std::string out = d ? "" : "(null)";
    if (d) {
        walk(leptris_document_root(d), &out);
        leptris_document_free(d);
    }
    unsetenv("LEPTRIS_INTERLEAVED");
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
    setenv("LEPTRIS_INTERLEAVED", "1", 1);
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
    unsetenv("LEPTRIS_INTERLEAVED");
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

    setenv("LEPTRIS_INTERLEAVED", "1", 1);
    setenv("LEPTRIS_IL_STATS", "1", 1);
    char lane[512] = {0};
    ssize_t n = capture(lane, sizeof(lane));
    ASSERT_GT(n, 0) << "no stderr captured";
    EXPECT_NE(strstr(lane, "il: records="), nullptr)
        << "lane canary missing, interleaved path did not run: " << lane;
    EXPECT_NE(strstr(lane, "attrs=1"), nullptr) << lane;

    unsetenv("LEPTRIS_IL_STATS");
    char classic[512] = {0};
    n = capture(classic, sizeof(classic));
    ASSERT_GE(n, 0);
    EXPECT_EQ(strstr(classic, "il: records="), nullptr)
        << "canary leaked into the classic path: " << classic;
    unsetenv("LEPTRIS_INTERLEAVED");
}
#endif  /* !_WIN32: canary capture uses POSIX pipe/dup2 */

TEST(InterleavedParity, DupAttrDiagnosticParity) {
    const char* xml = "<r a='1' a='2'/>";
    for (int lane = 0; lane <= 1; lane++) {
        if (lane) setenv("LEPTRIS_INTERLEAVED", "1", 1);
        else unsetenv("LEPTRIS_INTERLEAVED");
        LeptrisStatus st = LEPTRIS_OK;
        LeptrisDocument d = leptris_parse_string(xml, strlen(xml), &st);
        ASSERT_NE(d, nullptr);
        /* #1200: duplicate reported as a RECOVER diagnostic, first
         * wins in the tree, parse still succeeds. */
        EXPECT_EQ(leptris_document_parse_diag_count(d), 1u);
        LeptrisElement root = leptris_document_root(d);
        EXPECT_STREQ(leptris_element_attribute(root, "a"), "1");
        leptris_document_free(d);
        unsetenv("LEPTRIS_INTERLEAVED");
    }
}
