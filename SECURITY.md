# Security Policy

libleptris is a parser and transformation engine: it reads untrusted
documents. Memory safety and predictable resource behavior on hostile
input are part of the product, not an afterthought.

## Supported versions

Security fixes land on the latest release of the current major line and
are shipped in the next patch release. The engine follows the lockstep
runtime rule — binding versions are `{engine-semver}.{binding-patch}` —
so a fix is considered delivered only when the corresponding binding
release carries the fixed engine.

| Version | Supported |
|---|---|
| latest `1.x` release | yes |
| any earlier release/tag | no (upgrade to the latest patch) |

## Reporting a vulnerability

**Do not open a public issue for a security report.**

Use GitHub's private vulnerability reporting on this repository:
**Security → Advisories → Report a vulnerability** (direct link:
<https://github.com/leptris/leptris/security/advisories/new>).

Please include, when available:

- the engine version (or binding version and its pinned engine),
- the input document/expression that triggers the problem,
- the build configuration (sanitizer output, platform, compiler),
- the observed impact (crash, out-of-bounds access, infinite loop,
  excessive memory, code execution, information disclosure).

If you report a bug that affects a binding, include both the binding
version and the engine version it pins; the engine is the shared
implementation and the fix belongs there first.

## What to expect

- **Acknowledgment** within 5 business days.
- **Triage and reproduction** in the private advisory thread. We may
  ask for the exact input and a minimized reproducer.
- **Fix and coordinated disclosure.** The fix lands on a private or
  public branch as circumstances require, is released through the
  normal release workflow, and the advisory is published with credit
  (unless you prefer to remain anonymous).
- **CVE assignment** is requested through the GitHub advisory when the
  issue warrants one.

Please give us a reasonable window to ship a patch before public
disclosure. We will keep you informed of the timeline in the advisory
thread.

## In scope

- The C engine: XML/HTML parsing, DTD validation, XPath, XSLT,
  XInclude, serialization, encoding conversion, SAX and pull APIs.
- The shipped CLI (`leptris`) and public C API surface.
- The FFI layer and descriptor surface consumed by the language
  bindings (leptris-ruby, leptris-py, leptris-ts, leptris-rs).
- Resource-exhaustion behavior on untrusted input (unbounded
  recursion, allocation amplification, algorithmic complexity) when it
  is reachable without a misconfiguration.

## Out of scope

- Vulnerabilities in third-party dependencies of the language
  bindings, unless the engine's handling of their output is unsafe.
- Builds with sanitizers or assertions disabled by the reporter.
- Reports that require a modified engine binary.
- Denial of service by simply feeding a very large document when the
  configured limits are explicitly disabled by the caller.

## Hardening in the repository

- **Zero required runtime dependencies** by default; `utf8proc` and
  `iconv` are optional compile-time features.
- **Sanitizers in CI**: AddressSanitizer with leak detection on Linux
  (`asan.yml`), plus the macOS `leaks` lane.
- **Fuzzing**: nightly libFuzzer runs over the parser
  (`fuzz-nightly.yml`), with the crash corpus triaged into regression
  specs; the XPath and mutation harnesses live alongside it in
  `test/fuzz/`.
- **Depth and size limits** enforced by the parser; all limits are
  explicit and documented in the public headers.
- **Conformance suites** (W3C XPath, libxslt, WHATWG html5lib, XSD,
  DTD) run on every change, so hardening fixes cannot regress silently.

## Recognition

We are happy to credit reporters in the published advisory and in the
release notes. Thank you for helping keep document parsing safe.
