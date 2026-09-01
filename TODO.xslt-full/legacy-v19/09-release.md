# 09 — v1.10.0 release (gated)

Preconditions (ALL):
- test/xslt/open_cases.txt EMPTY (205/205 libxslt suite)
- 45/45 XsltFull, 438/438 XPath conformance, full ctest green
  serially AND parallel (allowing only the documented macOS CLI
  spawn flake)
- ASAN (Linux) clean, leaks (macOS) clean, TSAN clean, Windows
  builds green
- Benchmarks: no >3% regression vs main; competitor table attached

Then: minor bump via the automated workflow
(gh workflow run release.yml -f next_version=1.10.0), real release
notes in the release PR, rebase-merge, verify tag + GitHub Release.
