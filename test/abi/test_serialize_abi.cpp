/* Issue #568 regression: LeptrisSerializeOptions is ABI-FROZEN.
 *
 * 1.9.1 appended fields (cdata-section-elements, html_method) to
 * the public struct. Every binding compiled against the 1.9.0
 * layout allocates the OLD size; the serializer then read the new
 * fields past the caller's allocation — a segfault in
 * is_cdata_element when the tail bytes were live pointers, and a
 * silently-dropped indent when they were not. The struct is now
 * pinned by _Static_assert in types.h; this spec proves the
 * serializer never reads past the frozen three fields, by calling
 * it through a poisoned old-layout buffer (exactly what cffi/FFI
 * produce when the tail holds other objects). */
#include <gtest/gtest.h>
extern "C" {
#include "leptris.h"
}
#include <cstring>
#include <cstdlib>
#include <string>

namespace {

/* The 1.9.0 layout every shipped binding has compiled in. */
typedef struct {
    int indent;
    int xml_declaration;
    const char* encoding;
} OldLayoutOptions;

TEST(SerializeAbi, FrozenOptionsLayout) {
    /* If this fails, the struct grew: move new settings behind the
     * extended-options entry instead (issue #568). */
    ASSERT_EQ(sizeof(LeptrisSerializeOptions), sizeof(OldLayoutOptions));
}

TEST(SerializeAbi, PoisonedTailOptionsDoNotCrash) {
    LeptrisDocument doc = leptris_parse_string(
        "<a><b>1</b><c>2</c></a>", strlen("<a><b>1</b><c>2</c></a>"),
        nullptr);
    ASSERT_NE(doc, nullptr);

    /* 1.9.0-sized allocation with poison past the frozen fields —
     * what a binding buffer looks like after reuse. 50 iterations:
     * the original crash reproduced in a loop. */
    for (int i = 0; i < 50; i++) {
        unsigned char* buf = (unsigned char*)malloc(48);
        ASSERT_NE(buf, nullptr);
        memset(buf, 0xAB + i, 48);   /* varying garbage incl. 0x00 stops */
        OldLayoutOptions* o = (OldLayoutOptions*)buf;
        o->indent = 2;
        o->xml_declaration = 0;
        o->encoding = (i % 2) ? "UTF-8" : nullptr;  /* both repro shapes */

        char* out = leptris_document_serialize(
            doc, (LeptrisSerializeOptions*)buf);
        ASSERT_NE(out, (char*)nullptr);
        /* The indent must survive too: the "silent indent loss"
         * half of #568. */
        EXPECT_NE(std::string(out).find("\n  <b>"), std::string::npos)
            << "iteration " << i;
        leptris_free_string(out);
        free(buf);
    }
    leptris_document_free(doc);
}

}  // namespace
