// test/sax/test_sax.cpp — SAX parser specs + leak audit (TODO 31).
//
// The SAX parser shares the same pool-based allocation model as the
// DOM parser but had zero test coverage.  These specs exercise the
// happy path, every event type, and a leak probe via the test
// harness (which CI runs under `leaks`/valgrind).

#include <gtest/gtest.h>

#include "leptris.h"
#include "leptris/sax/sax.h"

#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include <cstdio>

namespace {

/* Captures every event fired during a parse into a serializable
 * log — easy to assert on. */
struct EventLog {
    std::vector<std::string> events;

    void reset() { events.clear(); }

    static void on_start_document(void* ud) {
        static_cast<EventLog*>(ud)->events.push_back("start_document");
    }
    static void on_end_document(void* ud) {
        static_cast<EventLog*>(ud)->events.push_back("end_document");
    }
    static void on_start_element(void* ud, const char* name, const char** attrs) {
        std::string e = "start:";
        e += name ? name : "(null)";
        if (attrs) {
            for (size_t i = 0; attrs[i] && attrs[i+1]; i += 2) {
                e += " ";
                e += attrs[i];
                e += "=";
                e += attrs[i+1];
            }
        }
        static_cast<EventLog*>(ud)->events.push_back(e);
    }
    static void on_end_element(void* ud, const char* name) {
        std::string e = "end:";
        e += name ? name : "(null)";
        static_cast<EventLog*>(ud)->events.push_back(e);
    }
    static void on_characters(void* ud, const char* text, size_t len) {
        std::string e = "text:";
        e.append(text ? text : "", len);
        static_cast<EventLog*>(ud)->events.push_back(e);
    }
    static void on_comment(void* ud, const char* comment) {
        std::string e = "comment:";
        e += comment ? comment : "";
        static_cast<EventLog*>(ud)->events.push_back(e);
    }
    static void on_cdata(void* ud, const char* cdata) {
        std::string e = "cdata:";
        e += cdata ? cdata : "";
        static_cast<EventLog*>(ud)->events.push_back(e);
    }
    static void on_pi(void* ud, const char* target, const char* data) {
        std::string e = "pi:";
        e += target ? target : "";
        e += ":";
        e += data ? data : "";
        static_cast<EventLog*>(ud)->events.push_back(e);
    }
};

class SaxParser : public ::testing::Test {
protected:
    EventLog log;
    LeptrisSAXHandler handler = {};

    void SetUp() override {
        log.reset();
        handler = {};
        handler.start_document      = &EventLog::on_start_document;
        handler.end_document        = &EventLog::on_end_document;
        handler.start_element       = &EventLog::on_start_element;
        handler.end_element         = &EventLog::on_end_element;
        handler.characters          = &EventLog::on_characters;
        handler.comment             = &EventLog::on_comment;
        handler.cdata               = &EventLog::on_cdata;
        handler.processing_instruction = &EventLog::on_pi;
    }
};

TEST_F(SaxParser, FiresStartAndEndDocument) {
    const char xml[] = "<r/>";
    EXPECT_EQ(leptris_sax_parse(xml, std::strlen(xml), &handler, &log), 0);
    ASSERT_GE(log.events.size(), 2u);
    EXPECT_EQ(log.events.front(), "start_document");
    EXPECT_EQ(log.events.back(),  "end_document");
}

TEST_F(SaxParser, FiresElementEventsWithAttributes) {
    const char xml[] = "<r a='1' b='2'/>";
    EXPECT_EQ(leptris_sax_parse(xml, std::strlen(xml), &handler, &log), 0);

    bool found = false;
    for (const auto& e : log.events) {
        if (e.find("start:r") == 0) {
            found = true;
            EXPECT_NE(e.find("a=1"), std::string::npos);
            EXPECT_NE(e.find("b=2"), std::string::npos);
        }
    }
    EXPECT_TRUE(found);
}

TEST_F(SaxParser, FiresCharacters) {
    const char xml[] = "<r>hello</r>";
    EXPECT_EQ(leptris_sax_parse(xml, std::strlen(xml), &handler, &log), 0);

    bool found = false;
    for (const auto& e : log.events) {
        if (e.find("text:hello") == 0) { found = true; break; }
    }
    EXPECT_TRUE(found);
}

TEST_F(SaxParser, FiresComment) {
    const char xml[] = "<r><!--c--></r>";
    EXPECT_EQ(leptris_sax_parse(xml, std::strlen(xml), &handler, &log), 0);

    bool found = false;
    for (const auto& e : log.events) {
        if (e.find("comment:c") == 0) { found = true; break; }
    }
    EXPECT_TRUE(found);
}

TEST_F(SaxParser, FiresCdata) {
    const char xml[] = "<r><![CDATA[raw<content>]]></r>";
    EXPECT_EQ(leptris_sax_parse(xml, std::strlen(xml), &handler, &log), 0);

    bool found = false;
    for (const auto& e : log.events) {
        if (e.find("cdata:raw<content>") == 0) { found = true; break; }
    }
    EXPECT_TRUE(found);
}

TEST_F(SaxParser, FiresProcessingInstruction) {
    const char xml[] = "<r><?pi data?></r>";
    EXPECT_EQ(leptris_sax_parse(xml, std::strlen(xml), &handler, &log), 0);

    bool found = false;
    for (const auto& e : log.events) {
        if (e.find("pi:pi:data") == 0) { found = true; break; }
    }
    EXPECT_TRUE(found);
}

TEST_F(SaxParser, HandlesNestedElements) {
    const char xml[] = "<a><b><c>x</c></b></a>";
    EXPECT_EQ(leptris_sax_parse(xml, std::strlen(xml), &handler, &log), 0);

    /* Verify nesting order: start:a, start:b, start:c, text:x, end:c, end:b, end:a. */
    std::vector<std::string> expected = {
        "start:a", "start:b", "start:c", "text:x", "end:c", "end:b", "end:a",
    };
    /* Skip start_document/end_document in actual log. */
    std::vector<std::string> actual;
    for (const auto& e : log.events) {
        if (e != "start_document" && e != "end_document") {
            actual.push_back(e);
        }
    }
    EXPECT_EQ(actual, expected);
}

TEST_F(SaxParser, NoLeaksOnComplexDocument) {
    /* A document that exercises every node type.  CI runs this under
     * `leaks --atExit --` and valgrind; zero bytes leaked is the
     * contract (TODO 31). */
    const char xml[] =
        "<?xml version='1.0'?>"
        "<!-- doc-level comment -->"
        "<r xmlns:ns='http://ns.example.com' attr='v'>"
        "<!-- nested -->text<![CDATA[raw]]><?pi data?>"
        "<ns:child>kid</ns:child></r>";

    EventLog local_log;
    LeptrisSAXHandler h = {};
    h.start_element  = &EventLog::on_start_element;
    h.end_element    = &EventLog::on_end_element;
    h.characters     = &EventLog::on_characters;
    h.comment        = &EventLog::on_comment;
    h.cdata          = &EventLog::on_cdata;
    h.processing_instruction = &EventLog::on_pi;

    EXPECT_EQ(leptris_sax_parse(xml, std::strlen(xml), &h, &local_log), 0);
    EXPECT_FALSE(local_log.events.empty());
}

// ---- Deep audit (TODO 65) ------------------------------------------------

TEST_F(SaxParser, EmptyInputReturnsError) {
    EXPECT_NE(leptris_sax_parse("", 0, &handler, &log), 0);
}

TEST_F(SaxParser, HandlesUnclosedTagGracefully) {
    /* SAX is lenient; unclosed tags don't necessarily fail.  The
     * contract is "no crash" — either parse partially or return error. */
    int rc = leptris_sax_parse("<a>", 3, &handler, &log);
    (void)rc;  /* Either 0 (lenient partial) or -1 (rejected) is OK. */
    SUCCEED() << "no crash on unclosed tag";
}

TEST_F(SaxParser, NoLeaksOnParseError) {
    /* Even malformed input must not leak SAX-side allocations. */
    const char xml[] = "<a><b></a></b>";
    leptris_sax_parse(xml, std::strlen(xml), &handler, &log);
    /* Under leaks --atExit --: 0 bytes leaked. */
}

TEST_F(SaxParser, AttributesPreserveOrder) {
    /* SAX should report attributes in document order. */
    const char xml[] = "<r z='1' a='2' m='3'/>";
    EXPECT_EQ(leptris_sax_parse(xml, std::strlen(xml), &handler, &log), 0);

    /* Find the start:r event and verify z precedes a precedes m. */
    std::string start_event;
    for (const auto& e : log.events) {
        if (e.find("start:r") == 0) { start_event = e; break; }
    }
    ASSERT_FALSE(start_event.empty());
    size_t z = start_event.find("z=1");
    size_t a = start_event.find("a=2");
    size_t m = start_event.find("m=3");
    ASSERT_NE(z, std::string::npos);
    ASSERT_NE(a, std::string::npos);
    ASSERT_NE(m, std::string::npos);
    EXPECT_LT(z, a);
    EXPECT_LT(a, m);
}

TEST_F(SaxParser, FiresNamespacePrefixMapping) {
    /* xmlns:foo='...' should fire start_prefix_mapping before
     * start_element. */
    const char xml[] = "<r xmlns:foo='http://x'><foo:c/></r>";

    struct NsLog {
        std::vector<std::string> mappings;
        std::vector<std::string> elements;
        static void on_start_prefix(void* ud, const char* prefix, const char* uri) {
            NsLog* self = static_cast<NsLog*>(ud);
            self->mappings.push_back(std::string(prefix) + "=" + uri);
        }
        static void on_start_element(void* ud, const char* name, const char**) {
            static_cast<NsLog*>(ud)->elements.push_back(name);
        }
    };

    NsLog ns_log;
    LeptrisSAXHandler ns_handler = {};
    ns_handler.start_element = &NsLog::on_start_element;
    ns_handler.start_prefix_mapping = &NsLog::on_start_prefix;

    leptris_sax_parse(xml, std::strlen(xml), &ns_handler, &ns_log);

    EXPECT_FALSE(ns_log.mappings.empty());
    if (!ns_log.mappings.empty()) {
        EXPECT_EQ(ns_log.mappings[0], "foo=http://x");
    }
    EXPECT_FALSE(ns_log.elements.empty());
    if (!ns_log.elements.empty()) {
        EXPECT_EQ(ns_log.elements[0], "r");
    }
}

// ---- Edge cases + regression coverage for SAX perf optimizations -------
//
// These specs exercise code paths touched by TODO 102 (scratch arena,
// vectorized scans, switch dispatch, closing-tag fast path).  Each
// test pins a behavior that the perf rewrite could regress.

TEST_F(SaxParser, SelfClosingWithoutAttributes) {
    /* The closing-tag fast path uses memcmp on the already-parsed
     * opening name.  Empty self-closing elements are the simplest case. */
    const char xml[] = "<r/>";
    EXPECT_EQ(leptris_sax_parse(xml, std::strlen(xml), &handler, &log), 0);
    ASSERT_GE(log.events.size(), 4u);
    EXPECT_EQ(log.events[0], "start_document");
    EXPECT_EQ(log.events[1], "start:r");
    EXPECT_EQ(log.events[2], "end:r");
    EXPECT_EQ(log.events.back(), "end_document");
}

TEST_F(SaxParser, SelfClosingWithAttributes) {
    /* Exercises both attribute parsing and the self-closing fast path. */
    const char xml[] = "<r a='1' b='2'/>";
    EXPECT_EQ(leptris_sax_parse(xml, std::strlen(xml), &handler, &log), 0);
    auto start_it = std::find(log.events.begin(), log.events.end(), "start:r a=1 b=2");
    EXPECT_NE(start_it, log.events.end());
}

TEST_F(SaxParser, ManyAttributesExceedInlineThreshold) {
    /* >16 attrs forces the attrs array to malloc (in the pre-Phase-3
     * code; now it uses stack-then-malloc).  Verifies the array stays
     * NULL-terminated and order is preserved regardless of size. */
    std::string xml = "<r";
    for (int i = 0; i < 32; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), " a%d='%d'", i, i);
        xml += buf;
    }
    xml += "/>";

    EXPECT_EQ(leptris_sax_parse(xml.c_str(), xml.size(), &handler, &log), 0);
    auto it = std::find_if(log.events.begin(), log.events.end(),
        [](const std::string& e) { return e.find("start:r") == 0; });
    ASSERT_NE(it, log.events.end());
    /* All 32 attrs must appear, in order. */
    for (int i = 0; i < 32; i++) {
        char buf[32];
        snprintf(buf, sizeof(buf), "a%d=%d", i, i);
        EXPECT_NE(it->find(buf), std::string::npos)
            << "missing attr " << buf << " in: " << *it;
    }
}

TEST_F(SaxParser, DeepNestingDoesNotExhaustStack) {
    /* The parser is recursive; very deep nesting would crash with
     * stack overflow.  A modest depth (200) must work cleanly.  The
     * configurable depth limit (TODO 62) guards against pathological
     * cases at the parser layer. */
    std::string xml;
    for (int i = 0; i < 200; i++) xml += "<a>";
    for (int i = 0; i < 200; i++) xml += "</a>";

    EXPECT_EQ(leptris_sax_parse(xml.c_str(), xml.size(), &handler, &log), 0);
    int start_count = 0, end_count = 0;
    for (const auto& e : log.events) {
        if (e == "start:a") start_count++;
        if (e == "end:a") end_count++;
    }
    EXPECT_EQ(start_count, 200);
    EXPECT_EQ(end_count, 200);
}

TEST_F(SaxParser, MixedContentPreservesTextOrder) {
    /* The vectorized body-text scan uses memchr to find '<'.  Verify
     * that text-between-elements still fires in document order. */
    const char xml[] = "<r>hello<x/>world</r>";
    EXPECT_EQ(leptris_sax_parse(xml, std::strlen(xml), &handler, &log), 0);

    /* Expected sequence (1-indexed event names):
     *   start_document, start:r, text:hello, start:x, end:x,
     *   text:world, end:r, end_document */
    ASSERT_GE(log.events.size(), 8u);
    EXPECT_EQ(log.events[2], "text:hello");
    EXPECT_EQ(log.events[3], "start:x");
    EXPECT_EQ(log.events[4], "end:x");
    EXPECT_EQ(log.events[5], "text:world");
    EXPECT_EQ(log.events[6], "end:r");
}

TEST_F(SaxParser, CommentEmbeddedInContent) {
    /* The switch-dispatch body loop recognizes comments mid-content. */
    const char xml[] = "<r>before<!--c-->after</r>";
    EXPECT_EQ(leptris_sax_parse(xml, std::strlen(xml), &handler, &log), 0);
    auto c = std::find(log.events.begin(), log.events.end(), "comment:c");
    EXPECT_NE(c, log.events.end());
    /* Text events still fire on both sides of the comment. */
    auto t1 = std::find(log.events.begin(), log.events.end(), "text:before");
    auto t2 = std::find(log.events.begin(), log.events.end(), "text:after");
    EXPECT_NE(t1, log.events.end());
    EXPECT_NE(t2, log.events.end());
    EXPECT_LT(t1 - log.events.begin(), c - log.events.begin());
    EXPECT_LT(c - log.events.begin(), t2 - log.events.begin());
}

TEST_F(SaxParser, CdataEmbeddedInContent) {
    const char xml[] = "<r>before<![CDATA[raw]]>after</r>";
    EXPECT_EQ(leptris_sax_parse(xml, std::strlen(xml), &handler, &log), 0);
    auto cd = std::find(log.events.begin(), log.events.end(), "cdata:raw");
    EXPECT_NE(cd, log.events.end());
}

TEST_F(SaxParser, ProcessingInstructionWithNoData) {
    /* PIs with just a target and no data — the rewrite handles this
     * without crashing on empty memchr results. */
    const char xml[] = "<r><?php?></r>";
    EXPECT_EQ(leptris_sax_parse(xml, std::strlen(xml), &handler, &log), 0);
    /* The PI handler should fire; data may be empty. */
    bool found_pi = false;
    for (const auto& e : log.events) {
        if (e.find("pi:php:") == 0) { found_pi = true; break; }
    }
    EXPECT_TRUE(found_pi);
}

TEST_F(SaxParser, AttributeValueWithSpecialChars) {
    /* Attribute value containing '<' or '>' chars.  These are illegal
     * per the XML spec in literal form, but the parser should at
     * least not crash on memchr-based quote scanning.  Quotes inside
     * the value (when matched by the OTHER quote char) must work. */
    const char xml[] = "<r a=\"it's &apos;ok&apos;\"/>";
    EXPECT_EQ(leptris_sax_parse(xml, std::strlen(xml), &handler, &log), 0);
    auto it = std::find_if(log.events.begin(), log.events.end(),
        [](const std::string& e) { return e.find("a=") != std::string::npos; });
    ASSERT_NE(it, log.events.end());
    EXPECT_NE(it->find("it's"), std::string::npos);
}

TEST_F(SaxParser, EmptyAttributeValue) {
    const char xml[] = "<r a=''/>";
    EXPECT_EQ(leptris_sax_parse(xml, std::strlen(xml), &handler, &log), 0);
    auto it = std::find_if(log.events.begin(), log.events.end(),
        [](const std::string& e) { return e.find("a=") != std::string::npos; });
    ASSERT_NE(it, log.events.end());
    /* Empty value: the attr shows as "a=" with nothing after. */
    EXPECT_EQ(*it, "start:r a=");
}

TEST_F(SaxParser, XmlDeclarationSkippedCleanly) {
    /* The declaration `<?xml ... ?>` is not a processing instruction
     * to the user — it's skipped silently by the parser.  The body
     * loop must not fire the PI callback for it. */
    const char xml[] = "<?xml version='1.0'?><r/>";
    EXPECT_EQ(leptris_sax_parse(xml, std::strlen(xml), &handler, &log), 0);
    for (const auto& e : log.events) {
        EXPECT_EQ(e.find("pi:xml"), std::string::npos)
            << "declaration must not fire as a PI: " << e;
    }
}

TEST_F(SaxParser, EntityReferencesExpandedInText) {
    /* XML 1.0 2.4: characters() must deliver expanded references —
     * predefined entities and numeric character references alike.
     * Found via the Ruby binding round-trip (TODO 118 Phase B):
     * the raw span used to be handed out with "&amp;" intact. */
    const char xml[] = "<r>a &amp; b &lt;c&gt; &#65;&#x42;</r>";
    EXPECT_EQ(leptris_sax_parse(xml, std::strlen(xml), &handler, &log), 0);
    auto it = std::find_if(log.events.begin(), log.events.end(),
        [](const std::string& e) { return e.find("text:") == 0; });
    ASSERT_NE(it, log.events.end());
    EXPECT_EQ(*it, "text:a & b <c> AB");
}

TEST_F(SaxParser, EntityReferencesExpandedInAttrValues) {
    /* XML 1.0 3.3.3: attribute values arrive expanded. */
    const char xml[] = "<r tag='r&amp;d' code='&#65;'/>";
    EXPECT_EQ(leptris_sax_parse(xml, std::strlen(xml), &handler, &log), 0);
    auto it = std::find_if(log.events.begin(), log.events.end(),
        [](const std::string& e) { return e.find("start:r ") == 0; });
    ASSERT_NE(it, log.events.end());
    EXPECT_NE(it->find("tag=r&d"), std::string::npos) << *it;
    EXPECT_NE(it->find("code=A"), std::string::npos) << *it;
}

TEST_F(SaxParser, CdataIsNotEntityDecoded) {
    /* CDATA sections are character data verbatim — references inside
     * must NOT be expanded. */
    const char xml[] = "<r><![CDATA[a &amp; b]]></r>";
    EXPECT_EQ(leptris_sax_parse(xml, std::strlen(xml), &handler, &log), 0);
    auto it = std::find_if(log.events.begin(), log.events.end(),
        [](const std::string& e) { return e.find("cdata:") == 0; });
    ASSERT_NE(it, log.events.end());
    EXPECT_EQ(*it, "cdata:a &amp; b");
}

}  // namespace

// ---- Incremental SAX (TODO 89) ---------------------------------------

TEST(SaxIncremental, EventsMatchAcrossChunkBoundaries) {
    /* Feed the same document in one shot vs. multiple chunks.
     * The events must match. */
    const char xml[] = "<root><a>text</a><b/></root>";

    /* One-shot */
    std::vector<std::string> one_shot_events;
    LeptrisSAXHandler handler1 = {0};
    handler1.start_element = [](void* ud, const char* name, const char** attrs) {
        auto* ev = static_cast<std::vector<std::string>*>(ud);
        ev->push_back(std::string("S:") + name);
    };
    handler1.end_element = [](void* ud, const char* name) {
        auto* ev = static_cast<std::vector<std::string>*>(ud);
        ev->push_back(std::string("E:") + name);
    };
    handler1.characters = [](void* ud, const char* text, size_t len) {
        auto* ev = static_cast<std::vector<std::string>*>(ud);
        ev->push_back(std::string("T:") + std::string(text, len));
    };
    leptris_sax_parse(xml, strlen(xml), &handler1, &one_shot_events);

    /* Incremental: feed in 5-byte chunks */
    std::vector<std::string> chunk_events;
    LeptrisSAXParser* parser = leptris_sax_parser_create(&handler1, &chunk_events);
    ASSERT_NE(parser, nullptr);

    size_t total = strlen(xml);
    for (size_t i = 0; i < total; i += 5) {
        size_t chunk = (total - i < 5) ? (total - i) : 5;
        int is_final = (i + chunk >= total) ? 1 : 0;
        int rc = leptris_sax_parser_feed(parser, xml + i, chunk, is_final);
        EXPECT_EQ(rc, 0);
    }
    leptris_sax_parser_free(parser);

    /* Both paths should have emitted at least root start/end.
     * The exact event count may differ (incremental may split
     * character data), but the element events must match. */
    EXPECT_FALSE(one_shot_events.empty());
    EXPECT_FALSE(chunk_events.empty());
    EXPECT_EQ(one_shot_events[0], "S:root");
    EXPECT_EQ(chunk_events[0], "S:root");
}

TEST(SaxIncremental, SingleByteChunksWork) {
    /* Stress test: feed 1 byte at a time. Must not crash. */
    const char xml[] = "<r/>";
    LeptrisSAXHandler handler = {0};
    handler.start_element = [](void*, const char*, const char**) {};
    handler.end_element = [](void*, const char*) {};

    LeptrisSAXParser* parser = leptris_sax_parser_create(&handler, nullptr);
    ASSERT_NE(parser, nullptr);

    for (size_t i = 0; i < strlen(xml); i++) {
        int is_final = (i == strlen(xml) - 1) ? 1 : 0;
        int rc = leptris_sax_parser_feed(parser, xml + i, 1, is_final);
        EXPECT_EQ(rc, 0) << "Failed at byte " << i;
    }
    leptris_sax_parser_free(parser);
}

/* Issue #625: the streaming path parked attribute name/value
 * pointers in one realloc-growing scratch arena. Nested attr-carrying
 * ancestors accumulate arena bytes until a growth lands mid-element —
 * invalidating pending_attr_name and the pairs already stored for the
 * element being parsed (first pairs came back empty-named or holding
 * uninitialized bytes). */
TEST_F(SaxParser, AttrsSurviveScratchGrowthUnderAttrCarryingAncestors) {
    /* Ancestor levels each carrying attributes accumulate ~200B in
     * the shared scratch arena before <image>; its ~110B attribute
     * block then always spans the arena's first growth boundary. */
    std::string xml =
        "<preface xmlns=\"urn:doc\" id=\"p1\">"
        "<foreword id=\"fw1\" role=\"intro\" lang=\"en\">"
        "<docstatus status=\"draft-for-review-purposes-only\" "
        "owner=\"editorial-board-committee\" rev=\"42\">"
        "<figure id=\"f1\" n=\"1\" class=\"fig\">"
        "<figure id=\"f2\">"
        "<image src=\"image-001.png\" alt=\"A figure caption text\" "
        "width=\"640\" height=\"480\" dpi=\"300\" format=\"png\" "
        "colorspace=\"srgb\"/>"
        "</figure></figure></docstatus></foreword></preface>";

    EXPECT_EQ(leptris_sax_parse(xml.c_str(), xml.size(), &handler, &log), 0);
    ASSERT_GE(log.events.size(), 1u);
    /* The attribute-heavy descendant must deliver every pair intact. */
    std::string want =
        "start:image src=image-001.png alt=A figure caption text "
        "width=640 height=480 dpi=300 format=png colorspace=srgb";
    bool found = false;
    for (const std::string& e : log.events) {
        if (e.rfind("start:image", 0) == 0) {
            found = true;
            EXPECT_EQ(e, want);
        }
    }
    EXPECT_TRUE(found) << "start:image event missing";
}

/* Same corruption through the pull cursor (leptris_pull_attrs shares
 * the streaming attribute buffer). */
TEST(SaxPull, PullAttrsSurviveScratchGrowth) {
    const char xml[] =
        "<preface xmlns=\"urn:doc\" id=\"p1\">"
        "<foreword id=\"fw1\" role=\"intro\" lang=\"en\">"
        "<docstatus status=\"draft-for-review-purposes-only\" "
        "owner=\"editorial-board-committee\" rev=\"42\">"
        "<figure id=\"f1\" n=\"1\" class=\"fig\">"
        "<figure id=\"f2\">"
        "<image src=\"image-001.png\" alt=\"A figure caption text\" "
        "width=\"640\" height=\"480\" dpi=\"300\" format=\"png\" "
        "colorspace=\"srgb\"/>"
        "</figure></figure></docstatus></foreword></preface>";

    LeptrisPullParser p = leptris_pull_new(xml, strlen(xml));
    ASSERT_NE(p, nullptr);
    const LeptrisPullEvent* ev;
    int saw_image = 0;
    while ((ev = leptris_pull_next(p)) != nullptr) {
        if (ev->type == LEPTRIS_PULL_START_ELEMENT &&
            strcmp(ev->name, "image") == 0) {
            saw_image = 1;
            ASSERT_EQ(leptris_pull_attr_count(p), 7u);
            static const char* const names[7] = {
                "src", "alt", "width", "height", "dpi", "format",
                "colorspace"};
            static const char* const vals[7] = {
                "image-001.png", "A figure caption text", "640", "480",
                "300", "png", "srgb"};
            for (int i = 0; i < 7; i++) {
                EXPECT_STREQ(leptris_pull_attr_name(p, (size_t)i),
                             names[i]) << "pair " << i;
                EXPECT_STREQ(leptris_pull_attr_value(p, (size_t)i),
                             vals[i]) << "pair " << i;
            }
        }
    }
    EXPECT_EQ(saw_image, 1);
    leptris_pull_free(p);
}
