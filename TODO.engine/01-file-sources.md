# TODO.engine/01-file-sources — File-backed pull and iterparse sources

leptris_pull_new_file(path) / leptris_iterparse_new_file(path): the puller reads the input in PULL_SLICE chunks straight from a FILE* — huge documents stream off disk with memory bounded by the slice + one subtree (iterparse), no whole-document buffer. The memory constructors stay.

DONE 2026-08-24: leptris_pull_new_file(path) + leptris_iterparse_new_file(path).
The puller gained a FILE* source — slices are fread into a bounded
256-byte buffer, so both constructors stream off disk with no
whole-document buffer (iterparse stays bounded by the largest
subtree). Memory constructors unchanged. Specs: Pull.FileSourceMatchesMemorySource
+ Iterparse.FileSourceYieldsSubtrees (every event kind; missing-file
NULL). README notes the file variants.
