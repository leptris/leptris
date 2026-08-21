# TODO 116 — True streaming SAX parser (state machine)

**Priority**: P2
**Status**: Open. Supersedes TODO 89's incremental-feed approach.

## Why

TODO 89 (merged) added incremental feed via a growable internal
buffer. The buffer accumulates chunks and parses once `is_final` is
true. This bounds memory to document size — good — but doesn't emit
events until the entire document arrives. True streaming emits events
as chunks arrive; memory is bounded by max nesting depth, not
document size. Required for parsing documents larger than RAM.

## Plan

Convert the recursive-descent parser (`leptris_sax_parse`) from a
function-call stack to an explicit state machine that can be resumed
mid-token. The states roughly map to today's recursive calls:

```
state TopLevel          -- outside any element
state InXmlDeclaration  -- inside <?xml ... ?>
state InElementOpen     -- inside <tag attr...>
state InAttrName
state InAttrEq
state InAttrValue
state InElementChildren -- between > and </tag>
state InComment
state InCdata
state InPi
state InDoctype
state InClosingTag
```

Each state has a `feed(chunk, is_final)` method that consumes as
much input as it can, accumulates partial tokens in scratch buffers,
and emits events when complete tokens arrive.

### Migration strategy

1. Implement the state machine alongside the existing recursive
   parser. Feature-flag the new path behind
   `leptris_sax_parser_set_streaming(parser, 1)`.
2. Run both parsers on the W3C XML test suite. Output must match.
3. Once confidence is high, make the state machine the default and
   remove the recursive path.

### Scratch buffers

Each state that accumulates partial tokens needs a scratch buffer:
- InAttrName: pending name bytes
- InAttrValue: pending value bytes (and entity decode state)
- InComment / InCdata / InPi: pending content bytes

These buffers grow on demand and reset on token completion. Total
scratch is bounded by `max(name length, attr value length, comment
length)`.

### Depth guard

Today's recursive parser uses the C stack for nesting. The state
machine needs an explicit element-name stack (8 KB for 256-deep
nesting at 32 B/entry). When the stack overflows, emit a parse error.

## Acceptance

- 1 GB document can be parsed in 1 MB chunks with constant memory.
- Events fire as chunks arrive (no buffering-until-final).
- All existing SAX conformance tests pass.
- Throughput matches or beats the current ~5.7 µs / 4 KB SAX benchmark.
