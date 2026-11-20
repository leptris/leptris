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
