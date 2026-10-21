# Debian packaging for libleptris

This directory is the `debian/` metadata for building native `.deb`
packages of the engine: `libleptris1` (runtime shared library),
`libleptris-dev` (headers, CMake/pkg-config metadata, static library),
and `leptris` (the CLI).

dpkg tooling requires `debian/` at the **source-tree root**, but this
directory lives under `packaging/` to keep the repository root clean.
Before building, expose it at the root of an unpacked release tarball
(never commit a root-level `debian/` from this checkout):

```bash
VER=1.9.379
curl -L "https://github.com/leptris/leptris/archive/refs/tags/v${VER}.tar.gz" \
    -o "leptris_${VER}.orig.tar.gz"
tar xf "leptris_${VER}.orig.tar.gz"
cp -r packaging/debian "leptris-${VER}/debian"
cd "leptris-${VER}"
```

## Version bump

1. The `debian/changelog` version must be `<engine X.Y.Z>-<deb revision>`
   (start at `-1` for each new upstream version). The checked-in entry is a
   placeholder; regenerate before building:

   ```bash
   dch -v "${VER}-1" "New upstream release v${VER}"
   ```

2. Confirm `control`'s shared-library package name still matches the
   soname: CMake sets `SOVERSION ${PROJECT_VERSION_MAJOR}` and the output
   name to `libleptris.so.<MAJOR>` — so 1.9.x packages are `libleptris1`.
   If the major version changes, rename the binary package and the
   `libleptris1.install` file accordingly.

## Local build (debuild)

```bash
# inside the prepared tree
debuild -us -uc          # unsigned, local-only
# artifacts land in the parent directory:
#   libleptris1_*.deb  libleptris-dev_*.deb  leptris_*.deb  *.changes
lintian ../*.changes     # should come back clean or near-clean
```

## Clean-room build (pbuilder)

```bash
# one-time environment
sudo pbuilder create     # or: pbuilder-dist <series> amd64 create (ubuntu-dev-tools)

# per build: pass the source .dsc produced by debuild -S
debuild -S -us -uc
sudo pbuilder build ../*.dsc
```

`pbuilder` is the authoritative check: it builds in a minimal chroot with
only `Build-Depends` installed, so it catches missing dependencies that a
host `debuild` hides.

## What the build does

`debian/rules` drives `dh` with the CMake buildsystem and passes:

- `LEPTRIS_BUILD_SHARED=ON` — Debian splits the shared library across
  `libleptris1` / `libleptris-dev`. The project gates its library
  targets on its own options (src/CMakeLists.txt), not
  `BUILD_SHARED_LIBS`; upstream defaults to static-only.
- `BUILD_TESTING=OFF` — upstream tests fall back to
  `FetchContent(googletest)` when no system GTest target exists, which
  needs network access and is rejected in the Debian build sandbox.
  Turning tests back on requires wiring a system-libgtest path upstream.
- `LEPTRIS_ENABLE_UTF8PROC=ON` with `libutf8proc-dev` in `Build-Depends`.
- `LEPTRIS_ENABLE_ICONV=ON` — glibc provides iconv on Linux.

## Before any Debian submission

- [ ] Replace the placeholder `Maintainer` address in `control`/`changelog`
      with the real owner address.
- [ ] File the ITP (intent to package) bug and get a sponsor — owner call.
- [ ] `lintian` clean, `pbuilder` green on the target release(s).
- [ ] Verify symbol visibility/`shlibs` via `dpkg-shlibdeps` warnings.

Nothing here has been submitted to Debian yet — this is the in-repo
skeleton (Gate C5). Submission is an owner decision.
