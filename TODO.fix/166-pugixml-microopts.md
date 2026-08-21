# TODO 166 — pugixml-inspired micro-optimizations (post-research)

## Status

**Landed Phase A + Phase C. Phase B reverted (regression). Phase D
out of scope (no applicable call sites).**

## Outcome by phase

### Phase A — LEPTRIS_NOINLINE + cold-path extraction: LANDED

Added `LEPTRIS_NOINLINE` and `LEPTRIS_ALWAYS_INLINE` macros to
`common/port.h` (GCC/Clang/MSVC portable). Extracted the DOCTYPE
handling body (140 lines) out of `direct_parse_internal` into a
new `dp_parse_doctype` helper marked `LEPTRIS_NOINLINE`.

Result: the hot parse loop's instruction footprint shrinks. The
DOCTYPE code (PUBLIC/SYSTEM re-scan, internal subset parsing, DTD
construction) lives at a separate address now — it no longer
pollutes the i-cache working set of the inner element/attr loop.
Symbol table confirms both functions are separate in the binary.

### Phase B — 4-byte ASCII name-scan fast path: REVERTED

Prototyped the `(w & 0x80808080u) == 0` guard + 4 IS_NAME_CHAR
checks per iteration. Measured ~25% regression at K=50
attrs/element on `benchmark_many_attrs` (median 3328µs →
4009-4336µs across 3 runs).

Root cause: the 4-byte fast path added MORE work per 4 chars
than the byte loop saved. The `memcpy` + mask check + 4 byte
extractions via shift cost more than 4 byte loads, while modern
branch predictors already make the byte loop nearly free. The
fast path only wins when name lengths are ≥16 chars (so loop
overhead dominates), but the typical XML name is 5-10 chars.

The pugixml trick this came from (SCANWHILE_UNROLL) is for
*long* character runs (PCDATA, CDATA, comment bodies), not for
short token names. leptris already uses `memchr` for those long
runs, which is the libc-vectorized equivalent and beats the
manual unroll.

Kept `dp_scan_name` as a `LEPTRIS_ALWAYS_INLINE` wrapper — DRY
for the 6 name-scan call sites in `direct_parse.c`, zero call
overhead, single source of truth if we revisit.

### Phase C — IS_WS DRY cleanup: LANDED

Replaced 6 ad-hoc `*scan == ' ' || *scan == '\t' || *scan ==
'\n' || *scan == '\r'` chains in the XML-declaration scanner
with `IS_WS(*scan)`. Behavior identical (the chartype table
includes exactly space/tab/CR/LF). One table lookup beats 3
branches on every architecture.

### Phase D — `(unsigned)(ch - '0') < 10` digit trick: SKIPPED

The intended call sites (xml-decl version / standalone parsing)
have no digit checks — they're string equality tests
(`memcmp(pname, "version", 7)`, `strcmp(pval, "yes")`). No
applicable site in the parser.

## Why

After TODO 159 (Phase A0–G) + TODOs 162/163/165, the remaining
gap to pugixml is structural (see
[[161-pugixml-gap-closure-survey]]). The remaining leverage is
micro-optimization in the parse hot path. This TODO captured
the realistic candidates from the WebFetch/WebReader research
pass on pugixml's "Parsing XML at the Speed of Light" article
and modern SIMD XML parser notes (Bun.XML, simdxml, ARM HTML
scanning).

## Techniques considered but NOT pursued (with reasons)

- **Computed-goto dispatch (GCC labels-as-values).** GCC-only.
  PGO covers most of the same ground without portability cost.
- **SIMD 16-byte ASCII classify (SSE2/NEON).** Investigated in
  TODO 157; for leptris's short-token scan pattern (avg name
  length ~8 bytes) the SIMD setup overhead exceeded the gain.
  Parabix-style parallel bit streams would require a full
  parser rewrite — not realistic for the current parse
  architecture.
- **Boolean template specialization for parse flags.** C has
  no templates. A macro-based equivalent (4 copies of the loop
  body, one per common flag combo) would 4× the code size for
  <5% measured win in pugixml's own benchmarks. Not worth it.
- **Null-terminator trick (replace last byte).** We already
  NUL-terminate in-place; the trick would save end-of-buffer
  checks but couples parse code to the buffer-ownership model.
  High correctness risk.
- **Compact 1-byte in-page pointers (pugixml's
  compact_pointer).** Multi-week refactor; leptris's 4-byte
  int32 compact pointers are already on parity for our
  cache-line-sized element struct.
- **Header-only build.** Breaks our ABI stability promise.
- **4-byte ASCII fast path (Phase B).** Tried, measured 25%
  regression on attr-heavy parse, reverted. See above.

## Risk

- **Phase A:** strictly safer than refactoring. Cold code is
  already correct; we just keep it off the hot i-cache. No
  behavior change.
- **Phase C:** behavior-preserving; the chartype table
  includes exactly space/tab/CR/LF.

## Measured impact

The benchmark variance on this machine is high (std/mean ~2-3×
on `benchmark_many_attrs`), so small improvements are hard to
measure. The min-time metric (more stable than median) is
unchanged across runs. Best read: this is a **code-quality +
architecture release**, not a measurable perf release.

- `bench_dom_leptris`: parse CPU time 36.27µs → 36.81µs
  (within noise).
- `bench_xpath_leptris`: complex-query CPU 6.86µs → 6.20µs
  (improvement, within noise).
- `benchmark_many_attrs` K=5/20/50/100: no significant change.

## Status

Done. Phases A+C landed in one PR per [[feedback-one-pr-per-todo]].
Phase B's failure is documented so the next attempt to apply
this technique knows the threshold (≥16-char names) before
trying again.
