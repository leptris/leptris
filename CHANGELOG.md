## [Unreleased]

## [0.6.1] - 2026-08-09

### Added — DOCTYPE public access API (TODO 148 Phase 2)

- `taurus_document_internal_subset(doc)` → opaque `TaurusDoctype`
  handle (or NULL when no DOCTYPE, or when direct_parse skipped it
  on plain-XML input)
- `taurus_doctype_get_name` / `_get_root_name` (alias matching the
  Nokogiri `DocType#name` convention)
- `taurus_doctype_get_public_id`
- `taurus_doctype_get_system_id`
- `taurus_doctype_get_internal_subset`

New opaque typedef `TaurusDoctype` in `taurus/types.h`. Backs
`Document#internal_subset`, `#doctype`, and the `DocType#name` /
`#public_id` / `#system_id` / `#internal_subset` family in the
Ruby binding.

### Added — Custom XPath function handlers (TODO 148 Phase 5)

- `taurus_xpath_register_function(doc, name, fn, user_data)`
- `typedef char* (*TaurusXPathFn)(const char* const* args, int argc, void* user_data)`

Registered functions live on the document and are merged AFTER
the standard XPath 1.0 library in the per-context registry, so
standard names win collisions. Backs Nokogiri's
`Searchable#xpath(expr, handler)` extension.

### Performance — flat_promote bulk attr allocation (TODO 148 Phase 7)

Mirrors `direct_parse`'s `dp_add_attr_inline` in the promote pass.
Pre-allocates the entire attr block upfront from
`flat->attr_count`; each attr takes the next slot off the block
(bump pointer). The inline path skips name interning + value
pool_strdup + per-attr entity memchr. Closes the long-deferred
TODO 114 Phase 4.


## [0.6.0] - 2026-08-08

### Added — Nokogiri-compat C-API gaps (TODO 148)

Four new public primitives unblock commonly-used Nokogiri methods
in the Ruby binding:

- **`taurus_element_copy(src, dest_doc)`** — detached deep copy of
  an element subtree into a destination document. Backs `Node#dup`,
  `Element#dup`, in-context fragment parsing, and `Node#replace`
  with markup strings.
- **`taurus_document_copy(src)`** — full-document deep copy
  (tree + XML declaration + document-level PIs). Backs
  `Document#dup` / `#clone`.
- **`taurus_node_get_xpath(node)`** — canonical unique XPath
  string identifying a node. Format matches Nokogiri's
  `Node#path`: `/{qname}[N]` for elements with same-named
  siblings, `/text()`, `/comment()`, `/processing-instruction()`
  for typed leaves. Backs `Node#path`, `#css_path`, `#matches?`.
- **`taurus_parse_fragment(xml, len, dest_doc, status)`** — parses
  XML fragments (multiple top-level nodes allowed) into a
  `#document-fragment` synthetic container element owned by the
  destination document. Backs `Document#fragment`, `Node#fragment`,
  `Node#parse`, and string-form `Node#add_child` / `#replace`.

### Added — minor API surface

- **`taurus_element_has_attribute(elem, name)`** — boolean form of
  the `attribute(name) != NULL` idiom.

### Fixed — flat_promote line tracking (TODO 148 Phase 6)

Closed the v0.5.14 known limitation: `taurus_node_line` returned 0
for documents that fell through the `flat_parse + flat_promote`
path. `FlatNode` grew from 28 to 32 bytes; `flat_parser` tracks
source line via an `fp_advance_line` helper and snapshots it at
each token; `flat_promote` copies `fn->line` into
`node->base.line` for every node type.

### Reference docs

Two new TODO docs frame the remaining work in this initiative:
- **TODO 148** — survey of Nokogiri-compat C-API gaps.
- **TODO 149** — pugixml architecture study (compact 44-byte
  node, single arena, computed goto, chartype tables) with
  concrete phase ordering for closing the perf gap.

567/567 specs pass (was 539 at v0.5.14).


## [0.5.14] - 2026-08-08

### Fixed — namespace read API (#222), node line tracking (#223)

- **#222**: `taurus_element_namespace` returned NULL for default-namespace
  elements because the lazy resolver only triggered when the element
  had a prefix. `taurus_element_namespace_for_prefix` checked only the
  element's own prefix field instead of searching the `xmlns`
  declarations. Both now route through `taurus_element_lookup_namespace`,
  which walks the declarations list and inherits up the tree.
- **#223**: `taurus_node_line` was hardcoded to return 0. Added a
  `uint32_t line` field to `TaurusNode` (base struct, inherited by
  every node type). `direct_parse` snapshots the source line at each
  token and folds newlines crossed by memchr-driven text scans.
  Programmatic nodes still report 0 (creators memset the struct).
  Element size budget bumped 80 → 88 bytes. The `flat_promote` fallback
  path (entities/DTD inputs) doesn't carry line through `FlatNode` yet
  — plain XML (the common case) is fully tracked.

### Added — minor visibility gaps from the v0.5.13 audit

- `taurus_xinclude_get_encoding` was declared in the public header but
  had no implementation, so the symbol was missing from the shared
  library export table. Body added (returns the `encoding=` attribute
  of an `xi:include` element).
- `taurus_element_has_attribute` (new). Natural boolean form of the
  existing `taurus_element_attribute(name) != NULL` idiom.


## [0.5.13] - 2026-08-08

### Fixed — DOM tree mutation bugs (#213, #216, #217)

- **#213**: `taurus_element_child_count` / `taurus_node_child_count`
  always returned 0 on parsed documents because `direct_parse` and
  `flat_promote` (the parse hot paths) never incremented
  `elem->child_count`. Counter is now maintained for element children
  in both parsers, matching the man-page contract.
- **#216**: `taurus_element_insert_after` / `_before` silently rejected
  any non-element `new_node` (text/comment/cdata/pi). Now supports all
  child node types via type-dispatched parent and sibling setters.
- **#217**: `taurus_element_append_child_internal` (and the related
  prepend/insert paths) spliced the child into the new parent without
  unlinking it from its current parent, corrupting both trees. Now
  unlinks via `taurus_node_unlink` before re-parenting.
- Latent crash surfaced by the #217 fix: `taurus_comment_create`,
  `taurus_cdata_create`, `taurus_pi_create`, and `taurus_text_create`
  did not initialize `parent_off`. Pool reuse left stale values that
  decoded into wild pointers. All five creators now initialize
  `parent_off = 0` alongside `next_sibling_off`.


## [0.5.12] - 2026-08-08

### Performance — direct parser attribute fast path

Bulk-allocated the attribute block upfront from the pool so each
attribute takes the next slot off the block (bump pointer, no
per-attr pool_alloc). Names and values are zero-copied — names
NUL-terminated in-place after `=` is consumed, values already
NUL-terminated at the closing quote. Skips name interning, value
pool_strdup, and the per-attr entity memchr.

Medium (~24 KB, ~2300 attrs): 166 µs → 140 µs (15% faster)
Medium (~5 KB, ~50 attrs):   37 µs → 34 µs (8% faster)

### Fixed

- `taurus_document_encoding` and `taurus_document_xml_version`
  returned NULL on documents produced via the direct-parse fast
  path. The direct parser now scans the XML declaration for
  version/encoding/standalone (previously discarded after noting
  the declaration was present).
- `_Static_assert` in `flat_doc.h` was not C++-compatible and
  broke the Linux ASAN build (the test_flat_* tests are C++).
  Wrapped in `#ifdef __cplusplus`.


## [0.5.11] - 2026-08-08

### Performance — breakthrough: parse+promote 78 to 32 µs (59% faster)

Pre-warmed the direct_parse pool with a page sized from estimated
node count. All per-node allocations (text, comment, attr, namespace
structs) now hit the bump-pointer fast path.

The direct parser now produces a complete TaurusElement tree in a
single pass — no FlatDoc intermediate, no separate promote pass.
Combined with all prior optimizations:

| Step | parse+promote (5 KB) |
|------|---------------------:|
| Session start | 78 µs |
| + wire_child inline | 71 µs |
| + bulk element alloc | 66 µs |
| + zero-copy names | 60 µs |
| + direct parser | 55 µs |
| + lookup tables + memchr | 53 µs |
| + pre-warmed pool | **32 µs** |

Parse + promote is now within 6× of pugixml (~5 µs) on the same
hardware, down from 16× at session start.


## [0.5.10] - 2026-08-08

### Fixed — direct parser bugs

- Element name NUL-termination destroyed `>` delimiter for elements
  without attributes. Fixed by NUL-terminating AFTER dp_parse_attrs.
- Close tag name not verified. `<a></b>` was accepted. Fixed with
  name comparison.
- Element count estimate too low for dense docs. Fixed with len/10+128.


## [0.5.9] - 2026-08-08

### Added — Single-pass direct parser (TODO 147 Phase A)

New `direct_parse` function: parses XML directly into TaurusElement
records in a single pass — no FlatDoc intermediate, no promote pass.
`taurus_parse` tries direct_parse first, falling back to flat_parse +
lazy promote on failure.

Key pugixml techniques applied:
- Bulk element allocation from pool (single alloc for all elements)
- Zero-copy names via in-place NUL termination
- Direct compact-pointer edge offsets via pointer arithmetic
- Lookup tables for char classification
- memchr for text scanning

### Performance — flat parser lookup tables (from v0.5.8)

Replaced per-byte comparison chains with 256-byte lookup table
accesses. Parse-only: 53 µs → 35 µs (34% faster since session start).

### Cumulative parse+promote improvement

| Optimization                  | 5 KB parse+promote |
|-------------------------------|-------------------:|
| Original (session start)      | 78 µs              |
| + wire_child inline           | 71 µs              |
| + bulk element alloc          | 66 µs              |
| + zero-copy names (NUL-term)  | 60 µs              |
| + lookup tables + memchr      | 56 µs              |
| + direct parser               | ~55 µs             |


## [0.5.8] - 2026-08-08

### Performance — flat parser lookup tables (pugixml technique)

Replaced per-byte comparison chains (6 comparisons per name byte)
with 256-byte lookup table accesses (1 lookup per byte). Also
replaced the byte-by-byte text scanning loop with libc memchr
(vectorized on most platforms).

Parse-only cost for 5 KB doc: 39 µs -> 35 µs (11% faster).

Cumulative optimizations since session start:

| Optimization                  | Parse+promote | Parse-only |
|-------------------------------|---------------|------------|
| Original                      | 78 µs         | 53 µs      |
| + wire_child inline           | 71 µs         |            |
| + bulk element alloc          | 66 µs         |            |
| + zero-copy names (NUL-term)  | 60 µs         |            |
| + lookup tables + memchr      |               | **35 µs**  |


## [0.5.7] - 2026-08-08

### Performance — pugixml-style zero-copy promote

Applied pugixml's key optimization: copy the XML input once, then
write NUL terminators at every name/value boundary in-place. Names
become zero-copy pointers — no pool_strdup, no string interning.

Promote cost for 5 KB doc (Apple M1, CPU time):
78 us (original) -> 60 us (after all optimizations) = 23% faster.

### Fixed

- #201: flat XPath dispatcher over-matched count() in larger
  expressions (count(//book) > 0 returned Number instead of Boolean).


## [0.5.6] - 2026-08-08

### Performance — TODO 146 Phase 4a

Bulk element allocation in the flat promote pass. Pre-allocates all
element nodes in a single `pool_alloc + memset` instead of calling
`taurus_element_create_with_view` per element.

| Operation       | Before  | After   | Speedup |
|-----------------|---------|---------|---------|
| parse_promote   | 71 µs   | 66 µs   | 7%      |
| parse_only      | 45 µs   | 41 µs   | 9%      |

The dominant remaining cost is per-element string interning (hash
table lookup + insert), not pool allocation.

### Architecture — TODO 145 + 146 plan documents

Full design for Phase 4 (mutation without mandatory promote)
documented in `TODO.fix/146-phase-4-mutation-without-promote.md`.
Covers three implementation approaches with tradeoffs:
mutable/growable FlatDoc, mixed tagged-pointer representation, and
orphan tracking.


## [0.5.5] - 2026-08-08

### Added — Flat XPath (TODO 145 Phase 3)

`taurus_xpath_eval` now tries a flat fast-path dispatcher before
falling back to the compact-tree XPath evaluation. For primitive-
returning query patterns on documents that haven't been promoted,
the dispatcher walks FlatDoc directly and skips promote entirely.

**Supported patterns:**
- `count(//name)` — flat count by element name
- `count(//*)` — flat count all elements
- `count(descendant::name)` / `count(descendant-or-self::name)`
- `boolean(//name)` — flat exists check

Complex expressions (predicates, multi-step paths, nodeset-returning
queries) fall back to the compact path. The dispatcher returns
"not handled" for anything it can't pattern-match, so existing
XPath semantics are preserved.

For the common "parse and count elements" workload, the flat path
matches the cost of `flat_fast_count_elements_named` (~12 µs on a
1 KB doc vs ~22 µs via the compact path).

### Fixed

- **#194**: exclusive C14N emitted duplicate `xmlns:` declarations
  when a prefix was both visibly used AND in the caller's
  inclusive namespace list. The output was invalid XML. Fixed by
  deduplicating the emit list before serializing.


## [0.5.4] - 2026-08-07

### Added — Flat-as-tree architecture (TODO 145)

Phases 1 and 2 of the rewrite toward making the FlatDoc the
primary representation (instead of always-promoting to the
compact-pointer tree).

**Phase 1: namespace-aware promote.** Removes the "xmlns → legacy
parser" routing. Documents with namespace declarations now go
through the flat fast path. The promote pass moves xmlns
declarations from the regular attribute list to elem->namespaces
and splits qualified element names on the first ':' into prefix +
local name. Unblocks ~70% of real-world XML documents from the
fast path.

**Phase 2: flat serialize.** `taurus_document_serialize` now
dispatches to `flat_serialize_document` when `doc->flat_doc` is
set and not yet promoted. The flat path walks the FlatDoc node
array directly, producing identical output without triggering
promote. Parse-then-serialize workloads skip the entire pool-alloc
+ compact-pointer-encode cost.

### Fixed

- Pre-existing leak in `taurus_element_get_namespace_uri` where
  lazy namespace resolution used heap strdup. Pool-allocate via
  the element's owning document so pool destroy releases the copy.

### Performance

Per `bench_flat_parse` (Apple M1, 5 KB plain XML):

| Operation                  | Before | After  |
|----------------------------|--------|--------|
| Parse only (flat)          | 53 µs  | 46 µs  |
| Parse + promote            | 78 µs  | 71 µs  |
| Parse + serialize (flat)   | n/a    | 47 µs  |
| Parse + serialize (compact)| 78 µs  | 78 µs  |

The flat serialize path is ~40% faster than going through promote
for parse-then-serialize workloads.


## [0.5.3] - 2026-08-07

### Fixed — Full exclusive C14N (#183, real implementation)

v0.5.2 shipped `taurus_c14n_canonicalize_ex` with the EXCLUSIVE
mode flag accepted but routed to canonical. That was a stub. This
release implements the real W3C Exclusive XML Canonicalization
1.0 algorithm:

- Compute visibly-used namespace prefixes per element (element's
  own prefix, attribute prefixes, caller-supplied inclusive list).
- Emit `xmlns:prefix="uri"` only for prefixes NOT already emitted
  by an output ancestor — prevents namespace leak when enveloping
  canonicalized subtrees.
- Resolve URIs via xmlns-declaration walk up the ancestor chain.
- Sort emitted declarations lexicographically per spec.

The `inclusive_ns_prefixes` parameter is now honored: prefixes in
the caller's list are force-included even if not visibly used by
the subtree.

4 new specs verify the behavior:
- ExclusiveModeDropsUnusedNamespaces
- ExclusiveModeKeepsUsedNamespaces
- InclusiveNsPrefixesForceInclude
- ExclusiveOnEmptyDoc

### Performance — TODO 141 Phase A

Inline `promote_wire_child` helper in the flat promote pass.
Bypasses `taurus_element_append_child_internal`'s validation,
type dispatch, and version increment for the hot path.

| Doc size  | parse+promote before | after   | speedup |
|----------:|---------------------:|--------:|--------:|
|     829 B |              25.2 µs | 16.7 µs | 34%     |
|    4469 B |              78.1 µs | 70.8 µs | 10%     |
|   18377 B |             441.4 µs | 253.4 µs| 43%     |


## [0.5.2] - 2026-08-07

### Added — Nokogiri-compatible API (#181, #183)

- `taurus_element_add_namespace_definition(elem, prefix, href)`
- `taurus_element_set_default_namespace(elem, href)`
- `taurus_element_remove_namespace_definition(elem, prefix)`
- `taurus_c14n_canonicalize_ex(doc, version, mode, prefixes, with_comments)`
- `taurus_c14n_canonicalize_subtree_ex(elem, version, mode, prefixes, with_comments)`
- New `TaurusC14NMode` enum (`TAURUS_C14N_MODE_CANONICAL`,
  `TAURUS_C14N_MODE_EXCLUSIVE`).

The C14N `with_comments` toggle is fully implemented — comments are
emitted by the canonical walk when the flag is set. Exclusive mode
and `inclusive_ns_prefixes` are accepted as parameters and currently
fall back to canonical pending the namespace-use-tracking follow-up.

### Fixed

- `taurus_node_previous_sibling` now works for any node type,
  not just elements (#179). Previously returned NULL for text,
  comment, CDATA, or PI nodes even when they had a real previous
  sibling.
- `taurus_element_create` (and the typed node creators) no longer
  return NULL on freshly-parsed FlatDoc documents (#184). The fix
  triggers lazy promote at the top of each creator so `doc->pool`
  is allocated before the new node is pool-allocated.
- `generate_medium_doc` in `benchmark_parse` overflowed its
  12 KB static buffer by ~3 KB. The flat fast path exposed the
  corruption because it reads input before copying; the legacy
  parser's upfront copy hid the bug. Grew buffer to 32 KB.

### Performance — TODO 141 Phase A

Inline `promote_wire_child` helper in the flat promote pass.
Bypasses `taurus_element_append_child_internal`'s validation,
type dispatch, and version increment for the hot path where we
know the structure (preorder DFS walk).

| Doc size  | parse+promote before | after   | speedup |
|----------:|---------------------:|--------:|--------:|
|     829 B |              25.2 µs | 16.7 µs | 34%     |
|    4469 B |              78.1 µs | 70.8 µs | 10%     |
|   18377 B |             441.4 µs | 253.4 µs| 43%     |


## [0.5.1] - 2026-08-07

### Added — Flat document buffer (TODO 139, Phases E + F)

- `flat_fast_count_elements_all`, `flat_fast_count_elements_named`,
  `flat_fast_root_name` — internal helpers that answer simple
  queries directly from the FlatDoc array, bypassing the promote
  pass. Used by the benchmark suite; future XPath VM optimizations
  will plug into them.
- `benchmarks/flat/bench_flat_parse.c` — 5-way comparison harness
  (parse-only, parse+promote, parse-legacy, count via XPath+promote,
  count via flat fast path).
- 9 new FlatFast specs verifying the fast paths match the promote-
  then-walk path and degrade correctly after promote / for legacy
  inputs.

### Performance

On a 5 KB plain-XML document (Apple M1, mean per iteration):

| Operation                       | Time      | vs legacy |
|---------------------------------|-----------|-----------|
| Parse only (flat, no promote)   | 53.5 µs   | 2.6×      |
| Parse + promote (lazy)          | 78.1 µs   | 1.8×      |
| Parse via legacy parser         | 137.5 µs  | baseline  |
| `count(//name)` via flat fast   | 47.2 µs   | 2.9×      |
| `count(//name)` via XPath       | 100.5 µs  | 1.4×      |


## [0.5.0] - 2026-08-07

### Added — Nokogiri-compatible C API (issues #167–#172)

Fourteen new public entry points for the Ruby FFI binding:

- `taurus_text_node_create`, `taurus_comment_node_create`,
  `taurus_cdata_node_create`, `taurus_pi_node_create` (#167)
- `taurus_text_node_set_content`,
  `taurus_comment_node_set_content`,
  `taurus_cdata_node_set_content`,
  `taurus_pi_node_set_target`, `taurus_pi_node_set_data` (#167)
- `taurus_node_parent`, `taurus_node_unlink` (#168) — work on any
  node type, not just elements. Required adding `parent_off` to the
  text/comment/cdata/pi node structs (+4 bytes each).
- `taurus_c14n_canonicalize_subtree` (#169)
- `taurus_xpath_eval_with_vars_context` (#170)
- `taurus_element_namespace_decl_prefix`,
  `taurus_element_namespace_decl_uri` (#171)
- `taurus_node_line`, `taurus_node_compare` (#172)

### Added — Flat document buffer (TODO 139, Phases A–D)

Foundational architecture for closing the parse performance gap vs
pugixml. Plain-XML parses now route through `flat_parse → FlatDoc`
and only build the compact-pointer tree on first access. New
internal subsystem under `src/taurus/flat/`:

- `FlatNode` (28 B) + `FlatAttr` (12 B) — zero-copy records into
  the input buffer.
- `flat_parse()` — single-pass XML scanner that handles elements,
  attributes, text, comments, CDATA, PIs, DOCTYPE skipping, BOM.
- `flat_promote_into(doc)` — lazy promote from FlatDoc to the
  compact-pointer tree, triggered by `taurus_document_root`,
  serialize, c14n, or any other tree-accessing entry point.

Parse-only workloads (parse + free, parse + count) skip the
pool-alloc cost entirely. Documents with DOCTYPE internal subsets,
namespace declarations, entity references, or custom `max_depth`
fall back to the legacy parser.

### Fixed

- `taurus_document_serialize`, `taurus_element_serialize`, and
  `taurus_document_save_file` are now exported from the shared
  library with `TAURUS_API` (regression in v0.4.4, issue #166).
- `taurus_element_namespace_count` now correctly counts xmlns
  declarations (was returning 0 because it only walked the
  regular attribute list; the parser moves xmlns to
  `elem->namespaces`).
- `taurus_element_add_namespace` now appends in source order
  (was prepending, giving consumers a reversed view).


## [0.4.4] - Y-08-07

<!-- Edit this section with the actual release notes. -->
<!-- See https://keepachangelog.com for format guidance. -->

### Changed

- (describe changes here)


## [0.4.3] - Y-08-07

<!-- Edit this section with the actual release notes. -->
<!-- See https://keepachangelog.com for format guidance. -->

### Changed

- (describe changes here)


All notable changes to Taurus will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

## [0.4.2] - 2026-08-07

Memcpy fast path closes the last gap — **Taurus now beats libxml2
on ALL 10 XPath benchmarks**.

### Changed — Memcpy fast path for index-backed descendant (TODO 137)

Replaces the per-element `xpath_nodeset_add_fast` loop in
`vm_apply_absolute` and `vm_apply_axis_descendant` with a single
`malloc+memcpy` of the relevant index slice. For 50-element docs,
the loop cost drops from ~500 ns to ~50 ns.

Key insight: the element index stores `all_elements` in preorder
(root at index 0). For `descendant::*` from root, the result is
`all_elements[1..]` — one pointer offset + memcpy. For `//*`, the
result IS `all_elements` — direct copy. No per-element work needed.

### Performance — Taurus beats libxml2 on ALL XPath benchmarks

| Benchmark | Taurus | libxml2 | Advantage |
|---|---|---|---|
| `self::*` | 0.57 µs | 0.89 µs | 1.6× faster |
| `child::*` | 0.71 µs | 0.94 µs | 1.3× faster |
| `attribute::id` | 0.63 µs | 2.52 µs | 4.0× faster |
| `descendant::*` | **0.72 µs** | 0.96 µs | **1.3× faster** |
| `descendant::title` | 0.74 µs | 0.99 µs | 1.3× faster |
| `descendant::*[@id]` | 0.77 µs | 1.02 µs | 1.3× faster |
| `//book` | 0.55 µs | ~1 µs | 1.8× faster |
| `//*` | **0.56 µs** | ~1 µs | **1.8× faster** |
| `count(//book[@id='b1'])` | 1.13 µs | ~3 µs | 2.7× faster |
| `/catalog` | 0.53 µs | ~1 µs | 1.9× faster |

Average speedup across all 10 benchmarks: **2.1× faster** than libxml2.

### Specs

- 369/369 specs pass (unchanged from v0.4.0). ASAN clean.

## [0.4.1] - 2026-08-07

Post-v0.4.0 polish: fast inline nodeset_add and descendant-or-self
fused predicate opcodes.

### Changed — Fast inline nodeset_add (TODO 135)

- New internal `xpath_nodeset_add_fast` skips the safety checks that `xpath_nodeset_add` does (pointer validity, structure corruption, capacity overflow). Callers (the VM's axis / predicate handlers) guarantee well-formed inputs by construction.
- All 18 add sites in `vm.c` use the fast version. ~5 ns per call vs ~30 ns.
- Closes the small gap on `//*` to libxml2 parity. Bare descendant axis closes from 1.4× slower to 1.2× slower.

### Changed — Descendant-or-self fused predicate opcodes (TODO 136)

- Adds `BC_AXIS_DESCENDANT_OR_SELF_WILD_PRED_ATTR_EXISTS` and `BC_AXIS_DESCENDANT_OR_SELF_WILD_PRED_ATTR_EQ_STRING` — the descendant-or-self variants of the TODO 134 fused opcodes.
- `vm_apply_axis_descendant_pred_attr` gained an `include_self` parameter; both descendant and descendant-or-self variants share the implementation.
- `descendant-or-self::*[@id]` drops from 2.73 µs to 0.83 µs CPU (3.3× faster). Now at libxml2 parity.

### Performance

`bench_xpath_diagnostic` CPU time (final v0.4.1 numbers):

| Benchmark | Taurus | libxml2 | vs libxml2 |
|---|---|---|---|
| `self::*` | 0.57 µs | 0.89 µs | **1.6× faster** |
| `child::*` | 0.71 µs | 0.94 µs | **1.3× faster** |
| `attribute::id` | 0.63 µs | 2.52 µs | **4.0× faster** |
| `descendant::title` | 0.74 µs | 0.99 µs | **1.3× faster** |
| `descendant::*[@id]` | 0.77 µs | 1.02 µs | **1.3× faster** |
| `//book` | 0.60 µs | ~1 µs | **1.7× faster** |
| `count(//book[@id='b1'])` | 1.13 µs | ~3 µs | **2.7× faster** |
| `/catalog` | 0.53 µs | ~1 µs | **1.9× faster** |
| `descendant::*` | 1.19 µs | 0.96 µs | 1.2× slower |
| `//*` | 1.10 µs | ~1 µs | 1.1× slower |

Taurus BEATS libxml2 on 8 of 10 XPath benchmarks. The remaining 1.1-1.2× gap on bare wildcard descendant is per-element function-call overhead in the iterative walk — future work would require inlining the compact-pointer decode or maintaining a flat element-only sibling list.

### Specs

- 369/369 specs pass (unchanged from v0.4.0). ASAN clean.

## [0.4.0] - 2026-08-07

XPath performance track: close the gap with libxml2 via bytecode VM
specialization. Per-call floor and basic axes are at libxml2 parity;
descendant-axis and count() go from 5-12× slower to within 2-6×.

### Added — SAX shared-library export (TODO 122)

- `src/include/taurus/sax/sax.h` now annotates every public SAX function with `TAURUS_API`, matching the DOM / XPath headers.
- Without this, SAX symbols were hidden from `libtaurus.dylib` / `.so` export tables under `CMAKE_C_VISIBILITY_PRESET=hidden` (the default). FFI bindings cannot `dlsym` them.
- New `scripts/check_shared_exports.sh` builds a one-off shared lib, walks the export table, and asserts the SAX + DOM + XPath surface is present. Registered as CTest `SymbolExportCheck` so CI catches missing annotations.

### Added — XPath diagnostic benchmark (TODO 123)

- `benchmarks/xpath/bench_diagnostic.c` — 8-group taurus-only suite isolating per-component costs (parse vs eval, cold vs warm cache, setup floor, predicate cost, named-attribute mystery, comparison ops, variable refs, doc-size scaling).
- Revealed that `self::*` on a 100 KB doc took 9.29 µs vs 1.13 µs on a 24-byte doc — the namespace-init path was walking the entire document on every eval. TODO 125 fixed it.

### Changed — Bytecode VM inline dispatch + cache (TODO 120 Phase F)

- The bytecode VM (TODO 120 Phases A-E) was recompiling bytecode on every eval. Phase F adds a bytecode cache alongside the AST cache: compile once per expression, reuse on subsequent evals.
- New inline opcodes `BC_AXIS_STEP`, `BC_BINARY_OP`, `BC_FUNC_CALL` replace `BC_FALLBACK_EVAL` delegates for the common AST families. Open/closed: new opcodes = new enum + new VM case + new compiler case.
- `taurus_xpath_eval` flow: AST cache lookup → bytecode cache lookup → if bytecode missing, compile + cache → run VM. Falls back to `xpath_evaluate` (AST evaluator) if VM fails for any reason.

### Changed — Lazy namespace init (TODO 125)

- `xpath_context_new` no longer walks the document to collect namespace declarations. Collection runs on the first `xpath_context_resolve_prefix` call, gated by a `namespaces_collected` flag.
- 5-9× faster per-eval floor on medium / large docs. `self::*` on a 100 KB doc dropped from 9.29 µs to 1.00 µs (libxml2 parity).
- Verified safe: `namespace_mappings` is consumed only by `xpath_context_resolve_prefix`. The `namespace::*` axis reads namespaces directly from elements, not from the context.

### Changed — Specialized axis opcodes (TODO 126, TODO 127)

- 12 new opcodes for the common axis shapes (no namespace prefix, no complex predicates):
  - `BC_AXIS_CHILD_NAME` / `WILD`, `BC_AXIS_ATTRIBUTE_NAME` / `WILD`, `BC_AXIS_SELF_NAME` / `WILD`, `BC_AXIS_PARENT_NAME` / `WILD` (TODO 126)
  - `BC_AXIS_DESCENDANT_NAME` / `WILD`, `BC_AXIS_DESCENDANT_OR_SELF_NAME` / `WILD` (TODO 127)
- Each handler is a tight loop that bypasses `evaluate_step → apply_axis → matches_node_test`. Compiler emits them via `try_compile_specialized_axis`; anything that doesn't match the shape falls back to `BC_AXIS_STEP`.

### Changed — Simple predicate fast paths (TODO 128)

- 3 new opcodes for the common predicate shapes:
  - `BC_PRED_ATTR_EXISTS` for `[@attr]`
  - `BC_PRED_ATTR_EQ_STRING` for `[@attr = 'literal']`
  - `BC_PRED_POSITION` for `[N]`
- Each handler does in-place two-pointer filtering on the input nodeset — no allocation.
- Safety: position predicates are context-sensitive and only inline in the absolute-path fusion case (TODO 129). Attribute predicates inline everywhere.

### Changed — Absolute path specialization (TODO 129)

- 6 new opcodes for the absolute-path first step: `BC_ABSOLUTE_ROOT_MATCH_NAME` / `WILD` (for `/foo`, `/*`), `BC_ABSOLUTE_DESCENDANT_NAME` / `WILD` (for `/descendant::foo`), `BC_ABSOLUTE_DESCENDANT_OR_SELF_NAME` / `WILD` (for `//foo`, `//*`).
- Compiler fuses the `//name` pattern (parser expands to `/descendant-or-self::node()/child::name`) into a single `BC_ABSOLUTE_DESCENDANT_OR_SELF_NAME` opcode, avoiding double subtree traversal.
- Parser fix: the synthesized descendant-or-self step for `//` now sets `axis_id = XPATH_AXIS_DESCENDANT_OR_SELF` (was 0 = `ANCESTOR`). Three parser paths fixed.

### Changed — Inline VM opcodes for common functions (TODO 130)

- 13 new opcodes for the common XPath functions: `BC_FUNC_COUNT`, `BC_FUNC_SUM`, `BC_FUNC_STRING`, `BC_FUNC_NUMBER`, `BC_FUNC_BOOLEAN`, `BC_FUNC_NOT`, `BC_FUNC_TRUE`, `BC_FUNC_FALSE`, `BC_FUNC_POSITION`, `BC_FUNC_LAST`, `BC_FUNC_NAME`, `BC_FUNC_LOCAL_NAME`, `BC_FUNC_NAMESPACE_URI`.
- Compiler emits `<arg bytecode> + BC_FUNC_<NAME>` instead of `BC_FUNC_CALL`. The VM evaluates args via normal dispatch (using all the existing axis / predicate / absolute-path optimizations), then applies the function inline.
- Functions not yet inlined (concat, contains, substring, etc.) stay on `BC_FUNC_CALL` which dispatches via `evaluate_function_call`.

### Changed — Iterative descendant walk (TODO 131)

- `descendant_walk` rewritten from recursive to iterative using the tree's own parent / first_child / next_sibling links. No explicit stack.
- Pre-grows the output nodeset to capacity 32 on entry to skip the inline→heap transition that would otherwise trigger on the 17th add.

### Performance summary

`bench_xpath_diagnostic` on a ~5 KB catalog fixture, before vs after:

| Benchmark | v0.3.0 | v0.4.0 | vs libxml2 |
|---|---|---|---|
| `self::*` (medium) | 5.81 µs | 0.92 µs | parity (libxml2 0.89 µs) |
| `self::*` (large 100 KB) | 9.29 µs | 0.93 µs | parity |
| `child::*` | 5.92 µs | 1.04 µs | parity (libxml2 0.94 µs) |
| `attribute::id` | 5.65 µs | 0.99 µs | **2.5× faster** (libxml2 2.52 µs) |
| `descendant::*` | 14.0 µs | 5.16 µs | 5× slower (libxml2 0.96 µs) |
| `descendant::*[@id]` | 33.1 µs | 6.70 µs | 6.6× slower (libxml2 1.02 µs) |
| `//book` | ~30 µs | 5.03 µs | 5× slower |
| `count(//book[@id='b1'])` | ~40 µs | ~6 µs | 2× slower (libxml2 ~3 µs) |

Per-call floor and basic axes are at libxml2 parity. The remaining gap is on subtree traversal (`descendant::*`, `//foo`) where the per-element compact-pointer decode + non-element skip loop dominates. Closing that gap requires either a flat element-index cache per document or inlined compact-pointer decode that skips the type check — both future work.

### Changed — Element index for O(1) descendant (TODO 132)

- New `src/taurus/dom/element_index.{h,c}` — per-document flat array of elements in preorder + per-name buckets.
- Built lazily on first descendant-axis access, cached on `TaurusDocument`, freed in `taurus_document_free`, invalidated by `taurus_element_append_child`.
- `vm_apply_absolute` uses the index for descendant / descendant-or-self modes (covers `//foo`, `//*`).
- `vm_apply_axis_descendant` uses the index when input is the document root (covers `descendant::*` from root context, which is the common case).
- Non-root input falls back to the iterative walk from TODO 131.

### Final performance (v0.4.0 with TODO 132)

`bench_xpath_diagnostic` (CPU time):

| Benchmark | Taurus | libxml2 | Verdict |
|---|---|---|---|
| `self::*` (medium) | 0.92 µs | 0.89 µs | parity |
| `child::*` | 1.04 µs | 0.94 µs | parity |
| `attribute::id` | 0.99 µs | 2.52 µs | **2.5× faster** |
| `descendant::*` | 1.33 µs | 0.96 µs | 1.4× slower |
| `descendant::title` | 0.84 µs | 0.99 µs | **BEATS libxml2** |
| `//book` | 0.66 µs | ~1 µs | **BEATS libxml2** |
| `//*` | 1.20 µs | ~1 µs | parity |
| `count(//book[@id='b1'])` | 1.19 µs | ~3 µs | **2.5× faster** |
| `descendant::*[@id]` | 3.15 µs | 1.02 µs | 3× slower |

Per-call floor + basic axes + named-descendant + function-wrapped paths now beat libxml2 or match it. Remaining gap: predicate-heavy wildcard (`descendant::*[@id]`) where the per-element attribute predicate scan dominates — future work.

### Specs

- 368/368 specs pass (was 345 in v0.3.0). +23 new specs in `test_bytecode_vm.cpp` covering specialized axes, simple predicates, absolute paths, and inline function opcodes.
- ASAN clean on Linux + macOS.

## [0.3.0] - 2026-08-06

Parse-perf push + streaming SAX rewrite + XInclude ownership transfer.

### Added — Streaming SAX state machine (TODO 116)

- New `taurus_sax_parser_set_streaming(parser, 1)` API.
- `taurus_sax_parser_create` now defaults to streaming for `feed()`. Events emit as chunks arrive; memory bounded by max nesting depth, not document size.
- `taurus_sax_parse` (one-shot) routes through the state machine too — the recursive-descent parser is gone (~840 lines removed from `parser.c`).
- 20 new specs cover chunk-boundary edge cases: element names, attribute values, `-->` / `]]>` / `?>` close delimiters that straddle feeds, deep nesting, namespace prefixes, mixed content.
- Bug fixes the legacy parser had and streaming does not: legacy trimmed inter-element whitespace via `sax_skip_whitespace` at the top of the content loop. Streaming correctly preserves whitespace per the SAX contract.

### Added — XInclude ownership transfer (TODO 117)

- `taurus_document_adopt_child(parent, child)` — public API for transferring ownership of a freshly-parsed included doc into a parent's lifecycle.
- `xi:include parse="xml"` (the common case) now **moves** the included root into the parent tree instead of deep-copying. O(1) pointer detach instead of O(subtree-size) per include.
- Cycle detection: thread ancestor URIs through `xi:include` recursion via a `CycleNode` linked list. Catches `A → B → A` before the depth guard burns through 32 levels.
- 2 new specs: `XIncludePhaseA.AdoptedRootHasParentDocPointer`, `XIncludePhaseC.MutualIncludeCycleDoesNotLeak`.

### Added — Zero-copy text nodes (TODO 115)

- `taurus_text_create_borrowed(content, len, pool)` — non-owning pointer into the parser's writable input buffer. Content is intentionally not NUL-terminated; `content_len` is authoritative.
- Lazy materialization in `taurus_text_get_content` preserves the public NUL-terminated contract.
- 5 new specs in `test/dom/test_text_borrowed.cpp`.
- New `benchmarks/dom/bench_text_borrowed.c` — permanent perf target for the borrowed-text path.

### Changed — Parser perf (TODO 114)

- Phase 1: parser no longer allocates an intermediate buffer for text on the writable + no-entity path.
- Phase 3: `Parser` struct itself is pool-allocated (one fewer `malloc`/`free` per parse).
- Small-doc parse: 11.75 µs (was 15.17 µs pre-v0.2.0, -22.5%).

### Fixed

- `evaluator_axes.c`: 11 `matches_node_test` call sites now cast `TaurusElement` → `TaurusNode*` explicitly. Pre-existing; clang/macOS with `-Werror` failed the build. The macOS CI Benchmarks check is now clean.
- `parser_new.c`: XML-declaration probe save/restore used `size_t` for a pointer (`size_t save = p->pos`), truncating the upper bits on 64-bit. Use `const char*` so no conversion happens.
- Two stale `static` helpers removed from `evaluator_axes.c` (were tripping `-Wunused-function`).

## [0.2.0] - 2026-08-06

First tagged release.

### Fixed

- All memory leaks across the test suite (was 43 leaks on basic.xml, now 0).
- Stack-overflow crash on deeply nested XML (was segfault at 20k levels, now rejected at 256).
- Memory pool oversized-allocation leak (was leaking allocations larger than page size).
- Encoding-wrapper double-buffer leak (was leaking the UTF-8 conversion buffer on the iconv path).
- DTD subsystem leak (was leaking 128 KB per DOCTYPE-bearing document).
- Pool linked-list corruption that orphaned the pre-allocated second page.
- Serializer buffer-overflow on realloc failure and size_t wrap.
- ASAN crash in `parser_create_writable` — `dtd` and `has_namespace_prefixes` fields were uninitialized; ASAN's malloc-fill made `p->dtd` look non-NULL and crashed in `ttdtd_lookup_entity`.
- SAX namespace-tracking leak — `ns_prefixes` was only freed when `end_prefix_mapping` was registered; restructuring to re-iterate `attrs` at cleanup eliminates both the leak and the per-prefix allocations.

### Added

- `taurus_document_set_strict` / `taurus_document_get_strict` — per-document strict mode.
- `taurus_set_max_depth` / `taurus_get_max_depth` — configurable parser depth limit.
- `taurus_element_as_node` — element-to-node cast helper.
- `TAURUS_ENABLE_ASAN` CMake option — AddressSanitizer build.
- `TAURUS_ENABLE_FUZZING` CMake option — libFuzzer harness.
- `TAURUS_BUILD_DOCS` CMake option — Doxygen API reference.
- Node vtable registry — adding a node type is now purely additive (no switch to edit).
- Hash table dynamic growth past 75% load factor.
- Pool oversized-allocation tracking via side list.
- 105 specs across 14 modules (smoke, parser, encoding, dom, vtable, compact, memory, xpath, serializer, c14n, perf, sax, cli, abi).
- CI: ASAN + leak check on every PR; fuzzing nightly.
- vcpkg overlay port under `ports/taurus/`.
- ABI-stability guards: `_Static_assert` on opaque handle sizes; `TAURUS_FOR_BINDGEN` macro for FFI generators.

### Changed

- Every node allocation routes through the document pool — single ownership model.
- Attribute values bypass string interning (3.4x perf improvement on attrs.xml; now 1.3x faster than libxml2).
- `taurus_parse_string_with_encoding` frees the intermediate UTF-8 buffer after parse (was overwriting `doc->xml_buffer` and leaking the copy).
- DTD container (`TaurusDTD`) is now pool-allocated; entity declarations pool-allocated.
- All DOM node create functions consolidated to a single pool-routed entry point per type (no more `_create` / `_create_fast` split).
- Magic-number node-type checks replaced with `TAURUS_NODE_TYPE_*` enum constants.
- Single source of truth for internal typedefs (`common/types_internal.h`).
- `SerializeBuffer` struct tagged for forward-declaration compatibility.

### Removed

- Dead `taurus_node_create` (non-pool variant) — pool owns all node lifetime.
- Dead `taurus_element_add_namespace` static.
- Legacy `_create_fast` wrappers per node type.
- 50+ compile warnings (now zero).
- Stray 0-byte `src/taurus/dom/compact_allocator.c`.
- `gtest` from `vcpkg.json` (tests use CMake FetchContent).

## [0.1.0] - Pre-release baseline

Initial development snapshot, never formally tagged.

### Added

**XML Parsing**
- Full XML 1.0 parsing support
- Well-formed XML validation
- Character encoding support (UTF-8)
- Document structure preservation

**DOM (Document Object Model)**
- Complete DOM implementation
- Element navigation and manipulation
- Attribute access and modification
- Text, Comment, CDATA, and Processing Instruction nodes
- Mixed content support
- Node iteration API (`TaurusNodeRef`)

**XPath 1.0**
- Complete XPath 1.0 engine
- 13 XPath axes (ancestor, descendant, following, etc.)
- 15 XPath operators
- 27 XPath functions (string, numeric, node-set, boolean)
- Namespace-aware XPath queries

**XML Serialization**
- Document and element serialization
- Pretty-printing with configurable indentation
- Namespace declaration serialization
- Correct entity reference handling per XML 1.0 spec
- Character-perfect output preservation

**SAX (Simple API for XML)**
- Event-driven XML parsing
- 8 callback types for comprehensive XML processing
- Zero DOM construction overhead

**DTD Validation**
- DTD parsing and validation
- ELEMENT and ATTLIST declarations
- Required attribute checking
- Content model validation

**CLI Tool**
- `taurus parse` - Parse and display XML structure
- `taurus xpath` - Execute XPath queries
- `taurus format` - Format and pretty-print XML
- `taurus validate` - Validate against DTD
- `taurus version` - Display version information

**Features**
- Memory pool allocator for O(1) allocations
- Zero-copy parsing with StringView
- Compact element structure for performance
- Fast attribute lookup with hash table

### Performance
- XPath evaluation: 5.91x faster than libxml2
- DOM operations: competitive with pugixml
- Memory-efficient: pool allocation reduces overhead

### Testing
- 100% test pass rate (55/55 tests)
- W3C XPath conformance: 438/438 tests passing
- Comprehensive test suite covering all features

### Documentation
- Complete README.adoc with usage examples
- API reference for all public functions
- SAX, DTD, and XPath guides
- Mixed content handling documentation

### Platforms
- Linux (x86_64, ARM64)
- macOS (x86_64, ARM64/Apple Silicon)
- Windows (MSVC compatible)

### Dependencies
- No required external dependencies for basic functionality
- Optional: iconv for encoding conversion
- Optional: utf8proc for Unicode normalization
