# TODO 112 — parser drops leading PI immediately inside element

**Priority**: P3 (correctness)
**Status**: open
**Effort**: S

## Problem

When a `<?...?>` processing instruction is the FIRST child of an
element, the parser drops it silently. The serializer confirms the
PI is gone from the tree:

```
in:  <r><?pi1?><a/><?pi2?></r>     out: <r><?pi1?><a/><?pi2?></r>   (3 children)
in:  <r><?xml-stylesheet?><a/><?other?></r>
out: <r><a/><?other?></r>          (pi1 dropped)
in:  <r><?pi1?><?pi2?></r>         out: <r><?pi2?></r>               (pi1 dropped)
```

Comments and text don't trigger the bug — only leading PIs.

## Cause (initial inspection)

`src/taurus/parse/parser_new.c` parses children of an element in a
loop. Each iteration checks for closing tag, then parses one child.
The PI parsing path appears to consume but not append the first PI
correctly when `elem->first_child` and `elem->last_child` are both
still NULL. A quick look at the parse loop's PI branch (around
`parser_parse_pi`) is the next step.

The compact parser (`src/taurus/parse/compact_parser.c`) is a
separate code path that doesn't share this bug.

## Fix

Audit the PI append path in `parser_parse_element_impl`. Likely a
NULL-dereference or wrong-condition check on first child.

## Verification

```bash
cmake --build build
build/test/test_xpath_conf --gtest_filter="*ProcessingInstruction*"
```

After the fix, restore the simpler XML in the conformance specs
(remove the `<a/>` separator).
