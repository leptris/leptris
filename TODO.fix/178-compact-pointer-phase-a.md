# TODO 178 — Compact pointer Phase A (encoding primitives)

**Priority**: P0 (foundation for 179–182)
**Status**: scoped

Supersedes the high-level deferral note in [[169-compact-1-byte-in-page-pointers]].
This is the **Phase A** the user authorized as "Option B" plus the
foundation for C / D / E.

## Goal

Define the compact-pointer encoding contract independent of any
consumer. No migrations in this PR — just types, accessors, the
overflow side-table, and tests.

## Encoding contract

### `compact_pointer_1byte`

1-byte offset from the host struct's address to the target's
address, divided by alignment. For 8-byte-aligned allocations, the
1-byte value covers ±1024 bytes (256 positions × 8 bytes — covers
small pages; falls back to overflow table otherwise).

```c
typedef struct {
    uint8_t raw;   /* 0 = NULL */
} compact_pointer_1byte;
```

Sentinel `0` = NULL. 8-byte alignment guarantees no two distinct
nodes have offset 0 between them.

### `compact_string_2byte`

2-byte offset from a per-document string-pool base. Covers 64 KB
of interned strings per document — sufficient for all realistic
docs.

```c
typedef struct {
    uint16_t raw;   /* 0 = NULL */
} compact_string_2byte;
```

### Overflow side-table

For pointer offsets that exceed ±1024 bytes (large docs whose tree
span > 1 KB between adjacent nodes), fall back to a per-document
hash table:

```c
struct taurus_pointer_overflow {
    void* host_addr;          /* key — pointer to the host struct */
    uint16_t field_offset;    /* which field of the host struct */
    void* target_addr;        /* value */
};
```

Open-addressing hash, 50% load factor, grows by doubling. Looked up
only when `raw == 0xFF` (reserved sentinel for "see overflow table").

## Files

- `src/taurus/dom/compact_pointer.h` — type defs + inline accessors
- `src/taurus/dom/compact_pointer.c` — overflow table impl
- `src/taurus/dom/compact_pointer_test.c` — Google Test coverage

## Phases — one PR (this TODO)

### Phase 1 — Type definitions and inline accessors

```c
static inline void* cp1_get(const void* host, compact_pointer_1byte cp,
                             struct taurus_pointer_overflow* ovf);
static inline compact_pointer_1byte cp1_set(void* host, void* target,
                                             uint16_t field_offset,
                                             struct taurus_pointer_overflow** ovf_p);
```

`cp1_set` returns the encoded value and may allocate the overflow
table on first overflow.

### Phase 2 — Overflow table

Open-addressing hash on `host_addr + field_offset` key. Initial
capacity 16, doubling growth. Memory owned by the document.

### Phase 3 — Tests

- Encoding round-trip: every offset 0–1023 round-trips correctly.
- Overflow path: 10,000 nodes spanning > 1024 bytes apart works.
- NULL handling: `raw == 0` always returns NULL on read.
- Field-offset disambiguation: same host, two fields, both encode.

## Estimated impact

None yet — this PR is foundational. Enables 179–182.

## Risk

Low — additive only. No existing code paths touched.

## References

- Parent: [[169-compact-1-byte-in-page-pointers]]
- Next: [[179-compact-pointer-phase-b-nodes]]
