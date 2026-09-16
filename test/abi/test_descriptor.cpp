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

TEST(PlanBuild, CopiesTheSpecStrings) {
    LeptrisDocument doc = parse("<doc><t>v</t></doc>");
    ASSERT_NE(doc, nullptr);

    /* Heap spec freed right after build — the pool must own copies. */
    char name_buf[] = "t";
    leptris_child_plan* kids =
        (leptris_child_plan*)malloc(sizeof(leptris_child_plan));
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
