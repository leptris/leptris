# Packaging — "install libleptris" per platform

In-repo packaging artifacts (Gate C5). Everything here is additive — no
library changes — and **none of the upstream channels below have been
submitted to yet; each submission is an owner decision.**

| Channel | Artifact | Status |
|---|---|---|
| Homebrew | `packaging/homebrew/leptris.rb` (formula, source build) | in-repo, unsubmitted |
| Conan | `packaging/conan/` (recipe + test_package) | in-repo, unsubmitted |
| Debian | `packaging/debian/` (debhelper skeleton) | in-repo, unsubmitted |
| vcpkg | `vcpkg.json` at the repo root | already consumable |

Version in every recipe is the released engine tag `vX.Y.Z` (cut by
`.github/workflows/release.yml`); all three channels must be bumped
together when a release lands. Source of truth for the version:
`CMakeLists.txt` `project(leptris VERSION ...)`.

---

## Homebrew — `packaging/homebrew/leptris.rb`

Builds the engine + CLI from the release tarball
(`https://github.com/leptris/leptris/archive/refs/tags/v<VERSION>.tar.gz`)
with `LEPTRIS_BUILD_SHARED=ON` (the project's own gate for the dylib —
`BUILD_SHARED_LIBS` does not control its library targets), linking
Homebrew's `utf8proc` and the system `libiconv`. Bump procedure is
documented in the formula header (`url` + `sha256` edited together;
Homebrew derives the version from the URL).

Validate locally:

```bash
ruby -c packaging/homebrew/leptris.rb                    # syntax
brew install --build-from-source packaging/homebrew/leptris.rb
brew test leptris                                        # runs the test block
brew audit --formula packaging/homebrew/leptris.rb       # style + checks
brew style packaging/homebrew/leptris.rb
brew uninstall leptris
```

Publishing today (no homebrew-core PR needed): a personal or org tap —
`homebrew-leptris` with the same file at `Formula/leptris.rb` — gives
consumers `brew install leptris/leptris/leptris`.

Upstream (homebrew-core) acceptance, summarized: the project must be
notable (meaningful user base / downloads), stable official URL with a
stable checksum, open-source license, builds on supported macOS and Linux
CI runners, must not bundle vendored copies of dependencies Homebrew
already ships, and must pass `brew audit --strict` plus the formula
review process on a PR to Homebrew/homebrew-core. Archive-of-tag URLs are
accepted; pinned-to-commit URLs are not.

## Conan — `packaging/conan/`

Conan 2 recipe (`cmake_layout`, `CMakeToolchain` + `CMakeDeps`, default
package ID — no custom `package_id()`/`build_id()` overrides). Options
mirror the CMake switches: `shared`, `fPIC`, `with_utf8proc`
(`LEPTRIS_ENABLE_UTF8PROC`), `with_iconv` (`LEPTRIS_ENABLE_ICONV`),
`with_ducet` (`LEPTRIS_ENABLE_DUCET`, forced off without utf8proc). The
library-only build (`LEPTRIS_BUILD_CLI=OFF`) ships via
`cmake.install()` and exports the upstream `leptris::leptris` CMake
target and `leptris` pkg-config name. `test_package/` parses a document
and frees it.

Validate locally:

```bash
python3 -m py_compile packaging/conan/conanfile.py       # syntax
conan profile detect                                     # once
conan create packaging/conan --build=missing             # full build + smoke test
conan create packaging/conan -o 'leptris/*:shared=True' --build=missing
```

Publishing today: `conan create` produces a local cache package; an org
Artifactory/Cloudsmith remote makes `conan install --requires=leptris/1.9.379`
work for consumers.

Upstream (conan-center-index) acceptance, summarized: recipe goes into the
conan-center-index repository under `recipes/leptris/`, must follow CCI
recipe layout (`conanfile.py`, `test_package/`), build and pass the smoke
test for all supported configurations (Windows/macOS/Linux ×
static/shared × build types), use `conan.tools` only, fetch sources
reproducibly, declare license/topics, and pass CCI CI plus human review.
No build-id customization; the package ID must be the standard one.

## Debian — `packaging/debian/`

debhelper-compat 13 skeleton: `control` (three binary packages),
`rules` (dh + CMake, `BUILD_SHARED_LIBS=ON`, tests off — see the
README for why), `changelog` (placeholder version — regenerate with
`dch`), `install` files for the shared lib / dev files / CLI,
DEP-5 `copyright`, and `source/format`. The `packaging/debian/README.md`
documents the exact `debuild`/`pbuilder` flow (including the
"debian/ must live at the source root" step).

Validate locally (Linux, or a Debian container on macOS):

```bash
# inside a prepared source tree (see packaging/debian/README.md)
debuild -us -uc
lintian ../*.changes
sudo pbuilder build ../*.dsc     # authoritative clean-room check
```

Upstream (Debian) acceptance, summarized: an ITP (intent-to-package) bug,
a sponsor or DM/DD status, lintian-clean packaging, reproducible build,
upstream-source provenance (`orig.tar` + quilt patches), copyright in
DEP-5 format, and a maintainer of record. This skeleton is the starting
point; none of that process has started.

---

## Owner checklist (before any channel submission)

1. Pick the channel(s) and the maintainer identity of record.
2. Bump all formulas to the released tag in one commit.
3. Run the per-channel validation above on the target platforms.
4. File the upstream submission (tap PR / CCI PR / ITP bug) — each is a
   public, owner-approved action.
