# dependency gates (spec-permitting divergences)

FULLY-NORMALIZED: the XQuery 1.0-era normalization form is gone from F&O 3.x; the satisfied=false twins run and expect FOCH0003. uca-fallback: the engine implements the strict F&O reading (unknown collation parameter values raise FOCH0002), not the optional lax simple/advanced-uca-fallback features.

Disposition: spec-permitting divergence — deliberate engine semantics
Pins: 41

## fn/contains.xml
- fn-contains-21
- fn-contains-23
- fn-contains-32
- fn-contains-33
- fn-contains-34
- fn-contains-36
- fn-contains-38
## fn/normalize-unicode.xml
- cbcl-fn-normalize-unicode-001
- cbcl-fn-normalize-unicode-006
## misc/UCACollation.xml
- UCA-collation-011
- UCA-collation-012
- UCA-collation-013
- UCA-collation-014
- UCA-collation-015
- UCA-collation-016
- UCA-collation-017
- UCA-collation-018
- UCA-collation-019
- UCA-collation-020
- UCA-collation-021
- UCA-collation-022
- UCA-collation-022a
- UCA-collation-023
- UCA-reorder-codes-002
- UCA-reorder-codes-003
## fn/collation-key.xml
- collation-key-009u
## fn/starts-with.xml
- fn-starts-with-21
- fn-starts-with-23
- fn-starts-with-32
- fn-starts-with-33
## fn/ends-with.xml
- fn-ends-with-21
- fn-ends-with-23
- fn-ends-with-32
## fn/substring-before.xml
- fn-substring-before-31
- fn-substring-before-33
- fn-substring-before-42
## fn/substring-after.xml
- fn-substring-after-31
- fn-substring-after-33
- fn-substring-after-42
- fn-substring-after-43
## fn/deep-equal.xml
- fn-deep-equal-arrays-13
