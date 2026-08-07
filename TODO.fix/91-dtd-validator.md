# TODO 91 — DTD validator Phase 8+

**Priority**: P2 (feature gap)
**Status**: Phases 1-7 + #FIXED shipped; Phase 8+ pending

## What's shipped

The DTD validator at `src/taurus/dtd/{parser,model,resolver,content_check,validator}.c`
implements:

- **Element content models**: EMPTY, ANY, Mixed (`(#PCDATA | tag)*`),
  Element (`(a, b, c)` choice and sequence, with `?`, `*`, `+` quantifiers)
- **Attribute checking**:
  - `#REQUIRED` — error if missing
  - `#IMPLIED` — silently optional
  - `#FIXED "value"` — error if attribute present with different value;
    auto-fill if absent
  - Default values — auto-fill when attribute is absent
- **Attribute type validation**: CDATA, NMTOKEN, NMTOKENS, ID, IDREF,
  IDREFS, ENUMERATED (`(a|b|c)`)
- **Notation declarations**: `<!NOTATION ...>` parsed and stored

The engine passes the W3C XML conformance test suite for these cases.

## What's still missing (Phase 8+)

### ENTITY / ENTITIES attribute type

When an attribute is declared as type ENTITY, the validator must
verify that its value matches an unparsed entity declared in the
DTD (`<!ENTITY name SYSTEM "uri" NDATA notation>`).

Infrastructure is in place:
- `<!ENTITY ...>` is parsed and stored in `ttdtd_add_entity`
- `DTDEntityDecl` has a `notation_name` field for NDATA

Work needed:
- In `validator.c`'s attribute-type check, look up the entity
  for ENTITY/ENTITIES values
- Verify the entity is unparsed (has a notation)
- Verify the notation itself is declared

Estimated: 2-3 days.

### Parameter entities (`%pe;`)

Internal and external parameter entities. The DTD parser sees
`%name;` references today and skips them. Real expansion requires:

- Tracking parameter entity declarations (`<!ENTITY % pe "...">`)
- Substituting `%pe;` references in their declaration context
- Handling the well-formedness constraints around where parameter
  entities may appear (between markup, not within markup in the
  internal subset)

Estimated: 1-2 weeks (parameter entities are subtle).

### Choice-model backtracking on ambiguous content models

The current `match_seq` in `content_check.c` uses recursive
descent. For ambiguous content models like `(a, a?)*`, recursive
descent can be exponential on adversarial inputs. Real
implementation needs:

- A proper NFA or compiled state machine
- Memoization to avoid revisiting states
- Optional: rewrite to deterministic form (Brzozowski derivatives
  or similar)

Estimated: 1-2 weeks for the algorithm; verify against the XML
test suite.

### Conditional sections (`INCLUDE` / `IGNORE`)

`<![INCLUDE[ ... ]]>` and `<![IGNORE[ ... ]]>` in external
subsets. Mostly affects DTD-driven parsing workflows.

Estimated: 3-5 days.

## Acceptance

Each phase:

- W3C conformance test suite cases for that feature all pass
- New specs cover at minimum: positive case, negative case,
  error-recovery case, no-leak case
- Public API (`taurus_dtd_validate`) error reporting identifies
  the specific failure type
