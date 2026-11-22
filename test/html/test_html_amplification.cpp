#include <gtest/gtest.h>

#include "leptris.h"
#include "leptris/html.h"

#include <cstring>
#include <cstddef>

namespace {

/* A visited-set walk: the foster splice once built chains whose
 * sibling links looped back into themselves — every walker (count,
 * serialize) circled forever and the "amplification" was really an
 * 8-node cycle walked hundreds of millions of times. */
struct Seen {
    const void* slot[4096];
};
bool seen_add(Seen& s, const void* p) {
    size_t h = (reinterpret_cast<size_t>(p) >> 4) % 4096;
    for (size_t i = 0; i < 4096; i++) {
        size_t j = (h + i) % 4096;
        if (!s.slot[j]) { s.slot[j] = p; return true; }
        if (s.slot[j] == p) return false;
    }
    return true;
}
bool walk_tree(LeptrisNodeRef n, Seen& s, size_t& out) {
    if (!n) return true;
    if (!seen_add(s, n)) return false;
    if (leptris_node_get_type(n) == LEPTRIS_NODE_TYPE_ELEMENT) out++;
    for (LeptrisNodeRef c = leptris_node_first_child(n); c;
         c = leptris_node_next_sibling(c))
        if (!walk_tree(c, s, out)) return false;
    return true;
}

}  // namespace

/* leptris#2096: 682 bytes of malformed MathML-foreign content used to
 * build a 99-million-element tree (hundreds of MB): each NUL-driven
 * character run in the table context reconstructed the dangling
 * active-formatting list, re-opening a full clone chain per run, while
 * the foster path moved the clones out of the table and the next run
 * repeated the cycle. Per WHATWG 13.2.6.4.9 the fostered token is
 * processed under the in-body CHARACTER rules - characters never
 * reconstruct. The tree must stay bounded. */
TEST(HtmlTreeAmplification, ReconstructionNestStaysBounded) {
    static const char hto[] =
        "<html><body><div >ocet<isn class=""\x22""x""\x22""5<math style=""\x22""color:red""\x22"
        " disabled =""\x22""\x22""><mn style=""\x22""color:remn style=""\x22""color:redtml><b"
        "\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00"
        "\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00"
        "\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00"
        "\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00"
        "\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00"
        "\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""\x00""ody><div ><section class=""\x22""x"
        "\x22""><math style=""\x22""color:red""\x22"" disabled =""\x22""\x22""><mn style=""\x22"
        "color:red""\x22"" =""\x22""\x22"" />><mn class=""\x22""x""\x22"" =""\x22""\x22"
        " />><![CDATA[x]]></math><table><math DATA-UPER=dl0 /><mo id=noquotes><![CDATA[x]]></math></table></section><table>a < b</table>dtml><body><div ><section class="
        "\x22""x""\x22""><math style=""\x22""color:red""\x22"" disabled =""\x22""\x22""><mn style="
        "\x22""colorz:red""\x22"" =""\x22""\x22"" />><mn class=""\x22""x""\x22"" =""\x22""\x22"
        " />><![CDATA[x]]></math><table><maid=noquotes><![CDATA[x]u""\x00"
        "</math></table></section><table>a < b</table>1/dv>i</b""\x00""\x00""\x00""3ody>"
        ;
    size_t len = sizeof(hto) - 1;
    ASSERT_EQ(len, 682u) << "the pinned #2096 corpus input";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument d = leptris_parse_html_string(hto, len, &st);
    ASSERT_NE(d, nullptr);
    Seen s;
    memset(&s, 0, sizeof(s));
    size_t n = 0;
    LeptrisNodeRef root = (LeptrisNodeRef)leptris_document_root(d);
    ASSERT_TRUE(walk_tree(root, s, n))
        << "the foster splice must never link a node into a cycle";
    leptris_document_free(d);
    EXPECT_LT(n, 1000u) << "the tree must stay bounded for this input";
}

/* The fuzz-nightly timeout family (2026-11-19..22): an
 * explicit <head> token spliced into the table region made the
 * document-assembly pass ADOPT the head element while its head-run
 * walk continued PAST it — the move loop re-parented the head element
 * under ITSELF (head's child chain looped head->head->body), and the
 * whole tree walk (serialize included) circled forever on a 41-byte
 * input. Per WHATWG the <head> start tag in table context is a parse
 * error to ignore; the assembly must never adopt a run that contains
 * the adopted element itself. */
TEST(HtmlTreeAmplification, ExplicitHeadInTableStaysBounded) {
    static const char trig[] =
        "<meta ct=utf   ><table DAPPER=1 =otel><head></html>";
    size_t len = sizeof(trig) - 1;
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument d = leptris_parse_html_string(trig, len, &st);
    ASSERT_NE(d, nullptr);
    Seen s;
    memset(&s, 0, sizeof(s));
    size_t n = 0;
    LeptrisNodeRef root = (LeptrisNodeRef)leptris_document_root(d);
    ASSERT_TRUE(walk_tree(root, s, n))
        << "the head-run adoption must never link head under itself";
    leptris_document_free(d);
    EXPECT_LT(n, 100u) << "the tree must stay bounded for this input";

    /* The same shape with the table carrying the run past an
     * explicit <head> (the 758-byte nightly mutant's core). */
    static const char trig2[] =
        "<!DOCTYPE html><html><head><meta charset=utf-8><title>t</title>"
        "</heai><body><table DATA-UPPER=1 =dinoquotes style=\"color:red\">"
        "<!-- comment 0 --><li><tr><img s</style></body></hs #DATA-UPPER=1 "
        "disabled>af (a<b) x();</script><head><meta charset=utf-8></html>";
    LeptrisDocument d2 = leptris_parse_html_string(
        trig2, sizeof(trig2) - 1, &st);
    ASSERT_NE(d2, nullptr);
    Seen s2;
    memset(&s2, 0, sizeof(s2));
    size_t n2 = 0;
    ASSERT_TRUE(walk_tree((LeptrisNodeRef)leptris_document_root(d2), s2, n2))
        << "the full nightly shape must stay acyclic";
    leptris_document_free(d2);
}
