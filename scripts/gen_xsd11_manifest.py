#!/usr/bin/env python3
"""XSD 1.1 manifest generator for the xsdtests baseline lane (#2148).

Reads a pinned w3c/xsdtests snapshot and produces
test/xsd/xsd11-cases/: the referenced instance + schema files (with their
transitive relative xs:include/xs:import dependencies, directory
structure preserved) plus MANIFEST.tsv.

The SUITE IS ITS OWN ORACLE: expected verdicts come from each test's
<expected validity> metadata — never from libxml2, which supports
little of XSD 1.1.

Selection rule (all of it, nothing silent):
  - every <testGroup> whose effective version is "1.1" (group version,
    else testSet version) across every *Meta/*.testSet in the snapshot;
  - every schemaTest and instanceTest whose <current> status is
    "accepted" and whose <expected> validity is "valid" or "invalid".
Manifest row: instance<TAB>schema<TAB>expected<TAB>family
  - instance "-" = schema-only row (a schemaTest): expected "valid"
    means the schema must compile clean; "schema-error" means the
    schema must be rejected (the suite's validity="invalid" on a
    schemaTest).
  - family: assertions / cta / override / openContent / core, mapped
    from the testSet file (1.1-only construct families vs the 1.1-core
    surface).

Skips are COUNTED IN THE MANIFEST HEADER, one class per counter:
  - testGroups not in the 1.1 collection;
  - tests with non-accepted <current> status (queried/submitted/stable);
  - <expected validity="notKnown"> (no absolute requirement);
  - schemaTests listing more than one schemaDocument (needs
    compile-as-a-set; the group's instanceTests are skipped with it);
  - tests whose schema references a non-relative (remote) schemaLocation.
precisionDecimal/PDecimal sets ride along even though suite.xml parks
them in extra-suite.xml — they are XSD 1.1 core datatype tests.
"""
import argparse
import os
import posixpath
import shutil
import xml.etree.ElementTree as ET
from collections import Counter

XLINK = "{http://www.w3.org/1999/xlink}href"

# testSet file stem (lowercased) -> 1.1 construct family
FAMILY = {
    "assert": "assertions",
    "assertion": "assertions",
    "assert.testset": "assertions",
    "constraintsonattribute": "assertions",
    "typealternatives": "cta",
    "typealternativesmixed": "cta",
    "conditionalinclusion": "cta",
    "cta": "cta",
    "override": "override",
    "cyclicredefineincludeimportoverride": "override",
    "opencontent": "openContent",
    "open": "openContent",
}


def local(tag):
    return tag.rsplit("}", 1)[-1]


def href_of(el):
    for k, v in el.attrib.items():
        if k == XLINK or k.endswith("href"):
            return v
    return None


def family_of(set_stem):
    return FAMILY.get(os.path.basename(set_stem).lower(), "core")


def schema_deps(path, suite_root):
    """Relative sibling schemaLocation refs declared by a schema
    document (xs:include / xs:import), as suite-root-relative paths."""
    out = []
    try:
        tree = ET.parse(path)
    except ET.ParseError:
        return out
    for el in tree.getroot().iter():
        loc = None
        for k, v in el.attrib.items():
            if local(k) == "schemaLocation":
                loc = v
        if not loc:
            continue
        loc = loc.split()[0] if " " in loc else loc
        if "#" in loc:
            loc = loc.split("#")[0]
        if not loc:
            continue
        if loc.startswith(("http:", "https:", "urn:", "/")):
            continue
        abs_path = os.path.normpath(os.path.join(os.path.dirname(path), loc))
        if os.path.isfile(abs_path):
            rel = os.path.relpath(abs_path, suite_root)
            if rel not in out:
                out.append(rel)
    return out


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--suite", required=True, help="pinned xsdtests checkout")
    ap.add_argument("--out", required=True, help="test/xsd/xsd11-cases dir")
    ap.add_argument("--sha", required=True, help="pinned snapshot SHA")
    args = ap.parse_args()

    suite_root = os.path.abspath(args.suite)
    out_root = os.path.abspath(args.out)
    os.makedirs(out_root, exist_ok=True)

    meta_files = sorted(
        f for f in os.listdir(suite_root)
        if f.endswith("Meta") and os.path.isdir(os.path.join(suite_root, f))
    )

    rows = {}            # (inst, schema, expected) -> family
    copied = set()       # suite-relative paths already copied
    skips = Counter()
    groups_11 = 0
    groups_total = 0

    def copy_rel(rel):
        if rel in copied:
            return
        copied.add(rel)
        src = os.path.join(suite_root, rel)
        dst = os.path.join(out_root, rel)
        os.makedirs(os.path.dirname(dst), exist_ok=True)
        shutil.copy2(src, dst)

    def usable_schema(meta_dir, href):
        """suite-relative schema path, or (None, reason)."""
        clean = href.split("#")[0]
        if not clean:
            return None, None
        if clean.startswith(("http:", "https:", "urn:", "/")):
            return None, "remote"
        abs_path = os.path.normpath(os.path.join(meta_dir, clean))
        if not os.path.isfile(abs_path):
            return None, "missing"
        return os.path.relpath(abs_path, suite_root), None

    for meta_dir_name in meta_files:
        meta_dir = os.path.join(suite_root, meta_dir_name)
        for set_file in sorted(os.listdir(meta_dir)):
            if not set_file.endswith(".testSet"):
                continue
            set_path = os.path.join(meta_dir, set_file)
            set_stem = set_file[: -len(".testSet")]
            fam = family_of(set_stem)
            try:
                root = ET.parse(set_path).getroot()
            except ET.ParseError:
                skips["unparseable_testSet"] += 1
                continue
            default_version = root.get("version", "1.0")

            for group in root:
                if local(group.tag) != "testGroup":
                    continue
                groups_total += 1
                version = group.get("version") or default_version
                if version != "1.1":
                    skips["testGroup_not_1_1"] += 1
                    continue
                groups_11 += 1

                # the group's schema: the first runnable schemaTest
                group_schema = None
                schema_ok = False
                for st in group:
                    if local(st.tag) != "schemaTest":
                        continue
                    expected = next(
                        (c for c in st if local(c.tag) == "expected"), None)
                    cur = next(
                        (c for c in st if local(c.tag) == "current"), None)
                    docs = [c for c in st if local(c.tag) == "schemaDocument"]
                    if expected is None:
                        skips["no_expected_metadata"] += 1
                        continue
                    validity = expected.get("validity")
                    status = cur.get("status") if cur is not None else None
                    if status != "accepted":
                        skips["status_not_accepted"] += 1
                        continue
                    if validity not in ("valid", "invalid"):
                        skips["validity_" + (validity or "absent")] += 1
                        continue
                    if len(docs) != 1:
                        skips["multi_document_schemaTest"] += 1
                        continue
                    href = href_of(docs[0])
                    if href is None:
                        skips["no_schema_document"] += 1
                        continue
                    rel, why = usable_schema(meta_dir, href)
                    if rel is None:
                        skips["schema_" + (why or "unusable")] += 1
                        continue
                    copy_rel(rel)
                    for dep in schema_deps(
                            os.path.join(suite_root, rel), suite_root):
                        copy_rel(dep)
                    expect = "valid" if validity == "valid" else "schema-error"
                    key = ("-", rel, expect)
                    rows.setdefault(key, fam)
                    if group_schema is None:
                        group_schema = rel
                        schema_ok = True

                if not schema_ok:
                    # the group's schema could not be established; its
                    # instance tests have nothing to run against
                    for it in group:
                        if local(it.tag) == "instanceTest":
                            skips["instance_group_schema_unusable"] += 1
                    continue

                for it in group:
                    if local(it.tag) != "instanceTest":
                        continue
                    expected = next(
                        (c for c in it if local(c.tag) == "expected"), None)
                    cur = next(
                        (c for c in it if local(c.tag) == "current"), None)
                    doc = next(
                        (c for c in it if local(c.tag) == "instanceDocument"),
                        None)
                    if expected is None or doc is None:
                        skips["no_expected_metadata"] += 1
                        continue
                    validity = expected.get("validity")
                    status = cur.get("status") if cur is not None else None
                    if status != "accepted":
                        skips["status_not_accepted"] += 1
                        continue
                    if validity not in ("valid", "invalid"):
                        skips["validity_" + (validity or "absent")] += 1
                        continue
                    href = href_of(doc)
                    rel_i, why = usable_schema(meta_dir, href)
                    if rel_i is None:
                        skips["instance_" + (why or "unusable")] += 1
                        continue
                    copy_rel(rel_i)
                    key = (rel_i, group_schema, validity)
                    rows.setdefault(key, fam)

    ordered = sorted(rows.items(), key=lambda kv: (kv[1], kv[0][1], kv[0][0]))
    fam_rows = Counter(f for _, f in ordered)

    header = [
        "# XSD 1.1 conformance manifest — oracle: the w3c/xsdtests suite"
        " metadata itself (NOT libxml2, which supports little of 1.1)",
        "# suite: w3c/xsdtests @ %s" % args.sha,
        "# selection: every testGroup version=1.1, every schemaTest and",
        "#   instanceTest with <current> status=\"accepted\" and",
        "#   <expected> validity in {valid, invalid} (%d of %d testGroups)"
        % (groups_11, groups_total),
        "# row: instance<TAB>schema<TAB>expected<TAB>family",
        "#   instance \"-\" = schema-only row (suite schemaTest):",
        "#     expected valid        -> the schema must compile clean",
        "#     expected schema-error -> the schema must be rejected",
        "#   expected valid/invalid on instance rows = document verdict",
        "# skips (explicit, never silent):",
    ]
    for k in sorted(skips):
        header.append("#   %-32s %d" % (k, skips[k]))
    header.append("# family rows: " + "  ".join(
        "%s=%d" % (f, fam_rows[f]) for f in sorted(fam_rows)))
    header.append("# files copied: %d" % len(copied))

    with open(os.path.join(out_root, "MANIFEST.tsv"), "w") as f:
        for line in header:
            f.write(line + "\n")
        for (inst, schema, expect), fam in ordered:
            f.write("%s\t%s\t%s\t%s\n" % (inst, schema, expect, fam))

    print("rows=%d groups_1.1=%d/%d copied_files=%d"
          % (len(ordered), groups_11, groups_total, len(copied)))
    print("families: " + "  ".join(
        "%s=%d" % (f, fam_rows[f]) for f in sorted(fam_rows)))
    print("skips: " + ", ".join(
        "%s=%d" % (k, v) for k, v in sorted(skips.items())))


if __name__ == "__main__":
    main()
