# TODO 90: Link text nodes as children in compact parser

**Priority**: P3 (compact parser not wired in)
**Status**: Closed — academic only; compact_parser.c is not in the active call graph
**Effort**: 0

## Original concern

`src/leptris/parse/compact_parser.c:375`:

```c
/* TODO: Link text nodes as children */
/* For now, text nodes are not fully linked in the tree structure */
```

## Investigation outcome

`grep -rn "parse_compact_document\|compact_parse" src/leptris/leptris.c src/leptris/parse/parser_new.c`
returns **no matches**. The compact parser is compiled into the
library (it's listed in `src/CMakeLists.txt`) but **no entry point
calls into it**. The active parser is `parser_new.c`; documents flow
through `leptris_parse` → `parser_create_writable` →
`parser_parse_document` in `parser_new.c`. The compact parser is a
parallel implementation that was never integrated.

## Implications

- Fixing the text-node linking bug inside `parse_compact_element`
  would not change observable behavior — no code path reaches it.
- Wiring in the compact parser would require its own integration
  effort: command-line flag, performance validation, regression
  tests against the regular parser's output.
- Without that integration, the compact parser is best understood
  as **experimental code retained for reference**.

## Decision

**Close this TODO with no code change.** The compact parser is
documented in `docs/ARCHITECTURE_REVIEW.md` as "experimental, not
wired into the build's call graph."

Future work: either delete `compact_parser.c` (per the user's
"never delete source files" rule, this requires explicit approval)
or wire it in via a `--parser=compact` CLI flag (significant scope,
no demonstrated need).

## Acceptance

- The TODO comment in `compact_parser.c:375` is left as-is, marking
  the incomplete logic IF the file is ever activated.
- `docs/ARCHITECTURE_REVIEW.md` captures the current "not wired in"
  status under `parse/`.
