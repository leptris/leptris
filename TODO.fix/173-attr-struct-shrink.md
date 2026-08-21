# TODO 173 — Attribute struct shrink (ns_cache side table)

## Status

Done. Landed alongside TODO 172 (lazy FNV hash).

## Why

The attribute struct was 112 bytes — larger than pugixml's whole
xml_attribute_struct by ~5×. The bulk went to namespace-related
fields (`prefix_view`, `namespace_uri_view`, `prefix`,
`namespace_uri`) that are empty/NULL for the vast majority of
attrs (any attr without a namespace prefix).

For 100,000 attrs at K=100, that's 4.8 MB of wasted memory and
the corresponding cache-traffic overhead.

## Change

Moved `prefix_view`, `namespace_uri_view`, `prefix`, and
`namespace_uri` out of the main attribute struct into a side
cache struct:

```c
struct leptris_attr_ns_cache {
    LeptrisStringView prefix_view;
    LeptrisStringView namespace_uri_view;
    char* prefix;
    char* namespace_uri;
};
```

The main `struct leptris_attribute` gets a single nullable pointer
`struct leptris_attr_ns_cache* ns_cache`. When the attr has no
namespace activity (the common case), `ns_cache == NULL` and zero
overhead is paid. Attrs that DO have a prefix or namespace_uri
pay one 48-byte pool allocation for the cache struct.

Attr struct size: 112 → 72 bytes (36% reduction).

## Accessors

Readers use these helpers (in element.h):

- `attr_get_prefix(a)` — returns NULL when no ns_cache.
- `attr_get_namespace_uri(a)` — same.
- `attr_get_prefix_view(a)` — returns empty StringView.
- `attr_get_namespace_uri_view(a)` — same.

Writers either allocate the cache directly via `leptris_pool_alloc`
(used in element_modify.c's copy path) or simply leave `ns_cache`
NULL when not setting prefix/ns (the common case at parse time).

## Sites updated

- `dom/element.c` — attr creation paths set `ns_cache = NULL`.
- `dom/element_modify.c` — attr copy path allocates new ns_cache
  if source has one; setattr creation path sets NULL.
- `flat/direct_parse.c` — parse path sets `ns_cache = NULL` (parse
  never sets attr prefix/uri; namespace resolution happens at the
  element level via element's own ns_cache).
- `xpath/evaluator_axes.c` — `attr_get_namespace_uri` for the
  synthetic attribute-node construction.
- `xpath/functions.c` — `xml:lang` lookup uses accessors.
- `leptris_memory.c` — `leptris_attribute_free` releases ns_cache
  contents if present.
- `leptris.c` — finalize_element_strings materializes
  namespace_uri from the side cache.

## Risk

- **ABI break.** The attribute struct layout changes. The struct
  is internal (no public API exposes field offsets). Per the user's
  instruction (ABI constraint removed), this is acceptable.
- **Correctness.** Every read of `attr->prefix` / `attr->namespace_uri`
  / their views had to be updated. Audited via `grep` — 25 sites
  found, all updated.

## Measured impact

benchmark_many_attrs K=100 (median, default Release build):

| version                          | K=100 median |
|----------------------------------|--------------|
| v0.18.4 baseline                 | 6885 µs      |
| v0.19.1 (lazy hash)              | 6284 µs      |
| v0.19.2 (lazy hash + ns_cache)   | **4568 µs**  |

Combined: 33% improvement vs v0.18.4 baseline.

## Status

Done. PR #322 ships this change alongside TODO 172.
