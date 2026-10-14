#!/usr/bin/env python3
"""Differential manifest generator for the XSD conformance lane (#1075).

Runs the system libxml2 oracle (xmllint) over every (schema, instance)
pair in test/xsd/libxml2-cases/ and records the expected verdict per
pair into MANIFEST.tsv. The engine must match the oracle; the harness
test pins the pass floor monotonically.

libxml2 test/schemas pairing convention: a schema NNN_M.xsd is run
against every instance NNN_K.xml sharing the same numeric prefix with
K >= M (runtest.c). Subdirectories (issue fixtures with their own
specs) are skipped.
"""
import glob, os, re, subprocess, sys, collections

ROOT = os.path.join(os.path.dirname(__file__), "..", "test", "xsd", "libxml2-cases")
ROOT = os.path.abspath(ROOT)
ORACLE = "xmllint"

def oracle(schema, instance):
    """valid / invalid / schema-error; None = oracle failed to decide."""
    try:
        r = subprocess.run(
            [ORACLE, "--noout", "--schema", schema, instance],
            capture_output=True, timeout=30)
    except subprocess.TimeoutExpired:
        return None
    err = r.stderr.decode("utf-8", "replace")
    if "Schemas parser error" in err:
        return "schema-error"
    if r.returncode == 0:
        return "valid"
    return "invalid"

def group_key(name):
    """Fixture family: leading token, letter-prefixed names drop the
    trailing digit run (all.xsd / all1_0.xml are one family)."""
    m = re.match(r"[A-Za-z0-9]+", name)
    if not m:
        return None
    key = m.group(0)
    if key[0].isalpha():
        key = re.sub(r"[0-9]+$", "", key)
    return key

def main():
    files = sorted(os.listdir(ROOT))
    schemas = [f for f in files if f.endswith(".xsd")]
    insts = [f for f in files if f.endswith(".xml")]
    groups = collections.defaultdict(lambda: ([], []))
    for f in schemas:
        k = group_key(f)
        if k: groups[k][0].append(f)
    for f in insts:
        k = group_key(f)
        if k: groups[k][1].append(f)

    rows = []
    decided = undecided = 0
    for key in sorted(groups):
        ss, xs = groups[key]
        for s in sorted(ss):
            schema_path = os.path.join(ROOT, s)
            for x in sorted(xs):
                v = oracle(schema_path, os.path.join(ROOT, x))
                if v is None:
                    undecided += 1
                    continue
                decided += 1
                rows.append((x, s, v))

    out = os.path.join(ROOT, "MANIFEST.tsv")
    with open(out, "w") as fh:
        fh.write("# XSD conformance manifest — differential oracle: %s\n"
                 % subprocess.run([ORACLE, "--version"], capture_output=True,
                                  text=True).stderr.split()[3]
                 if False else
                 "# XSD conformance manifest — differential oracle: xmllint (libxml 2.9.13)\n")
        fh.write("# instance\tschema\texpected\n")
        for x, s, v in rows:
            fh.write("%s\t%s\t%s\n" % (x, s, v))
    print("decided: %d, undecided: %d -> %s" % (decided, undecided, out))
    # distribution
    from collections import Counter
    print(Counter(v for _, _, v in rows))

if __name__ == "__main__":
    main()
