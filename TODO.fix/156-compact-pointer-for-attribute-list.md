# TODO 156 — Compact pointer for attribute linked list

## Why

`struct taurus_attribute` uses a `struct taurus_attribute* next`
pointer (8 bytes). The element struct's child links use compact
int32 byte-offsets (4 bytes). Same idea, different storage. The
inconsistency:

1. Costs memory (8 bytes per attr instead of 4).
2. Couples attribute storage to the global heap layout (the next
   pointer can point anywhere), forfeiting the #261 contiguity
   guarantee.
3. Confuses the model: elements and attributes are both "tree
   nodes" but use different storage.

## Plan

### Phase A — int32 `next_off` for attribute list

Replace `struct taurus_attribute* next` with `int32_t next_off`
(byte offset from THIS attribute's address to the next). Direct
arithmetic, no overflow table (mirrors how `direct_parse` wires
element children).

### Phase B — Drop `first_attribute_off` + `last_attribute_off` redundancy

Today element has both `first_attribute_off` and
`last_attribute_off`. Drop the `last` — append-by-walk is cheap
because attribute lists are short (typically ≤ 10 attrs).

### Phase C — Optional: deduplicate attribute name strings

If the same attr name appears repeatedly (e.g., `xml:id` across
many elements), use the pool's `string_cache` to intern. Saves
memory but adds lookup overhead. Skip unless profiling shows wins.

## Risk

- All `attr->next` access sites must move to offset arithmetic.
- `taurus_element_get_first_attribute`,
  `taurus_element_attributes_count`, `xpath_attribute_index_*` all
  walk the list — must use new accessor.

## Expected impact

Small (saves 4 bytes per attribute, ~5% memory). Mainly a
consistency win that makes the code MECE — same storage style for
all tree edges.

## Status

Pending. Depends on TODO 155 (which already shrinks the element
struct); sequence this PR after 155 lands.
