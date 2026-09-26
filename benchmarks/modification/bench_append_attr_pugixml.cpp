// bench_append_attr_pugixml — Lane 18 (#1179): DOM append and
// attr-parse throughput vs pugixml. The lane bar is append <=1x and
// attr-parse <=1x against pugixml on these shapes.
#include "leptris.h"
#include "pugixml.hpp"
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>

static double now_ns() {
    return (double)std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

int main(void) {
    const int N_APPEND = 200000;
    const int N_PARSE = 20000;

    /* ---- append: 200k element children on one root ---- */
    {
        double t0 = now_ns();
        LeptrisDocument d = leptris_document_create();
        LeptrisElement root = leptris_element_create(d, "root");
        leptris_document_set_root(d, root);
        for (int i = 0; i < N_APPEND; i++) {
            LeptrisElement c = leptris_element_create(d, "c");
            leptris_element_append_child(root, c);
        }
        double t1 = now_ns();
        printf("append leptris  : %8.1f ns/child\n",
               (t1 - t0) / N_APPEND);
        leptris_document_free(d);
    }
    {
        double t0 = now_ns();
        pugi::xml_document d;
        pugi::xml_node root = d.append_child("root");
        for (int i = 0; i < N_APPEND; i++)
            root.append_child("c");
        double t1 = now_ns();
        printf("append pugixml  : %8.1f ns/child\n",
               (t1 - t0) / N_APPEND);
    }

    /* ---- attr-parse: parse 20k docs, 4 elems x 16 attrs ---- */
    std::string xml = "<r>";
    for (int e = 0; e < 4; e++) {
        xml += "<e";
        for (int a = 0; a < 16; a++)
            xml += " a" + std::to_string(a) + "='value-" +
                   std::to_string(a) + "-xyz'";
        xml += "/>";
    }
    xml += "</r>";
    {
        double t0 = now_ns();
        for (int i = 0; i < N_PARSE; i++) {
            LeptrisDocument d =
                leptris_parse_string(xml.data(), xml.size(), NULL);
            leptris_document_free(d);
        }
        double t1 = now_ns();
        printf("attrparse leptris: %7.1f ns/doc (%zu bytes)\n",
               (t1 - t0) / N_PARSE, xml.size());
    }
    {
        double t0 = now_ns();
        for (int i = 0; i < N_PARSE; i++) {
            pugi::xml_document d;
            d.load_buffer((void*)xml.data(), xml.size());
        }
        double t1 = now_ns();
        printf("attrparse pugixml: %7.1f ns/doc\n",
               (t1 - t0) / N_PARSE);
    }
    return 0;
}
