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
        /* Warmup pass, untimed: the leptris row runs first in this
         * bench, and its first-touch page faults (mut blocks + name
         * blocks, ~9 MB at 200k children) landed in the timed row
         * while pugixml's row ran second on a warm allocator — a
         * systematic order bias. Both sides get the same warmup
         * treatment; the timed rows below are steady-state. */
        LeptrisDocument d = leptris_document_create();
        LeptrisElement root = leptris_element_create(d, "root");
        leptris_document_set_root(d, root);
        for (int i = 0; i < N_APPEND; i++) {
            LeptrisElement c = leptris_element_create(d, "c");
            leptris_element_append_child(root, c);
        }
        leptris_document_free(d);
    }
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

    /* ---- mut-doc-cycle: 20k create/build/free cycles, 1024
     * children each (1 elem block + ~7 name blocks per cycle). The
     * mut-block recycle target: without it every cycle frees those
     * blocks back to malloc (madvise) and the next cycle re-mallocs
     * and re-faults them on the carve memsets. */
    const int N_CYCLE = 20000;
    const int CYCLE_CHILDREN = 1024;
    {
        double t0 = now_ns();
        for (int i = 0; i < N_CYCLE; i++) {
            LeptrisDocument d = leptris_document_create();
            LeptrisElement root = leptris_element_create(d, "root");
            leptris_document_set_root(d, root);
            for (int c = 0; c < CYCLE_CHILDREN; c++) {
                LeptrisElement e = leptris_element_create(d, "c");
                leptris_element_append_child(root, e);
            }
            leptris_document_free(d);
        }
        double t1 = now_ns();
        printf("mutcycle leptris: %7.1f ns/child\n",
               (t1 - t0) / ((double)N_CYCLE * CYCLE_CHILDREN));
    }
    {
        double t0 = now_ns();
        for (int i = 0; i < N_CYCLE; i++) {
            pugi::xml_document d;
            pugi::xml_node root = d.append_child("root");
            for (int c = 0; c < CYCLE_CHILDREN; c++)
                root.append_child("c");
        }
        double t1 = now_ns();
        printf("mutcycle pugixml: %7.1f ns/child\n",
               (t1 - t0) / ((double)N_CYCLE * CYCLE_CHILDREN));
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
        /* Door A (leptris#1436, shipped v1.9.272): the opt-in fast
         * path — duplicate detection and source positions off. The
         * default row above carries full conformance work pugixml
         * does none of; this row shows the speed callers can opt
         * into when their pipeline guarantees uniqueness and skips
         * position reporting. */
        LeptrisParseFlags flags = static_cast<LeptrisParseFlags>(
            LEPTRIS_PARSE_SKIP_DUP_DETECTION |
            LEPTRIS_PARSE_SKIP_SOURCE_POSITIONS);
        double t0 = now_ns();
        for (int i = 0; i < N_PARSE; i++) {
            LeptrisDocument d = leptris_parse_string_flags(
                xml.data(), xml.size(), flags, NULL);
            leptris_document_free(d);
        }
        double t1 = now_ns();
        printf("attrparse leptris+flags: %7.1f ns/doc (Door A)\n",
               (t1 - t0) / N_PARSE);
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
