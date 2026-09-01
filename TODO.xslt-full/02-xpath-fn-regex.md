# 02 — XPath fn: catalog: regex trio (#691-B)

matches(input, pattern[, flags]), replace(input, pattern, replacement[, flags]),
tokenize(input, pattern[, flags]) — XPath 2.0+ semantics over the
existing POSIX ERE engine (REG_EXTENDED | REG_ICASE per flags; 'x'
flag = strip whitespace from pattern first; 'm' via REG_NEWLINE).
Replacement uses '$1'..'$9' capture references (translate to
POSIX-free manual splice: split replacement on \$N and stitch from
regmatch captures).
Windows: matches the portable-regex caveat (#686 family) — POSIX-
gated with loud errors, engine swap tracked in 02b.

Gate: Xslt30.FnRegex spec (Saxon-probed) + suite green.
