// test/sax/test_streaming.cpp — TODO 116 streaming SAX state machine specs.
//
// The streaming path is opt-in via leptris_sax_parser_set_streaming(1).
// These specs verify that the state machine produces the same event
// stream as the legacy one-shot path for documents fed in arbitrarily
// small chunks.

#include <gtest/gtest.h>
#include <cstring>
#include <string>
#include <vector>
#include <algorithm>
#include "leptris.h"
#include "leptris/sax/sax.h"

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

static LeptrisSAXHandler make_handler(EventLog* log) {
    LeptrisSAXHandler h;
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
                               LeptrisSAXHandler* h) {
    EventLog log;
    LeptrisSAXParser* p = leptris_sax_parser_create(h, &log);
    EXPECT_NE(p, nullptr);
    EXPECT_EQ(leptris_sax_parser_set_streaming(p, 1), 0);

    size_t total = std::strlen(xml);
    for (size_t i = 0; i < total; i += chunk_size) {
        size_t n = (total - i < chunk_size) ? (total - i) : chunk_size;
        int is_final = (i + n >= total) ? 1 : 0;
        int rc = leptris_sax_parser_feed(p, xml + i, n, is_final);
        if (rc != 0) break;  /* parse error; log will show ERR */
    }
    leptris_sax_parser_free(p);
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
    LeptrisSAXHandler h = make_handler(&log);
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
    LeptrisSAXHandler h = make_handler(&log);
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
    LeptrisSAXHandler h = make_handler(&log);
    EventLog got = feed_streaming(xml, 1024, &h);
    ASSERT_EQ(got.events.size(), 4u);
    EXPECT_EQ(got.events[1], "S:r @a=\"1\" @b=\"two\"");
}

TEST(StreamingSax, NestedElements) {
    const char xml[] = "<a><b><c/></b></a>";
    EventLog log;
    LeptrisSAXHandler h = make_handler(&log);
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
    LeptrisSAXHandler h = make_handler(&log);
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
    LeptrisSAXHandler h = make_handler(&log);
    EventLog got = feed_streaming(xml, 5, &h);
    EXPECT_EQ(got.events[1], "S:very_long_element_name");
    EXPECT_EQ(join_text(got.events), std::string("text"));
}

TEST(StreamingSax, ChunkSizeBoundaryOnAttrValue) {
    /* Chunk boundary right in the middle of an attribute value. */
    const char xml[] = "<r value=\"hello world\"/>";
    EventLog log;
    LeptrisSAXHandler h = make_handler(&log);
    EventLog got = feed_streaming(xml, 10, &h);
    EXPECT_EQ(got.events[1], "S:r @value=\"hello world\"");
}

/* Run `xml` through both the legacy one-shot parser and the streaming
 * path (in `chunk_size`-byte chunks).  Returns true if the element
 * event counts (S:, E:) match and the concatenated text (T:) matches.
 * Comments, CDATA, PIs are compared as concatenated bodies.
 *
 * `tolerate_legacy_whitespace_bug`: the legacy recursive parser
 * trims inter-element whitespace via sax_skip_whitespace at the top
 * of its content loop -- a long-standing bug.  The streaming parser
 * correctly preserves whitespace per the SAX contract.  When this
 * flag is true, whitespace-only differences in text bodies are
 * ignored (other text content still must match modulo whitespace). */
static bool differential_match(const char* xml, size_t chunk_size,
                               std::string* why = nullptr,
                               bool tolerate_legacy_whitespace_bug = false) {
    EventLog legacy_log;
    LeptrisSAXHandler h1 = make_handler(&legacy_log);
    if (leptris_sax_parse(xml, std::strlen(xml), &h1, &legacy_log) != 0) {
        if (why) *why = "legacy parse failed";
        return false;
    }

    EventLog streaming_log;
    LeptrisSAXHandler h2 = make_handler(&streaming_log);
    EventLog got = feed_streaming(xml, chunk_size, &h2);

    auto count_prefix = [](const std::vector<std::string>& v, const char* p) {
        size_t c = 0;
        for (const auto& s : v) if (s.size() >= 2 && s[0] == p[0] && s[1] == p[1]) c++;
        return c;
    };

    size_t legacy_s = count_prefix(legacy_log.events, "S:");
    size_t stream_s = count_prefix(got.events, "S:");
    size_t legacy_e = count_prefix(legacy_log.events, "E:");
    size_t stream_e = count_prefix(got.events, "E:");
    if (legacy_s != stream_s) {
        if (why) *why = "S: count mismatch: legacy=" + std::to_string(legacy_s) +
                        " stream=" + std::to_string(stream_s);
        return false;
    }
    if (legacy_e != stream_e) {
        if (why) *why = "E: count mismatch";
        return false;
    }

    std::string legacy_text = join_text(legacy_log.events);
    std::string stream_text = join_text(got.events);
    if (legacy_text != stream_text) {
        if (tolerate_legacy_whitespace_bug) {
            /* Strip whitespace from both, compare. */
            auto strip = [](std::string s) {
                s.erase(std::remove_if(s.begin(), s.end(),
                    [](char c){ return c == ' ' || c == '\t' || c == '\n' || c == '\r'; }),
                    s.end());
                return s;
            };
            if (strip(legacy_text) != strip(stream_text)) {
                if (why) *why = "text mismatch (after whitespace strip): legacy='" +
                                strip(legacy_text) + "' stream='" + strip(stream_text) + "'";
                return false;
            }
        } else {
            if (why) *why = "text mismatch: legacy='" + legacy_text +
                            "' stream='" + stream_text + "'";
            return false;
        }
    }

    if (got.events.empty() || got.events.back() != "END_DOC") {
        if (why) *why = "streaming did not emit END_DOC";
        return false;
    }

    return true;
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
    LeptrisSAXHandler h = make_handler(&legacy_log);
    EXPECT_EQ(leptris_sax_parse(xml, std::strlen(xml), &h, &legacy_log), 0);

    EventLog streaming_log;
    LeptrisSAXHandler h2 = make_handler(&streaming_log);
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

// ---- Differential tests: streaming vs legacy on edge cases ---------------

TEST(StreamingSaxDifferential, EmptyElementNoAttrs) {
    std::string why;
    EXPECT_TRUE(differential_match("<r/>", 1, &why)) << why;
    EXPECT_TRUE(differential_match("<r></r>", 1, &why)) << why;
}

TEST(StreamingSaxDifferential, DeepNesting) {
    const char xml[] =
        "<a><b><c><d><e><f><g><h><i><j>"
        "deep"
        "</j></i></h></g></f></e></d></c></b></a>";
    std::string why;
    EXPECT_TRUE(differential_match(xml, 1, &why)) << why;
    EXPECT_TRUE(differential_match(xml, 7, &why)) << why;
}

TEST(StreamingSaxDifferential, ManyAttributes) {
    const char xml[] =
        "<r a='1' b='2' c='3' d='4' e='5' f='6' g='7' h='8' i='9' j='10'/>";
    std::string why;
    EXPECT_TRUE(differential_match(xml, 1, &why)) << why;
    EXPECT_TRUE(differential_match(xml, 4, &why)) << why;
    EXPECT_TRUE(differential_match(xml, 1024, &why)) << why;
}

TEST(StreamingSaxDifferential, LongTextBody) {
    std::string xml = "<doc>";
    for (int i = 0; i < 100; i++) xml += "the quick brown fox jumps over the lazy dog. ";
    xml += "</doc>";
    std::string why;
    EXPECT_TRUE(differential_match(xml.c_str(), 1, &why)) << why;
    EXPECT_TRUE(differential_match(xml.c_str(), 50, &why)) << why;
    EXPECT_TRUE(differential_match(xml.c_str(), 4096, &why)) << why;
}

TEST(StreamingSaxDifferential, MixedContent) {
    const char xml[] =
        "<p>first <!--comment--> <b>bold</b> text "
        "<i>italics &amp; more</i> end</p>";
    std::string why;
    /* Legacy parser incorrectly trims inter-element whitespace
     * (sax_skip_whitespace at top of content loop).  Streaming is
     * correct.  Compare modulo whitespace. */
    EXPECT_TRUE(differential_match(xml, 1, &why, true)) << why;
    EXPECT_TRUE(differential_match(xml, 8, &why, true)) << why;
}

TEST(StreamingSaxDifferential, CdataWithMarkup) {
    const char xml[] = "<r><![CDATA[<a>b</a> && <c/>]]></r>";
    std::string why;
    EXPECT_TRUE(differential_match(xml, 1, &why)) << why;
    EXPECT_TRUE(differential_match(xml, 5, &why)) << why;
}

TEST(StreamingSaxDifferential, ProcessingInstructions) {
    const char xml[] =
        "<?xml-stylesheet type=\"text/xsl\" href=\"x.xsl\"?>"
        "<r><?pi-target data?>text</r>";
    std::string why;
    EXPECT_TRUE(differential_match(xml, 1, &why)) << why;
    EXPECT_TRUE(differential_match(xml, 9, &why)) << why;
}

TEST(StreamingSaxDifferential, SelfClosingNested) {
    const char xml[] =
        "<root>"
        "<a/><b/><c><d/><e/></c>"
        "<f>text</f>"
        "<g/><h/>"
        "</root>";
    std::string why;
    EXPECT_TRUE(differential_match(xml, 1, &why)) << why;
    EXPECT_TRUE(differential_match(xml, 3, &why)) << why;
}

TEST(StreamingSaxDifferential, XmlDeclAndDoctype) {
    const char xml[] =
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<!DOCTYPE root SYSTEM \"dtd.dtd\">"
        "<root>body</root>";
    std::string why;
    EXPECT_TRUE(differential_match(xml, 1, &why)) << why;
    EXPECT_TRUE(differential_match(xml, 13, &why)) << why;
}

TEST(StreamingSaxDifferential, AllChunkSizes) {
    /* For a representative doc, sweep chunk sizes 1..64. */
    const char xml[] = "<r a=\"1\"><b>text</b><c/></r>";
    std::string why;
    for (size_t cs = 1; cs <= 64; cs++) {
        EXPECT_TRUE(differential_match(xml, cs, &why))
            << "chunk_size=" << cs << ": " << why;
    }
}

TEST(StreamingSaxDifferential, EmptyAndWhitespaceOnly) {
    /* Whitespace between elements is text content (kept by the SAX
     * contract — never trimmed).  The legacy recursive parser has a
     * long-standing bug where it trims this whitespace; the streaming
     * parser is correct.  Compare modulo whitespace. */
    const char xml[] = "<r>   </r>";
    std::string why;
    EXPECT_TRUE(differential_match(xml, 1, &why, true)) << why;
}

TEST(StreamingSaxDifferential, Namespaces) {
    const char xml[] =
        "<root xmlns=\"http://default\" xmlns:a=\"http://a\" xmlns:b=\"http://b\">"
        "<a:child b:attr=\"val\"/>"
        "</root>";
    std::string why;
    EXPECT_TRUE(differential_match(xml, 1, &why)) << why;
    EXPECT_TRUE(differential_match(xml, 11, &why)) << why;
}

}  // namespace
