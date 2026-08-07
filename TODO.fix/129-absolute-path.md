# TODO 129 — Phase 6: Specialize absolute path (`/foo`, `//foo`)

## Goal

After TODO 125-128, the biggest remaining gap is on absolute paths:

    count(//book[@id='b1'])    35-40 us   vs libxml2 ~3 us   (12x slower)
    count(//book)              33 us      vs libxml2 ~3 us   (10x slower)

Absolute paths currently fall back to evaluate_expr →
evaluate_location_path, which goes through evaluate_step →
apply_axis → matches_node_test → apply_predicates. The Phase 5
predicate opcodes don't help because the path itself isn't
specialized.

## What's slow

For `//book`:
- evaluate_location_path does the document-root special case to
  match the first step against the root element.
- Then it iterates each step via evaluate_step.
- Each evaluate_step calls apply_axis which strcmp-dispatches the
  axis name.
- For descendant-or-self::node() (which `//` expands to), apply_axis
  recursively walks the subtree.
- Then the next step (child::book) iterates each result and walks
  its children.

So `//book` does TWO subtree walks (descendant-or-self, then for
each result child::*). That's wasteful.

For our impl, `//book` semantically = "all elements named book
at any depth". A single subtree walk suffices.

## Plan

Add opcodes for the common absolute-path first step:

- BC_ABSOLUTE_ROOT_MATCH_NAME  u16 operand: name
  For `/foo`: push [root] if root.name == foo, else push [].

- BC_ABSOLUTE_ROOT_MATCH_WILD
  For `/*`: push [root].

- BC_ABSOLUTE_DESCENDANT_NAME  u16 operand: name
  For `//foo` and `/descendant::foo`: walk subtree from root,
  return all descendants (excluding root) matching name.

- BC_ABSOLUTE_DESCENDANT_WILD
  For `//*`-shape: walk subtree, return all descendants.

- BC_ABSOLUTE_DESCENDANT_OR_SELF_NAME  u16 operand: name
  For `//foo` (post-parser-expansion): walk subtree from root,
  return root (if matches) + all matching descendants.

- BC_ABSOLUTE_DESCENDANT_OR_SELF_WILD
  For `//*`: return root + all descendants.

Compiler emits these for the first step of an absolute path when
the step shape matches. Subsequent steps use the Phase 3-5
specialized opcodes.

For `/catalog/foo`:
  BC_ABSOLUTE_ROOT_MATCH_NAME "catalog"   # push [root] if root is catalog
  BC_AXIS_CHILD_NAME "foo"                # filter children

For `//book[@id='b1']`:
  BC_ABSOLUTE_DESCENDANT_OR_SELF_NAME "book"
  BC_PRED_ATTR_EQ_STRING "id" "b1"

## Expected outcome

    benchmark                       Phase 5   Phase 6
    count(//book)                   33 us     ~7 us
    count(//book[@id='b1'])         35 us     ~8 us
    count(//* )                     ~30 us    ~6 us

Brings absolute-path-with-count into 3x of libxml2 (was 10x).

## Branch
`todo-129-absolute-path`
