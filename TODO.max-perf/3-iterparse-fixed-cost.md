# Lever 3: #563 iterparse 6.9us fixed cost + error channel

Status: TODO (task #3). Per-call cost dominated by re-setup; cache
the scan environment per document (the exec-scoped eval-environment
pattern from task #4 applies), batch events (#585/#589 machinery).
