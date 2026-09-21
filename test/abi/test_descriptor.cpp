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
