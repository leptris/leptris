# TODO.engine/04-ruby-release-input — leptris-ruby release bump computes major/minor/patch

Their release.yml accepts an explicit x.y.z only — the advertised major/minor/patch values are not computed from the current version. Read LIBLEPTRIS_VERSION (Rakefile) + gem version and compute the bump in the workflow, mirroring the leptris C-repo bump-version.sh contract.

DONE 2026-08-24: verified already fixed upstream — leptris-ruby's
release.yml bump job accepts x.y.z OR major/minor/patch (computed
from Leptris::VERSION, with a semver guard). No change needed;
TODO.md note updated.
