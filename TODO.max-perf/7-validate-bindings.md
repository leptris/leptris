# Lever 7: Lane 16.6 — leptris validate CLI + bindings

Status: DONE (issue #1183). All three legs shipped and verified on
main:

- **CLI**: `cli/commands/validate.c` (286 lines) — RELAX NG +
  Schematron validation, stdin/file input; registered in
  `cli/main.c:97`, listed in the help text (`leptris validate`).
- **leptris-ruby**: `RelaxNG#validate/#valid?/#validate_errors/
  #validate_report`, `Schematron#validate/#valid?`,
  `DTD#validate/#valid?` (lib/leptris/xml/{relaxng,schematron,dtd}.rb).
- **leptris-py**: `RelaxNG`, `DTD`, `Schematron` exported from the
  package root (leptris/__init__.py).

Well-formedness validation is `leptris_parse_string` itself (line:col
error contract); no separate wrapper required.
