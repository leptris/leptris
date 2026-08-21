# TODO 93: Final architecture review

**Priority**: P1 (quality — close-out before tagging)
**Status**: In progress (TODO 70 is the placeholder)
**Effort**: M

## Checklist

### Layering

- [ ] CLI never touches DOM internals (only public API).
- [ ] Public API never exposes internal types.
- [ ] Core never reaches up to CLI.
- [ ] No `#ifdef` code guards in source — platform differences solved
      architecturally.

### Ownership

- [ ] Every heap allocation has a single owner.
- [ ] Pool ownership documented in `pool.h`.
- [ ] Public API documents ownership per-function ("Memory:" comments).
- [ ] ASAN run reports zero leaks.

### Dispatch

- [ ] Node-type dispatch goes through the vtable registry, not switches.
- [ ] Adding a node type is purely additive (one vtable entry, no edits
      to switch statements).

### Naming

- [ ] Public API uses `leptris_*` prefix consistently.
- [ ] Internal API uses `leptris_*` or subsystem-specific prefix.
- [ ] No name collisions with C standard library or libc.

### Build

- [ ] Zero warnings with `-Wall -Wextra`.
- [ ] clang-format clean (informational).
- [ ] cppcheck clean (informational, false-positives suppressed).

### Test coverage

- [ ] 100+ specs across all modules.
- [ ] Each public API function has at least one spec.
- [ ] ASAN + leak check pass on all specs.
- [ ] Fuzz harness builds and runs without crashes.

### Documentation

- [ ] README.adoc accurate and complete.
- [ ] CLI man pages up to date.
- [ ] Doxygen generates without warnings.
- [ ] CHANGELOG reflects all notable changes.

### ABI

- [ ] `_Static_assert` guards on opaque handle sizes.
- [ ] `LEPTRIS_FOR_BINDGEN` macro present where binding generators need it.
- [ ] Versioning: SOVERSION bumps on ABI breaks.

### Performance

- [ ] Benchmark parity or better vs libxml2 on representative inputs.
- [ ] No regressions vs last release.

## Acceptance

- All boxes checked OR explicitly waived with a documented reason.
- Tag the release.
