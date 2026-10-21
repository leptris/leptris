# Differential corpus (TODO.SOTA A6 / Gate D3)

Universality proof for the three published bindings — **ruby**
(leptris-ruby), **python** (leptris-py), **typescript/wasm**
(leptris-ts) — driven from ONE curated corpus. Each runner parses every
document, evaluates the document's XPath, and prints one canonical line:

    docname|xpath_count|serialized_sha256

CI (`.github/workflows/differential.yml`) runs all three runners and
`diff`s their outputs **byte-for-byte**; any difference fails the job.

## Corpus (`corpus/`)

| Document | Reused from | Semantics exercised |
| --- | --- | --- |
| `basic.xml` | `test/fixtures/basic.xml` | default + prefixed namespaces, `&amp;`, CDATA, comment, PI, non-ASCII (café, ☃) |
| `namespaces.xml` | `test/fixtures/namespaces.xml` | two prefix bindings, prefixed-attribute XPath |
| `cdata.xml` | `test/fixtures/cdata.xml` | CDATA with markup chars, CJK/accents |
| `comments_pis.xml` | new | prolog/epilog comments + PIs, in-root comments/PIs |
| `dtd_model.xml` | new | internal subset (ELEMENT/ATTLIST `#REQUIRED`, ENTITY), entity resolution in content |
| `entities_dtd.xml` | new | multiple internal entities, entity use in attribute value, accented expansion text |
| `mixed_deep.xml` | new | mixed content, numeric charrefs (&#960;, &#10;) |
| `xpath_target.xml` | new | predicate XPath over attributes/status |

Per-document XPath (identical in all three runners — prefix bindings are
resolved from in-scope declarations, which all three bindings do):

    basic.xml          count(//ns:elem)
    cdata.xml          count(//text)
    comments_pis.xml   count(//comment())
    dtd_model.xml      count(//item[@sku])
    entities_dtd.xml   count(//title)
    mixed_deep.xml     count(//p/*)
    namespaces.xml     count(//b:book)
    xpath_target.xml   count(//order[@status='active']/item)

## Running locally

    ruby test/differential/run.rb                                   # gem install leptris
    python3 test/differential/run.py                                # pip install leptris
    (cd test/differential && npm install && node run.mjs)           # npm install leptris

All three must print identical lines; verify with:

    diff <(ruby run.rb) <(python3 run.py) && diff <(ruby run.rb) <(node run.mjs)

## Versions verified identical (2026-10-21, macOS arm64)

| Binding | Package | Engine |
| --- | --- | --- |
| ruby | gem leptris **1.9.379.2** | 1.9.379.2 |
| python | pip leptris **1.9.379.0** | 1.9.379 |
| node/wasm | npm leptris **0.1.22** | 1.9.379 |

Canonical output (all three, byte-identical):

    basic.xml|1|888f291b7062dbd2e416a68a6f9cf412da6426dbc8a39b372d22f32e1192598a
    cdata.xml|1|21e22f173b8f0f4169dd4e337a24201c75416ddae12b690050888ebf3f3b0b81
    comments_pis.xml|4|371574738a8871d267424cce08da1604f8e8184065625a078a38b0ff8f3e79f1
    dtd_model.xml|2|1bbdc0cc3af3e80236fe4f76af70799808a101a19aff8f96d5921826d643ef8d
    entities_dtd.xml|1|604c36ad520354ee4c68ad1d7f856f381bd4899b9b311028e2a08203498860e6
    mixed_deep.xml|2|9d82d393f1c2f4b5cf1ea0f0066c66dd4e873fe07272f518e2adf673cce8b13a
    namespaces.xml|1|ae82a39fd2b54643689602258164516fbd17edece497c7c3a2a93170f11f8253
    xpath_target.xml|3|45a56190351bc09a6834559da89ec89f5e57a6f22f3e1bfc760993bb0d2aaf2d

## Normalizations (stated, not hidden)

1. **Prolog strip — comparison is root-element-onward.** Serializers
   disagree on the pre-root prolog by design: ruby `to_xml` always emits
   an XML declaration (plus the DOCTYPE when present); the wasm `xml()`
   emit echoes the source prolog (declaration/DOCTYPE when the source had
   one); python `tostring` emits neither. Every runner slices its output
   at the root element start (first `<` followed by a name character) and
   hashes only from there. Consequence: declaration/DOCTYPE *retention*
   is not covered by this gate.
2. **No whitespace normalization is applied — none is needed.** All three
   bindings round-trip parsed text losslessly (identical inter-element
   whitespace, CDATA boundaries, comments, PIs). The corpus is stored
   byte-fixed, so no pretty-print defaults can leak in.
3. **DTD validator output is NOT covered.** The wasm binding exposes no
   DTD-validation API. DTD agreement is exercised indirectly:
   `dtd_model.xml` and `entities_dtd.xml` carry internal subsets, and the
   hashes prove identical internal-entity resolution in content and
   attribute values across all three.
4. **XPath count formatting.** `count()` results are integral; each
   runner prints the integer (ruby `Float#to_i`, python `int()`,
   wasm `Math.trunc`), never a float spelling.
5. **Encoding conversion is out of scope.** The wasm build compiles
   `iconv`/`utf8proc` out, so no non-UTF-8 (e.g. UTF-16) documents are in
   the corpus.
6. **Parse flags: defaults everywhere.** The wasm surface takes no parse
   options, so the gate pins all three to default parsing.

## Adding a document

Add the file to `corpus/`, then add its name + XPath to the `DOCS` /
`XPATHS` tables in **all three** runners (order matters: fixed corpus
order, no sorting), re-run all three, and update the verified-output
block above plus this README's tables.
