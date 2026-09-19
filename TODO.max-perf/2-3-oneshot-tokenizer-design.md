# Levers 2+3 — compiled-dispatch + zero-copy one-shot emission (design)

Status: LANDED (2026-09-19, PR #1236). Slices 1+3 implemented;
slice 2's class LUT (`sxs_chartype`) + dense state switch already
existed. Measured: bench_iterparse full-doc 295->52-63us wall,
15.9->74-91 MB/s, 2.54->0.45-0.55us/event. Residual to the 0.15
stretch = iterparse per-event DOM construction (element create +
namespace resolution through the public element API), a consumer
lever, not string copies.
Owners: engine repo. Bench: `benchmarks/sax/bench_iterparse.c` (#1195).

## Problem

The pull/iterparse path costs 4.4x the compact DOM parse over the
same bytes (10KB medium: pull 54.7us vs DOM one-shot 12.3us).
Root cause (measured, #1178): every event string is copied twice —

1. streaming.c copies names/text/attrs into per-frame storage
   (scratch arena + inline name bufs), because a streaming feed()
   chunk may be freed once the call returns;
2. pull.c's cb_* strdups them again into queue events, freed at the
   next _next.

A falsified attempt (per-call arena staging in _next) ADDED a third
copy layer and measured slower (54.7 -> 67.6us) — churn-shaving the
wrong layer cannot win; the copies must not happen at all.

## Insight

One-shot parses (leptris_sax_parse / leptris_pull_new over an
in-memory buffer) have no interruption: the input buffer is
immutable (#1125), caller-owned, and alive for the whole iteration
(the pull API already documents input-lifetime for buffer mode).
Names and attribute names cannot contain entity references (XML
names are name-chars only), so in one-shot mode they are always
contiguous slices of the input — borrowable with zero copies.

Only entity-expanded payloads (attribute values, text with &...;)
must materialize; the common case has none.

## Design

### Slice events (one-shot only)

```
typedef struct {
    const char* ptr;   /* points INTO the input buffer */
    size_t      len;
} LeptrisSlice;

typedef struct {
    LeptrisPullEventType type;
    LeptrisSlice name;         /* element / PI target          */
    LeptrisSlice text;         /* ONLY if no entities: borrowed */
    char*        text_owned;   /* materialized when entities    */
    LeptrisSlice attr_flat[];  /* name,value pairs, names always
                                  borrowed; values borrowed when
                                  entity-free, else owned       */
} OneShotEvent;
```

Rule: a payload is borrowed iff the engine can prove (single scan,
memchr '&') that its span contains no '&'. One memchr per payload
replaces one malloc+memcpy+free — and '&' is absent from the vast
majority of real payloads.

### Compiled dispatch (lever 2)

The streaming machine's per-state if-chains (peek '<', '/', '!',
name-start, ws...) become a 256-entry class table + a state-indexed
goto/switch jump table:

- class LUT: 4 bits (WS, NAME_START, NAME_CHAR, SPECIAL masks) —
  one load classifies each byte (the #1177 LUT result generalized);
- state dispatch: `switch (state)` compiled to a jump table by the
  compiler once states are a dense enum — keep cases small and
  branchless-friendly (the hot tokenizer states are ~20).

The SAME token loop serves both modes: streaming keeps frames/carry
(one_shot=0); one-shot mode (one_shot=1) skips carry and frame
copies, emits slices, and materializes only entity spans.

### Consumer adaptation

pull.c gains a one-shot queue variant: cb_* check parser->one_shot
and enqueue slice events (no strdup); queue_reset_event frees only
owned pointers. Batch staging (#589) copies out of slices on
demand — the existing stage chain unchanged.

## Slices

1. **Slice events + one-shot flag** in streaming.c; pull one-shot
   queue. Acceptance: bench_iterparse full-doc 0.47us/event ->
   <=0.15us/event; no test changes (contracts identical).
2. **Class LUT + dense-state dispatch** in the tokenizer loop.
   Acceptance: bench_sax +5% minimum; no behavioral delta.
3. **memchr fast paths** for text/attr-value spans feeding the
   borrow check. Acceptance: entity-free docs near-memcpy speed.

## Non-goals

- No API changes; event lifetime contracts unchanged.
- Streaming feed() semantics untouched (frames/carry stay).
- No new allocation strategies in pull.c (the falsified lesson).
