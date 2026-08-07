# TODO 103 — DOM parser throughput vs libxml2

**Priority**: P1 (largest remaining perf gap)
**Status**: open

## Symptom

```
Taurus DOM
  Parse + Root (medium ~10KB)    98.50 us   9.9k ops/s

libxml2 DOM
  Parse + Root (medium ~10KB)    47 us     ~21k ops/s
```

Taurus is ~2.1x slower at DOM parse. The access operations
(Tree Traversal, Attribute Access, etc.) are already fast on
Taurus — the gap is in the parse itself.

## What's already optimized in the DOM parser

Reading `src/taurus/parse/parser_new.c`, the parser is already
well-tuned:

- `parser_peek_inline` / `parser_is_name_char_inline` /
  `parser_skip_whitespace_inline` are `static inline` — compiler
  inlines them
- `parse_name_view` returns a `TaurusStringView` (zero-copy) instead
  of malloc'ing
- `parse_attribute_value_view` uses `memchr` to find the closing
  quote — vectorized
- Element creation uses `taurus_element_create_with_view` (pool
  bump alloc, no malloc)
- Strict-mode validation guarded by `if (taurus_get_strict_mode())`
  — pays no cost when strict mode is off
- Fast paths for `<tag/>` and `<tag></tag>` (no children, no attrs)

So the per-character work is already minimal. Where's the
remaining cost?

## Hypotheses (need profiling to confirm)

1. **Per-element eager name conversion** (`taurus_sv_to_cstr_pooled`
   at element.c:1229). The comment says this is "eager" to work
   around the document pointer being NULL during parsing. Lazy
   conversion would save one pool_strdup per element.

2. **Strict-mode function call overhead**. Each check is
   `if (taurus_get_strict_mode())` — a thread-local variable read
   wrapped in a function call. The function isn't `static inline`.
   8 call sites in `parser_new.c` × 200 elements = 1600 calls.

3. **UTF-8 validation overhead**. The name parser validates UTF-8
   continuation bytes byte-by-byte. libxml2 might do this in
   vectorized SIMD blocks.

4. **Per-attribute malloc**. Looking at `parser_parse_element_impl`
   around line 1339+, each attribute goes through
   `taurus_element_add_namespace` or `taurus_element_add_attribute_pooled`
   — both pool-routed. Should be fast, but worth profiling.

## Plan

### Phase 1 — cache strict_mode in parser struct (quick win)
Add `int strict_mode` to `Parser`. Set once at `parser_create`.
Replace `taurus_get_strict_mode()` calls in `parser_new.c` with
`p->strict_mode` reads. Saves a function call per check.

### Phase 2 — lazy name conversion (medium win)
Defer `elem->name = taurus_sv_to_cstr_pooled(...)` until first
access. Requires setting `elem->document` during parsing OR
stashing the parser's pool in the element temporarily. Saves
one pool_strdup per element (~50ns × 200 = 10us).

### Phase 3 — UTF-8 validation vectorization (large win)
Replace the byte-by-byte continuation-byte check in
`parse_name_view` with a vectorized scan. Use `__builtin_ia32_*`
intrinsics or portable SIMD via `utf8proc`'s fast path. Saves
~100ns per multi-byte name × ~50 names = 5us.

### Phase 4 — architectural: shared XML lexer
The biggest DRY violation in the codebase: SAX and DOM each have
their own XML tokenizer (~800 + ~1800 lines). A shared lexer
module would (a) eliminate the duplication and (b) centralize
all perf work. Big refactor — multi-week.

## Acceptance

- DOM parse parity with libxml2 within 10% on the medium fixture
- No regression on the existing DOM specs
- macOS `leaks` clean on `test_dom`
