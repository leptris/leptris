# Jing-verified RELAX NG conformance cases

19 schema/instance triples: `{NNN}.rng` + `{NNN}.ok.xml` (valid) +
`{NNN}.bad.xml` (invalid), one per feature area (attributes, repeats,
choice, group/interleave order, data types, value, list, mixed, refs,
combine, nesting). Case names live in `meta.json`.

`jing-ref.json` records the reference verdicts: each entry is
`{ "NNN.ok" | "NNN.bad": { "rc": 0|1, "out": ... } }` — Jing's exit
code on that instance (`0` = valid, `1` = invalid) plus its message.
The table in `../test_rng_corpus.cpp` mirrors these verdicts; the
runner (`test_rng_corpus`) requires `leptris_rng_validate` to agree
with Jing on every instance.

## Regenerating after changing a case

```sh
cd test/rng/jing-cases
for f in ???.rng; do stem=${f%.rng};
  for kind in ok bad; do
    jing "$PWD/$f" "$PWD/$stem.$kind.xml" > /tmp/jing.out 2>&1
    echo "$stem.$kind rc=$?"
  done
done
```

Re-record the rc values into `jing-ref.json` and the `kCases` table
in `../test_rng_corpus.cpp`. Jing: https://relaxng.org/jcc/ (homebrew
`jing`). Schemas using `<data>` must carry
`datatypeLibrary='http://www.w3.org/2001/XMLSchema-datatypes'` or Jing
rejects the schema itself (`datatype "integer" from library "" not
recognized`).
