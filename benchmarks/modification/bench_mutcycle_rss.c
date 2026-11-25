/* bench_mutcycle_rss — #2132 phase 2 ship-gate: PEAK RSS of the
 * 20k-cycle mutation shape (create/build/free 1024-child documents),
 * the same shape as bench_append_attr_pugixml's mutcycle row.
 *
 * Peak RSS is the load-insensitive half of the gate: a high-water
 * mark does not move under foreign load, so main-vs-branch A/B stays
 * valid on a busy machine where ns/child does not. Run the SAME
 * binary built against main and against the intern branch and compare
 * the printed number.
 *
 * Kept OUT of the CI bench loop on purpose: ru_maxrss is process-wide
 * and monotonic, so a harness that runs other rows first would report
 * THEIR peak — this probe isolates the mutcycle shape.
 */
#include "leptris.h"

#include <stdio.h>
#include <sys/resource.h>

int main(void) {
    const int N_WARM = 5000;
    const int N_CYCLE = 20000;
    const int CYCLE_CHILDREN = 1024;

    /* Same warmup discipline as the timing harness: the recycle
     * layers are adaptive, so steady state (no malloc/free per cycle)
     * is the regime whose footprint the gate describes. */
    for (int i = 0; i < N_WARM; i++) {
        LeptrisDocument d = leptris_document_create();
        LeptrisElement root = leptris_element_create(d, "root");
        leptris_document_set_root(d, root);
        for (int c = 0; c < CYCLE_CHILDREN; c++) {
            LeptrisElement e = leptris_element_create(d, "c");
            leptris_element_append_child(root, e);
        }
        leptris_document_free(d);
    }
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

    struct rusage ru;
    if (getrusage(RUSAGE_SELF, &ru) != 0) return 1;
    /* Linux reports ru_maxrss in KB; macOS in bytes — normalize. */
#if defined(__APPLE__)
    double peak_kib = (double)ru.ru_maxrss / 1024.0;
#else
    double peak_kib = (double)ru.ru_maxrss;
#endif
    printf("mutcycle peak RSS: %.0f KiB (20k cycles x 1024 children, "
           "5k warm)\n",
           peak_kib);
    leptris_explicit_cleanup();
    return 0;
}
