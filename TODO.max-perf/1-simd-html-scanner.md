# Lever 1: SIMD byte classification for the HTML tokenizer hot loops

Status: TODO. Research (2022-2026): simdjson/Lemire (arXiv:2503.01662)
shows GB/s lexing from SIMD byte classification; our
`common/simd_text.c` already has dispatch hooks (`g_contains_fn`,
`g_find3_fn`, `g_count_fn`, `g_scan_fn`) to bolt implementations in
without touching call sites.

Levers:
- Vectorized tag-open/quote/ws/name-class scans in the html_parse.c
  hot loops (`h_pooled_lower`, attr scanner, name/ws scans).
- Replace per-byte `while (q < end && !h_is_ws(*q))` scans with
  the dispatched `g_find*` primitives.
Gate: `benchmarks/dom_benchmark` vs pugixml attr-parse row (#682);
fresh build dirs, identical CMAKE_BUILD_TYPE (feedback_benchmark_discipline).
