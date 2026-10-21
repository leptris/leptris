#!/usr/bin/env python3
"""Verify locally built libleptris artifacts against a CI build manifest.

Consumer-side check for the reproducible-build manifests (Gate C3): the
build-manifests.yml workflow attaches build-manifest.json to each GitHub
Release; this script recomputes SHA256 over artifacts produced by a local
build and compares them against the manifest entry for one platform.

Usage:
    # Show which platforms a manifest covers, and what was recorded:
    python3 scripts/verify_manifest.py --manifest build-manifest.json --list

    # Verify one leg (recommended: build with the same flags as build.yml):
    python3 scripts/verify_manifest.py \
        --manifest build-manifest.json --platform linux-amd64 \
        --build-dir build

    --platform may be repeated; omit it to verify every platform in the
    manifest against the same --build-dir (multi-config dirs and per-leg
    build trees under one root also work: artifacts are matched by
    basename anywhere below --build-dir).

Exit status: 0 = every manifest artifact for the selected platform(s)
matched a local file byte-for-byte; 1 = at least one artifact is missing
or mismatched. A local file with no manifest entry is a warning, not a
failure (extra build outputs are allowed).

Manifest schema ("leptris.build-manifest/1", as produced by
.github/workflows/build-manifests.yml):

    {
      "schema": "leptris.build-manifest/1",
      "tag": "v1.9.367",                      // release tag built
      "generated_at_utc": "2026-10-21T00:00:00Z",
      "platforms": {
        "<platform>": {                       // key: matrix leg name, e.g. linux-amd64
          "leg": "native",                    //   native | amalgamated
          "compiler": "Apple clang version 15.0.0 ...",  // `cc --version | head -1`
                                              //   (Windows: "MSVC cl.exe <fileversion>")
          "cmake": "cmake version 3.28.3",    //   `cmake --version | head -1`
          "commit": "d904d400...",            //   engine commit the tag points at
          "version": "1.9.367",               //   project() VERSION at that commit
          "artifacts": {
            "<filename>": "<sha256 hex>"      //   every static/shared lib
                                              //   produced (symlinks skipped)
          }
        }
      }
    }

Platform keys shipped by the workflow: linux-amd64, linux-arm64,
macos-arm64, macos-x86_64, windows-amd64, amalgamated-linux-amd64.
"""
import argparse
import hashlib
import json
import re
import sys
from pathlib import Path

# Keep byte-identical to ARTIFACT_RE in .github/workflows/build-manifests.yml.
ARTIFACT_RE = re.compile(
    r"^(libleptris\.(a|dll|lib)|libleptris\.so(\.[0-9.]+)?"
    r"|libleptris\.[0-9.]+\.dylib|leptris\.(lib|dll))$")

SCHEMA = "leptris.build-manifest/1"


def sha256_of(path):
    h = hashlib.sha256()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def local_artifacts(build_root):
    """basename -> [Path, ...] for every artifact file below build_root."""
    found = {}
    for p in sorted(build_root.rglob("*")):
        if p.is_symlink() or not p.is_file():
            continue
        if ARTIFACT_RE.match(p.name):
            found.setdefault(p.name, []).append(p)
    return found


def list_platforms(manifest):
    print(f"manifest: {manifest.get('schema', '?')}  tag: {manifest.get('tag', '?')}")
    for platform, entry in sorted(manifest["platforms"].items()):
        print(f"  {platform}")
        print(f"    leg      : {entry.get('leg', '?')}")
        print(f"    compiler : {entry.get('compiler', '?')}")
        print(f"    cmake    : {entry.get('cmake', '?')}")
        print(f"    commit   : {entry.get('commit', '?')}")
        print(f"    version  : {entry.get('version', '?')}")
        for name, digest in sorted(entry["artifacts"].items()):
            print(f"    {digest}  {name}")


def verify_platform(platform, entry, build_root):
    failures = 0
    local = local_artifacts(build_root)
    if not local:
        print(f"[{platform}] FAIL: no leptris artifacts found under {build_root}")
        return 1

    expected = entry["artifacts"]
    for name, digest in sorted(expected.items()):
        matches = local.get(name)
        if not matches:
            print(f"[{platform}] FAIL: {name}: not found under {build_root}")
            failures += 1
            continue
        digests = {p: sha256_of(p) for p in matches}
        if digest in digests.values():
            hit = next(p for p, d in digests.items() if d == digest)
            print(f"[{platform}] ok  : {name} = {digest} ({hit})")
        else:
            printed = ", ".join(f"{p} = {d}" for p, d in digests.items())
            print(f"[{platform}] FAIL: {name}: manifest {digest} != local {printed}")
            failures += 1

    for name in sorted(set(local) - set(expected)):
        print(f"[{platform}] warn: {name} built locally but not in manifest")

    return failures


def main():
    ap = argparse.ArgumentParser(
        description="Verify local libleptris builds against a release "
                    "build manifest (schema in this script's docstring).")
    ap.add_argument("--manifest", required=True,
                    help="path to build-manifest.json")
    ap.add_argument("--build-dir", dest="build_dir",
                    help="local build tree to hash artifacts from")
    ap.add_argument("--platform", action="append", default=[],
                    help="platform key(s) to verify (repeatable; default: all)")
    ap.add_argument("--list", action="store_true",
                    help="list platforms recorded in the manifest and exit")
    args = ap.parse_args()

    manifest = json.loads(Path(args.manifest).read_text())
    if manifest.get("schema") != SCHEMA:
        print(f"FAIL: unsupported manifest schema "
              f"{manifest.get('schema')!r} (expected {SCHEMA!r})")
        return 1

    if args.list:
        list_platforms(manifest)
        return 0

    if not args.build_dir:
        ap.error("--build-dir is required (unless --list)")
    build_root = Path(args.build_dir)
    if not build_root.is_dir():
        print(f"FAIL: build dir not found: {build_root}")
        return 1

    platforms = args.platform or sorted(manifest["platforms"])
    unknown = [p for p in platforms if p not in manifest["platforms"]]
    for p in unknown:
        print(f"FAIL: platform {p!r} not in manifest "
              f"(known: {', '.join(sorted(manifest['platforms']))})")

    failures = len(unknown)
    for platform in platforms:
        if platform in manifest["platforms"]:
            failures += verify_platform(platform,
                                        manifest["platforms"][platform],
                                        build_root)

    if failures:
        print(f"verification FAILED ({failures} problem(s))")
        return 1
    print("verification PASSED")
    return 0


if __name__ == "__main__":
    sys.exit(main())
