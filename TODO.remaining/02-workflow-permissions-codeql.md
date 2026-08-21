# 02 — Add explicit permissions to remaining workflows (CodeQL medium, alerts #8/#23–#28)

`actions/missing-workflow-permissions` is open for:
asan.yml, benchmark.yml, test.yml, build.yml, fuzz-nightly.yml.

checks.yml already carries `permissions: contents: read` — copy that
block into the five listed workflows (least privilege: contents:read
for PR checks; the release workflow needs its own set and already
has it).

Acceptance: alerts #8, #23, #24, #25, #28 closed; no workflow runs
with implicit default permissions.
