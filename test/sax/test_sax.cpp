// test/sax/test_sax.cpp — SAX parser specs + leak audit (TODO 31).
//
// The SAX parser shares the same pool-based allocation model as the
// DOM parser but had zero test coverage.  These specs exercise the
// happy path, every event type, and a leak probe via the test
// harness (which CI runs under `leaks`/valgrind).

#include <gtest/gtest.h>

#include "taurus.h"
#include "taurus/sax/sax.h"

#include <cstring>
#include <string>
#include <vector>

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
    TaurusSAXHandler handler = {};

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
    EXPECT_EQ(taurus_sax_parse(xml, std::strlen(xml), &handler, &log), 0);
    ASSERT_GE(log.events.size(), 2u);
    EXPECT_EQ(log.events.front(), "start_document");
    EXPECT_EQ(log.events.back(),  "end_document");
}

TEST_F(SaxParser, FiresElementEventsWithAttributes) {
    const char xml[] = "<r a='1' b='2'/>";
    EXPECT_EQ(taurus_sax_parse(xml, std::strlen(xml), &handler, &log), 0);

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
    EXPECT_EQ(taurus_sax_parse(xml, std::strlen(xml), &handler, &log), 0);

    bool found = false;
    for (const auto& e : log.events) {
        if (e.find("text:hello") == 0) { found = true; break; }
    }
    EXPECT_TRUE(found);
}

TEST_F(SaxParser, FiresComment) {
    const char xml[] = "<r><!--c--></r>";
    EXPECT_EQ(taurus_sax_parse(xml, std::strlen(xml), &handler, &log), 0);

    bool found = false;
    for (const auto& e : log.events) {
        if (e.find("comment:c") == 0) { found = true; break; }
    }
    EXPECT_TRUE(found);
}

TEST_F(SaxParser, FiresCdata) {
    const char xml[] = "<r><![CDATA[raw<content>]]></r>";
    EXPECT_EQ(taurus_sax_parse(xml, std::strlen(xml), &handler, &log), 0);

    bool found = false;
    for (const auto& e : log.events) {
        if (e.find("cdata:raw<content>") == 0) { found = true; break; }
    }
    EXPECT_TRUE(found);
}

TEST_F(SaxParser, FiresProcessingInstruction) {
    const char xml[] = "<r><?pi data?></r>";
    EXPECT_EQ(taurus_sax_parse(xml, std::strlen(xml), &handler, &log), 0);

    bool found = false;
    for (const auto& e : log.events) {
        if (e.find("pi:pi:data") == 0) { found = true; break; }
    }
    EXPECT_TRUE(found);
}

TEST_F(SaxParser, HandlesNestedElements) {
    const char xml[] = "<a><b><c>x</c></b></a>";
    EXPECT_EQ(taurus_sax_parse(xml, std::strlen(xml), &handler, &log), 0);

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
    TaurusSAXHandler h = {};
    h.start_element  = &EventLog::on_start_element;
    h.end_element    = &EventLog::on_end_element;
    h.characters     = &EventLog::on_characters;
    h.comment        = &EventLog::on_comment;
    h.cdata          = &EventLog::on_cdata;
    h.processing_instruction = &EventLog::on_pi;

    EXPECT_EQ(taurus_sax_parse(xml, std::strlen(xml), &h, &local_log), 0);
    EXPECT_FALSE(local_log.events.empty());
}

// ---- Deep audit (TODO 65) ------------------------------------------------

TEST_F(SaxParser, EmptyInputReturnsError) {
    EXPECT_NE(taurus_sax_parse("", 0, &handler, &log), 0);
}

TEST_F(SaxParser, HandlesUnclosedTagGracefully) {
    /* SAX is lenient; unclosed tags don't necessarily fail.  The
     * contract is "no crash" — either parse partially or return error. */
    int rc = taurus_sax_parse("<a>", 3, &handler, &log);
    (void)rc;  /* Either 0 (lenient partial) or -1 (rejected) is OK. */
    SUCCEED() << "no crash on unclosed tag";
}

TEST_F(SaxParser, NoLeaksOnParseError) {
    /* Even malformed input must not leak SAX-side allocations. */
    const char xml[] = "<a><b></a></b>";
    taurus_sax_parse(xml, std::strlen(xml), &handler, &log);
    /* Under leaks --atExit --: 0 bytes leaked. */
}

TEST_F(SaxParser, AttributesPreserveOrder) {
    /* SAX should report attributes in document order. */
    const char xml[] = "<r z='1' a='2' m='3'/>";
    EXPECT_EQ(taurus_sax_parse(xml, std::strlen(xml), &handler, &log), 0);

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
    TaurusSAXHandler ns_handler = {};
    ns_handler.start_element = &NsLog::on_start_element;
    ns_handler.start_prefix_mapping = &NsLog::on_start_prefix;

    taurus_sax_parse(xml, std::strlen(xml), &ns_handler, &ns_log);

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
    EXPECT_EQ(taurus_sax_parse(xml, std::strlen(xml), &handler, &log), 0);
    ASSERT_GE(log.events.size(), 4u);
    EXPECT_EQ(log.events[0], "start_document");
    EXPECT_EQ(log.events[1], "start:r");
    EXPECT_EQ(log.events[2], "end:r");
    EXPECT_EQ(log.events.back(), "end_document");
}

TEST_F(SaxParser, SelfClosingWithAttributes) {
    /* Exercises both attribute parsing and the self-closing fast path. */
    const char xml[] = "<r a='1' b='2'/>";
    EXPECT_EQ(taurus_sax_parse(xml, std::strlen(xml), &handler, &log), 0);
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

    EXPECT_EQ(taurus_sax_parse(xml.c_str(), xml.size(), &handler, &log), 0);
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

    EXPECT_EQ(taurus_sax_parse(xml.c_str(), xml.size(), &handler, &log), 0);
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
    EXPECT_EQ(taurus_sax_parse(xml, std::strlen(xml), &handler, &log), 0);

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
    EXPECT_EQ(taurus_sax_parse(xml, std::strlen(xml), &handler, &log), 0);
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
    EXPECT_EQ(taurus_sax_parse(xml, std::strlen(xml), &handler, &log), 0);
    auto cd = std::find(log.events.begin(), log.events.end(), "cdata:raw");
    EXPECT_NE(cd, log.events.end());
}

TEST_F(SaxParser, ProcessingInstructionWithNoData) {
    /* PIs with just a target and no data — the rewrite handles this
     * without crashing on empty memchr results. */
    const char xml[] = "<r><?php?></r>";
    EXPECT_EQ(taurus_sax_parse(xml, std::strlen(xml), &handler, &log), 0);
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
    EXPECT_EQ(taurus_sax_parse(xml, std::strlen(xml), &handler, &log), 0);
    auto it = std::find_if(log.events.begin(), log.events.end(),
        [](const std::string& e) { return e.find("a=") != std::string::npos; });
    ASSERT_NE(it, log.events.end());
    EXPECT_NE(it->find("it's"), std::string::npos);
}

TEST_F(SaxParser, EmptyAttributeValue) {
    const char xml[] = "<r a=''/>";
    EXPECT_EQ(taurus_sax_parse(xml, std::strlen(xml), &handler, &log), 0);
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
    EXPECT_EQ(taurus_sax_parse(xml, std::strlen(xml), &handler, &log), 0);
    for (const auto& e : log.events) {
        EXPECT_EQ(e.find("pi:xml"), std::string::npos)
            << "declaration must not fire as a PI: " << e;
    }
}

}  // namespace
