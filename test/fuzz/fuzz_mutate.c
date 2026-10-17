/* test/fuzz/fuzz_mutate.c — libFuzzer harness for the MUTATION API
 * (approach ported from teptris's op-driven fuzz_dump): the input
 * bytes drive a deterministic builder/mutation sequence — element
 * creation, attribute and text edits, append/insert/remove, and
 * cross-document adoption — against three documents (a RECOVER
 * parse, a second parse, and a fresh programmatic one), then the
 * final trees serialize and re-parse. Invariants under ASan/UBSan:
 * no crash, no leak, every op result consumed; status errors from
 * rejected ops (cycles, foreign shapes) are fine — that IS the
 * contract. The mutation faces are where the adopt/copy and
 * sibling-chain bug classes lived (#1755, #1717, #1636). */
#include "leptris.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const char* const kNames[] = {
    "a", "b", "r", "item", "p", "x"};
#define MUT_ALPHA 6

static const char* name_at(const uint8_t* d, size_t sz, size_t i) {
    return sz ? kNames[d[i % sz] % MUT_ALPHA] : kNames[0];
}

int LLVMFuzzerTestOneInput(const uint8_t* data, size_t size) {
    if (size < 4 || size > (1u << 20)) return 0;

    /* First half: markup for two documents. Second half: ops. */
    size_t cut = 1 + (size % (size / 2 ? size / 2 : 1));
    if (cut >= size) cut = size / 2;
    const uint8_t* ops = data + cut;
    size_t nops = size - cut;
    if (nops > 256) nops = 256;

    LeptrisParseOptions opts;
    memset(&opts, 0, sizeof(opts));
    opts.recover = 1;
    LeptrisStatus st = LEPTRIS_OK;
    LeptrisDocument doc =
        leptris_parse_string_ex((const char*)data, cut, &opts, &st);
    if (!doc) return 0;
    LeptrisDocument doc2 = leptris_parse_string_ex(
        (const char*)data + cut, nops, &opts, &st);
    if (!doc2) {
        leptris_document_free(doc);
        return 0;
    }
    LeptrisDocument fresh = leptris_document_create();

    /* A stable pool of nodes to point ops at: the docs' roots, the
     * fresh doc's created children, and a few created elements. */
    enum { POOL = 8 };
    LeptrisElement pool[POOL] = {0};
    LeptrisElement root = leptris_document_root(doc);
    LeptrisElement root2 = leptris_document_root(doc2);
    if (root) pool[0] = root;
    if (root2) pool[1] = root2;
    if (root)
        for (int i = 2, j = 0; i < POOL && j < 3; j++) {
            LeptrisElement c = leptris_element_create(fresh, "c");
            if (!c) break;
            pool[i++] = c;
        }

    for (size_t k = 0; k + 1 < nops; k += 2) {
        unsigned op = ops[k] % 10u;
        LeptrisElement tgt = pool[ops[k + 1] % POOL];
        if (!tgt) continue;
        const char* name = name_at(data, size, k);
        switch (op) {
            case 0: {
                LeptrisElement e = leptris_element_create(doc, name);
                if (e) (void)leptris_element_append_child(tgt, e);
                break;
            }
            case 1: {
                /* cross-document adopt-by-copy (#1528/#1755) */
                LeptrisElement e =
                    leptris_element_create(doc2, name);
                if (e) (void)leptris_element_append_child(tgt, e);
                break;
            }
            case 2:
                (void)leptris_element_set_text(tgt, name);
                break;
            case 3:
                (void)leptris_element_set_attribute(tgt, name, "v");
                break;
            case 4:
                (void)leptris_element_remove_attribute(tgt, name);
                break;
            case 5: {
                LeptrisElement kid =
                    leptris_element_first_child_any(tgt);
                if (kid)
                    (void)leptris_element_remove_child(tgt, kid);
                break;
            }
            case 6:
                (void)leptris_element_remove_children(tgt);
                break;
            case 7: {
                /* insert_before with a fresh sibling of the target's
                 * parent (cycle rejection walks the parent chain) */
                LeptrisElement p = leptris_element_parent(tgt);
                if (p) {
                    LeptrisElement e =
                        leptris_element_create(doc, name);
                    if (e) (void)leptris_element_insert_before(
                            tgt, e);
                }
                break;
            }
            case 8: {
                char* out = leptris_document_serialize(doc, NULL);
                if (out) leptris_free_string(out);
                break;
            }
            default: {
                LeptrisXPathResult r =
                    leptris_xpath_eval(doc, NULL, "//node()");
                if (r) leptris_xpath_result_free(r);
                break;
            }
        }
    }

    /* Final trees must serialize and re-parse without incident. */
    LeptrisDocument docs[3] = {doc, doc2, fresh};
    for (int i = 0; i < 3; i++) {
        char* out = leptris_document_serialize(docs[i], NULL);
        if (out) {
            LeptrisParseOptions back_opts;
            memset(&back_opts, 0, sizeof(back_opts));
            back_opts.recover = 1;
            LeptrisStatus st2 = LEPTRIS_OK;
            LeptrisDocument back = leptris_parse_string_ex(
                out, strlen(out), &back_opts, &st2);
            if (back) leptris_document_free(back);
            leptris_free_string(out);
        }
        leptris_document_free(docs[i]);
    }
    return 0;
}
