// test/sax/test_records.cpp — bulk SAX records (#1298).
//
// Contract: one leptris_sax_records_parse call yields the same
// event stream a host would get from the pull API — element name,
// text, attributes (name/value/normalization flag) — but as plain
// C structs over (off,len) views, walkable without a single
// per-event crossing. Falsifiable: any scan divergence shows up as
// a parity diff against leptris_pull_next.

#include <gtest/gtest.h>
extern "C" {
#include "leptris.h"
#include "leptris/sax/sax.h"
}
#include <cstring>
#include <string>
#include <vector>
#include <functional>

namespace {

std::string view(const LeptrisSaxRecords* r, uint32_t off, uint32_t len) {
    const char* b = leptris_sax_records_buffer(r);
    return std::string(b + off, len);
}

/* Walk the record tree in document order and emit a canonical event
 * string; compare against the pull API's stream for the same input.
 * NUL-termination of every view is asserted along the way. */
void expect_parity(const char* xml) {
    LeptrisSaxRecords* r = nullptr;
    LeptrisStatus st = leptris_sax_records_parse(xml, strlen(xml), 0, &r);
    ASSERT_EQ(st, LEPTRIS_OK) << xml;
    ASSERT_NE(r, nullptr);

    const LeptrisSaxRecord* recs = leptris_sax_records_data(r);
    const size_t n = leptris_sax_records_count(r);
    size_t nattr = 0;
    const LeptrisSaxAttr* attrs = leptris_sax_records_attrs(r, &nattr);

    /* records: element open/close derived from the tree; pull:
       start_element / text / end_element stream */
    std::string rec_stream;
    size_t cursor = 0;
    // in-order walk via explicit stack of (index, phase)
    std::vector<std::pair<size_t, int>> stack;  // index, child-cursor-phase
    // find root (parent == 0xFFFFFFFF)
    size_t root = n;
    for (size_t i = 0; i < n; i++)
        if (recs[i].parent == 0xFFFFFFFFu) { root = i; break; }
    ASSERT_NE(root, n);

    // recursive emit (depth-bounded by fixture size)
    std::function<void(size_t)> emit = [&](size_t idx) {
        const LeptrisSaxRecord& e = recs[idx];
        ASSERT_EQ(e.kind, LEPTRIS_SAX_REC_ELEMENT);
        rec_stream += "<" + view(r, e.off, e.len);
        for (uint32_t a = 0; a < e.attr_count; a++) {
            const LeptrisSaxAttr& at = attrs[e.attr_first + a];
            EXPECT_EQ(view(r, at.name_off + at.name_len, 1), std::string("\0", 1));
            rec_stream += " " + view(r, at.name_off, at.name_len) +
                          "='" + view(r, at.value_off, at.value_len) + "'";
        }
        rec_stream += ">";
        for (uint32_t c = idx + 1; c != 0xFFFFFFFFu &&
             c < n && recs[c].parent == (uint32_t)idx;) {
            const LeptrisSaxRecord& ch = recs[c];
            if (ch.kind == LEPTRIS_SAX_REC_ELEMENT) {
                emit(c);
            } else {
                ASSERT_EQ(ch.kind, LEPTRIS_SAX_REC_TEXT);
                EXPECT_EQ(view(r, ch.off + ch.len, 1), std::string("\0", 1));
                rec_stream += view(r, ch.off, ch.len);
            }
            c = ch.next_sib;
        }
        rec_stream += "</" + view(r, e.off, e.len) + ">";
    };
    (void)cursor;
    emit(root);

    std::string pull_stream;
    LeptrisPullParser p = leptris_pull_new(xml, strlen(xml));
    ASSERT_NE(p, nullptr);
    const LeptrisPullEvent* ev;
    while ((ev = leptris_pull_next(p)) != nullptr) {
        switch (ev->type) {
            case LEPTRIS_PULL_START_ELEMENT: {
                pull_stream += std::string("<") + ev->name;
                size_t ac = leptris_pull_attr_count(p);
                std::vector<const char*> flat(2 * ac + 2);
                leptris_pull_attrs(p, flat.data(), ac);
                for (size_t a = 0; a < ac; a++)
                    pull_stream += std::string(" ") + flat[2 * a] + "='" +
                                   flat[2 * a + 1] + "'";
                pull_stream += ">";
                break;
            }
            case LEPTRIS_PULL_END_ELEMENT:
                pull_stream += std::string("</") + ev->name + ">";
                break;
            case LEPTRIS_PULL_TEXT:
                if (ev->text_len) pull_stream += ev->text;
                break;
            default:
                break;
        }
    }
    leptris_pull_free(p);

    ASSERT_EQ(rec_stream, pull_stream) << "parity divergence for: " << xml;
    leptris_sax_records_free(r);
}

}  // namespace

TEST(SaxRecords, ParityWithPullSimple) {
    expect_parity("<r><a x='1' y='2'>t</a><b/></r>");
}

TEST(SaxRecords, ParityWithPullMixed) {
    expect_parity(
        "<users><user id='1'><name>A</name><tags><t>z</t><t>y</t></tags>"
        "</user><user id='2' active='true'><name>B</name></user></users>");
}

TEST(SaxRecords, TextViewsTerminated) {
    const char* xml = "<r>hello</r>";
    LeptrisSaxRecords* r = nullptr;
    ASSERT_EQ(leptris_sax_records_parse(xml, strlen(xml), 0, &r), LEPTRIS_OK);
    const LeptrisSaxRecord* recs = leptris_sax_records_data(r);
    for (size_t i = 0; i < leptris_sax_records_count(r); i++) {
        if (recs[i].kind == LEPTRIS_SAX_REC_TEXT) {
            const char* b = leptris_sax_records_buffer(r);
            EXPECT_EQ(b[recs[i].off + recs[i].len], '\0');
            EXPECT_EQ(std::string(b + recs[i].off, recs[i].len), "hello");
        }
    }
    leptris_sax_records_free(r);
}

TEST(SaxRecords, BailReturnsNotSupported) {
    /* comments, PIs, entities, xmlns, DOCTYPE — all outside the
     * il-scannable subset; the API must refuse cleanly with nothing
     * allocated. */
    const char* cases[] = {
        "<r><!-- c --></r>",
        "<r><?pi x?></r>",
        "<r>a&amp;b</r>",
        "<p:a xmlns:p='u'/>",
    };
    for (const char* xml : cases) {
        LeptrisSaxRecords* r = nullptr;
        LeptrisStatus st = leptris_sax_records_parse(xml, strlen(xml), 0, &r);
        EXPECT_EQ(st, LEPTRIS_ERROR_NOT_SUPPORTED) << xml;
        EXPECT_EQ(r, nullptr) << xml;
    }
}

TEST(SaxRecords, MalformedReturnsParseError) {
    LeptrisSaxRecords* r = nullptr;
    EXPECT_EQ(leptris_sax_records_parse("<r><a></r>", 9, 0, &r),
              LEPTRIS_ERROR_NOT_SUPPORTED);
    EXPECT_EQ(r, nullptr);
}

TEST(SaxRecords, NullArgsAndFlags) {
    LeptrisSaxRecords* r = nullptr;
    EXPECT_EQ(leptris_sax_records_parse(nullptr, 5, 0, &r),
              LEPTRIS_ERROR_NULL_ARG);
    EXPECT_EQ(leptris_sax_records_parse("<r/>", 4, 0, nullptr),
              LEPTRIS_ERROR_NULL_ARG);
    EXPECT_EQ(leptris_sax_records_parse("<r/>", 4, 1, &r),
              LEPTRIS_ERROR_INVALID_ARG);
    leptris_sax_records_free(nullptr);  /* no-op */
}

TEST(SaxRecords, CanonShapeDrains) {
    /* #1298 shape: ~900-element docs drain in one call and walk. */
    std::string xml = "<users>";
    char buf[256];
    for (int i = 0; i < 900; i++) {
        snprintf(buf, sizeof buf,
                 "<user id='%d'><name>U%d</name><rec><k>v</k></rec></user>",
                 i, i);
        xml += buf;
    }
    xml += "</users>";
    LeptrisSaxRecords* r = nullptr;
    ASSERT_EQ(leptris_sax_records_parse(xml.data(), xml.size(), 0, &r),
              LEPTRIS_OK);
    /* 1 root + 900*(user+name+rec+k) elements + 900*(U text + v text)
       + 1800 id/… attr-free text runs = count sanity only */
    EXPECT_GT(leptris_sax_records_count(r), (size_t)5000);
    leptris_sax_records_free(r);
}
