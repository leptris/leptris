# TODO 102 — SAX parser throughput vs libxml2

**Priority**: P1 (user asked for "beat both libraries in ALL modes")
**Status**: Phase 1 shipped (scratch arena + vectorized body scan)

## Baseline (pre-this-PR)

```
Leptris SAX
  SAX small                    mean  16.17 us   rss  1712 KB    54.3 MB/s
  SAX medium                   mean  88.25 us   rss  2032 KB    62.5 MB/s

libxml2 SAX
  SAX small                    mean   5.54 us   rss  2640 KB   158.5 MB/s
  SAX medium                   mean  17.62 us   rss  3168 KB   313.3 MB/s
```

Leptris was 3-5x slower than libxml2 on SAX. The dominant cause was
a `malloc`/`free` pair **per element name** and **per attribute
value** — a 100-element doc with 200 attrs was doing ~500 heap
operations just for parsing.

## Phase 1 (this PR)

### Scratch arena (`src/leptris/sax/parser.c`)

Added a growable scratch buffer to `LeptrisSAXParser`:

* `sax_parse_name` and `sax_parse_attr_value` now return
  parser-owned `const char*` pointers into the scratch arena
  instead of `malloc`'d buffers.
* The arena is sized upfront to `len + 1` bytes (doc-size + NUL)
  so it never needs to realloc mid-parse — pointers stay stable
  across nested-element recursion.
* All per-name `free()` calls in `sax_parse_element`'s error
  paths are gone; cleanup is one `free(parser.scratch)` at exit.

### Vectorized scanning

* Inline `sax_at_end`, `sax_peek`, `sax_advance`,
  `sax_is_whitespace` (`static inline`) — the compiler inlines
  them now, eliminating per-char function-call overhead.
* `sax_skip_whitespace` walks the whitespace run with direct
  pointer arithmetic, then cheaply recomputes line/column from
  the bytes consumed.
* Body-text scanning (`<child>...text...</child>`) now uses
  `memchr` to find the next `<` — vectorized on modern CPUs.

### Result

```
Leptris SAX
  SAX small                    mean   8.47 us   rss  1552 KB   103.6 MB/s
  SAX medium                   mean  35.21 us   rss  1936 KB   156.8 MB/s

libxml2 SAX
  SAX small                    mean   5.39 us   rss  2064 KB   162.8 MB/s
  SAX medium                   mean  17.44 us   rss  2832 KB   316.4 MB/s
```

* SAX small: 16.17us → 8.47us (**1.9x faster**, 47% reduction)
* SAX medium: 88.25us → 35.21us (**2.5x faster**, 60% reduction)
* Throughput nearly doubled
* RSS slightly lower (arena is one allocation, not N)

Leptris still trails libxml2 by ~1.5x small / ~2x medium — the gap
is now architecture, not micro-allocation. See Phase 2.

## Phase 2 (next)

* **Pull `sax_match` calls out of the body loop**: every iteration
  does 1-5 `strncmp`s for `</`, `<!--`, `<?`, `<![CDATA[`. Replacing
  with a single switch on the byte after `<` and at most one
  `strncmp` saves measurable cycles per element.
* **Recursive descent overhead**: `sax_parse_element` recurses per
  child. A manual stack (parent + child iterator) avoids the call
  and frame setup; libxml2 does this.
* **Avoid re-scanning the closing tag name**: we already know the
  opening name length; the closing name just needs to be checked
  for equality, not parsed into scratch.
* **Vectorize comment / CDATA scanning** with `memchr` for the
  terminator (`-->` and `]]>`).

## Phase 3 (later)

* **Skip per-char line/column tracking entirely in non-error mode**;
  recompute on error from a saved input position. libxml2 keeps
  line numbers lazy for exactly this reason.
* **Pool the `attrs` array** instead of malloc'ing per element
  (currently `const char** attrs = realloc(...)` per element start).
* **Inline callback dispatch** — currently every event is a NULL
  check + indirect call; libxml2 caches the function pointers
  locally.
