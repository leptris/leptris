// test/abi/test_dom_mutation_bugs.cpp — Regression tests for #213, #216, #217.
//
// #213: leptris_element_child_count / leptris_node_child_count always
//      returned 0 on parsed docs because direct_parse and flat_promote
//      did not maintain elem->child_count.
// #216: leptris_element_insert_after/_before silently rejected non-element
//      new_node (returned LEPTRIS_ERROR_INVALID_ARG).
// #217: leptris_element_append_child_internal did not unlink a child from
//      its current parent before re-parenting, corrupting both trees.

#include <gtest/gtest.h>

#include "leptris.h"

#include <cstring>
#include <string>

namespace {

LeptrisDocument Parse(const char* xml) {
    LeptrisStatus st = LEPTRIS_OK;
    return leptris_parse_string(xml, std::strlen(xml), &st);
}

// Node type codes from leptris_node_get_type (matches LeptrisNodeTypeEnum).
constexpr int kElem    = 0;
constexpr int kText    = 1;
constexpr int kComment = 2;
constexpr int kCdata   = 3;
constexpr int kPi      = 4;

// Helper: collect child element names in order via the element walk.
static std::string childElementNames(LeptrisElement parent) {
    std::string out;
    LeptrisElement c = leptris_element_first_child_any(parent);
    while (c) {
        if (!out.empty()) out += ",";
        out += leptris_element_name(c);
        c = leptris_element_next_sibling_any(c);
    }
    return out;
}

// Helper: collect all child node types in order via the generic walk.
static std::string childNodeTypes(LeptrisElement parent) {
    std::string out;
    LeptrisNodeRef n = leptris_node_first_child(leptris_element_as_node(parent));
    while (n) {
        if (!out.empty()) out += ",";
        switch (leptris_node_get_type(n)) {
            case kElem:    out += "E"; break;
            case kText:    out += "T"; break;
            case kComment: out += "C"; break;
            case kCdata:   out += "D"; break;
            case kPi:      out += "P"; break;
            default:       out += "?"; break;
        }
        n = leptris_node_next_sibling(n);
    }
    return out;
}

}  // namespace

// =====================================================================
// Issue #213 — child_count returns 0 on parsed docs
// =====================================================================

TEST(ChildCountBug, DirectParsePath) {
    // Plain XML hits the direct_parse fast path.
    LeptrisDocument doc = Parse("<root><a/><b/><c/></root>");
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(leptris_element_child_count(root), 3u)
        << "direct_parse must maintain elem->child_count";
    leptris_document_free(doc);
}

TEST(ChildCountBug, SkipsNonElementChildren) {
    // Comment between elements is skipped (matches man-page contract:
    // child_count counts elements only).
    LeptrisDocument doc = Parse("<root><a/><!--c--><b/></root>");
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    ASSERT_NE(root, nullptr);
    EXPECT_EQ(leptris_element_child_count(root), 2u);
    leptris_document_free(doc);
}

TEST(ChildCountBug, NestedElements) {
    LeptrisDocument doc = Parse(
        "<root>"
          "<a><x/><y/></a>"
          "<b/>"
        "</root>");
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement a = leptris_element_first_child_any(root);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(leptris_element_child_count(a), 2u);
    EXPECT_EQ(leptris_element_child_count(root), 2u);
    leptris_document_free(doc);
}

TEST(ChildCountBug, NodeChildCountMatches) {
    LeptrisDocument doc = Parse("<root><a/><b/></root>");
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    EXPECT_EQ(leptris_node_child_count(leptris_element_as_node(root)), 2u);
    leptris_document_free(doc);
}

// =====================================================================
// Issue #217 — append_child unlinks child from current parent
// =====================================================================

TEST(AppendChildUnlink, MovesChildBetweenParents) {
    LeptrisDocument doc = Parse("<root><from><move/></from><to/></root>");
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement from = leptris_element_first_child_any(root);
    LeptrisElement to = leptris_element_next_sibling_any(from);
    LeptrisElement move = leptris_element_first_child_any(from);

    EXPECT_EQ(leptris_element_append_child(to, move), LEPTRIS_OK);

    // 'from' no longer has 'move' as a child.
    EXPECT_EQ(leptris_element_child_count(from), 0u);
    EXPECT_EQ(childElementNames(from), "");

    // 'to' now has 'move' as its only child.
    EXPECT_EQ(leptris_element_child_count(to), 1u);
    EXPECT_EQ(childElementNames(to), "move");

    // Serializing should produce exactly one <move>.
    char* serialized = leptris_document_serialize(doc, NULL);
    ASSERT_NE(serialized, nullptr);
    std::string s(serialized);
    EXPECT_EQ(s.find("<move"), s.rfind("<move"))
        << "append_child must not leave a duplicate in the old parent";
    free(serialized);
    leptris_document_free(doc);
}

// =====================================================================
// Issue #216 — insert_after/_before support non-element new_node
// =====================================================================

TEST(InsertNonElement, InsertTextBefore) {
    LeptrisDocument doc = Parse("<root><a/></root>");
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement a = leptris_element_first_child_any(root);

    LeptrisNodeRef text = leptris_text_node_create(doc, "hello");
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(leptris_element_insert_before(a, (LeptrisElement)text), LEPTRIS_OK);

    EXPECT_EQ(childNodeTypes(root), "T,E");
    leptris_document_free(doc);
}

TEST(InsertNonElement, InsertCommentAfter) {
    LeptrisDocument doc = Parse("<root><a/></root>");
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement a = leptris_element_first_child_any(root);

    LeptrisNodeRef c = leptris_comment_node_create(doc, "note");
    ASSERT_NE(c, nullptr);
    EXPECT_EQ(leptris_element_insert_after(a, (LeptrisElement)c), LEPTRIS_OK);

    EXPECT_EQ(childNodeTypes(root), "E,C");
    leptris_document_free(doc);
}

TEST(InsertNonElement, InsertCdataAndPi) {
    LeptrisDocument doc = Parse("<root><a/></root>");
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement a = leptris_element_first_child_any(root);

    LeptrisNodeRef cd = leptris_cdata_node_create(doc, "raw");
    ASSERT_NE(cd, nullptr);
    EXPECT_EQ(leptris_element_insert_after(a, (LeptrisElement)cd), LEPTRIS_OK);

    LeptrisNodeRef pi = leptris_pi_node_create(doc, "p", "v");
    ASSERT_NE(pi, nullptr);
    EXPECT_EQ(leptris_element_insert_after(a, (LeptrisElement)pi), LEPTRIS_OK);

    // Order: a, pi, cdata (each insert_after puts new node right after a).
    EXPECT_EQ(childNodeTypes(root), "E,P,D");
    leptris_document_free(doc);
}

TEST(InsertNonElement, InsertTextMiddle) {
    LeptrisDocument doc = Parse("<root><a/><b/></root>");
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement a = leptris_element_first_child_any(root);
    LeptrisElement b = leptris_element_next_sibling_any(a);

    LeptrisNodeRef text = leptris_text_node_create(doc, "mid");
    ASSERT_NE(text, nullptr);
    EXPECT_EQ(leptris_element_insert_before(b, (LeptrisElement)text), LEPTRIS_OK);

    EXPECT_EQ(childNodeTypes(root), "E,T,E");
    leptris_document_free(doc);
}

// ============================================================================
// #1528: a node arriving from a DIFFERENT document was spliced in as a
// raw pointer. The splice target keeps nodes (and their text content)
// from the source document's pool — when the caller frees that source
// (Document.parse scratch docs), every adopted node dangles and the
// next native walk detonates. The repro family: SAX-built tree, moxml
// in-place cleanup (replace/move/insert), then compiled XPath eval.
// The fix adopts cross-document nodes by deep copy (the set_root
// precedent, #371); same-document moves stay zero-copy.
// ============================================================================

namespace {

// Splice scratch_root into live per fn, free the scratch document,
// then exercise the tree the way the cleanup phase does: a compiled
// XPath evaluation followed by serialization.
static void splice_free_eval(
    void (*splice)(LeptrisDocument live, LeptrisElement target,
                   LeptrisElement scratch_root),
    const char* expected_xml) {
    LeptrisDocument live = Parse("<r><a/><b/></r>");
    ASSERT_NE(live, nullptr);
    LeptrisDocument scratch = Parse("<x>replacement</x>");
    ASSERT_NE(scratch, nullptr);

    LeptrisElement target = leptris_document_root(live);
    ASSERT_NE(target, nullptr);
    LeptrisElement scratch_root = leptris_document_root(scratch);
    ASSERT_NE(scratch_root, nullptr);

    splice(live, target, scratch_root);

    // The scratch document dies here — exactly the moxml lifecycle
    // where the parsed fragment's document drops out of scope.
    leptris_document_free(scratch);

    // Detonation site from the issue: compiled XPath over the
    // mutated tree.
    LeptrisXPathCompiled c = leptris_xpath_compile("count(//x)");
    ASSERT_NE(c, nullptr);
    LeptrisXPathResult r = leptris_xpath_compiled_eval(c, live, nullptr);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_xpath_result_number(r), 1.0);
    leptris_xpath_result_free(r);
    leptris_xpath_compiled_free(c);

    LeptrisXPathCompiled s = leptris_xpath_compile("string(//x)");
    ASSERT_NE(s, nullptr);
    LeptrisXPathResult sr = leptris_xpath_compiled_eval(s, live, nullptr);
    ASSERT_NE(sr, nullptr);
    char* text = leptris_xpath_result_string(sr);
    ASSERT_NE(text, nullptr);
    EXPECT_STREQ(text, "replacement");
    leptris_free_string(text);
    leptris_xpath_result_free(sr);
    leptris_xpath_compiled_free(s);

    // Serialization walks the same spine the axes do.
    char* xml = leptris_document_serialize(live, NULL);
    ASSERT_NE(xml, nullptr);
    EXPECT_STREQ(xml, expected_xml);
    leptris_free_string(xml);

    leptris_document_free(live);
}

}  // namespace

TEST(CrossDocumentAdoption, InsertAfterSurvivesScratchFree) {
    splice_free_eval([](LeptrisDocument, LeptrisElement target,
                        LeptrisElement scratch_root) {
        LeptrisElement a = leptris_element_first_child_any(target);
        ASSERT_NE(a, nullptr);
        EXPECT_EQ(leptris_element_insert_after(a, scratch_root),
                  LEPTRIS_OK);
    },
    "<r><a/><x>replacement</x><b/></r>");
}

TEST(CrossDocumentAdoption, InsertBeforeSurvivesScratchFree) {
    splice_free_eval([](LeptrisDocument, LeptrisElement target,
                        LeptrisElement scratch_root) {
        LeptrisElement b = leptris_element_first_child_any(target);
        ASSERT_NE(b, nullptr);
        b = leptris_element_next_sibling_any(b);
        ASSERT_NE(b, nullptr);
        EXPECT_EQ(leptris_element_insert_before(b, scratch_root),
                  LEPTRIS_OK);
    },
    "<r><a/><x>replacement</x><b/></r>");
}

TEST(CrossDocumentAdoption, AppendChildSurvivesScratchFree) {
    splice_free_eval([](LeptrisDocument, LeptrisElement target,
                        LeptrisElement scratch_root) {
        EXPECT_EQ(leptris_element_append_child(target, scratch_root),
                  LEPTRIS_OK);
    },
    "<r><a/><b/><x>replacement</x></r>");
}

TEST(CrossDocumentAdoption, PrependChildSurvivesScratchFree) {
    splice_free_eval([](LeptrisDocument, LeptrisElement target,
                        LeptrisElement scratch_root) {
        EXPECT_EQ(leptris_element_prepend_child(target, scratch_root),
                  LEPTRIS_OK);
    },
    "<r><x>replacement</x><a/><b/></r>");
}

TEST(CrossDocumentAdoption, ReplaceKeepsSameDocumentMoveZeroCopy) {
    // A same-document move must stay the raw splice (identity and
    // pointer preserved) — the adoption path is cross-document only.
    LeptrisDocument doc = Parse("<r><a/><b/><c/></r>");
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement a = leptris_element_first_child_any(root);
    LeptrisElement b = leptris_element_next_sibling_any(a);

    EXPECT_EQ(leptris_element_insert_after(a, b), LEPTRIS_OK);
    char* xml = leptris_document_serialize(doc, NULL);
    ASSERT_NE(xml, nullptr);
    EXPECT_STREQ(xml, "<r><a/><b/><c/></r>");
    leptris_free_string(xml);
    leptris_document_free(doc);
}

// ============================================================================
// #1534 — mutate → free-scratch → serialize → RE-PARSE round trips.
// Two holes behind one symptom ("malformed input" re-parsing generated
// documents after moxml-style cleanup):
//   (1) the #1528 adoption gate covered ELEMENT splices only — a
//       TEXT/CDATA/COMMENT/PI node arriving from a scratch document
//       was still spliced raw, so it dangled the moment the scratch
//       document was freed;
//   (2) leptris_element_remove_child walked and relinked the child
//       chain with the ELEMENT-FILTERED accessors, which skip
//       non-element siblings — removing <a> from [<a>, text] read
//       next(<a>) as NULL and set first_child to NULL, orphaning the
//       leaf tail (the #1220 class; remove_all_children already
//       walked type-safely).
// ============================================================================

namespace {

// First child of ANY node type — the element-form first_child_any is
// element-filtered by contract, so leaves must come from the
// node-form walk.
static LeptrisElement FirstChildNode(LeptrisElement elem) {
    return (LeptrisElement)leptris_node_first_child(
        leptris_element_as_node(elem));
}

// Serialize `doc`, free it, re-parse the serialization, and return the
// fresh document. The re-parse is the #1534 gate: a dangling or
// orphaned leaf surfaces as corrupted bytes or as a parse error.
// `expected_xml` may be null when the serializer spelling is not
// pinned; content assertions belong to the caller then.
static LeptrisDocument SerializeReparse(LeptrisDocument doc,
                                        const char* expected_xml) {
    char* xml = leptris_document_serialize(doc, NULL);
    EXPECT_NE(xml, nullptr);
    LeptrisDocument out = nullptr;
    if (xml) {
        if (expected_xml) EXPECT_STREQ(xml, expected_xml);
        LeptrisStatus st = LEPTRIS_OK;
        out = leptris_parse_string(xml, std::strlen(xml), &st);
        EXPECT_EQ(st, LEPTRIS_OK);
        leptris_free_string(xml);
    }
    leptris_document_free(doc);
    return out;
}

static std::string XPathString(LeptrisDocument doc, const char* expr) {
    std::string out;
    LeptrisXPathCompiled c = leptris_xpath_compile(expr);
    if (!c) return out;
    LeptrisXPathResult r = leptris_xpath_compiled_eval(c, doc, nullptr);
    if (r) {
        char* s = leptris_xpath_result_string(r);
        if (s) {
            out = s;
            leptris_free_string(s);
        }
        leptris_xpath_result_free(r);
    }
    leptris_xpath_compiled_free(c);
    return out;
}

}  // namespace

// The exact #1534 pipeline: a scratch-document TEXT node replaces <a>
// (insert_after + remove_child), the scratch document dies, the result
// must serialize AND re-parse with the text intact.
TEST(CrossDocumentLeafAdoption, TextSpliceRemoveRoundTrips) {
    LeptrisDocument live =
        Parse("<html><body><div><a>old</a></div></body></html>");
    ASSERT_NE(live, nullptr);
    LeptrisDocument scratch = Parse("<z>replaced &lt;tag&gt; text</z>");
    ASSERT_NE(scratch, nullptr);

    LeptrisElement html = leptris_document_root(live);
    ASSERT_NE(html, nullptr);
    LeptrisElement body = leptris_element_first_child_any(html);
    ASSERT_NE(body, nullptr);
    LeptrisElement div = leptris_element_first_child_any(body);
    ASSERT_NE(div, nullptr);
    LeptrisElement a = leptris_element_first_child_any(div);
    ASSERT_NE(a, nullptr);

    LeptrisElement zroot = leptris_document_root(scratch);
    ASSERT_NE(zroot, nullptr);
    LeptrisElement ztext = FirstChildNode(zroot);
    ASSERT_NE(ztext, nullptr);
    ASSERT_EQ(leptris_node_get_type(leptris_element_as_node(ztext)), kText);

    EXPECT_EQ(leptris_element_insert_after(a, ztext), LEPTRIS_OK);
    EXPECT_EQ(leptris_element_remove_child(div, a), LEPTRIS_OK);
    leptris_document_free(scratch);

    LeptrisDocument rt = SerializeReparse(
        live,
        "<html><body><div>replaced &lt;tag&gt; text</div></body></html>");
    ASSERT_NE(rt, nullptr);
    EXPECT_EQ(XPathString(rt, "string(//div)"), "replaced <tag> text");
    leptris_document_free(rt);
}

// Same pipeline for the other leaf kinds. CDATA's serialized spelling
// is not pinned (CDATA or escaped text are both conformant); its
// content must survive the round trip either way.
TEST(CrossDocumentLeafAdoption, OtherLeafKindsRoundTrip) {
    const struct {
        const char* scratch_xml;
        const char* expected_xml;  // null = don't pin spelling
        const char* div_text;      // string(//div) on the re-parse
    } cases[] = {
        {"<z><!--note--></z>",
         "<html><body><div><!--note--></div></body></html>", ""},
        {"<z><![CDATA[cd-data]]></z>", nullptr, "cd-data"},
        {"<z><?tgt data?></z>",
         "<html><body><div><?tgt data?></div></body></html>", ""},
    };
    for (const auto& tc : cases) {
        LeptrisDocument live =
            Parse("<html><body><div><a/></div></body></html>");
        ASSERT_NE(live, nullptr);
        LeptrisDocument scratch = Parse(tc.scratch_xml);
        ASSERT_NE(scratch, nullptr);

        LeptrisElement html = leptris_document_root(live);
        ASSERT_NE(html, nullptr);
        LeptrisElement body = leptris_element_first_child_any(html);
        ASSERT_NE(body, nullptr);
        LeptrisElement div = leptris_element_first_child_any(body);
        ASSERT_NE(div, nullptr);
        LeptrisElement a = leptris_element_first_child_any(div);
        ASSERT_NE(a, nullptr);

        LeptrisElement zroot = leptris_document_root(scratch);
        ASSERT_NE(zroot, nullptr);
        LeptrisElement leaf = FirstChildNode(zroot);
        ASSERT_NE(leaf, nullptr);

        EXPECT_EQ(leptris_element_insert_after(a, leaf), LEPTRIS_OK);
        EXPECT_EQ(leptris_element_remove_child(div, a), LEPTRIS_OK);
        leptris_document_free(scratch);

        LeptrisDocument rt = SerializeReparse(live, tc.expected_xml);
        ASSERT_NE(rt, nullptr);
        EXPECT_EQ(XPathString(rt, "string(//div)"), tc.div_text);
        leptris_document_free(rt);
    }
}

// Hole (1) in isolation: a scratch leaf APPENDED to a live element
// must survive the scratch document being freed.
TEST(CrossDocumentLeafAdoption, TextAppendSurvivesScratchFree) {
    LeptrisDocument live = Parse("<r><a/></r>");
    ASSERT_NE(live, nullptr);
    LeptrisDocument scratch = Parse("<z>replacement</z>");
    ASSERT_NE(scratch, nullptr);

    LeptrisElement root = leptris_document_root(live);
    ASSERT_NE(root, nullptr);
    LeptrisElement zroot = leptris_document_root(scratch);
    ASSERT_NE(zroot, nullptr);
    LeptrisElement ztext = FirstChildNode(zroot);
    ASSERT_NE(ztext, nullptr);

    EXPECT_EQ(leptris_element_append_child(root, ztext), LEPTRIS_OK);
    leptris_document_free(scratch);

    LeptrisDocument rt = SerializeReparse(live, "<r><a/>replacement</r>");
    ASSERT_NE(rt, nullptr);
    EXPECT_EQ(XPathString(rt, "string(/r)"), "replacement");
    leptris_document_free(rt);
}

// Hole (2) in isolation: same-document mixed chains. The removed
// child sits in every position against non-element siblings; the
// leaves around it must all survive.
TEST(RemoveChildMixedChain, EveryPositionKeepsLeafSiblings) {
    const struct {
        const char* doc_xml;
        const char* remove_name;  // element to remove ("#text" = the
                                  // first text child, cast per the
                                  // ABI-stable convention)
        const char* expected_xml;
    } cases[] = {
        {"<r><a/>t<b/></r>", "a", "<r>t<b/></r>"},
        {"<r><a/>t</r>", "a", "<r>t</r>"},
        {"<r>t<a/></r>", "a", "<r>t</r>"},
        {"<r><a/>t<b/></r>", "b", "<r><a/>t</r>"},
        {"<r>t0<a/>t1<b/></r>", "a", "<r>t0t1<b/></r>"},
        {"<r><a/>t</r>", "#text", "<r><a/></r>"},
    };
    for (const auto& tc : cases) {
        SCOPED_TRACE(tc.doc_xml);
        LeptrisDocument doc = Parse(tc.doc_xml);
        ASSERT_NE(doc, nullptr);
        LeptrisElement root = leptris_document_root(doc);
        ASSERT_NE(root, nullptr);

        LeptrisElement victim;
        if (std::strcmp(tc.remove_name, "#text") == 0) {
            // First text child of the root, via the node-form walk.
            victim = FirstChildNode(root);
            while (victim && leptris_node_get_type(
                                 leptris_element_as_node(victim)) != kText) {
                victim = (LeptrisElement)leptris_node_next_sibling(
                    leptris_element_as_node(victim));
            }
            ASSERT_NE(victim, nullptr);
            ASSERT_EQ(
                leptris_node_get_type(leptris_element_as_node(victim)),
                kText);
        } else {
            victim = leptris_element_first_child_any(root);
            while (victim && std::strcmp(leptris_element_name(victim),
                                         tc.remove_name) != 0) {
                victim = leptris_element_next_sibling_any(victim);
            }
            ASSERT_NE(victim, nullptr);
        }

        size_t elem_count_before = leptris_element_child_count(root);
        EXPECT_EQ(leptris_element_remove_child(root, victim), LEPTRIS_OK);
        if (std::strcmp(tc.remove_name, "#text") != 0) {
            // Issue #213 semantics: child_count counts ELEMENT
            // children only — removing a leaf must not decrement it.
            EXPECT_EQ(leptris_element_child_count(root),
                      elem_count_before - 1);
        } else {
            EXPECT_EQ(leptris_element_child_count(root),
                      elem_count_before);
        }

        char* xml = leptris_document_serialize(doc, NULL);
        ASSERT_NE(xml, nullptr);
        EXPECT_STREQ(xml, tc.expected_xml);
        leptris_free_string(xml);
        leptris_document_free(doc);
    }
}

// A child that is not actually a child must still be rejected.
TEST(RemoveChildMixedChain, NonChildStillInvalid) {
    LeptrisDocument doc = Parse("<r><a/></r>");
    ASSERT_NE(doc, nullptr);
    LeptrisElement detached = leptris_element_create(doc, "z");
    ASSERT_NE(detached, nullptr);
    LeptrisElement root = leptris_document_root(doc);
    EXPECT_EQ(leptris_element_remove_child(root, detached),
              LEPTRIS_ERROR_INVALID_ARG);
    leptris_document_free(doc);
}
