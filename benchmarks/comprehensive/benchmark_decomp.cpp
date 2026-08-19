#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <stdarg.h>
#include "taurus.h"
#include "pugixml.hpp"
static double now_us(void){struct timespec t;clock_gettime(CLOCK_MONOTONIC,&t);return t.tv_sec*1e6+t.tv_nsec/1e3;}
/* snprintf accumulator that can never run past the buffer: clamps
 * on truncation so the (buf + off, cap - off) pairs stay in range. */
static size_t snappend(char* buf, size_t off, size_t cap,
                       const char* fmt, ...) {
    if (off >= cap - 1) return off;
    va_list ap;
    va_start(ap, fmt);
    int r = vsnprintf(buf + off, cap - off, fmt, ap);
    va_end(ap);
    if (r < 0) return off + 1;               /* error: don't stall */
    size_t add = (size_t)r;
    return (add > cap - off - 1) ? cap - 1 : off + add;
}

static void bench(const char* label, const char* xml, size_t n, int iters=30) {
    TaurusStatus st=(TaurusStatus)0;
    double bt=1e18;
    for(int r=0;r<iters;r++){
        double t0=now_us(); TaurusDocument d=taurus_parse_string(xml,n,&st);
        double dt=now_us()-t0; if(d){if(dt<bt)bt=dt; taurus_document_free(d);} else {printf("%-28s PARSE FAIL\n",label);return;}
    }
    double pt=1e18;
    for(int r=0;r<iters;r++){
        double t0=now_us(); pugi::xml_document pd; pd.load_buffer(xml,n);
        double dt=now_us()-t0; if(dt<pt)pt=dt;
    }
    printf("%-28s %6zuKB taurus=%8.1fus pugi=%8.1fus ratio=%.2fx  (taurus %.2f GB/s)\n",
           label, n/1024, bt, pt, bt/pt, n/bt/1000.0);
}
int main(void){
    /* P1: text throughput — 1 element, ~1MB of entity-free text */
    {   size_t cap=2u<<20; char* b=(char*)malloc(cap); size_t n=0;
        n=snappend(b,n,cap,"<r>");
        const char* word="lorem_ipsum_dolor_sit_amet_"; /* 27B */
        while (n < cap-64) { memcpy(b+n, word, 27); n+=27; b[n++]=' '; }
        n=snappend(b,n,cap,"</r>");
        bench("P1 text-stream (1 elem)", b, n, 15); free(b);
    }
    /* P2: per-element overhead — 100k tiny elements, minimal bytes */
    {   size_t cap=1u<<20; char* b=(char*)malloc(cap); size_t n=0;
        n=snappend(b,n,cap,"<r>");
        while (n < cap-8) n=snappend(b,n,cap,"<a/>");
        n=snappend(b,n,cap,"</r>");
        bench("P2 tiny-elements (100k)", b, n); free(b);
    }
    /* P3: per-attr overhead — 50k attrs spread over 5k elements */
    {   size_t cap=4u<<20; char* b=(char*)malloc(cap); size_t n=0;
        n=snappend(b,n,cap,"<r>");
        for (int e=0; e<5000 && n<cap-4096; e++) {
            n=snappend(b,n,cap,"<e");
            for (int a=0;a<10;a++) n+=snprintf(b+n,cap-n," k%d='v%d'",a,e);
            n=snappend(b,n,cap,"/>");
        }
        n=snappend(b,n,cap,"</r>");
        bench("P3 attrs (5k x 10)", b, n); free(b);
    }
    /* P4: per-text-node overhead — 25k short text nodes */
    {   size_t cap=1u<<20; char* b=(char*)malloc(cap); size_t n=0;
        n=snappend(b,n,cap,"<r>");
        while (n < cap-32) n=snappend(b,n,cap,"<i>ab</i>");
        n=snappend(b,n,cap,"</r>");
        bench("P4 text-nodes (25k)", b, n); free(b);
    }
    /* P5: size scaling of P2 shape (throughput curve) */
    for (int scale = 1; scale <= 8; scale *= 8) {
        size_t cap=(size_t)(120000*scale)+64; char* b=(char*)malloc(cap); size_t n=0;
        n=snappend(b,n,cap,"<r>");
        while (n < cap-8) n=snappend(b,n,cap,"<a/>");
        n=snappend(b,n,cap,"</r>");
        char lbl[64]; snprintf(lbl,64,"P5 elements x%dk", 120*scale);
        bench(lbl, b, n, scale==8?10:20); free(b);
    }
    return 0;
}
