// test/xsd/test_xsd_ns_qualification.cpp — leptris-ruby#679: the XSD
// compiler must enforce targetNamespace qualification on validated
// documents. A schema with a targetNamespace requires elements in
// that namespace: the root-resolution ladder must bind the global
// declaration whose {target namespace, name} pair equals the
// instance element's (namespaceURI, localName) — never fall back
// past a namespace mismatch — and form-qualified declarations must
// see the namespace (XSD 1.0 §3.3.4/§3.2.3, C.1/C.2):
//   - a global element declaration is ALWAYS qualified with the
//     schema's targetNamespace (absent tns -> no namespace);
//   - a local element/attribute particle is qualified or not per
//     elementFormDefault/attributeFormDefault (or its own form).
#include <gtest/gtest.h>

#include <string>

#include "leptris.h"

namespace {

const char* kDir = LEPTRIS_XSD_NS_QUAL_DIR;

std::string path(const char* name) {
    return std::string(kDir) + "/" + name;
}

int verdict_of(const char* schema, const char* instance) {
    LeptrisStatus st = (LeptrisStatus)0;
    LeptrisXsdSchema s = leptris_xsd_compile_file(path(schema).c_str(), &st);
    EXPECT_NE(s, (LeptrisXsdSchema)0) << schema << " must compile";
    EXPECT_EQ(st, LEPTRIS_OK) << schema << " compile status";
    if (!s) return -1;
    LeptrisStatus pst = (LeptrisStatus)0;
    LeptrisDocument d = leptris_parse_file(path(instance).c_str(), &pst);
    EXPECT_NE(d, (LeptrisDocument)0) << instance << " must parse";
    if (!d) {
        leptris_xsd_free(s);
        return -1;
    }
    int v = leptris_xsd_validate(s, d);
    leptris_document_free(d);
    leptris_xsd_free(s);
    return v;
}

}  // namespace

/* The report's cell: an instance document whose root carries NO
 * namespace must not validate against a targetNamespace schema with
 * qualified element declarations. */
TEST(XsdNsQualification, UnqualifiedRootAgainstTnsSchemaInvalid) {
    EXPECT_NE(verdict_of("tns_qualified.xsd", "tns_missing.xml"), 1);
}

/* A root namespaced differently than the targetNamespace is equally
 * foreign — the ladder must not fall back past the strict (ns, name)
 * pair (the po0/po1 disposition class). */
TEST(XsdNsQualification, ForeignNamespaceRootInvalid) {
    EXPECT_NE(verdict_of("tns_qualified.xsd", "tns_foreign.xml"), 1);
    /* same shape against a schema with NO targetNamespace: a
     * namespaced root binds nothing (the no-tns declaration names a
     * NO-namespace element) */
    EXPECT_NE(verdict_of("no_tns.xsd", "no_tns_foreign.xml"), 1);
}

/* The correctly-namespaced instance keeps validating. */
TEST(XsdNsQualification, QualifiedRootValid) {
    EXPECT_EQ(verdict_of("tns_qualified.xsd", "tns_ok.xml"), 1);
}

/* elementFormDefault="unqualified": LOCAL elements stay
 * unqualified — the unqualified instance children validate — while
 * the GLOBAL element still requires the target namespace. */
TEST(XsdNsQualification, UnqualifiedFormLocalsValidGlobalsRequireTns) {
    EXPECT_EQ(verdict_of("efd_unqualified.xsd", "efd_locals_ok.xml"), 1);
    EXPECT_NE(verdict_of("efd_unqualified.xsd", "efd_root_missing.xml"), 1);
}

/* attributeFormDefault="qualified": an unqualified instance
 * attribute is not the declared qualified-form attribute — the
 * strict walk must reject it — while the qualified spelling
 * validates (the ns0_1 disposition class). */
TEST(XsdNsQualification, QualifiedFormAttributeRequiresNamespace) {
    EXPECT_NE(verdict_of("afd_qualified.xsd", "afd_missing.xml"), 1);
    EXPECT_EQ(verdict_of("afd_qualified.xsd", "afd_ok.xml"), 1);
}

/* Guard against over-tightening: a schema with NO targetNamespace
 * keeps validating unqualified instances (an unprefixed root binds
 * the unstamped global declaration exactly). */
TEST(XsdNsQualification, NoTargetNamespaceSchemaStillValid) {
    EXPECT_EQ(verdict_of("no_tns.xsd", "no_tns_ok.xml"), 1);
}
