# TODO 109 — XPath engine doesn't traverse comment/PI nodes

**Priority**: P2 (correctness — XPath 1.0 spec compliance)
**Status**: open
**Effort**: M

## Problem

The parser preserves comments and processing-instructions in the DOM
tree (the serializer emits them faithfully), but the XPath engine's
descendant axes (`//`, `descendant::*`, `node()`) skip non-element
children. As a result:

```
XML:   <r><!-- one --><!-- two -->x<?pi target?></r>
//comment()              => 0 nodes   (expected 2)
//processing-instruction() => 0 nodes (expected 1)
/r/node()               => 0 nodes   (expected 4: 2 comments + text + PI)
```

The XPath 1.0 spec requires `node()` to match every kind of node in
the tree, including comments and PIs.

## Cause (initial inspection)

The descendant-axis walker in `src/taurus/xpath/evaluator_axes.c`
iterates `first_child`/`next_sibling` but filters via `node->type ==
TAURUS_NODE_TYPE_ELEMENT` for the `*` node test, and the `node()`
case in the node-test matcher appears to short-circuit on the same
filter. Needs a closer read.

## Fix

1. Audit `evaluator_axes.c` for every place that filters by element type.
2. `node()` should match every node; `*` matches every element; `comment()`
   matches every comment; `processing-instruction()` matches every PI;
   `text()` matches every text/cdata node.
3. Add specs in `test/xpath/test_xpath_conformance.cpp` (currently has
   a stub `TextMatchesTextNodes` only).

## Verification

```bash
cmake --build build && \
ctest --test-dir build --output-on-failure -R "XPathConformance"
```

After the fix, expand the conformance suite with `CommentMatchesComments`,
`ProcessingInstructionMatchesAny`, `ProcessingInstructionMatchesByTarget`,
and `NodeMatchesAnyKind`.
