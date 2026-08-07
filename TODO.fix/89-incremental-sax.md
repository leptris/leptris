# TODO 89: Implement true incremental SAX parsing

**Priority**: P3 (feature — currently one-shot only)
**Status**: Stub
**Effort**: L

## Problem

`src/taurus/sax/parser.c:751`:

```c
/* TODO: Implement true incremental parsing in Session 2 */
```

`taurus_sax_parser_feed` is the incremental API but today it just
buffers all chunks until `is_final` is set, then parses in one shot.
True incremental parsing would emit events as chunks arrive.

## Plan

1. The SAX parser must be resumable — save its state at every
   `sax_advance` and be able to continue from there when more input
   arrives.
2. When the parser hits end-of-buffer mid-construct (e.g., inside an
   attribute value), it must remember what it was doing and resume
   when the next chunk arrives.
3. Spec: feed a 1MB document in 4KB chunks; assert the events fired
   match the events from a one-shot parse of the same document.

## Why this matters

Stream-processing multi-GB XML files (think OpenStreetMap, wikis)
without holding the whole document in memory is the whole point of
SAX. The current buffering defeats that.

## Acceptance

- `taurus_sax_parser_feed` works correctly with chunks as small as
  1 byte.
- Memory usage stays bounded regardless of document size (no full
  document buffered).
- SAX spec suite passes.
