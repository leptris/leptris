# TODO 107 — pugixml speed tricks (study notes)

**Priority**: P1 (user goal: "achieve read AND write dominance")
**Status**: source-study complete; most impactful tricks listed below

## Source layout

pugixml is a **single 13,800-line C++ file** (`src/pugixml.cpp`).
Everything in one translation unit = compiler sees the entire
parser, allocator, and serializer at once = aggressive inlining
across what would otherwise be TU boundaries.

## The tricks (ranked by impact for leptris)

### 1. Chartype lookup table (line 1929)

A 256-byte table `chartype_table[]` maps each byte value to a
bitmap of categories:

```c
enum chartype_t {
    ct_parse_pcdata = 1,   // \0, &, \r, <
    ct_parse_attr   = 2,   // \0, &, \r, ', "
    ct_space        = 8,   // \r, \n, space, tab
    ct_parse_cdata  = 16,  // \0, ], >, \r
    ct_symbol       = 64,  // a-z A-Z 0-9 _ : - .
    ct_start_symbol = 128, // a-z A-Z _ :
    // ...
};
```

Categorizing a char is **one byte load + one AND + one branch**:

```c
#define PUGI_IMPL_IS_CHARTYPE(c, ct) (chartype_table[(unsigned char)(c)] & (ct))
```

vs leptris's pattern of `c == ' ' || c == '\t' || c == '\n' || c == '\r'`
which is **3-4 branches**.

**Impact for leptris**: replace `sax_is_whitespace`, `sax_is_name_start`,
`sax_is_name_char`, `parser_is_*_inline` with table lookups. Should
save 20-40% on the SAX scan loops. Big win.

### 2. SCANWHILE_UNROLL macro (line 2679)

Manually unrolls scan loops 4 iterations at a time:

```c
#define PUGI_IMPL_SCANWHILE_UNROLL(X) do {           \
    for (;;) {                                        \
        char_t ss = s[0];                              \
        if (PUGI_IMPL_UNLIKELY(!(X))) break;          \
        ss = s[1];                                     \
        if (PUGI_IMPL_UNLIKELY(!(X))) { s += 1; break; }  \
        ss = s[2];                                     \
        if (PUGI_IMPL_UNLIKELY(!(X))) { s += 2; break; }  \
        ss = s[3];                                     \
        if (PUGI_IMPL_UNLIKELY(!(X))) { s += 3; break; }  \
        s += 4;                                        \
    }                                                 \
} while (0)
```

Reduces loop overhead (compare + branch) by 4x.  Especially
helpful when the body is one instruction (the table lookup above).

**Impact for leptris**: replace the body of `sax_skip_whitespace`,
`parse_name_view`'s scan loop, the attribute value scan, etc.
Should save 10-20% on each.

### 3. Compact pointer encoding (line 860)

Each in-page pointer is stored as 1 byte (offset from the pointer's
own address, divided by alignment).  For offsets ≤ 254 bytes the
encoding is one byte; for larger offsets a 4-byte fallback is used
with a sentinel value.

```c
template <typename T, int header_offset, int start = -126>
class compact_pointer {
    unsigned char _data;
    // ...
};
```

Combined with a 2-byte `compact_header` (1 byte page offset +
1 byte flags), every node-edge pointer (parent, first_child,
next_sibling, first_attribute) is 1 byte.

**Result (compact mode)**:
  - `xml_attribute_struct`: **8 bytes** total
  - `xml_node_struct`: **12 bytes** total

vs leptris's regular-pointer layout:
  - leptris `leptris_attribute`: ~96 bytes
  - leptris `leptris_element`: ~104 bytes

pugixml's element is **8.6x smaller** than leptris's.  Better cache
locality, fewer allocations per page, fewer cycles per node walk.

**Impact for leptris**: architectural change.  Leptris already HAS a
compact-pointer implementation (`dom/compact.h`) but it's not the
default and not wired into the parser (TODO 90).  Adopting the
compact-pointer layout as the primary storage would deliver the
single largest perf improvement leptris could get — but it's
multi-week work.

### 4. Compact string encoding (line 1002)

Strings within a page are stored as 2-byte offsets from a
`compact_string_base` pointer (per page).  Saves 6 bytes per
string.  Each `compact_string<offset, base_offset>` is 2 bytes.

### 5. Single-page allocator with freelist tracking

`xml_allocator` (line 556) is a single 32 KB page with bump-pointer
allocation.  Same as leptris's pool.  But pugixml ALSO tracks
`freed_size` — when an entire page is freed, the page is reused.
This is why pugixml's repeated parse-then-free benchmarks are fast.

Leptris's pool allocator already does bump-pointer; it lacks the
freelist tracking, so a high-churn workload pays repeated page
allocations.

### 6. Gap-based text accumulation (line ~2504)

Text in XML can be split across CDATA sections, entity references,
and adjacent text nodes.  pugixml uses a "gap buffer" to accumulate
text in-place without allocations, then writes it once.

Leptris allocates per-text-node.  Each text segment is a separate
pool allocation.

### 7. Header-only — everything inlines

pugixml is a header-only library.  Every function is `inline` or
`forceinline`.  When the compiler builds a translation unit that
includes `pugixml.hpp`, it sees the entire parser/serializer and
can inline aggressively.

Leptris is compiled as a static/shared library.  Even with `static
inline` annotations, the compiler can't inline across TUs without
LTO.  This is a fundamental architectural difference.

For leptris to match this, it would need to either:
- Ship as a header-only library (breaking ABI stability)
- Add LTO to the build (already a CMake option, just not on by default)

### 8. Compile-time flag masks (line 2678)

Parse options are encoded as flag bits.  Macros like `PUGI_IMPL_OPTSET`
become single-instruction bit tests:

```c
#define PUGI_IMPL_OPTSET(OPT) (optmsk & OPT)
```

When the option set is a compile-time constant, the compiler
eliminates the branch entirely.

Leptris's strict_mode caching (PR #62) achieves a similar effect
but is a runtime branch.

### 9. No name interning

pugixml doesn't dedupe attribute or element names by default.
Strings are stored once per use.  Saves the hash-table cost that
leptris pays.

leptris interns names via the pool's hash table (TODO 22).  Useful
when many elements share attr names (parse case), pure overhead
when names are unique (mutation case — addressed in PR #70).

### 10. No StringView layer

pugixml stores names/values as plain `char_t*` pointers.  No
StringView abstraction.  When the user calls `node.name()`, they
get the char* directly.

Leptris has StringView (zero-copy during parse) PLUS a cached
NUL-terminated char* (eager-converted for thread-safety — see
TODO 103 Phase 2).  Each element pays 24 bytes for StringView +
24 bytes for cached cstrs = 48 bytes of "name" storage per element.
pugixml: 2 bytes (compact_string offset) + the actual string once
in the page.

## Most-impactful-to-adopt-now

Ranked by effort × impact for leptris:

| # | Trick | Effort | Impact | Phase |
|---|---|---|---|---|
| 1 | Chartype table | S | H | TODO 108 (next PR) |
| 2 | SCANWHILE_UNROLL | S | M | TODO 108 (next PR) |
| 3 | Drop name interning on mutation | done (PR #70) | S | — |
| 4 | Force-inline hot helpers | S | M | TODO 109 |
| 5 | Compact pointer encoding (compact-mode primary) | L | H | TODO 90 (revive) |
| 6 | Compact string encoding | L | M | TODO 90 |
| 7 | Header-only build | XL | H | Not realistic — breaks ABI |
| 8 | LTO build option | S | M | TODO 110 (one-liner CMake change) |
| 9 | Gap-based text accumulation | M | M | TODO 111 |

## The honest answer

pugixml is fast primarily because of **compact node layout** (trick
3+4: 8-12 byte nodes vs leptris's 96-104).  Every other trick
accelerates work proportional to the node size — the smaller the
node, the fewer cache misses, the more nodes per page, the faster
every walk.

Leptris can adopt tricks 1, 2, 4, 8 incrementally for incremental
gains.  Trick 3 (compact layout) is the only path to true pugixml
parity and it's multi-week work.

Until leptris adopts compact layout, "WRITE dominance over pugixml"
is not achievable — we can close the gap by maybe 30%, but not
match the ~6-7x speed advantage that compact storage gives.
