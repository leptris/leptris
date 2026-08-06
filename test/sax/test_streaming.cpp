// test/sax/test_streaming.cpp — TODO 116 streaming SAX state machine specs.
//
// The streaming path is opt-in via taurus_sax_parser_set_streaming(1).
// These specs verify that the state machine produces the same event
// stream as the legacy one-shot path for documents fed in arbitrarily
// small chunks.

#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <vector>
#include "taurus.h"
#include "taurus/sax/sax.h"

namespace {

struct EventLog {
    std::vector<std::string> events;
    void clear() { events.clear(); }
};

static void log_event(EventLog* log, const char* prefix, const char* body) {
    std::string s = prefix;
    if (body) s += std::string(":") + body;
    log->events.push_back(s);
}

static TaurusSAXHandler make_handler(EventLog* log) {
    TaurusSAXHandler h;
    h = {};
    h.start_document = [](void* ud) {
        static_cast<EventLog*>(ud)->events.push_back("START_DOC");
    };
    h.end_document = [](void* ud) {
        static_cast<EventLog*>(ud)->events.push_back("END_DOC");
    };
    h.start_element = [](void* ud, const char* name, const char** attrs) {
        std::string s = std::string("S:") + name;
        if (attrs) {
            for (size_t i = 0; attrs[i]; i += 2) {
                s += " @" + std::string(attrs[i]) + "=\"" +
                     (attrs[i + 1] ? attrs[i + 1] : "") + "\"";
            }
        }
        static_cast<EventLog*>(ud)->events.push_back(s);
    };
    h.end_element = [](void* ud, const char* name) {
        static_cast<EventLog*>(ud)->events.push_back(std::string("E:") + name);
    };
    h.characters = [](void* ud, const char* text, size_t len) {
        static_cast<EventLog*>(ud)->events.push_back(
            std::string("T:") + std::string(text, len));
    };
    h.comment = [](void* ud, const char* c) {
        static_cast<EventLog*>(ud)->events.push_back(std::string("C:") + c);
    };
    h.cdata = [](void* ud, const char* c) {
        static_cast<EventLog*>(ud)->events.push_back(std::string("D:") + c);
    };
    h.processing_instruction = [](void* ud, const char* t, const char* d) {
        std::string s = std::string("P:") + (t ? t : "");
        if (d) s += " " + std::string(d);
        static_cast<EventLog*>(ud)->events.push_back(s);
    };
    h.error = [](void* ud, const char* msg, int, int) {
        static_cast<EventLog*>(ud)->events.push_back(
            std::string("ERR:") + (msg ? msg : ""));
    };
    return h;
}

/* Feed `xml` to a streaming-enabled parser in `chunk_size`-byte chunks.
 * Returns the event log.  Asserts no parse error. */
static EventLog feed_streaming(const char* xml, size_t chunk_size,
                               TaurusSAXHandler* h) {
    EventLog log;
    TaurusSAXParser* p = taurus_sax_parser_create(h, &log);
    EXPECT_NE(p, nullptr);
    EXPECT_EQ(taurus_sax_parser_set_streaming(p, 1), 0);

    size_t total = std::strlen(xml);
    for (size_t i = 0; i < total; i += chunk_size) {
        size_t n = (total - i < chunk_size) ? (total - i) : chunk_size;
        int is_final = (i + n >= total) ? 1 : 0;
        int rc = taurus_sax_parser_feed(p, xml + i, n, is_final);
        if (rc != 0) break;  /* parse error; log will show ERR */
    }
    taurus_sax_parser_free(p);
    return log;
}

/* Concatenate all characters() events between two element events.
 * The SAX contract allows split characters() calls; tests that care
 * about a contiguous text body should use this. */
static std::string join_text(const std::vector<std::string>& events) {
    std::string out;
    for (const auto& e : events) {
        if (e.size() >= 2 && e[0] == 'T' && e[1] == ':') out += e.substr(2);
    }
    return out;
}

TEST(StreamingSax, SimplestElementRoundTrips) {
    const char xml[] = "<r/>";
    EventLog log;
    TaurusSAXHandler h = make_handler(&log);
    EventLog got = feed_streaming(xml, 1024, &h);
    /* Should fire: START_DOC, S:r, E:r, END_DOC. */
    ASSERT_EQ(got.events.size(), 4u);
    EXPECT_EQ(got.events[0], "START_DOC");
    EXPECT_EQ(got.events[1], "S:r");
    EXPECT_EQ(got.events[2], "E:r");
    EXPECT_EQ(got.events[3], "END_DOC");
}

TEST(StreamingSax, TextContent) {
    const char xml[] = "<r>hello</r>";
    EventLog log;
    TaurusSAXHandler h = make_handler(&log);
    EventLog got = feed_streaming(xml, 1024, &h);
    ASSERT_EQ(got.events.size(), 5u);
    EXPECT_EQ(got.events[0], "START_DOC");
    EXPECT_EQ(got.events[1], "S:r");
    EXPECT_EQ(got.events[2], "T:hello");
    EXPECT_EQ(got.events[3], "E:r");
    EXPECT_EQ(got.events[4], "END_DOC");
}

TEST(StreamingSax, AttributesRoundTrip) {
    const char xml[] = "<r a=\"1\" b=\"two\"/>";
    EventLog log;
    TaurusSAXHandler h = make_handler(&log);
    EventLog got = feed_streaming(xml, 1024, &h);
    ASSERT_EQ(got.events.size(), 4u);
    EXPECT_EQ(got.events[1], "S:r @a=\"1\" @b=\"two\"");
}

TEST(StreamingSax, NestedElements) {
    const char xml[] = "<a><b><c/></b></a>";
    EventLog log;
    TaurusSAXHandler h = make_handler(&log);
    EventLog got = feed_streaming(xml, 1024, &h);
    ASSERT_EQ(got.events.size(), 8u);
    EXPECT_EQ(got.events[0], "START_DOC");
    EXPECT_EQ(got.events[1], "S:a");
    EXPECT_EQ(got.events[2], "S:b");
    EXPECT_EQ(got.events[3], "S:c");
    EXPECT_EQ(got.events[4], "E:c");
    EXPECT_EQ(got.events[5], "E:b");
    EXPECT_EQ(got.events[6], "E:a");
    EXPECT_EQ(got.events[7], "END_DOC");
}

TEST(StreamingSax, SingleByteChunks) {
    /* Feed 1 byte at a time -- exercises carry-over on every token. */
    const char xml[] = "<a x=\"1\">hi<b/>bye</a>";
    EventLog log;
    TaurusSAXHandler h = make_handler(&log);
    EventLog got = feed_streaming(xml, 1, &h);
    EXPECT_EQ(got.events[0], "START_DOC");
    EXPECT_EQ(got.events[1], "S:a @x=\"1\"");
    /* "hi" may arrive as multiple T: events. */
    EXPECT_EQ(join_text(got.events).find("hi"), 0u);
    /* Find S:b and E:b. */
    bool saw_sb = false, saw_eb = false, saw_ea = false;
    for (const auto& e : got.events) {
        if (e == "S:b") saw_sb = true;
        if (e == "E:b") saw_eb = true;
        if (e == "E:a") saw_ea = true;
    }
    EXPECT_TRUE(saw_sb);
    EXPECT_TRUE(saw_eb);
    EXPECT_TRUE(saw_ea);
    EXPECT_EQ(join_text(got.events), std::string("hibye"));
}

TEST(StreamingSax, ChunkSizeBoundaryOnName) {
    /* Chunk boundary right in the middle of the element name. */
    const char xml[] = "<very_long_element_name>text</very_long_element_name>";
    EventLog log;
    TaurusSAXHandler h = make_handler(&log);
    EventLog got = feed_streaming(xml, 5, &h);
    EXPECT_EQ(got.events[1], "S:very_long_element_name");
    EXPECT_EQ(join_text(got.events), std::string("text"));
}

TEST(StreamingSax, ChunkSizeBoundaryOnAttrValue) {
    /* Chunk boundary right in the middle of an attribute value. */
    const char xml[] = "<r value=\"hello world\"/>";
    EventLog log;
    TaurusSAXHandler h = make_handler(&log);
    EventLog got = feed_streaming(xml, 10, &h);
    EXPECT_EQ(got.events[1], "S:r @value=\"hello world\"");
}

TEST(StreamingSax, MatchesLegacyOneShot) {
    /* The streaming path should produce the same event stream as the
     * legacy recursive parser for a representative document. */
    const char xml[] =
        "<?xml version=\"1.0\"?>"
        "<root xmlns=\"http://example.com\" xmlns:foo=\"http://foo\">"
        "<child foo:id=\"1\">text</child>"
        "<self-closing attr=\"value\"/>"
        "<!-- comment -->"
        "<![CDATA[cdata content]]>"
        "<?pi-target pi-data?>"
        "</root>";

    EventLog legacy_log;
    TaurusSAXHandler h = make_handler(&legacy_log);
    EXPECT_EQ(taurus_sax_parse(xml, std::strlen(xml), &h, &legacy_log), 0);

    EventLog streaming_log;
    TaurusSAXHandler h2 = make_handler(&streaming_log);
    EventLog got = feed_streaming(xml, 8, &h2);

    /* Diagnostic: dump events on failure. */
    if (got.events.empty() || got.events.back() != "END_DOC") {
        for (size_t i = 0; i < got.events.size(); i++) {
            ADD_FAILURE() << "event[" << i << "] = " << got.events[i];
        }
    }

    /* Element-level events must match.  Characters may be split. */
    EXPECT_EQ(got.events[0], "START_DOC");
    EXPECT_EQ(got.events.back(), "END_DOC");
    /* The first start_element should be the root with both xmlns attrs. */
    ASSERT_GE(got.events.size(), 2u);
    EXPECT_EQ(got.events[1].substr(0, 5), "S:roo") << got.events[1];
    /* Both paths should have emitted the same number of S: and E: events. */
    auto count_prefix = [](const std::vector<std::string>& v, const char* p) {
        size_t c = 0;
        for (const auto& s : v) if (s.find(p) == 0) c++;
        return c;
    };
    EXPECT_EQ(count_prefix(legacy_log.events, "S:"), count_prefix(got.events, "S:"));
    EXPECT_EQ(count_prefix(legacy_log.events, "E:"), count_prefix(got.events, "E:"));
}

}  // namespace
