# The libleptris Memory Model

This guide documents how libleptris allocates, who owns what, and which
calls hand back memory you must free. Every public function that returns
a string or handle carries a `Memory:` comment in `src/include/leptris.h`;
this page is the map of the underlying rules.

## The pool arena model

Every byte a parsed document references lives in one memory arena owned
by the document. There is no per-node allocation and no per-node free.

- **One allocation per document.** The parser sizes a contiguous arena
  from the input (`src/leptris/flat/direct_parse.c`), then creates an
  arena-backed pool over it (`leptris_pool_create_arena_backed` in
  `src/leptris/memory/pool.c`). Elements, attributes, text nodes, and
  strings are all bump-allocated inside that span, so any two allocations
  are within the arena size of each other by construction — the property
  the compact-pointer tree edges require.
- **O(1) allocation.** `leptris_pool_alloc` is a bump-pointer advance on
  the current page; the slow path (span exhausted) allocates a new page,
  still O(1). Requests larger than a page go to the allocator directly and
  are tracked on an oversized list so `leptris_pool_destroy` can free them.
- **No per-node free.** There is no `leptris_element_free` and no
  reference counting. Individual nodes are never released; removing a
  node from the tree returns its bytes to no one. This is deliberate —
  the allocator never needs to reason about partial teardown.
- **Batch release.** `leptris_document_free` destroys the pool and every
  byte reachable from the document in one pass: pages, oversized blocks,
  the input copy, and the document struct itself (which is also
  pool-allocated). Callers never free anything else.

Empty documents behave the same: `leptris_document_create` gives the
document its own arena-backed pool, and programmatic nodes created via
`leptris_element_create` land in it.

Ownership transfers exist for two composition cases:

- `leptris_document_adopt_child(parent, child)` keeps a freshly parsed
  child document alive until the parent is freed (used by XInclude).
- `leptris_document_absorb(dst, src)` moves `src`'s pool into `dst` so
  cross-document splices can move nodes by reference; after the call the
  `src` handle is handle-only and the memory dies with `dst`.

A foreign element passed across documents (for example to
`leptris_document_set_root`) is **adopted by copy**: nodes are pool-owned
by their document, so a move would dangle after the source document's
free. Always wrap the installed handle the API returns, never the
borrowed source pointer.

## String ownership

Every string-returning accessor falls into one of two classes. The
`Memory:` comment in the header says which, per function.

### Document-owned: do not free

Accessors returning `const char*` against a document-taking handle
return pointers into the document's arena. Examples:
`leptris_element_name`, `leptris_element_expanded_name`,
`leptris_comment_node_get_content`, the diff op accessors. They are
valid until `leptris_document_free` and must not be freed or modified:

```c
const char* name = leptris_element_name(root);  /* document-owned */
/* no free */
leptris_document_free(doc);                     /* releases name too */
```

Node handles are document-owned the same way
(`leptris_document_root`, `leptris_xpath_result_get`,
`leptris_node_children`): never free a node separately.

### Caller-owned: free with `leptris_free_string`

Functions that build a fresh string return `char*` and must be released
with `leptris_free_string` (which routes through the installed
allocator, so it matches how the string was made):

- `leptris_document_serialize` and the other serializers
- `leptris_xpath_result_string` (the string value of any result type)
- `leptris_node_get_xpath`
- `leptris_load_file` (the file buffer)
- `leptris_alloc_buffer` (when the caller keeps the buffer)

```c
char* xml = leptris_document_serialize(doc, NULL);
/* ... */
leptris_free_string(xml);
```

XPath results are their own class: `leptris_xpath_eval` returns a
caller-owned `LeptrisXPathResult` released with
`leptris_xpath_result_free` — but the node handles inside a nodeset
remain document-owned, so the result may be freed before the document,
never after.

### The borrowed-slice lifetime contract

During parse, text is handled as `LeptrisStringView` — a
`(data, length)` pair that borrows, not owns
(`src/leptris/common/string_view.h`). The mechanics:

1. The parser copies the input into the document's arena span (the
   parse scratch) and NUL-terminates tokens **in that copy** as it
   scans. Element names, attribute values, and text content are slices
   of this document-owned buffer — no per-string allocation happens.
2. What borrows what: node strings borrow from the document's private
   input copy; the document's copy borrows from nothing. A second
   pristine copy of the input is kept for line/column diagnostics,
   because the scratch gets NUL substitutions.
3. When a copy is made: entity-expanded content, interned strings (the
   intern table copies both key and value into the pool so nothing
   points into a reused buffer), and any string the public API returns
   as caller-owned.

Consequences:

- After `leptris_parse_string` returns, the document is self-contained;
  the header nevertheless documents `parse_string`'s input as valid for
  the call. `leptris_parse_string_inplace` is the zero-copy variant with
  a stricter contract: the document retains a reference to the caller's
  writable buffer, which the caller must keep alive for the document's
  lifetime (the parser writes NUL terminators into it).
- Nothing a `const char*` accessor returns survives
  `leptris_document_free`. Store copies, not pointers, if the data must
  outlive the document.

## Leak gates

Three layers catch leaks before they merge.

### CI

`.github/workflows/asan.yml` runs two jobs:

- **AddressSanitizer (Linux)** — full `ctest` under ASAN with
  `ASAN_OPTIONS=detect_leaks=1:halt_on_error=1:abort_on_error=1`.
- **leaks (macOS)** — builds and tests, then runs the CLI over fixtures
  under `leaks --atExit` and fails unless it reports
  `0 leaks for 0 total`.

The macOS job matters because LeakSanitizer's leak mode is not supported
on macOS ASAN; leaks there are only caught by this job (or Linux CI).

### Locally

```bash
# macOS
leaks --atExit -- ./build/test/c/test_dom

# Linux
valgrind --leak-check=full --error-exitcode=1 ./build/test/c/test_dom
```

Or configure an ASAN build and run any suite:

```bash
cmake -B build -S . -DLEPTRIS_ENABLE_ASAN=ON -DCMAKE_BUILD_TYPE=Debug
cmake --build build
ctest --test-dir build --output-on-failure
```

A finishing thread must not hold libleptris memory: call
`leptris_thread_cleanup` after the last `leptris_document_free` on that
thread to drain the per-thread caches and the pool-block recycler.

## The FFI / binding contract

`docs/FFI.md` states the boundary rules; the memory-relevant ones:

- **One release function.** A binding needs only `leptris_document_free`
  to release a parsed document. Everything reachable from the document
  dies with it.
- **Strings are read, then released per class.** Document-owned strings
  are copied out (or read in place) and never freed; caller-owned
  strings are freed with `leptris_free_string` before the binding object
  is collected.
- **Binding wrapper pointers are borrowed both ways.**
  `leptris_node_set_binding_wrapper` stores a pointer the binding owns;
  libleptris never dereferences or frees it. The binding must clear it
  or guarantee the wrapper outlives the document.
- **Finalizer-driven release must be exact.** The shipped Ruby binding
  does not use a GC finalizer for the document, precisely because a
  finalizer cannot tell an already-freed pointer from a valid one. If a
  binding uses finalizers, it must track freed state itself and release
  exactly once.

### The free-before-pool-recycle hazard

This is the #506-class failure for embedders. Destroyed arena blocks are
recycled: `leptris_pool_destroy` parks default-shape blocks on capped
thread-local lists and the next `leptris_parse_*` on the same thread
pops an exact-size match. This makes document churn cheap — and it
means **use-after-free does not crash at the bug site**. A stale handle
written after `leptris_document_free` lands inside the *next*
document's live tree and corrupts it silently; a double free hands the
allocator a block the next parse now owns.

Corollary hazards already guarded in the engine (do not reproduce them
in embedder code): a binding finalizer releasing a document after a
replacement document was created, and a caller freeing an absorbed
document's handle a second time.

## Rules for embedders

1. Free the document **last**. `leptris_document_free` after every
   borrowed pointer (nodes, document-owned strings, XPath results
   holding nodes into it) is dead.
2. Never free a `const char*` the API handed you. Only `char*` returns
   documented as caller-owned are yours — release those with
   `leptris_free_string`, once, on the same side of the boundary that
   received them.
3. Never write through a handle after its document is freed, and never
   free a document twice. With the block recycler, both corrupt the next
   parse instead of crashing.
4. Keep `leptris_parse_string_inplace`'s buffer alive and unmodified for
   the document's lifetime; prefer plain `leptris_parse_string` unless
   the zero-copy path is measured to matter.
5. Wrap installed handles, not borrowed sources — cross-document
   element adoption copies, so the handle you passed in may not be the
   node that got installed.
6. Call `leptris_thread_cleanup` before a worker thread that used
   libleptris exits.
7. Install custom allocators (`leptris_set_memory_management_functions`,
   or per-document `leptris_document_set_allocators`) before the first
   allocation they should govern, and route host-language allocations
   the engine will free through `leptris_alloc_buffer` so allocator and
   CRT always match.
