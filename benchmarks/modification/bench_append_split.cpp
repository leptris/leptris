/* bench_append_split — Lane 18 (#1179 tail): where does the
 * create+append ns/child go? Three rows over the same shape:
 * create only, append only (children pre-created), and the
 * create+append pair the gate bench measures. The split decides
 * which half a lever belongs to. */
#include "leptris.h"
#include <chrono>
#include <cstdio>

static double now_ns() {
    return (double)std::chrono::duration_cast<std::chrono::nanoseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

int main(void) {
    const int N = 200000;

    {
        LeptrisDocument d = leptris_document_create();
        LeptrisElement root = leptris_element_create(d, "root");
        leptris_document_set_root(d, root);
        double t0 = now_ns();
        for (int i = 0; i < N; i++)
            leptris_element_create(d, "c");
        double t1 = now_ns();
        printf("create only : %6.1f ns/elem\n", (t1 - t0) / N);
        leptris_document_free(d);
    }
    {
        LeptrisDocument d = leptris_document_create();
        LeptrisElement root = leptris_element_create(d, "root");
        leptris_document_set_root(d, root);
        LeptrisElement* kids = new LeptrisElement[N];
        for (int i = 0; i < N; i++)
            kids[i] = leptris_element_create(d, "c");
        double t0 = now_ns();
        for (int i = 0; i < N; i++)
            leptris_element_append_child(root, kids[i]);
        double t1 = now_ns();
        printf("append only : %6.1f ns/elem\n", (t1 - t0) / N);
        delete[] kids;
        leptris_document_free(d);
    }
    {
        LeptrisDocument d = leptris_document_create();
        LeptrisElement root = leptris_element_create(d, "root");
        leptris_document_set_root(d, root);
        double t0 = now_ns();
        for (int i = 0; i < N; i++) {
            LeptrisElement c = leptris_element_create(d, "c");
            leptris_element_append_child(root, c);
        }
        double t1 = now_ns();
        printf("create+app  : %6.1f ns/elem\n", (t1 - t0) / N);
        leptris_document_free(d);
    }
    return 0;
}
