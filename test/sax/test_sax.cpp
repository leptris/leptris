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

}  // namespace
