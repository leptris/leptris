# TODO 46: CI workflow integration

**Priority**: P1 (release infrastructure)
**Status**: Planned
**Effort**: S

## Problem

The build has ASAN + libFuzzer harnesses (TODOs 35/40) but CI doesn't
run them.  Regressions would only be caught when a developer happens
to run the right command locally.

## Fix

Extend `.github/workflows/test.yml` (or add a new file) with three
jobs:

### 1. ASAN job (Linux)

```yaml
asan:
  name: ASAN
  runs-on: ubuntu-latest
  steps:
    - uses: actions/checkout@v4
    - uses: jwlawson/actions-setup-cmake@v2
      with: { cmake-version: '3.20.x' }
    - name: Setup vcpkg
      uses: lukka/run-vcpkg@v11
      with: { vcpkgGitCommitId: 'master' }
    - name: Configure
      run: cmake -B build -S . -DBUILD_TESTING=ON \
                 -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/.../vcpkg.cmake \
                 -DTAURUS_ENABLE_ASAN=ON
    - name: Build
      run: cmake --build build
    - name: Test under ASAN
      run: ctest --test-dir build --output-on-failure
      env:
        ASAN_OPTIONS: detect_leaks=1:halt_on_error=1:abort_on_error=1
```

### 2. Leak-check job (macOS)

Uses the `leaks` tool already in macOS:

```yaml
leaks:
  name: macOS leaks
  runs-on: macos-latest
  steps:
    - uses: actions/checkout@v4
    - uses: jwlawson/actions-setup-cmake@v2
    - name: Build
      run: |
        cmake -B build -S . -DBUILD_TESTING=ON -DTAURUS_BUILD_CLI=ON
        cmake --build build
    - name: Leak check
      run: |
        for f in basic.xml small.xml full.xml; do
          leaks --atExit -- build/cli/taurus parse test/fixtures/$f \
            | grep -q "0 leaks for 0 total" || exit 1
        done
```

### 3. Fuzzing job (nightly, Linux)

```yaml
fuzz-nightly:
  name: libFuzzer (nightly)
  runs-on: ubuntu-latest
  if: github.event.schedule == '0 0 * * *'  # cron
  steps:
    - uses: actions/checkout@v4
    - name: Install clang
      run: sudo apt-get install -y clang
    - name: Build fuzzer
      run: |
        CC=clang CXX=clang++ cmake -B build -S . -DTAURUS_ENABLE_FUZZING=ON
        cmake --build build --target fuzz_parse
    - name: Fuzz for 5 minutes
      run: |
        mkdir -p corpus && cp test/fixtures/*.xml corpus/
        ./build/fuzz_parse -max_total_time=300 corpus/
```

## Tests

CI infrastructure itself is the deliverable.  No new specs needed.

## Architecture notes

Three independent jobs catch different bug classes:

- **ASAN**: memory safety at runtime (use-after-free, overflow).
- **leaks**: lifetime correctness (post-process).
- **Fuzzing**: input-space coverage (mutation-based discovery).

Run ASAN + leaks on every PR (fast).  Run fuzzing nightly (slow).

## Verification

PRs trigger ASAN + leaks jobs.  Both must pass for merge.  Nightly
fuzzing reports any crashes as separate issues.
