// test/abi/test_descriptor.cpp — specs for the tree-shaped
// schema-descriptor materialization API (issue #1039).
//
// The descriptor is the ABI between libleptris and hosts like
// lutaml-model: plans compiled once, whole-subtree walks, versioned
// lockstep. These specs pin the contract the bindings mirror.
#include <gtest/gtest.h>
#include <leptris.h>
#include <leptris/descriptor.h>
#include <cstring>
#include <string>

namespace {

LeptrisDocument parse(const char* xml) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument d = leptris_parse_string(xml, std::strlen(xml), &st);
    return d ? d : (LeptrisDocument)0;
}


// ---- #1115: rule-level ns forms + position on all kinds ----------

TEST(Plan1115, RuleLevelNsFormsMatchSiblingsByUri) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(
        "<root xmlns:a='urn:a' xmlns:b='urn:b'>"
        "<a:item>one</a:item><b:item>two</b:item>"
        "<item>bare</item></root>", strlen("<root xmlns:a='urn:a' xmlns:b='urn:b'>"
        "<a:item>one</a:item><b:item>two</b:item>"
        "<item>bare</item></root>"), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    /* Two sibling rows with the SAME wire_name but different
     * rule-level ns forms — mixed qualification under one parent. */
    leptris_child_plan kids[3] = {};
    kids[0].wire_name = "item";
    kids[0].kind = LEPTRIS_PLAN_KIND_SCALAR;
    kids[0].type_tag = 1;
    kids[0].child_plan_index = -1;
    kids[0].ns_form = LEPTRIS_PLAN_NS_EXACT;
    kids[0].ns_uri = "urn:a";
    kids[1].wire_name = "item";
    kids[1].kind = LEPTRIS_PLAN_KIND_SCALAR;
    kids[1].type_tag = 2;
    kids[1].child_plan_index = -1;
    kids[1].ns_form = LEPTRIS_PLAN_NS_ANY;
    kids[2].wire_name = "item";
    kids[2].kind = LEPTRIS_PLAN_KIND_SCALAR;
    kids[2].type_tag = 3;
    kids[2].child_plan_index = -1;   /* ns_form 0: no-namespace only */

    leptris_element_plan plans[1] = {};
    plans[0].element_name = "root";
    plans[0].child_count = 3;
    plans[0].child_plans = kids;
    leptris_plan_spec spec = {};
    spec.abi_version = leptris_plan_abi_version();
    spec.plan_count = 1;
    spec.plans = plans;

    LeptrisPlan plan = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan, nullptr);
    LeptrisPlanResult r = leptris_plan_walk(doc, root, plan, &st);
    ASSERT_NE(r, nullptr);
    /* Row order: EXACT(urn:a) -> one; ANY -> EVERY item (ns-bound
     * or not): one, two, bare; unset form -> the no-namespace
     * item: bare again. */
    ASSERT_EQ(leptris_plan_value_count(r), 5u);

    LeptrisPlanResult a = leptris_plan_value_at(r, 0);
    ASSERT_NE(a, nullptr);
    EXPECT_EQ(leptris_plan_value_type_tag(a), 1);
    EXPECT_STREQ(leptris_plan_value_string(a), "one");

    LeptrisPlanResult any1 = leptris_plan_value_at(r, 1);
    ASSERT_NE(any1, nullptr);
    EXPECT_EQ(leptris_plan_value_type_tag(any1), 2);
    EXPECT_STREQ(leptris_plan_value_string(any1), "one");
    LeptrisPlanResult any2 = leptris_plan_value_at(r, 2);
    ASSERT_NE(any2, nullptr);
    EXPECT_EQ(leptris_plan_value_type_tag(any2), 2);
    EXPECT_STREQ(leptris_plan_value_string(any2), "two");

    LeptrisPlanResult any3 = leptris_plan_value_at(r, 3);
    ASSERT_NE(any3, nullptr);
    EXPECT_EQ(leptris_plan_value_type_tag(any3), 2);
    EXPECT_STREQ(leptris_plan_value_string(any3), "bare");

    LeptrisPlanResult bare = leptris_plan_value_at(r, 4);
    ASSERT_NE(bare, nullptr);
    EXPECT_EQ(leptris_plan_value_type_tag(bare), 3);
    EXPECT_STREQ(leptris_plan_value_string(bare), "bare");
    leptris_plan_result_free(r);
    leptris_plan_free(plan);
    leptris_document_free(doc);
}

// ---- #1486: attribute rows gain the #1115 ns forms ---------------

TEST(Plan1486, AttrRowsBindNamespaceIdentity) {
    LeptrisStatus st = LEPTRIS_OK;
    /* Two prefixes -> two URIs, plus a bare same-local attribute:
     * the (URI, local) identity is what EXACT binds, not the
     * prefix spelling. */
    const char* xml =
        "<w:p xmlns:w14='urn:word2010' xmlns:z='urn:other' "
        "w14:paraId='1A04' z:paraId='OTHER' paraId='bare'/>";
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), &st);
    ASSERT_NE(doc, (LeptrisDocument)nullptr);
    LeptrisElement root = leptris_document_root(doc);

    leptris_attr_plan attrs[3] = {};
    attrs[0].wire_name = "paraId";
    attrs[0].kind = LEPTRIS_PLAN_KIND_SCALAR;
    attrs[0].type_tag = 1;
    attrs[0].ns_form = LEPTRIS_PLAN_NS_EXACT;   /* #1486 */
    attrs[0].ns_uri = "urn:word2010";
    attrs[1].wire_name = "paraId";
    attrs[1].kind = LEPTRIS_PLAN_KIND_SCALAR;
    attrs[1].type_tag = 2;
    /* ns_form 0 = historical wire-name behavior: the bare attr */
    attrs[2].wire_name = "paraId";
    attrs[2].kind = LEPTRIS_PLAN_KIND_SCALAR;
    attrs[2].type_tag = 3;
    attrs[2].ns_form = LEPTRIS_PLAN_NS_ANY;

    leptris_element_plan plans[1] = {};
    plans[0].element_name = "p";
    plans[0].attribute_count = 3;
    plans[0].attribute_plans = attrs;
    leptris_plan_spec spec = {};
    spec.abi_version = leptris_plan_abi_version();
    spec.plan_count = 1;
    spec.plans = plans;

    LeptrisPlan plan = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan, (LeptrisPlan)nullptr);
    LeptrisPlanResult r = leptris_plan_walk(doc, root, plan, &st);
    ASSERT_NE(r, (LeptrisPlanResult)nullptr);
    EXPECT_STREQ(leptris_plan_value_attribute(r, "paraId"), "1A04");
    leptris_plan_result_free(r);
    leptris_plan_free(plan);

    /* Prefix independence: the SAME EXACT row binds when the
     * document spells the URI with a different prefix — the
     * WordprocessingML w14/wordml2010 scenario. */
    const char* xml2 =
        "<w:p xmlns:wm='urn:word2010' wm:paraId='9F77'/>";
    LeptrisDocument doc2 = leptris_parse_string(xml2, strlen(xml2), &st);
    ASSERT_NE(doc2, (LeptrisDocument)nullptr);
    LeptrisElement root2 = leptris_document_root(doc2);
    leptris_attr_plan one[1] = {};
    one[0].wire_name = "paraId";
    one[0].kind = LEPTRIS_PLAN_KIND_SCALAR;
    one[0].type_tag = 1;
    one[0].ns_form = LEPTRIS_PLAN_NS_EXACT;
    one[0].ns_uri = "urn:word2010";
    plans[0].attribute_count = 1;
    plans[0].attribute_plans = one;
    spec.plans = plans;
    LeptrisPlan plan2 = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan2, (LeptrisPlan)nullptr);
    LeptrisPlanResult r2 = leptris_plan_walk(doc2, root2, plan2, &st);
    ASSERT_NE(r2, (LeptrisPlanResult)nullptr);
    EXPECT_STREQ(leptris_plan_value_attribute(r2, "paraId"), "9F77");
    leptris_plan_result_free(r2);
    leptris_plan_free(plan2);
    leptris_document_free(doc2);
    leptris_document_free(doc);
}

// ---- #1490: ns attr rows in NESTED child plans (pin) ---------

TEST(Plan1490, NestedChildPlanAttrNsFormsBindLikeTheRoot) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(
        "<r xmlns:a=\"urn:a\"><i a:id=\"A\" id=\"bare\"/></r>",
        strlen("<r xmlns:a=\"urn:a\"><i a:id=\"A\" id=\"bare\"/></r>"), &st);
    ASSERT_NE(doc, (LeptrisDocument)nullptr);
    LeptrisElement root = leptris_document_root(doc);

    leptris_attr_plan i_attrs[1] = {};
    i_attrs[0].wire_name = "id";
    i_attrs[0].kind = LEPTRIS_PLAN_KIND_SCALAR;
    i_attrs[0].type_tag = 7;
    i_attrs[0].ns_form = LEPTRIS_PLAN_NS_EXACT;
    i_attrs[0].ns_uri = "urn:a";

    leptris_child_plan kids[1] = {};
    kids[0].wire_name = "i";
    kids[0].kind = LEPTRIS_PLAN_KIND_NESTED;
    kids[0].type_tag = 7;
    kids[0].child_plan_index = 1;

    leptris_element_plan plans[2] = {};
    plans[0].element_name = "r";
    plans[0].child_count = 1;
    plans[0].child_plans = kids;
    plans[1].element_name = "i";
    plans[1].attribute_count = 1;
    plans[1].attribute_plans = i_attrs;
    leptris_plan_spec spec = {};
    spec.abi_version = leptris_plan_abi_version();
    spec.plan_count = 2;
    spec.plans = plans;

    /* EXACT match: the child matches and carries a:id="A". */
    LeptrisPlan plan = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan, (LeptrisPlan)nullptr);
    LeptrisPlanResult r = leptris_plan_walk(doc, root, plan, &st);
    ASSERT_NE(r, (LeptrisPlanResult)nullptr);
    ASSERT_EQ(leptris_plan_value_count(r), 1u);
    EXPECT_STREQ(leptris_plan_value_attribute(
        leptris_plan_value_at(r, 0), "id"), "A");
    leptris_plan_result_free(r);
    leptris_plan_free(plan);

    /* Non-matching URI: the child STILL matches — the attr is
     * simply absent, the element is not dropped. */
    i_attrs[0].ns_uri = "urn:other";
    plan = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan, (LeptrisPlan)nullptr);
    r = leptris_plan_walk(doc, root, plan, &st);
    ASSERT_NE(r, (LeptrisPlanResult)nullptr);
    ASSERT_EQ(leptris_plan_value_count(r), 1u);
    EXPECT_EQ(leptris_plan_value_attribute(
        leptris_plan_value_at(r, 0), "id"), nullptr);
    leptris_plan_result_free(r);
    leptris_plan_free(plan);

    /* Zero form: historical bare-attr lookup. */
    i_attrs[0].ns_form = 0;
    i_attrs[0].ns_uri = NULL;
    plan = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan, (LeptrisPlan)nullptr);
    r = leptris_plan_walk(doc, root, plan, &st);
    ASSERT_NE(r, (LeptrisPlanResult)nullptr);
    ASSERT_EQ(leptris_plan_value_count(r), 1u);
    EXPECT_STREQ(leptris_plan_value_attribute(
        leptris_plan_value_at(r, 0), "id"), "bare");
    leptris_plan_result_free(r);
    leptris_plan_free(plan);
    leptris_document_free(doc);
}

TEST(Plan1490, StructSizeAccessorsLetBindingsDetectSkew) {
    EXPECT_EQ(leptris_plan_attr_row_size(), sizeof(leptris_attr_plan));
    EXPECT_EQ(leptris_plan_child_row_size(), sizeof(leptris_child_plan));
    EXPECT_EQ(leptris_plan_element_row_size(), sizeof(leptris_element_plan));
    EXPECT_EQ(leptris_plan_spec_struct_size(), sizeof(leptris_plan_spec));
    EXPECT_EQ(leptris_plan_predicate_row_size(),
              sizeof(leptris_attr_predicate));
    /* the #1486 layout: 40 on LP64 — pointers halve on ILP32, where
     * the width-independent accessor-vs-sizeof checks above carry
     * the skew-detection contract. */
    if (sizeof(void*) == 8)
        EXPECT_EQ(leptris_plan_attr_row_size(), 48u); /* #1551 ns_prefix */
}

TEST(Plan1115, EveryValueKindCarriesPosition) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(
        "<root><first>x</first><items>i1</items><items>i2</items>"
        "<last>y</last></root>", strlen("<root><first>x</first><items>i1</items><items>i2</items>"
        "<last>y</last></root>"), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    leptris_child_plan kids[3] = {};
    kids[0] = {"first", LEPTRIS_PLAN_KIND_SCALAR, 1, -1};
    kids[1] = {"items", LEPTRIS_PLAN_KIND_COLLECTION, 2, -1};
    kids[2] = {"last", LEPTRIS_PLAN_KIND_SCALAR, 3, -1};
    leptris_element_plan plans[1] = {};
    plans[0].element_name = "root";
    plans[0].child_count = 3;
    plans[0].child_plans = kids;
    leptris_plan_spec spec = {};
    spec.abi_version = leptris_plan_abi_version();
    spec.plan_count = 1;
    spec.plans = plans;

    LeptrisPlan plan = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan, nullptr);
    LeptrisPlanResult r = leptris_plan_walk(doc, root, plan, &st);
    ASSERT_NE(r, nullptr);
    ASSERT_EQ(leptris_plan_value_count(r), 3u);

    size_t p_first = leptris_plan_value_position(leptris_plan_value_at(r, 0));
    size_t p_coll = leptris_plan_value_position(leptris_plan_value_at(r, 1));
    size_t p_last = leptris_plan_value_position(leptris_plan_value_at(r, 2));
    /* Parse-created nodes carry byteOffset+1 (#223); the values
     * echo it — nonzero and in document order across rows. */
    EXPECT_GT(p_first, 0u);
    EXPECT_GT(p_coll, p_first);
    EXPECT_GT(p_last, p_coll);
    /* Collection ITEMS carry their own offsets, ordered. */
    LeptrisPlanResult coll = leptris_plan_value_at(r, 1);
    size_t p_i1 = leptris_plan_value_position(leptris_plan_value_at(coll, 0));
    size_t p_i2 = leptris_plan_value_position(leptris_plan_value_at(coll, 1));
    EXPECT_GT(p_i1, 0u);
    EXPECT_GT(p_i2, p_i1);
    leptris_plan_result_free(r);
    leptris_plan_free(plan);
    leptris_document_free(doc);
}

}  // namespace

TEST(PlanAbi, VersionIsOneAndSpecVersionIsChecked) {
    EXPECT_EQ(leptris_plan_abi_version(), 1u);

    leptris_element_plan plans[1] = {};
    plans[0].element_name = "doc";
    leptris_plan_spec spec = {};
    spec.abi_version = 99;  /* wrong */
    spec.plan_count = 1;
    spec.plans = plans;
    LeptrisStatus st = LEPTRIS_OK;
    EXPECT_EQ(leptris_plan_build(&spec, &st), nullptr);
    EXPECT_NE(st, LEPTRIS_OK);
}

TEST(PlanWalk, NestedScalarsAttributesCollections) {
    LeptrisDocument doc = parse(
        "<doc lang=\"en\"><title>Hello</title>"
        "<item>a</item><item>b</item>"
        "<meta><k>v</k></meta>"
        "<skip/></doc>");
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    leptris_attr_plan doc_attrs[1] = {{"lang", LEPTRIS_PLAN_KIND_SCALAR, 7}};
    leptris_child_plan meta_kids[1] = {{"k", LEPTRIS_PLAN_KIND_SCALAR, 0, -1}};
    leptris_child_plan doc_kids[4] = {
        {"title", LEPTRIS_PLAN_KIND_SCALAR, 1, -1},
        {"item", LEPTRIS_PLAN_KIND_COLLECTION, 2, -1},
        {"meta", LEPTRIS_PLAN_KIND_NESTED, 3, 1},
        {"absent", LEPTRIS_PLAN_KIND_SCALAR, 4, -1},
    };
    leptris_element_plan plans[2] = {};
    plans[0].element_name = "doc";
    plans[0].attribute_count = 1;
    plans[0].attribute_plans = doc_attrs;
    plans[0].child_count = 4;
    plans[0].child_plans = doc_kids;
    plans[1].element_name = "meta";
    plans[1].child_count = 1;
    plans[1].child_plans = meta_kids;

    leptris_plan_spec spec = {};
    spec.abi_version = leptris_plan_abi_version();
    spec.plan_count = 2;
    spec.plans = plans;

    LeptrisStatus st = LEPTRIS_OK;
    LeptrisPlan plan = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan, nullptr);
    EXPECT_EQ(st, LEPTRIS_OK);

    LeptrisPlanResult r = leptris_plan_walk(doc, root, plan, &st);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(st, LEPTRIS_OK);
    EXPECT_EQ(leptris_plan_value_kind(r), LEPTRIS_PLAN_VALUE_ELEMENT);
    /* Attribute channel */
    EXPECT_STREQ(leptris_plan_value_attribute(r, "lang"), "en");
    EXPECT_EQ(leptris_plan_value_attribute(r, "nope"), nullptr);
    /* Children: title scalar, item collection, meta nested; the
     * undescribed <skip/> and unmatched row are absent. */
    ASSERT_EQ(leptris_plan_value_count(r), 3u);

    LeptrisPlanResult title = leptris_plan_value_at(r, 0);
    ASSERT_NE(title, nullptr);
    EXPECT_EQ(leptris_plan_value_kind(title), LEPTRIS_PLAN_VALUE_SCALAR);
    EXPECT_STREQ(leptris_plan_value_name(title), "title");
    EXPECT_EQ(leptris_plan_value_type_tag(title), 1);
    EXPECT_STREQ(leptris_plan_value_string(title), "Hello");
    EXPECT_EQ(leptris_plan_value_length(title), 5u);

    LeptrisPlanResult items = leptris_plan_value_at(r, 1);
    ASSERT_NE(items, nullptr);
    EXPECT_EQ(leptris_plan_value_kind(items), LEPTRIS_PLAN_VALUE_COLLECTION);
    /* #1113: the collection echoes its producing row's wire_name
     * and type_tag, like scalar/nested rows — the accessor's
     * documented contract. */
    EXPECT_STREQ(leptris_plan_value_name(items), "item");
    EXPECT_EQ(leptris_plan_value_type_tag(items), 2);
    ASSERT_EQ(leptris_plan_value_count(items), 2u);
    EXPECT_STREQ(leptris_plan_value_string(leptris_plan_value_at(items, 0)), "a");
    EXPECT_STREQ(leptris_plan_value_string(leptris_plan_value_at(items, 1)), "b");
    EXPECT_EQ(leptris_plan_value_at(items, 2), nullptr);

    LeptrisPlanResult meta = leptris_plan_value_at(r, 2);
    ASSERT_NE(meta, nullptr);
    EXPECT_EQ(leptris_plan_value_kind(meta), LEPTRIS_PLAN_VALUE_ELEMENT);
    ASSERT_EQ(leptris_plan_value_count(meta), 1u);
    EXPECT_STREQ(
        leptris_plan_value_string(leptris_plan_value_at(meta, 0)), "v");

    /* The result is standalone: free the document, the strings live. */
    leptris_document_free(doc);
    EXPECT_STREQ(leptris_plan_value_string(title), "Hello");
    EXPECT_STREQ(leptris_plan_value_attribute(r, "lang"), "en");

    leptris_plan_result_free(r);
    leptris_plan_free(plan);
}

TEST(PlanWalk, RawCallbackAndMixedContent) {
    LeptrisDocument doc = parse(
        "<p>intro <b>bold</b> mid"
        "<![CDATA[ cdata run ]]>tail"
        "<ref><inner x=\"1\"/></ref>"
        "<stamp>xyz</stamp></p>");
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    leptris_child_plan kids[4] = {
        {"b", LEPTRIS_PLAN_KIND_SCALAR, 0, -1},
        {"", LEPTRIS_PLAN_KIND_CONTENT, 0, -1},   /* text runs, doc order */
        {"ref", LEPTRIS_PLAN_KIND_RAW, 5, -1},
        {"stamp", LEPTRIS_PLAN_KIND_CALLBACK, 6, -1},
    };
    leptris_element_plan plans[1] = {};
    plans[0].element_name = "p";
    plans[0].child_count = 4;
    plans[0].child_plans = kids;
    plans[0].flags = LEPTRIS_PLAN_FLAG_MIXED_CONTENT | LEPTRIS_PLAN_FLAG_CDATA;

    leptris_plan_spec spec = {};
    spec.abi_version = leptris_plan_abi_version();
    spec.plan_count = 1;
    spec.plans = plans;

    LeptrisStatus st = LEPTRIS_OK;
    LeptrisPlan plan = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan, nullptr);
    LeptrisPlanResult r = leptris_plan_walk(doc, root, plan, &st);
    ASSERT_NE(r, nullptr);
    ASSERT_EQ(leptris_plan_value_count(r), 4u);

    LeptrisPlanResult b = leptris_plan_value_at(r, 0);
    ASSERT_NE(b, nullptr);
    EXPECT_EQ(leptris_plan_value_kind(b), LEPTRIS_PLAN_VALUE_SCALAR);
    EXPECT_STREQ(leptris_plan_value_string(b), "bold");

    /* Content row: the four text runs around <b> in document order
     * (the CDATA run included via the CDATA flag). */
    LeptrisPlanResult runs = leptris_plan_value_at(r, 1);
    ASSERT_NE(runs, nullptr);
    ASSERT_EQ(leptris_plan_value_kind(runs), LEPTRIS_PLAN_VALUE_COLLECTION);
    ASSERT_EQ(leptris_plan_value_count(runs), 4u);
    EXPECT_STREQ(leptris_plan_value_string(leptris_plan_value_at(runs, 0)),
                 "intro ");
    EXPECT_STREQ(leptris_plan_value_string(leptris_plan_value_at(runs, 1)),
                 " mid");
    EXPECT_STREQ(leptris_plan_value_string(leptris_plan_value_at(runs, 2)),
                 " cdata run ");
    EXPECT_STREQ(leptris_plan_value_string(leptris_plan_value_at(runs, 3)),
                 "tail");

    LeptrisPlanResult raw = leptris_plan_value_at(r, 2);
    ASSERT_NE(raw, nullptr);
    EXPECT_EQ(leptris_plan_value_kind(raw), LEPTRIS_PLAN_VALUE_RAW);
    EXPECT_EQ(leptris_plan_value_type_tag(raw), 5);
    EXPECT_NE(strstr(leptris_plan_value_string(raw), "<inner x=\"1\"/>"),
              nullptr);

    /* The escape hatch is exercised in the focused walk below. */
    leptris_child_plan kids2[1] = {
        {"stamp", LEPTRIS_PLAN_KIND_CALLBACK, 6, -1},
    };
    plans[0].child_count = 1;
    plans[0].child_plans = kids2;
    plans[0].flags = 0;
    LeptrisPlan plan2 = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan2, nullptr);
    LeptrisPlanResult r2 = leptris_plan_walk(doc, root, plan2, &st);
    ASSERT_NE(r2, nullptr);
    ASSERT_EQ(leptris_plan_value_count(r2), 1u);
    LeptrisPlanResult cb = leptris_plan_value_at(r2, 0);
    ASSERT_NE(cb, nullptr);
    EXPECT_EQ(leptris_plan_value_kind(cb), LEPTRIS_PLAN_VALUE_CALLBACK);
    EXPECT_STREQ(leptris_plan_value_string(cb), "xyz");
    EXPECT_EQ(leptris_plan_value_type_tag(cb), 6);
    EXPECT_GT(leptris_plan_value_position(cb), 0u);

    leptris_plan_result_free(r2);
    leptris_plan_free(plan2);
    leptris_plan_result_free(r);
    leptris_plan_free(plan);
    leptris_document_free(doc);
}

TEST(PlanWalk, NamespaceFormsAndLeniency) {
    LeptrisDocument doc = parse(
        "<root xmlns:x=\"urn:x\" xmlns=\"urn:default\">"
        "<plain>none</plain>"
        "<x:tag>xns</x:tag>"
        "</root>");
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    /* NS_NONE binds only no-namespace elements; the default-ns doc
     * has none — the row yields nothing. */
    leptris_child_plan none_kids[2] = {
        {"plain", LEPTRIS_PLAN_KIND_SCALAR, 0, -1},
        {"tag", LEPTRIS_PLAN_KIND_SCALAR, 0, -1},
    };
    leptris_element_plan plans[1] = {};
    plans[0].element_name = "root";
    plans[0].ns_form = LEPTRIS_PLAN_NS_ANY;
    plans[0].child_count = 2;
    plans[0].child_plans = none_kids;

    leptris_plan_spec spec = {};
    spec.abi_version = leptris_plan_abi_version();
    spec.plan_count = 1;
    spec.plans = plans;
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisPlan plan = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan, nullptr);

    /* Child binding uses the target plan's ns form: plans for the
     * children don't exist here (scalar kinds use the CHILD row's
     * implicit no-ns binding). <plain> is in urn:default (inherited),
     * <x:tag> in urn:x: neither is namespace-less -> nothing binds. */
    LeptrisPlanResult r = leptris_plan_walk(doc, root, plan, &st);
    ASSERT_NE(r, nullptr);
    EXPECT_EQ(leptris_plan_value_count(r), 0u);
    leptris_plan_result_free(r);

    /* NS_LENIENT on the parent: bind out-of-namespace children. */
    plans[0].flags = LEPTRIS_PLAN_FLAG_NS_LENIENT;
    LeptrisPlan plan2 = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan2, nullptr);
    LeptrisPlanResult r2 = leptris_plan_walk(doc, root, plan2, &st);
    ASSERT_NE(r2, nullptr);
    ASSERT_EQ(leptris_plan_value_count(r2), 2u);
    EXPECT_STREQ(leptris_plan_value_string(leptris_plan_value_at(r2, 0)),
                 "none");
    EXPECT_STREQ(leptris_plan_value_string(leptris_plan_value_at(r2, 1)),
                 "xns");
    leptris_plan_result_free(r2);
    leptris_plan_free(plan2);
    leptris_plan_free(plan);
    leptris_document_free(doc);
}

/* XSD-002 fallout class: child rows whose wire_name is the PREFIXED
 * form ("w:b") — exactly what the ABI doc promises ("XML element
 * name as it appears on the wire") — must bind namespaced elements.
 * The matcher historically compared the LOCAL name only, so every
 * prefixed row silently matched nothing: hosts hydrated empty
 * subtrees (attributes read NULL, elements fell back to defaults)
 * and re-serialized without them. Local-name rows keep binding. */
TEST(PlanWalk, PrefixedWireNamesBindNamespacedElements) {
    LeptrisDocument doc = parse(
        "<w:document xmlns:w=\"urn:w\">"
        "<w:r><w:b w:val=\"false\"/></w:r>"
        "</w:document>");
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    leptris_attr_plan b_attrs[] = {
        {"w:val", LEPTRIS_PLAN_KIND_SCALAR, 7, 0, 0, nullptr},
    };
    leptris_child_plan b_row = {};
    b_row.wire_name = "w:b";
    b_row.kind = LEPTRIS_PLAN_KIND_NESTED;
    b_row.child_plan_index = 2;
    b_row.ns_form = LEPTRIS_PLAN_NS_ANY;
    leptris_child_plan r_row = {};
    r_row.wire_name = "w:r";
    r_row.kind = LEPTRIS_PLAN_KIND_NESTED;
    r_row.child_plan_index = 1;
    r_row.ns_form = LEPTRIS_PLAN_NS_ANY;
    leptris_element_plan plans[3] = {};
    plans[0].element_name = "document";
    plans[0].ns_form = LEPTRIS_PLAN_NS_ANY;
    plans[0].child_count = 1;
    plans[0].child_plans = &r_row;
    plans[1].element_name = "r";
    plans[1].ns_form = LEPTRIS_PLAN_NS_ANY;
    plans[1].child_count = 1;
    plans[1].child_plans = &b_row;
    plans[2].element_name = "b";
    plans[2].ns_form = LEPTRIS_PLAN_NS_ANY;
    plans[2].attribute_count = 1;
    plans[2].attribute_plans = b_attrs;

    leptris_plan_spec spec = {};
    spec.abi_version = leptris_plan_abi_version();
    spec.plan_count = 3;
    spec.plans = plans;
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisPlan plan = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan, nullptr);
    LeptrisPlanResult r = leptris_plan_walk(doc, root, plan, &st);
    ASSERT_NE(r, nullptr);
    ASSERT_EQ(leptris_plan_value_count(r), 1u);
    LeptrisPlanResult rcoll = leptris_plan_value_at(r, 0);
    ASSERT_NE(rcoll, nullptr);
    /* the nested w:r row binds its element value */
    ASSERT_EQ(leptris_plan_value_kind(rcoll), LEPTRIS_PLAN_VALUE_ELEMENT);
    ASSERT_EQ(leptris_plan_value_count(rcoll), 1u);
    LeptrisPlanResult bval = leptris_plan_value_at(rcoll, 0);
    ASSERT_NE(bval, nullptr);
    ASSERT_EQ(leptris_plan_value_kind(bval), LEPTRIS_PLAN_VALUE_ELEMENT);
    const char* val = leptris_plan_value_attribute(bval, "w:val");
    ASSERT_NE(val, nullptr);
    EXPECT_STREQ(val, "false");
    leptris_plan_result_free(r);
    leptris_plan_free(plan);
    leptris_document_free(doc);
}

TEST(PlanBuild, CopiesTheSpecStrings) {
    LeptrisDocument doc = parse("<doc><t>v</t></doc>");
    ASSERT_NE(doc, nullptr);

    /* Heap spec freed right after build — the pool must own copies.
     * calloc: the ABI contract is a fully-initialized spec; malloc
     * leaves ns_form/ns_uri garbage and the walk dereferences it
     * (bus error on CI's warm heap, invisible on fresh pages). */
    char name_buf[] = "t";
    leptris_child_plan* kids =
        (leptris_child_plan*)calloc(1, sizeof(leptris_child_plan));
    kids[0].wire_name = name_buf;
    kids[0].kind = LEPTRIS_PLAN_KIND_SCALAR;
    kids[0].type_tag = 0;
    kids[0].child_plan_index = -1;
    leptris_element_plan* plans =
        (leptris_element_plan*)calloc(1, sizeof(leptris_element_plan));
    plans[0].element_name = "doc";
    plans[0].child_count = 1;
    plans[0].child_plans = kids;

    leptris_plan_spec spec = {};
    spec.abi_version = leptris_plan_abi_version();
    spec.plan_count = 1;
    spec.plans = plans;
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisPlan plan = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan, nullptr);
    free(kids);
    free(plans);

    LeptrisPlanResult r =
        leptris_plan_walk(doc, leptris_document_root(doc), plan, &st);
    ASSERT_NE(r, nullptr);
    ASSERT_EQ(leptris_plan_value_count(r), 1u);
    EXPECT_STREQ(leptris_plan_value_string(leptris_plan_value_at(r, 0)),
                 "v");

    leptris_plan_result_free(r);
    leptris_plan_free(plan);
    leptris_document_free(doc);
}

TEST(PlanAccessors, NullTolerant) {
    EXPECT_EQ(leptris_plan_value_kind(nullptr), LEPTRIS_PLAN_VALUE_ELEMENT);
    EXPECT_EQ(leptris_plan_value_name(nullptr), nullptr);
    EXPECT_EQ(leptris_plan_value_type_tag(nullptr), 0);
    EXPECT_EQ(leptris_plan_value_string(nullptr), nullptr);
    EXPECT_EQ(leptris_plan_value_length(nullptr), 0u);
    EXPECT_EQ(leptris_plan_value_position(nullptr), 0u);
    EXPECT_EQ(leptris_plan_value_count(nullptr), 0u);
    EXPECT_EQ(leptris_plan_value_at(nullptr, 0), nullptr);
    EXPECT_EQ(leptris_plan_value_attribute(nullptr, "x"), nullptr);
    leptris_plan_free(nullptr);
    leptris_plan_result_free(nullptr);
}

// ---- #1272: attribute-predicate rows partition same-wire siblings -

TEST(Plan1272, PredicateRowsPartitionSameWireSiblings) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(
        "<root>"
        "<comp type='guidance'>G1</comp>"
        "<comp type='purpose'>P1</comp>"
        "</root>",
        std::strlen("<root>"
        "<comp type='guidance'>G1</comp>"
        "<comp type='purpose'>P1</comp>"
        "</root>"), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    /* Two rows on the same wire_name with disjoint predicate filters
     * — each occurrence routes to exactly one row (no double-capture). */
    leptris_attr_predicate p0[1] = {{"type", "guidance"}};
    leptris_attr_predicate p1[1] = {{"type", "purpose"}};
    leptris_child_plan kids[2] = {};
    kids[0].wire_name = "comp";
    kids[0].kind = LEPTRIS_PLAN_KIND_SCALAR;
    kids[0].type_tag = 1;
    kids[0].child_plan_index = -1;
    kids[0].predicate_count = 1;
    kids[0].predicates = p0;
    kids[1].wire_name = "comp";
    kids[1].kind = LEPTRIS_PLAN_KIND_SCALAR;
    kids[1].type_tag = 2;
    kids[1].child_plan_index = -1;
    kids[1].predicate_count = 1;
    kids[1].predicates = p1;

    leptris_element_plan plans[1] = {};
    plans[0].element_name = "root";
    plans[0].child_count = 2;
    plans[0].child_plans = kids;
    leptris_plan_spec spec = {};
    spec.abi_version = leptris_plan_abi_version();
    spec.plan_count = 1;
    spec.plans = plans;

    LeptrisPlan plan = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan, nullptr);
    LeptrisPlanResult r = leptris_plan_walk(doc, root, plan, &st);
    ASSERT_NE(r, nullptr);
    /* Exactly two values, partitioned correctly:
     *  type_tag=1 -> "G1" (guidance row); type_tag=2 -> "P1" (purpose). */
    ASSERT_EQ(leptris_plan_value_count(r), 2u);

    LeptrisPlanResult v0 = leptris_plan_value_at(r, 0);
    ASSERT_NE(v0, nullptr);
    EXPECT_EQ(leptris_plan_value_type_tag(v0), 1);
    EXPECT_STREQ(leptris_plan_value_string(v0), "G1");

    LeptrisPlanResult v1 = leptris_plan_value_at(r, 1);
    ASSERT_NE(v1, nullptr);
    EXPECT_EQ(leptris_plan_value_type_tag(v1), 2);
    EXPECT_STREQ(leptris_plan_value_string(v1), "P1");

    leptris_plan_result_free(r);
    leptris_plan_free(plan);
    leptris_document_free(doc);
}

// ---- #1273: document-order identity (position + node_kind + order_index)

TEST(Plan1273, DocumentOrderIdentityStampsEveryValue) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(
        "<r><a>1</a><b>2</b><c>3</c></r>",
        std::strlen("<r><a>1</a><b>2</b><c>3</c></r>"), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    leptris_child_plan kids[3] = {};
    kids[0].wire_name = "a"; kids[0].kind = LEPTRIS_PLAN_KIND_SCALAR;
    kids[0].type_tag = 1; kids[0].child_plan_index = -1;
    kids[1].wire_name = "b"; kids[1].kind = LEPTRIS_PLAN_KIND_SCALAR;
    kids[1].type_tag = 1; kids[1].child_plan_index = -1;
    kids[2].wire_name = "c"; kids[2].kind = LEPTRIS_PLAN_KIND_SCALAR;
    kids[2].type_tag = 1; kids[2].child_plan_index = -1;

    leptris_element_plan plans[1] = {};
    plans[0].element_name = "r";
    plans[0].child_count = 3;
    plans[0].child_plans = kids;
    leptris_plan_spec spec = {};
    spec.abi_version = leptris_plan_abi_version();
    spec.plan_count = 1;
    spec.plans = plans;

    LeptrisPlan plan = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan, nullptr);
    LeptrisPlanResult r = leptris_plan_walk(doc, root, plan, &st);
    ASSERT_NE(r, nullptr);
    ASSERT_EQ(leptris_plan_value_count(r), 3u);

    /* Verify order_index is strictly increasing across the 3 child
     * scalars (the loop counter starts at 0 — first o must be 0,
     * subsequent must strictly increase). */
    for (size_t i = 0; i < 3; i++) {
        LeptrisPlanResult v = leptris_plan_value_at(r, i);
        ASSERT_NE(v, nullptr);
        EXPECT_EQ(leptris_plan_value_node_kind(v),
                  LEPTRIS_NODE_TYPE_ELEMENT);
        EXPECT_GT(leptris_plan_value_position(v), 0u);
        uint32_t o = leptris_plan_value_order_index(v);
        if (i == 0) {
            EXPECT_EQ(o, 0u);
        } else {
            LeptrisPlanResult prev = leptris_plan_value_at(r, i - 1);
            EXPECT_GT(o, leptris_plan_value_order_index(prev));
        }
    }
    leptris_plan_result_free(r);
    leptris_plan_free(plan);
    leptris_document_free(doc);
}

// ---- #1273: EMIT_ORDER_SPINE appends unmatched text runs

TEST(Plan1273, EmitOrderSpineIncludesUnmatchedTextRuns) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(
        "<r>before<a>1</a>middle<b>2</b>after</r>",
        std::strlen("<r>before<a>1</a>middle<b>2</b>after</r>"), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    leptris_child_plan kids[2] = {};
    kids[0].wire_name = "a"; kids[0].kind = LEPTRIS_PLAN_KIND_SCALAR;
    kids[0].type_tag = 1; kids[0].child_plan_index = -1;
    kids[1].wire_name = "b"; kids[1].kind = LEPTRIS_PLAN_KIND_SCALAR;
    kids[1].type_tag = 1; kids[1].child_plan_index = -1;

    leptris_element_plan plans[1] = {};
    plans[0].element_name = "r";
    plans[0].child_count = 2;
    plans[0].child_plans = kids;
    plans[0].flags = LEPTRIS_PLAN_FLAG_EMIT_ORDER_SPINE;
    leptris_plan_spec spec = {};
    spec.abi_version = leptris_plan_abi_version();
    spec.plan_count = 1;
    spec.plans = plans;

    LeptrisPlan plan = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan, nullptr);
    LeptrisPlanResult r = leptris_plan_walk(doc, root, plan, &st);
    ASSERT_NE(r, nullptr);
    /* 3 spine text runs ("before"/"middle"/"after") + 2 scalar
     * matches (a, b) — emission is document-ordered but the spine
     * fills the gaps before/after the matched elements. */
    EXPECT_GE(leptris_plan_value_count(r), 5u);

    /* Verify order_index is strictly increasing — that proves
     * document order across the spine + matches. The first emitted
     * value gets order_index=0 (the post-increment counter), the
     * rest must strictly grow. */
    uint32_t last_order = (uint32_t)-1;
    for (size_t i = 0; i < leptris_plan_value_count(r); i++) {
        LeptrisPlanResult v = leptris_plan_value_at(r, i);
        ASSERT_NE(v, nullptr);
        uint32_t o = leptris_plan_value_order_index(v);
        if (last_order == (uint32_t)-1) {
            EXPECT_EQ(o, 0u);
        } else {
            EXPECT_GT(o, last_order);
        }
        last_order = o;
    }
    leptris_plan_result_free(r);
    leptris_plan_free(plan);
    leptris_document_free(doc);
}

// ---- #1269a: in-pass type execution (int / float / bool)

TEST(Plan1269a, InPassTypeExecution) {
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc = leptris_parse_string(
        "<r><n>42</n><f>3.14</f><b>true</b><x>notanint</x></r>",
        std::strlen("<r><n>42</n><f>3.14</f><b>true</b><x>notanint</x></r>"), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    leptris_child_plan kids[4] = {};
    kids[0].wire_name = "n"; kids[0].kind = LEPTRIS_PLAN_KIND_SCALAR;
    kids[0].type_tag = 1; kids[0].child_plan_index = -1;   /* int */
    kids[1].wire_name = "f"; kids[1].kind = LEPTRIS_PLAN_KIND_SCALAR;
    kids[1].type_tag = 2; kids[1].child_plan_index = -1;   /* float */
    kids[2].wire_name = "b"; kids[2].kind = LEPTRIS_PLAN_KIND_SCALAR;
    kids[2].type_tag = 3; kids[2].child_plan_index = -1;   /* bool */
    kids[3].wire_name = "x"; kids[3].kind = LEPTRIS_PLAN_KIND_SCALAR;
    kids[3].type_tag = 1; kids[3].child_plan_index = -1;   /* int but bad */

    leptris_element_plan plans[1] = {};
    plans[0].element_name = "r";
    plans[0].child_count = 4;
    plans[0].child_plans = kids;
    leptris_plan_spec spec = {};
    spec.abi_version = leptris_plan_abi_version();
    spec.plan_count = 1;
    spec.plans = plans;

    LeptrisPlan plan = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan, nullptr);
    LeptrisPlanResult r = leptris_plan_walk(doc, root, plan, &st);
    ASSERT_NE(r, nullptr);
    ASSERT_EQ(leptris_plan_value_count(r), 4u);

    /* n -> int 42 */
    int64_t i = -1;
    EXPECT_EQ(leptris_plan_value_int(leptris_plan_value_at(r, 0), &i), 0);
    EXPECT_EQ(i, 42);

    /* f -> float 3.14 */
    double f = 0;
    EXPECT_EQ(leptris_plan_value_float(leptris_plan_value_at(r, 1), &f), 0);
    EXPECT_DOUBLE_EQ(f, 3.14);

    /* b -> bool true */
    int bv = -1;
    EXPECT_EQ(leptris_plan_value_bool(leptris_plan_value_at(r, 2), &bv), 0);
    EXPECT_EQ(bv, 1);

    /* x -> "notanint" but typed as int → typed parse fails, the
     * legacy string channel stays ("notanint"). */
    EXPECT_NE(leptris_plan_value_int(leptris_plan_value_at(r, 3), &i), 0);
    /* The string form is always populated. */
    EXPECT_STREQ(leptris_plan_value_string(leptris_plan_value_at(r, 3)),
                 "notanint");

    /* Compatibility: all four still readable as strings. */
    EXPECT_STREQ(leptris_plan_value_string(leptris_plan_value_at(r, 0)),
                 "42");
    leptris_plan_result_free(r);
    leptris_plan_free(plan);
    leptris_document_free(doc);
}

// ---- #1269b: leptris_plan_materialize (parse + walk + free)

TEST(Plan1269b, MaterializeParsePlusWalkEqualsWalk) {
    LeptrisStatus st = LEPTRIS_OK;
    const char xml[] = "<r><n>7</n><n>11</n></r>";
    LeptrisDocument doc = leptris_parse_string(xml, std::strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    leptris_child_plan kids[1] = {};
    kids[0].wire_name = "n"; kids[0].kind = LEPTRIS_PLAN_KIND_COLLECTION;
    kids[0].type_tag = 1; kids[0].child_plan_index = -1;
    leptris_element_plan plans[1] = {};
    plans[0].element_name = "r";
    plans[0].child_count = 1;
    plans[0].child_plans = kids;
    leptris_plan_spec spec = {};
    spec.abi_version = leptris_plan_abi_version();
    spec.plan_count = 1;
    spec.plans = plans;

    LeptrisPlan plan = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan, nullptr);

    /* Walk: same doc owned by the harness. */
    LeptrisPlanResult r_walk = leptris_plan_walk(doc, root, plan, &st);
    ASSERT_NE(r_walk, nullptr);
    /* Materialize: parse_string + walk + free inside the engine. */
    LeptrisPlanResult r_mat = leptris_plan_materialize(
        xml, std::strlen(xml), plan, &st);
    ASSERT_NE(r_mat, nullptr);
    /* Same collection shape — two items, each "n". */
    LeptrisPlanResult walk_col = leptris_plan_value_at(r_walk, 0);
    LeptrisPlanResult mat_col = leptris_plan_value_at(r_mat, 0);
    ASSERT_NE(walk_col, nullptr);
    ASSERT_NE(mat_col, nullptr);
    EXPECT_EQ(leptris_plan_value_count(walk_col),
              leptris_plan_value_count(mat_col));
    EXPECT_STREQ(leptris_plan_value_string(leptris_plan_value_at(walk_col, 0)),
                 leptris_plan_value_string(leptris_plan_value_at(mat_col, 0)));

    leptris_plan_result_free(r_walk);
    leptris_plan_result_free(r_mat);
    leptris_plan_free(plan);
    leptris_document_free(doc);
}

// ---- #1552: wildcard (catch-all) child rows -----------------------
// A WILDCARD row binds every element child that no named sibling row
// bound, regardless of row-list position: named rows always win. The
// row emits one COLLECTION echoing its wire_name/type_tag; members
// keep document order and each echoes the row type_tag for hydrator
// routing. Row-level ns_form filters the captured remainder; an unset
// form means ANY namespace (catch-all), unlike named rows' NONE.
// child_plan_index >= 0 walks each member through that plan (ELEMENT
// members); otherwise members are RAW serialized subtrees.

TEST(Plan1552, WildcardRowCatchesRemainderAfterNamedRows) {
    LeptrisStatus st = LEPTRIS_OK;
    const char* xml =
        "<root><a>1</a><m:x xmlns:m='urn:m'>2</m:x><b>3</b>"
        "<other>4</other><a>5</a></root>";
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    leptris_child_plan kids[3] = {};
    kids[0].wire_name = "a";
    kids[0].kind = LEPTRIS_PLAN_KIND_SCALAR;
    kids[0].type_tag = 1;
    kids[0].child_plan_index = -1;
    kids[1].wire_name = "b";
    kids[1].kind = LEPTRIS_PLAN_KIND_COLLECTION;
    kids[1].type_tag = 2;
    kids[1].child_plan_index = -1;
    kids[2].wire_name = "*";
    kids[2].kind = LEPTRIS_PLAN_KIND_WILDCARD;
    kids[2].type_tag = 9;
    kids[2].child_plan_index = -1;

    leptris_element_plan plans[1] = {};
    plans[0].element_name = "root";
    plans[0].child_count = 3;
    plans[0].child_plans = kids;
    leptris_plan_spec spec = {};
    spec.abi_version = leptris_plan_abi_version();
    spec.plan_count = 1;
    spec.plans = plans;

    LeptrisPlan plan = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan, nullptr);
    LeptrisPlanResult r = leptris_plan_walk(doc, root, plan, &st);
    ASSERT_NE(r, nullptr);

    /* Named rows capture exactly as before: two a-scalars, one
     * b-collection — then the wildcard bucket. */
    ASSERT_EQ(leptris_plan_value_count(r), 4u);
    EXPECT_STREQ(leptris_plan_value_string(leptris_plan_value_at(r, 0)), "1");
    EXPECT_STREQ(leptris_plan_value_string(leptris_plan_value_at(r, 1)), "5");

    LeptrisPlanResult bcol = leptris_plan_value_at(r, 2);
    ASSERT_NE(bcol, nullptr);
    ASSERT_EQ(leptris_plan_value_count(bcol), 1u);
    EXPECT_STREQ(leptris_plan_value_string(
                     leptris_plan_value_at(bcol, 0)), "3");

    LeptrisPlanResult wcol = leptris_plan_value_at(r, 3);
    ASSERT_NE(wcol, nullptr);
    EXPECT_EQ(leptris_plan_value_kind(wcol),
              LEPTRIS_PLAN_VALUE_COLLECTION);
    EXPECT_STREQ(leptris_plan_value_name(wcol), "*");
    EXPECT_EQ(leptris_plan_value_type_tag(wcol), 9);
    /* Remainder in document order: m:x (2nd child) then other
     * (4th). Both members echo the row type_tag. */
    ASSERT_EQ(leptris_plan_value_count(wcol), 2u);
    LeptrisPlanResult m0 = leptris_plan_value_at(wcol, 0);
    ASSERT_NE(m0, nullptr);
    EXPECT_EQ(leptris_plan_value_kind(m0), LEPTRIS_PLAN_VALUE_RAW);
    EXPECT_EQ(leptris_plan_value_type_tag(m0), 9);
    EXPECT_NE(strstr(leptris_plan_value_string(m0), "m:x"), nullptr);
    LeptrisPlanResult m1 = leptris_plan_value_at(wcol, 1);
    ASSERT_NE(m1, nullptr);
    EXPECT_NE(strstr(leptris_plan_value_string(m1), "other"), nullptr);
    EXPECT_LT(leptris_plan_value_order_index(m0),
              leptris_plan_value_order_index(m1));

    leptris_plan_result_free(r);
    leptris_plan_free(plan);
    leptris_document_free(doc);
}

TEST(Plan1552, NamedRowsTakePrecedenceRegardlessOfRowOrder) {
    LeptrisStatus st = LEPTRIS_OK;
    const char* xml =
        "<root><a>1</a><m:x xmlns:m='urn:m'>2</m:x><b>3</b>"
        "<other>4</other><a>5</a></root>";
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    /* Wildcard listed FIRST — it must still lose every child that
     * a named row binds. */
    leptris_child_plan kids[3] = {};
    kids[0].wire_name = "*";
    kids[0].kind = LEPTRIS_PLAN_KIND_WILDCARD;
    kids[0].type_tag = 9;
    kids[0].child_plan_index = -1;
    kids[1].wire_name = "a";
    kids[1].kind = LEPTRIS_PLAN_KIND_SCALAR;
    kids[1].type_tag = 1;
    kids[1].child_plan_index = -1;
    kids[2].wire_name = "b";
    kids[2].kind = LEPTRIS_PLAN_KIND_COLLECTION;
    kids[2].type_tag = 2;
    kids[2].child_plan_index = -1;

    leptris_element_plan plans[1] = {};
    plans[0].element_name = "root";
    plans[0].child_count = 3;
    plans[0].child_plans = kids;
    leptris_plan_spec spec = {};
    spec.abi_version = leptris_plan_abi_version();
    spec.plan_count = 1;
    spec.plans = plans;

    LeptrisPlan plan = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan, nullptr);
    LeptrisPlanResult r = leptris_plan_walk(doc, root, plan, &st);
    ASSERT_NE(r, nullptr);

    /* Same remainder as the wildcard-last case: only m:x and
     * other. The named a/b rows keep their captures. */
    ASSERT_EQ(leptris_plan_value_count(r), 4u);
    LeptrisPlanResult wcol = leptris_plan_value_at(r, 3);
    ASSERT_NE(wcol, nullptr);
    ASSERT_EQ(leptris_plan_value_count(wcol), 2u);
    EXPECT_NE(strstr(leptris_plan_value_string(
                         leptris_plan_value_at(wcol, 0)), "m:x"),
              nullptr);
    EXPECT_NE(strstr(leptris_plan_value_string(
                         leptris_plan_value_at(wcol, 1)), "other"),
              nullptr);

    leptris_plan_result_free(r);
    leptris_plan_free(plan);
    leptris_document_free(doc);
}

TEST(Plan1552, WildcardNsFormFiltersTheCapturedRemainder) {
    LeptrisStatus st = LEPTRIS_OK;
    const char* xml =
        "<root><a>1</a><m:x xmlns:m='urn:m'>2</m:x><other>4</other></root>";
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    leptris_child_plan kids[2] = {};
    kids[0].wire_name = "a";
    kids[0].kind = LEPTRIS_PLAN_KIND_SCALAR;
    kids[0].type_tag = 1;
    kids[0].child_plan_index = -1;
    kids[1].wire_name = "*";
    kids[1].kind = LEPTRIS_PLAN_KIND_WILDCARD;
    kids[1].type_tag = 9;
    kids[1].child_plan_index = -1;
    kids[1].ns_form = LEPTRIS_PLAN_NS_EXACT;
    kids[1].ns_uri = "urn:m";
    kids[1].pad0 = 1; /* explicit form */

    leptris_element_plan plans[1] = {};
    plans[0].element_name = "root";
    plans[0].child_count = 2;
    plans[0].child_plans = kids;
    leptris_plan_spec spec = {};
    spec.abi_version = leptris_plan_abi_version();
    spec.plan_count = 1;
    spec.plans = plans;

    LeptrisPlan plan = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan, nullptr);
    LeptrisPlanResult r = leptris_plan_walk(doc, root, plan, &st);
    ASSERT_NE(r, nullptr);

    ASSERT_EQ(leptris_plan_value_count(r), 2u);
    LeptrisPlanResult wcol = leptris_plan_value_at(r, 1);
    ASSERT_NE(wcol, nullptr);
    /* EXACT(urn:m) keeps only the namespaced member. */
    ASSERT_EQ(leptris_plan_value_count(wcol), 1u);
    EXPECT_NE(strstr(leptris_plan_value_string(
                         leptris_plan_value_at(wcol, 0)), "m:x"),
              nullptr);
    leptris_plan_result_free(r);
    leptris_plan_free(plan);

    /* NONE keeps only the no-namespace remainder. */
    kids[1].ns_form = LEPTRIS_PLAN_NS_NONE;
    kids[1].ns_uri = NULL;
    /* pad0 stays 1 from the first scenario: NONE is explicit. */
    plan = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan, nullptr);
    r = leptris_plan_walk(doc, root, plan, &st);
    ASSERT_NE(r, nullptr);
    wcol = leptris_plan_value_at(r, 1);
    ASSERT_NE(wcol, nullptr);
    ASSERT_EQ(leptris_plan_value_count(wcol), 1u);
    EXPECT_NE(strstr(leptris_plan_value_string(
                         leptris_plan_value_at(wcol, 0)), "other"),
              nullptr);
    leptris_plan_result_free(r);
    leptris_plan_free(plan);
    leptris_document_free(doc);
}

TEST(Plan1552, WildcardRowWalksMembersThroughChildPlan) {
    LeptrisStatus st = LEPTRIS_OK;
    const char* xml =
        "<root><a>1</a><wrap><z>deep</z></wrap></root>";
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    leptris_child_plan root_kids[2] = {};
    root_kids[0].wire_name = "a";
    root_kids[0].kind = LEPTRIS_PLAN_KIND_SCALAR;
    root_kids[0].type_tag = 1;
    root_kids[0].child_plan_index = -1;
    root_kids[1].wire_name = "*";
    root_kids[1].kind = LEPTRIS_PLAN_KIND_WILDCARD;
    root_kids[1].type_tag = 9;
    root_kids[1].child_plan_index = 1;

    leptris_child_plan wrap_kids[1] = {};
    wrap_kids[0].wire_name = "z";
    wrap_kids[0].kind = LEPTRIS_PLAN_KIND_SCALAR;
    wrap_kids[0].type_tag = 7;
    wrap_kids[0].child_plan_index = -1;

    leptris_element_plan plans[2] = {};
    plans[0].element_name = "root";
    plans[0].child_count = 2;
    plans[0].child_plans = root_kids;
    plans[1].element_name = "wrap";
    plans[1].child_count = 1;
    plans[1].child_plans = wrap_kids;
    leptris_plan_spec spec = {};
    spec.abi_version = leptris_plan_abi_version();
    spec.plan_count = 2;
    spec.plans = plans;

    LeptrisPlan plan = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan, nullptr);
    LeptrisPlanResult r = leptris_plan_walk(doc, root, plan, &st);
    ASSERT_NE(r, nullptr);

    ASSERT_EQ(leptris_plan_value_count(r), 2u);
    LeptrisPlanResult wcol = leptris_plan_value_at(r, 1);
    ASSERT_NE(wcol, nullptr);
    ASSERT_EQ(leptris_plan_value_count(wcol), 1u);
    /* The member is an ELEMENT value walked through plans[1]:
     * named after the child, tagged by the wildcard row, with the
     * nested z-scalar inside. */
    LeptrisPlanResult member = leptris_plan_value_at(wcol, 0);
    ASSERT_NE(member, nullptr);
    EXPECT_EQ(leptris_plan_value_kind(member), LEPTRIS_PLAN_VALUE_ELEMENT);
    EXPECT_STREQ(leptris_plan_value_name(member), "wrap");
    EXPECT_EQ(leptris_plan_value_type_tag(member), 9);
    ASSERT_EQ(leptris_plan_value_count(member), 1u);
    EXPECT_STREQ(leptris_plan_value_string(
                     leptris_plan_value_at(member, 0)), "deep");

    leptris_plan_result_free(r);
    leptris_plan_free(plan);
    leptris_document_free(doc);
}

TEST(Plan1552, EmptyRemainderStillEmitsTheBucket) {
    LeptrisStatus st = LEPTRIS_OK;
    const char* xml = "<root><a>1</a></root>";
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisElement root = leptris_document_root(doc);

    leptris_child_plan kids[2] = {};
    kids[0].wire_name = "a";
    kids[0].kind = LEPTRIS_PLAN_KIND_SCALAR;
    kids[0].type_tag = 1;
    kids[0].child_plan_index = -1;
    kids[1].wire_name = "*";
    kids[1].kind = LEPTRIS_PLAN_KIND_WILDCARD;
    kids[1].type_tag = 9;
    kids[1].child_plan_index = -1;

    leptris_element_plan plans[1] = {};
    plans[0].element_name = "root";
    plans[0].child_count = 2;
    plans[0].child_plans = kids;
    leptris_plan_spec spec = {};
    spec.abi_version = leptris_plan_abi_version();
    spec.plan_count = 1;
    spec.plans = plans;

    LeptrisPlan plan = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan, nullptr);
    LeptrisPlanResult r = leptris_plan_walk(doc, root, plan, &st);
    ASSERT_NE(r, nullptr);

    /* The bucket exists and is empty — presence vs absence is the
     * routing signal for the hydrator. */
    ASSERT_EQ(leptris_plan_value_count(r), 2u);
    LeptrisPlanResult wcol = leptris_plan_value_at(r, 1);
    ASSERT_NE(wcol, nullptr);
    EXPECT_EQ(leptris_plan_value_kind(wcol),
              LEPTRIS_PLAN_VALUE_COLLECTION);
    EXPECT_EQ(leptris_plan_value_count(wcol), 0u);

    leptris_plan_result_free(r);
    leptris_plan_free(plan);
    leptris_document_free(doc);
}

TEST(Plan1552, WildcardChildPlanIndexIsValidated) {
    LeptrisStatus st = LEPTRIS_OK;
    leptris_child_plan kids[1] = {};
    kids[0].wire_name = "*";
    kids[0].kind = LEPTRIS_PLAN_KIND_WILDCARD;
    kids[0].type_tag = 9;
    kids[0].child_plan_index = 3; /* past spec.plan_count == 1 */

    leptris_element_plan plans[1] = {};
    plans[0].element_name = "root";
    plans[0].child_count = 1;
    plans[0].child_plans = kids;
    leptris_plan_spec spec = {};
    spec.abi_version = leptris_plan_abi_version();
    spec.plan_count = 1;
    spec.plans = plans;

    LeptrisPlan plan = leptris_plan_build(&spec, &st);
    EXPECT_EQ(plan, nullptr);
    EXPECT_EQ(st, LEPTRIS_ERROR_INVALID_ARG);
    leptris_plan_free(plan);
}

// ---- #1551: plan-path serialization emits namespace declarations --
// Rows carrying ns_form EXACT + ns_prefix serialize as prefixed
// names; every (prefix, uri) pair used in the tree is declared
// exactly once on the output root, in first-encounter order. The
// plan supplies wrappers, the result supplies content, children
// emit in document order (walk order_index).

TEST(Plan1551, SerializeEmitsPrefixedNamesAndRootDeclarationOnce) {
    LeptrisStatus st = LEPTRIS_OK;
    const char* xml =
        "<w:document xmlns:w=\"urn:w\"><w:body><w:p><w:t>x</w:t>"
        "</w:p></w:body></w:document>";
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    leptris_child_plan p_kids[1] = {};
    p_kids[0].wire_name = "t";
    p_kids[0].kind = LEPTRIS_PLAN_KIND_SCALAR;
    p_kids[0].type_tag = 3;
    p_kids[0].child_plan_index = -1;
    p_kids[0].ns_form = LEPTRIS_PLAN_NS_EXACT;
    p_kids[0].ns_uri = "urn:w";
    p_kids[0].ns_prefix = "w";

    leptris_child_plan body_kids[1] = {};
    body_kids[0].wire_name = "p";
    body_kids[0].kind = LEPTRIS_PLAN_KIND_NESTED;
    body_kids[0].type_tag = 2;
    body_kids[0].child_plan_index = 2;
    body_kids[0].ns_prefix = "w";

    leptris_child_plan doc_kids[1] = {};
    doc_kids[0].wire_name = "body";
    doc_kids[0].kind = LEPTRIS_PLAN_KIND_NESTED;
    doc_kids[0].type_tag = 1;
    doc_kids[0].child_plan_index = 1;
    doc_kids[0].ns_prefix = "w";

    leptris_element_plan plans[3] = {};
    plans[0].element_name = "document";
    plans[0].child_count = 1;
    plans[0].child_plans = doc_kids;
    plans[0].ns_form = LEPTRIS_PLAN_NS_EXACT;
    plans[0].ns_uri = "urn:w";
    plans[0].ns_prefix = "w";
    plans[1].element_name = "body";
    plans[1].child_count = 1;
    plans[1].child_plans = body_kids;
    plans[1].ns_form = LEPTRIS_PLAN_NS_EXACT;
    plans[1].ns_uri = "urn:w";
    plans[2].element_name = "p";
    plans[2].child_count = 1;
    plans[2].child_plans = p_kids;
    plans[2].ns_form = LEPTRIS_PLAN_NS_EXACT;
    plans[2].ns_uri = "urn:w";

    leptris_plan_spec spec = {};
    spec.abi_version = leptris_plan_abi_version();
    spec.plan_count = 3;
    spec.plans = plans;

    LeptrisPlan plan = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan, nullptr);
    LeptrisPlanResult r = leptris_plan_walk(doc, leptris_document_root(doc), plan, &st);
    ASSERT_NE(r, nullptr);

    char* out = leptris_plan_serialize(plan, r, &st);
    ASSERT_NE(out, nullptr);
    /* Byte parity: prefixed names, declaration exactly once (on
     * the root, even though four elements share the prefix). */
    EXPECT_STREQ(out, xml);
    leptris_free_string(out);
    leptris_plan_result_free(r);
    leptris_plan_free(plan);
    leptris_document_free(doc);
}

TEST(Plan1551, TwoPrefixesDeclareInFirstEncounterOrder) {
    LeptrisStatus st = LEPTRIS_OK;
    const char* xml =
        "<a:root xmlns:a=\"urn:a\"><a:x><b:y xmlns:b=\"urn:b\">1</b:y>"
        "</a:x></a:root>";
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    leptris_child_plan x_kids[1] = {};
    x_kids[0].wire_name = "y";
    x_kids[0].kind = LEPTRIS_PLAN_KIND_SCALAR;
    x_kids[0].type_tag = 2;
    x_kids[0].child_plan_index = -1;
    x_kids[0].ns_form = LEPTRIS_PLAN_NS_EXACT;
    x_kids[0].ns_uri = "urn:b";
    x_kids[0].ns_prefix = "b";

    leptris_child_plan root_kids[1] = {};
    root_kids[0].wire_name = "x";
    root_kids[0].kind = LEPTRIS_PLAN_KIND_NESTED;
    root_kids[0].type_tag = 1;
    root_kids[0].child_plan_index = 1;
    root_kids[0].ns_prefix = "a";

    leptris_element_plan plans[2] = {};
    plans[0].element_name = "root";
    plans[0].child_count = 1;
    plans[0].child_plans = root_kids;
    plans[0].ns_form = LEPTRIS_PLAN_NS_EXACT;
    plans[0].ns_uri = "urn:a";
    plans[0].ns_prefix = "a";
    plans[1].element_name = "x";
    plans[1].child_count = 1;
    plans[1].child_plans = x_kids;
    plans[1].ns_form = LEPTRIS_PLAN_NS_EXACT;
    plans[1].ns_uri = "urn:a";

    leptris_plan_spec spec = {};
    spec.abi_version = leptris_plan_abi_version();
    spec.plan_count = 2;
    spec.plans = plans;

    LeptrisPlan plan = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan, nullptr);
    LeptrisPlanResult r = leptris_plan_walk(doc, leptris_document_root(doc), plan, &st);
    ASSERT_NE(r, nullptr);

    char* out = leptris_plan_serialize(plan, r, &st);
    ASSERT_NE(out, nullptr);
    /* Both declarations hoisted to the root, a (first encounter)
     * before b; inner xmlns:b duplicated NOT emitted. */
    EXPECT_STREQ(out,
                 "<a:root xmlns:a=\"urn:a\" xmlns:b=\"urn:b\">"
                 "<a:x><b:y>1</b:y></a:x></a:root>");
    leptris_free_string(out);
    leptris_plan_result_free(r);
    leptris_plan_free(plan);
    leptris_document_free(doc);
}

TEST(Plan1551, AttrRowsEmitPrefixedWireNames) {
    LeptrisStatus st = LEPTRIS_OK;
    const char* xml = "<w:p xmlns:w=\"urn:w\" w:val=\"1\"/>";
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    leptris_attr_plan attrs[1] = {};
    attrs[0].wire_name = "val";
    attrs[0].kind = LEPTRIS_PLAN_KIND_SCALAR;
    attrs[0].type_tag = 9;
    attrs[0].ns_form = LEPTRIS_PLAN_NS_EXACT;
    attrs[0].ns_uri = "urn:w";
    attrs[0].ns_prefix = "w";

    leptris_element_plan plans[1] = {};
    plans[0].element_name = "p";
    plans[0].attribute_count = 1;
    plans[0].attribute_plans = attrs;
    plans[0].ns_form = LEPTRIS_PLAN_NS_EXACT;
    plans[0].ns_uri = "urn:w";
    plans[0].ns_prefix = "w";

    leptris_plan_spec spec = {};
    spec.abi_version = leptris_plan_abi_version();
    spec.plan_count = 1;
    spec.plans = plans;

    LeptrisPlan plan = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan, nullptr);
    LeptrisPlanResult r = leptris_plan_walk(doc, leptris_document_root(doc), plan, &st);
    ASSERT_NE(r, nullptr);

    char* out = leptris_plan_serialize(plan, r, &st);
    ASSERT_NE(out, nullptr);
    /* The #1486 row matched by (URI, local); serialization emits
     * the plan-chosen prefix spelling. */
    EXPECT_STREQ(out, "<w:p xmlns:w=\"urn:w\" w:val=\"1\"/>");
    leptris_free_string(out);
    leptris_plan_result_free(r);
    leptris_plan_free(plan);
    leptris_document_free(doc);
}

TEST(Plan1551, UnprefixedPlanSerializesBare) {
    LeptrisStatus st = LEPTRIS_OK;
    const char* xml = "<document><body>plain</body></document>";
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    leptris_child_plan kids[1] = {};
    kids[0].wire_name = "body";
    kids[0].kind = LEPTRIS_PLAN_KIND_SCALAR;
    kids[0].type_tag = 1;
    kids[0].child_plan_index = -1;

    leptris_element_plan plans[1] = {};
    plans[0].element_name = "document";
    plans[0].child_count = 1;
    plans[0].child_plans = kids;

    leptris_plan_spec spec = {};
    spec.abi_version = leptris_plan_abi_version();
    spec.plan_count = 1;
    spec.plans = plans;

    LeptrisPlan plan = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan, nullptr);
    LeptrisPlanResult r = leptris_plan_walk(doc, leptris_document_root(doc), plan, &st);
    ASSERT_NE(r, nullptr);

    char* out = leptris_plan_serialize(plan, r, &st);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, xml);
    leptris_free_string(out);
    leptris_plan_result_free(r);
    leptris_plan_free(plan);
    leptris_document_free(doc);
}

TEST(Plan1551, SerializeRoundTripsThroughTheWalk) {
    LeptrisStatus st = LEPTRIS_OK;
    const char* xml =
        "<w:document xmlns:w=\"urn:w\"><w:body><w:p><w:t>x</w:t>"
        "</w:p></w:body></w:document>";
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    leptris_child_plan p_kids[1] = {};
    p_kids[0].wire_name = "t";
    p_kids[0].kind = LEPTRIS_PLAN_KIND_SCALAR;
    p_kids[0].type_tag = 3;
    p_kids[0].child_plan_index = -1;
    p_kids[0].ns_form = LEPTRIS_PLAN_NS_EXACT;
    p_kids[0].ns_uri = "urn:w";
    p_kids[0].ns_prefix = "w";

    leptris_child_plan body_kids[1] = {};
    body_kids[0].wire_name = "p";
    body_kids[0].kind = LEPTRIS_PLAN_KIND_NESTED;
    body_kids[0].type_tag = 2;
    body_kids[0].child_plan_index = 2;
    body_kids[0].ns_prefix = "w";

    leptris_child_plan doc_kids[1] = {};
    doc_kids[0].wire_name = "body";
    doc_kids[0].kind = LEPTRIS_PLAN_KIND_NESTED;
    doc_kids[0].type_tag = 1;
    doc_kids[0].child_plan_index = 1;
    doc_kids[0].ns_prefix = "w";

    leptris_element_plan plans[3] = {};
    plans[0].element_name = "document";
    plans[0].child_count = 1;
    plans[0].child_plans = doc_kids;
    plans[0].ns_form = LEPTRIS_PLAN_NS_EXACT;
    plans[0].ns_uri = "urn:w";
    plans[0].ns_prefix = "w";
    plans[1].element_name = "body";
    plans[1].child_count = 1;
    plans[1].child_plans = body_kids;
    plans[1].ns_form = LEPTRIS_PLAN_NS_EXACT;
    plans[1].ns_uri = "urn:w";
    plans[2].element_name = "p";
    plans[2].child_count = 1;
    plans[2].child_plans = p_kids;
    plans[2].ns_form = LEPTRIS_PLAN_NS_EXACT;
    plans[2].ns_uri = "urn:w";

    leptris_plan_spec spec = {};
    spec.abi_version = leptris_plan_abi_version();
    spec.plan_count = 3;
    spec.plans = plans;

    LeptrisPlan plan = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan, nullptr);
    LeptrisPlanResult r = leptris_plan_walk(doc, leptris_document_root(doc), plan, &st);
    ASSERT_NE(r, nullptr);
    char* out = leptris_plan_serialize(plan, r, &st);
    ASSERT_NE(out, nullptr);
    leptris_plan_result_free(r);
    leptris_document_free(doc);

    /* Re-parse the serialization and re-walk: the plan's EXACT
     * forms read the prefixed wire, values survive. */
    LeptrisDocument doc2 = leptris_parse_string(out, strlen(out), &st);
    leptris_free_string(out);
    ASSERT_NE(doc2, nullptr);
    LeptrisPlanResult r2 = leptris_plan_walk(doc2, leptris_document_root(doc2), plan, &st);
    ASSERT_NE(r2, nullptr);
    LeptrisPlanResult body = leptris_plan_value_at(r2, 0);
    ASSERT_NE(body, nullptr);
    LeptrisPlanResult p = leptris_plan_value_at(body, 0);
    ASSERT_NE(p, nullptr);
    EXPECT_STREQ(leptris_plan_value_string(leptris_plan_value_at(p, 0)), "x");
    leptris_plan_result_free(r2);
    leptris_plan_free(plan);
    leptris_document_free(doc2);
}

/* Issue #1565: a CONTENT row inside a NESTED child plan serialized
 * as an empty-named wrapper element (<w:item><>text</></w:item>).
 * Content rows emit their text inline at every nesting level. */
TEST(Plan1565, NestedContentRowEmitsInlineText) {
    LeptrisStatus st = LEPTRIS_OK;
    const char* xml =
        "<r xmlns:w=\"urn:w\"><w:item>text</w:item></r>";
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    leptris_child_plan item_row[1] = {};
    item_row[0].wire_name = "item";
    item_row[0].kind = LEPTRIS_PLAN_KIND_NESTED;
    item_row[0].child_plan_index = 1;
    item_row[0].ns_form = LEPTRIS_PLAN_NS_EXACT;
    item_row[0].ns_uri = "urn:w";
    item_row[0].ns_prefix = "w";

    leptris_child_plan content_row[1] = {};
    content_row[0].wire_name = "";
    content_row[0].kind = LEPTRIS_PLAN_KIND_CONTENT;
    content_row[0].type_tag = 2;
    content_row[0].child_plan_index = -1;

    leptris_element_plan plans[2] = {};
    plans[0].element_name = "r";
    plans[0].child_count = 1;
    plans[0].child_plans = item_row;
    plans[0].ns_form = LEPTRIS_PLAN_NS_EXACT;
    plans[0].ns_uri = "urn:w";
    plans[0].ns_prefix = "w";
    plans[1].element_name = "item";
    plans[1].child_count = 1;
    plans[1].child_plans = content_row;
    plans[1].flags = LEPTRIS_PLAN_FLAG_MIXED_CONTENT;
    plans[1].ns_form = LEPTRIS_PLAN_NS_EXACT;
    plans[1].ns_uri = "urn:w";
    plans[1].ns_prefix = "w";

    leptris_plan_spec spec = {};
    spec.abi_version = leptris_plan_abi_version();
    spec.plan_count = 2;
    spec.plans = plans;

    LeptrisPlan plan = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan, nullptr);
    LeptrisPlanResult r =
        leptris_plan_walk(doc, leptris_document_root(doc), plan, &st);
    ASSERT_NE(r, nullptr);
    char* out = leptris_plan_serialize(plan, r, &st);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out, "<w:r xmlns:w=\"urn:w\"><w:item>text</w:item></w:r>");
    leptris_free_string(out);
    leptris_plan_result_free(r);
    leptris_plan_free(plan);
    leptris_document_free(doc);
}

TEST(Plan1551, TextAndAttributeValuesAreEscaped) {
    LeptrisStatus st = LEPTRIS_OK;
    const char* xml =
        "<w:t xmlns:w=\"urn:w\" w:k=\"a&lt;b\">1&lt;2&amp;3</w:t>";
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    leptris_attr_plan attrs[1] = {};
    attrs[0].wire_name = "k";
    attrs[0].kind = LEPTRIS_PLAN_KIND_SCALAR;
    attrs[0].type_tag = 1;
    attrs[0].ns_form = LEPTRIS_PLAN_NS_EXACT;
    attrs[0].ns_uri = "urn:w";
    attrs[0].ns_prefix = "w";

    leptris_child_plan kids[1] = {};
    kids[0].wire_name = "#text";
    kids[0].kind = LEPTRIS_PLAN_KIND_CONTENT;
    kids[0].type_tag = 2;
    kids[0].child_plan_index = -1;

    leptris_element_plan plans[1] = {};
    plans[0].element_name = "t";
    plans[0].attribute_count = 1;
    plans[0].attribute_plans = attrs;
    plans[0].child_count = 1;
    plans[0].child_plans = kids;
    plans[0].flags = LEPTRIS_PLAN_FLAG_MIXED_CONTENT;
    plans[0].ns_form = LEPTRIS_PLAN_NS_EXACT;
    plans[0].ns_uri = "urn:w";
    plans[0].ns_prefix = "w";

    leptris_plan_spec spec = {};
    spec.abi_version = leptris_plan_abi_version();
    spec.plan_count = 1;
    spec.plans = plans;

    LeptrisPlan plan = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan, nullptr);
    LeptrisPlanResult r = leptris_plan_walk(doc, leptris_document_root(doc), plan, &st);
    ASSERT_NE(r, nullptr);
    char* out = leptris_plan_serialize(plan, r, &st);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out,
                 "<w:t xmlns:w=\"urn:w\" w:k=\"a&lt;b\">1&lt;2&amp;3</w:t>");
    leptris_free_string(out);
    leptris_plan_result_free(r);
    leptris_plan_free(plan);
    leptris_document_free(doc);
}

TEST(Plan1551, ChildrenEmitInDocumentOrderAcrossRows) {
    LeptrisStatus st = LEPTRIS_OK;
    const char* xml = "<w:b xmlns:w=\"urn:w\"><w:p>first</w:p>"
                      "<w:q>second</w:q></w:b>";
    LeptrisDocument doc = leptris_parse_string(xml, strlen(xml), &st);
    ASSERT_NE(doc, nullptr);

    /* Row order deliberately reversed vs document order. */
    leptris_child_plan kids[2] = {};
    kids[0].wire_name = "q";
    kids[0].kind = LEPTRIS_PLAN_KIND_SCALAR;
    kids[0].type_tag = 2;
    kids[0].child_plan_index = -1;
    kids[0].ns_form = LEPTRIS_PLAN_NS_EXACT;
    kids[0].ns_uri = "urn:w";
    kids[0].ns_prefix = "w";
    kids[1].wire_name = "p";
    kids[1].kind = LEPTRIS_PLAN_KIND_SCALAR;
    kids[1].type_tag = 1;
    kids[1].child_plan_index = -1;
    kids[1].ns_form = LEPTRIS_PLAN_NS_EXACT;
    kids[1].ns_uri = "urn:w";
    kids[1].ns_prefix = "w";

    leptris_element_plan plans[1] = {};
    plans[0].element_name = "b";
    plans[0].child_count = 2;
    plans[0].child_plans = kids;
    plans[0].ns_form = LEPTRIS_PLAN_NS_EXACT;
    plans[0].ns_uri = "urn:w";
    plans[0].ns_prefix = "w";

    leptris_plan_spec spec = {};
    spec.abi_version = leptris_plan_abi_version();
    spec.plan_count = 1;
    spec.plans = plans;

    LeptrisPlan plan = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan, nullptr);
    LeptrisPlanResult r = leptris_plan_walk(doc, leptris_document_root(doc), plan, &st);
    ASSERT_NE(r, nullptr);
    char* out = leptris_plan_serialize(plan, r, &st);
    ASSERT_NE(out, nullptr);
    EXPECT_STREQ(out,
                 "<w:b xmlns:w=\"urn:w\"><w:p>first</w:p>"
                 "<w:q>second</w:q></w:b>");
    leptris_free_string(out);
    leptris_plan_result_free(r);
    leptris_plan_free(plan);
    leptris_document_free(doc);
}

// ---- #1560: unqualified-only ns form -------------------------------
// UNQUALIFIED matches the WRITTEN spelling: a child row binds
// children with no prefix regardless of the effective namespace
// URI, and refuses prefixed spellings. An unprefixed child binds
// under both a namespace-less document and a default-xmlns
// document (whose children inherit the default namespace — NS_NONE
// drops them); a p:child never binds (ANY would accept it — the
// superset lutaml-model#932 had to settle for). Round-trip through
// the serializer agrees on all three shapes: the serializer emits
// prefix declarations only, so a default-ns child serializes bare —
// keying on the spelling, not the URI, is what keeps the re-walk
// in agreement.
TEST(Plan1560, UnqualifiedNsFormBindsNoPrefixSpellings) {
    LeptrisStatus st = LEPTRIS_OK;

    leptris_child_plan kids[1] = {};
    kids[0].wire_name = "child";
    kids[0].kind = LEPTRIS_PLAN_KIND_SCALAR;
    kids[0].type_tag = 1;
    kids[0].child_plan_index = -1;
    kids[0].ns_form = LEPTRIS_PLAN_NS_UNQUALIFIED;
    leptris_element_plan plans[1] = {};
    plans[0].element_name = "root";
    plans[0].ns_form = LEPTRIS_PLAN_NS_ANY;
    plans[0].child_count = 1;
    plans[0].child_plans = kids;
    leptris_plan_spec spec = {};
    spec.abi_version = leptris_plan_abi_version();
    spec.plan_count = 1;
    spec.plans = plans;
    LeptrisPlan plan = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan, nullptr);

    struct {
        const char* xml;
        const char* value; /* NULL = the row must NOT bind */
    } shapes[] = {
        {"<root><child>none</child></root>", "none"},
        {"<root xmlns=\"urn:d\"><child>default</child></root>", "default"},
        {"<root xmlns:p=\"urn:p\"><p:child>prefixed</p:child></root>", NULL},
    };
    for (const auto& sh : shapes) {
        SCOPED_TRACE(sh.xml);
        LeptrisDocument doc =
            leptris_parse_string(sh.xml, strlen(sh.xml), &st);
        ASSERT_NE(doc, nullptr);
        LeptrisElement root = leptris_document_root(doc);
        LeptrisPlanResult r = leptris_plan_walk(doc, root, plan, &st);
        ASSERT_NE(r, nullptr);
        if (sh.value == NULL) {
            EXPECT_EQ(leptris_plan_value_count(r), 0u);
        } else {
            ASSERT_EQ(leptris_plan_value_count(r), 1u);
            EXPECT_STREQ(leptris_plan_value_string(
                             leptris_plan_value_at(r, 0)),
                         sh.value);
        }
        /* Round-trip: serialize the walk, re-parse, re-walk — the
         * bound values agree on every shape. */
        char* out = leptris_plan_serialize(plan, r, &st);
        leptris_plan_result_free(r);
        ASSERT_NE(out, nullptr);
        LeptrisDocument doc2 = leptris_parse_string(out, strlen(out), &st);
        leptris_free_string(out);
        ASSERT_NE(doc2, nullptr);
        LeptrisPlanResult r2 =
            leptris_plan_walk(doc2, leptris_document_root(doc2), plan, &st);
        ASSERT_NE(r2, nullptr);
        if (sh.value == NULL) {
            EXPECT_EQ(leptris_plan_value_count(r2), 0u);
        } else {
            ASSERT_EQ(leptris_plan_value_count(r2), 1u);
            EXPECT_STREQ(leptris_plan_value_string(
                             leptris_plan_value_at(r2, 0)),
                         sh.value);
        }
        leptris_plan_result_free(r2);
        leptris_document_free(doc2);
        leptris_document_free(doc);
    }
    leptris_plan_free(plan);
}

// ---- #1563: nested attr parity -------------------------------------
// The report: a two-level plan whose root carries one exact-URI
// attribute row drops the CHILD's attribute capture — the child's
// PlanValue answers attribute("name") nil for a prefixed w:name
// while the same child plan compiled as ROOT binds it. This is the
// A/B discriminator: one child plan (plain unset-form attr row, the
// reporter's shape; ANY-form as the binding control), walked BOTH
// ways. Whatever the outcome, the two paths must AGREE.
TEST(Plan1563, PlainAttrRowAgreesNestedVsRoot) {
    LeptrisStatus st = LEPTRIS_OK;

    leptris_attr_plan root_attrs[1] = {};
    root_attrs[0].wire_name = "Ignorable";
    root_attrs[0].kind = LEPTRIS_PLAN_KIND_SCALAR;
    root_attrs[0].type_tag = 1;
    root_attrs[0].ns_form = LEPTRIS_PLAN_NS_EXACT;
    root_attrs[0].ns_uri = "urn:mc";
    root_attrs[0].ns_prefix = "mc";

    leptris_child_plan font_row[1] = {};
    font_row[0].wire_name = "font";
    font_row[0].kind = LEPTRIS_PLAN_KIND_NESTED;
    font_row[0].child_plan_index = 1;
    font_row[0].ns_form = LEPTRIS_PLAN_NS_EXACT;
    font_row[0].ns_uri = "urn:w";
    font_row[0].ns_prefix = "w";

    /* The reporter's child row: PLAIN wire_name, unset ns_form. */
    leptris_attr_plan font_attrs_plain[1] = {};
    font_attrs_plain[0].wire_name = "name";
    font_attrs_plain[0].kind = LEPTRIS_PLAN_KIND_SCALAR;
    font_attrs_plain[0].type_tag = 1;

    /* Control: ANY form with the LOCAL name must bind w:name. */
    leptris_attr_plan font_attrs_any[1] = {};
    font_attrs_any[0].wire_name = "name";
    font_attrs_any[0].kind = LEPTRIS_PLAN_KIND_SCALAR;
    font_attrs_any[0].type_tag = 1;
    font_attrs_any[0].ns_form = LEPTRIS_PLAN_NS_ANY;
    font_attrs_any[0].pad_ns = 1;

    leptris_element_plan nested_plans[2] = {};
    nested_plans[0].element_name = "fonts";
    nested_plans[0].attribute_count = 1;
    nested_plans[0].attribute_plans = root_attrs;
    nested_plans[0].child_count = 1;
    nested_plans[0].child_plans = font_row;
    nested_plans[0].ns_form = LEPTRIS_PLAN_NS_EXACT;
    nested_plans[0].ns_uri = "urn:w";
    nested_plans[0].ns_prefix = "w";
    nested_plans[1].element_name = "font";
    nested_plans[1].attribute_count = 1;
    nested_plans[1].attribute_plans = font_attrs_plain;
    nested_plans[1].ns_form = LEPTRIS_PLAN_NS_EXACT;
    nested_plans[1].ns_uri = "urn:w";
    nested_plans[1].ns_prefix = "w";

    leptris_plan_spec nested_spec = {};
    nested_spec.abi_version = leptris_plan_abi_version();
    nested_spec.plan_count = 2;
    nested_spec.plans = nested_plans;
    LeptrisPlan nested_plan = leptris_plan_build(&nested_spec, &st);
    ASSERT_NE(nested_plan, nullptr);

    /* Root walk of the SAME child plan (reporter's standalone doc). */
    leptris_element_plan root_plans[1] = {};
    root_plans[0] = nested_plans[1];
    leptris_plan_spec root_spec = {};
    root_spec.abi_version = leptris_plan_abi_version();
    root_spec.plan_count = 1;
    root_spec.plans = root_plans;
    LeptrisPlan root_plan = leptris_plan_build(&root_spec, &st);
    ASSERT_NE(root_plan, nullptr);

    const char* nested_xml =
        "<w:fonts xmlns:w=\"urn:w\" xmlns:mc=\"urn:mc\""
        " mc:Ignorable=\"w14\"><w:font w:name=\"Arial\"/></w:fonts>";
    const char* root_xml =
        "<w:font xmlns:w=\"urn:w\" w:name=\"Arial\"/>";

    LeptrisDocument doc = leptris_parse_string(nested_xml, strlen(nested_xml), &st);
    ASSERT_NE(doc, nullptr);
    LeptrisPlanResult r = leptris_plan_walk(doc, leptris_document_root(doc), nested_plan, &st);
    ASSERT_NE(r, nullptr);
    ASSERT_EQ(leptris_plan_value_count(r), 1u);
    LeptrisPlanResult font_nested = leptris_plan_value_at(r, 0);
    ASSERT_NE(font_nested, nullptr);
    const char* nested_plain = leptris_plan_value_attribute(font_nested, "name");
    leptris_plan_result_free(r);
    leptris_document_free(doc);

    LeptrisDocument doc2 = leptris_parse_string(root_xml, strlen(root_xml), &st);
    ASSERT_NE(doc2, nullptr);
    LeptrisPlanResult r2 = leptris_plan_walk(doc2, leptris_document_root(doc2), root_plan, &st);
    ASSERT_NE(r2, nullptr);
    /* The walk root carries no row stamp — its attrs answer through
     * the plan's rows. */
    const char* root_plain = leptris_plan_value_attribute(r2, "name");
    leptris_plan_result_free(r2);
    leptris_document_free(doc2);

    RecordProperty("nested_plain_nil", nested_plain ? "0" : "1");
    RecordProperty("root_plain_nil", root_plain ? "0" : "1");
    /* The two paths must agree, whichever semantic that is. */
    ASSERT_EQ(nested_plain == nullptr, root_plain == nullptr)
        << "nested=" << (nested_plain ? nested_plain : "(nil)")
        << " root=" << (root_plain ? root_plain : "(nil)");

    /* Control: ANY-form rows bind w:name BOTH ways. */
    nested_plans[1].attribute_plans = font_attrs_any;
    LeptrisPlan nested_any = leptris_plan_build(&nested_spec, &st);
    ASSERT_NE(nested_any, nullptr);
    LeptrisDocument doc3 = leptris_parse_string(nested_xml, strlen(nested_xml), &st);
    ASSERT_NE(doc3, nullptr);
    LeptrisPlanResult r3 = leptris_plan_walk(doc3, leptris_document_root(doc3), nested_any, &st);
    ASSERT_NE(r3, nullptr);
    LeptrisPlanResult font_any = leptris_plan_value_at(r3, 0);
    ASSERT_NE(font_any, nullptr);
    EXPECT_STREQ(leptris_plan_value_attribute(font_any, "name"), "Arial");
    leptris_plan_result_free(r3);
    leptris_document_free(doc3);
    leptris_plan_free(nested_any);
    leptris_plan_free(root_plan);
    leptris_plan_free(nested_plan);
}

// ---- #1585: child-row exact-URI ns_uri retention -------------------
// The build copied child ns_prefix but left ns_uri pointing at the
// CALLER's buffer — once the binding's build anchors are GC'd the
// row dangles and silently stops matching. Retention is observable
// without a GC: trash the caller's buffer after build; the row
// must still bind.
TEST(Plan1585, ChildRowNsUriIsRetained) {
    LeptrisStatus st = LEPTRIS_OK;
    char ns_uri_buf[64];
    strcpy(ns_uri_buf, "http://purl.org/dc/elements/1.1/");

    leptris_child_plan kids[2] = {};
    kids[0].wire_name = "c";
    kids[0].kind = LEPTRIS_PLAN_KIND_SCALAR;
    kids[0].type_tag = 1;
    kids[0].child_plan_index = -1;
    kids[0].ns_form = LEPTRIS_PLAN_NS_EXACT;
    kids[0].ns_uri = ns_uri_buf;
    kids[0].ns_prefix = "d";
    kids[1].wire_name = "p";
    kids[1].kind = LEPTRIS_PLAN_KIND_SCALAR;
    kids[1].type_tag = 2;
    kids[1].child_plan_index = -1;
    leptris_element_plan plans[1] = {};
    plans[0].element_name = "r";
    plans[0].ns_form = LEPTRIS_PLAN_NS_ANY;
    plans[0].child_count = 2;
    plans[0].child_plans = kids;
    leptris_plan_spec spec = {};
    spec.abi_version = leptris_plan_abi_version();
    spec.plan_count = 1;
    spec.plans = plans;
    LeptrisPlan plan = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan, nullptr);

    /* Simulate the GC reclaiming the build anchor: the spec buffer
     * is now garbage. */
    memset(ns_uri_buf, 'X', sizeof(ns_uri_buf) - 1);

    LeptrisDocument doc = parse(
        "<r xmlns:d=\"http://purl.org/dc/elements/1.1/\">"
        "<d:c>x</d:c><p>y</p></r>");
    ASSERT_NE(doc, nullptr);
    LeptrisPlanResult r =
        leptris_plan_walk(doc, leptris_document_root(doc), plan, &st);
    ASSERT_NE(r, nullptr);
    ASSERT_EQ(leptris_plan_value_count(r), 2u);
    EXPECT_STREQ(leptris_plan_value_string(leptris_plan_value_at(r, 0)),
                 "x")
        << "exact-URI child row lost its ns_uri";
    EXPECT_STREQ(leptris_plan_value_string(leptris_plan_value_at(r, 1)),
                 "y");
    leptris_plan_result_free(r);
    leptris_plan_free(plan);
    leptris_document_free(doc);
}

// ---- #1586: plain attribute rows match any qualification -----------
// The plan-path mirror of #758 (exact wire spelling first, then any
// qualification by local name) — a plain attr row must bind w:name
// the same way the interpretive path does, at top level and nested.
TEST(Plan1586, PlainAttrRowLenientSpelling) {
    LeptrisStatus st = LEPTRIS_OK;
    const char* xml = "<r xmlns:w=\"http://w/1\" w:top=\"T\"/>";

    leptris_attr_plan attrs[1] = {};
    attrs[0].wire_name = "top";
    attrs[0].kind = LEPTRIS_PLAN_KIND_SCALAR;
    attrs[0].type_tag = 1;
    leptris_element_plan plans[1] = {};
    plans[0].element_name = "r";
    plans[0].attribute_count = 1;
    plans[0].attribute_plans = attrs;
    plans[0].flags = LEPTRIS_PLAN_FLAG_NS_LENIENT;
    leptris_plan_spec spec = {};
    spec.abi_version = leptris_plan_abi_version();
    spec.plan_count = 1;
    spec.plans = plans;
    LeptrisPlan plan = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan, nullptr);

    LeptrisDocument doc = parse(xml);
    ASSERT_NE(doc, nullptr);
    LeptrisPlanResult r =
        leptris_plan_walk(doc, leptris_document_root(doc), plan, &st);
    ASSERT_NE(r, nullptr);
    EXPECT_STREQ(leptris_plan_value_attribute(r, "top"), "T");
    leptris_plan_result_free(r);
    leptris_document_free(doc);
    leptris_plan_free(plan);
}

TEST(Plan1586, PlainAttrRowLenientUnderNestedRow) {
    LeptrisStatus st = LEPTRIS_OK;
    const char* xml = "<r xmlns:w=\"http://w/1\"><w:c w:n=\"N\"/></r>";

    leptris_attr_plan c_attrs[1] = {};
    c_attrs[0].wire_name = "n";
    c_attrs[0].kind = LEPTRIS_PLAN_KIND_SCALAR;
    c_attrs[0].type_tag = 1;
    leptris_child_plan c_row = {};
    c_row.wire_name = "c";
    c_row.kind = LEPTRIS_PLAN_KIND_NESTED;
    c_row.child_plan_index = 1;
    c_row.ns_form = LEPTRIS_PLAN_NS_ANY;
    leptris_element_plan plans[2] = {};
    plans[0].element_name = "r";
    plans[0].child_count = 1;
    plans[0].child_plans = &c_row;
    plans[1].element_name = "c";
    plans[1].attribute_count = 1;
    plans[1].attribute_plans = c_attrs;
    plans[1].flags = LEPTRIS_PLAN_FLAG_NS_LENIENT;
    leptris_plan_spec spec = {};
    spec.abi_version = leptris_plan_abi_version();
    spec.plan_count = 2;
    spec.plans = plans;
    LeptrisPlan plan = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan, nullptr);

    LeptrisDocument doc = parse(xml);
    ASSERT_NE(doc, nullptr);
    LeptrisPlanResult r =
        leptris_plan_walk(doc, leptris_document_root(doc), plan, &st);
    ASSERT_NE(r, nullptr);
    ASSERT_EQ(leptris_plan_value_count(r), 1u);
    LeptrisPlanResult child = leptris_plan_value_at(r, 0);
    ASSERT_NE(child, nullptr);
    EXPECT_STREQ(leptris_plan_value_attribute(child, "n"), "N");
    leptris_plan_result_free(r);
    leptris_document_free(doc);
    leptris_plan_free(plan);
}

TEST(Plan1586, ExactUriRowStillWinsAndUnqualifiedStillBinds) {
    LeptrisStatus st = LEPTRIS_OK;
    /* exact-URI row keeps its #1486 semantics; unqualified wire
     * attr binds a plain row by the literal spelling. */
    const char* xml = "<r xmlns:w=\"http://w/1\" w:top=\"T\" top=\"U\"/>";
    leptris_attr_plan exact[1] = {};
    exact[0].wire_name = "top";
    exact[0].kind = LEPTRIS_PLAN_KIND_SCALAR;
    exact[0].type_tag = 1;
    exact[0].ns_form = LEPTRIS_PLAN_NS_EXACT;
    exact[0].ns_uri = "http://w/1";
    leptris_element_plan plans[1] = {};
    plans[0].element_name = "r";
    plans[0].attribute_count = 1;
    plans[0].attribute_plans = exact;
    leptris_plan_spec spec = {};
    spec.abi_version = leptris_plan_abi_version();
    spec.plan_count = 1;
    spec.plans = plans;
    LeptrisPlan plan = leptris_plan_build(&spec, &st);
    ASSERT_NE(plan, nullptr);
    LeptrisDocument doc = parse(xml);
    ASSERT_NE(doc, nullptr);
    LeptrisPlanResult r =
        leptris_plan_walk(doc, leptris_document_root(doc), plan, &st);
    ASSERT_NE(r, nullptr);
    /* The qualified spelling carries "T"; the exact row must take
     * it, not the unqualified "U". */
    EXPECT_STREQ(leptris_plan_value_attribute(r, "top"), "T");
    leptris_plan_result_free(r);
    leptris_document_free(doc);
    leptris_plan_free(plan);
}
