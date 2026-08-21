# TODO 152 — Per-node user_data for FFI wrapper caching (#262)

## Why

Ruby FFI bindings lose 7× vs Nokogiri on nodeset-returning XPath
(100-node `//book` takes 88 µs vs Nokogiri's 13 µs). The bottleneck
is per-node wrapping: each node requires 1 FFI call to get the type
+ Ruby object allocation. With 100 nodes, that's 100 FFI calls.

If each node carried a `void* user_data` field, the binding could
cache the Ruby wrapper object on first access. Subsequent traversals
would find the cached wrapper without any FFI call.

## Plan

1. Add `void* user_data` to `LeptrisNode` base struct (+8 bytes per
   node). Element goes from 80→88 bytes. Text/comment/etc. also grow.

2. Add public API:
   - `leptris_node_get_user_data(node)` — returns the cached pointer.
   - `leptris_node_set_user_data(node, data)` — sets the cached pointer.

3. The binding sets `user_data` on first wrap. On subsequent access,
   it checks `user_data` first. If non-NULL, reuse the cached wrapper.

## Risk

- **Struct growth**: +8 bytes per node. For a 100K-element doc, that's
  +800 KB. The user previously stated "size doesn't matter much for
  speed." This is a memory/ABI tradeoff, not a speed tradeoff.
- **ABI break**: requires a minor version bump.
- **Thread safety**: `user_data` is not thread-safe. The binding must
  set it from the same thread that parses. Ruby's GIL ensures this.

## Status

Completed (v0.12.0). Shipped as `binding_wrapper` field on
LeptrisNode base struct. `leptris_node_get/set_binding_wrapper`
public API. Element struct: 80→88 bytes.
