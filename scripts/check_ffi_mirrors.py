#!/usr/bin/env python3
"""FFI mirror drift gate (architecture review candidate D).

The binding mirrors (Ruby attach_function list, Python cdef) are
hand-maintained copies of the public interface. They have drifted
before: Ruby called an unattached function (latent NoMethodError)
and Python declared a signature that does not exist. This script
makes drift a CI failure instead of a runtime bug.

Checks (against the public headers, the single source of truth):
  1. PHANTOM   — a mirror declares a symbol no header declares.
  2. ARITY     — a mirror's argument count differs from the header's
                 parameter count.
  3. REQUIRED  — a documented core surface is missing from a mirror.

Exit 0 = clean; exit 1 = drift (each finding printed).

Usage: check_ffi_mirrors.py <repo-root>
"""

import re
import sys
from pathlib import Path

REQUIRED_CORE = [
    # document lifecycle + parse
    "leptris_parse_string",
    "leptris_document_free",
    "leptris_document_root",
    # element access
    "leptris_element_name",
    "leptris_element_text",
    "leptris_element_attribute",
    "leptris_element_attribute_count",
    "leptris_element_first_attribute",
    "leptris_attribute_next",
    "leptris_attribute_get_name",
    "leptris_attribute_get_value",
    "leptris_element_first_child_any",
    "leptris_element_next_sibling_any",
    # node typing (bindings hardcode kinds; the check keeps the walk)
    "leptris_node_get_type",
    "leptris_node_next_sibling",
    "leptris_node_as_element",
    # xpath
    "leptris_xpath_eval",
    "leptris_xpath_result_free",
    "leptris_xpath_result_count",
    "leptris_xpath_result_get",
    # strings
    "leptris_free_string",
]

# SAX is a Ruby-only surface today (issue #430: these were once
# missing from the Windows DLL — keep them pinned to the mirror that
# ships them). Python gains them with its SAX binding.
REQUIRED_PER_LANGUAGE = {
    "ruby": [
        "leptris_sax_parse",
        "leptris_sax_parser_create",
        "leptris_sax_parser_feed",
        "leptris_sax_parser_free",
        "leptris_sax_parser_set_streaming",
    ],
}

API_RE = re.compile(r"LEPTRIS_API\s+[^(;]+?\b(leptris_[a-z_0-9]+)\s*\(([^;]*?)\)\s*;", re.S)


def strip_comments(text):
    return re.sub(r"/\*.*?\*/", " ", text, flags=re.S)


def count_params(params):
    params = params.strip()
    if not params or params == "void":
        return 0
    return params.count(",") + 1


def parse_headers(root):
    declared = {}
    for hdr in (root / "src" / "include").rglob("*.h"):
        text = strip_comments(hdr.read_text(errors="replace"))
        for m in API_RE.finditer(text):
            name, params = m.group(1), m.group(2)
            if name in declared and declared[name] != count_params(params):
                print(f"HEADER CONFLICT: {name} declared with differing arity")
                sys.exit(1)
            declared[name] = count_params(params)
    return declared


def parse_ruby(root):
    """attach_function :name, [:t, :t, ...], :ret  ->  name -> arity"""
    text = (root / "bindings" / "ruby" / "lib" / "leptris.rb").read_text()
    out = {}
    for m in re.finditer(r"attach_function\s+:([a-z_0-9]+)\s*,\s*\[([^\]]*)\]", text):
        name, types = m.group(1), m.group(2).strip()
        out[name] = 0 if not types else types.count(",") + 1
    return out


def parse_python(root):
    """cdef declarations -> name -> arity (handles multiline params)."""
    text = (root / "bindings" / "python" / "pyleptris" / "_ffi.py").read_text()
    text = strip_comments(text)
    out = {}
    for m in re.finditer(r"\b(leptris_[a-z_0-9]+)\s*\(([^;]*?)\)\s*;", text, re.S):
        out[m.group(1)] = count_params(m.group(2))
    return out


def main():
    root = Path(sys.argv[1] if len(sys.argv) > 1 else ".")

    declared = parse_headers(root)
    if len(declared) < 100:
        print(f"MIRROR GATE: header parse looks wrong ({len(declared)} symbols)")
        return 1

    mirrors = {
        "ruby": parse_ruby(root),
        "python": parse_python(root),
    }

    failures = []
    for lang, mirror in mirrors.items():
        for name, arity in sorted(mirror.items()):
            if not name.startswith("leptris_"):
                continue
            if name not in declared:
                failures.append(f"PHANTOM   [{lang}] {name}: no such public symbol")
            elif declared[name] != arity:
                failures.append(
                    f"ARITY     [{lang}] {name}: mirror {arity} args, header {declared[name]}"
                )
        for req in REQUIRED_CORE + REQUIRED_PER_LANGUAGE.get(lang, []):
            if req not in mirror:
                failures.append(f"REQUIRED  [{lang}] {req}: missing from mirror")

    if failures:
        print("FFI MIRROR DRIFT:")
        for f in failures:
            print(f"  {f}")
        return 1
    print(
        f"FFI mirrors clean: {len(declared)} public symbols, "
        f"{len(mirrors['ruby'])} ruby + {len(mirrors['python'])} python declarations"
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
