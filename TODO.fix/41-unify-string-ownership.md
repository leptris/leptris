# TODO 41: Unify string ownership model

**Priority**: P3 (architecture — clarity + future-proofing)
**Status**: Design only — execution deferred
**Effort**: L

## Problem

The codebase has **three** string lifetimes in play:

1. **Document-buffer-owned** (StringView pointing into `doc->xml_buffer`).
   Zero-copy during parsing.  Cheap, but invalidates if the buffer is
   freed.
2. **Pool-owned** (`taurus_pool_strdup`, `taurus_sv_to_cstr_pooled`).
   Lives until pool destroy.  Currently used for node content,
   attribute values, etc.
3. **Heap-owned** (`taurus_strdup`, `taurus_sv_to_cstr`).  Currently
   leaks in most places; the few that don't leak are explicitly freed.

Mixed ownership is a constant source of bugs (the leaks I've been
chasing are all "wrong lifetime model for this string").  It also
makes the API confusing — callers don't know whether to free a
returned string or not.

## Fix (design)

### Target: one model — pool-owned

Every string reachable from a `TaurusDocument` is allocated from that
document's pool.  No exceptions.

### Migration path

1. **Audit every `strdup` / `calloc` for strings** that ends up
   reachable from a node, attribute, namespace, DTD entry, etc.
   Replace with `taurus_pool_strdup` / `taurus_pool_alloc`.
2. **Delete `taurus_strdup` and `taurus_sv_to_cstr`** from the
   internal API (keep as wrappers if needed during migration).
3. **StringView remains** as an internal optimization for parsing
   (zero-copy from xml_buffer).  But every StringView is converted to
   a pool-owned C string before being stored on a node.
4. **Public API**: every function that returns `const char*` documents
   that the string is pool-owned, freed by `taurus_document_free`.

### Trade-offs

- **Memory**: pool_strdup duplicates data that StringView pointed at.
  For typical XML (~10% strings repeat), this is a ~10% memory
  increase.  Acceptable.
- **Performance**: pool_strdup is one memcpy.  Faster than calloc +
  free cycle.
- **Clarity**: huge win.  One model, one free path.

### What this enables

After migration, several things simplify:

- The "string finalization" pass (TODO 25) goes away — there's no
  lazy conversion needed.
- The compact-pointer overflow table is the only place strings live
  outside the pool.
- ASAN + valgrind reports become trivial to read (no false-positive
  heap-vs-pool ambiguity).

## Tests

No new specs — the existing 59 cover correctness.  After migration,
they all still pass.

## Architecture notes

This is the capstone architectural improvement.  Combined with TODO 23
(vtable dispatch) and TODO 38 (document-scoped state), the codebase
reaches a clean state where:

- **One owner**: the document.
- **One dispatch**: the vtable.
- **One state**: document-scoped (no globals).

Each principle (OCP, MECE, DRY) is satisfied.

## Status

Design only.  Migration is mechanical but invasive — every `strdup` /
`calloc` in the codebase needs review.  Best done as a focused
multi-day refactor, not bundled with feature work.
