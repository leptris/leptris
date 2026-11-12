// Entity decode: unknown named entities preserved byte-exact in
// lenient mode (libxml2/pugixml semantics). The decode_entity core
// consumed the terminating ';' without emitting it — unknown refs
// came back one character short (&copy 2026). Found via the
// lutaml/moxml#540 investigation (the binding faces route unknown
// entities through this same core).
#include <leptris.h>
#include <leptris/dom/document.h>
#include <gtest/gtest.h>
#include <cstring>

TEST(EntityDecode, UnknownNamedEntityPreservesSemicolon) {
    const char* xml = "<r a=\"&copy; 2026\">x &copy; y</r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc =
        leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    EXPECT_STREQ(leptris_element_attribute(root, "a"), "&copy; 2026");
    LeptrisNodeRef t =
        leptris_node_first_child(leptris_element_as_node(root));
    ASSERT_NE(t, nullptr);
    EXPECT_STREQ(leptris_text_node_get_content(t), "x &copy; y");
    leptris_document_free(doc);
}

TEST(EntityDecode, UnknownEntityMixedWithKnownDecodesBoth) {
    /* &copy; and &nbsp; are HTML-only — in XML they are unknown
     * entities and stay literal (byte-exact, semicolons included);
     * only the five predefined entities decode. */
    const char* xml =
        "<r v=\"&amp; &copy; &nbsp; &lt;\">&amp;&lt;</r>";
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc =
        leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    EXPECT_STREQ(leptris_element_attribute(root, "v"),
                 "& &copy; &nbsp; <");
    EXPECT_STREQ(
        leptris_text_node_get_content(
            leptris_node_first_child(leptris_element_as_node(root))),
        "&<");
    leptris_document_free(doc);
}
