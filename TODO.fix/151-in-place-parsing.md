# TODO 151 — In-place parsing (eliminate buffer copy)

## Why

`direct_parse` always copies the input buffer (`malloc(len+1)` +
`memcpy`). For callers who already own a writable buffer (Ruby FFI,
in-place parse API), this wastes one malloc + one memcpy per parse.

On a 38 KB document at 5 kHz parse rate (benchmark-ips), the copy
costs ~190 MB/s of memory bandwidth — a measurable fraction of total
parse time.

## Plan

1. Add `direct_parse_inplace(char* writable_buf, size_t len)` that
   uses the caller's buffer directly (no copy). Sets
   `doc->xml_buffer = writable_buf` with `xml_buffer_needs_free = 0`.

2. Wire `leptris_parse_inplace` to call `direct_parse_inplace` instead
   of delegating to `leptris_parse` (which copies).

3. The Ruby FFI binding can pass its Ruby String's internal buffer
   directly (FFI::MemoryPointer from `string.to_ptr`).

## Risk

The caller must ensure the buffer outlives the document. If the Ruby
GC frees the String while the document is alive, all zero-copy names
and attr values point to freed memory.

Mitigation: the Ruby binding can `dup` the string before passing it,
or use `String.new(original)` to create a copy that the GC manages.
The C API contract is clear: caller owns the buffer, document borrows.

## Status

Completed. `direct_parse_inplace(char* buf, size_t len)` added —
parses a caller-owned writable buffer without copying. `leptris_parse_
inplace` now calls it directly (was delegating to leptris_parse which
copies).
