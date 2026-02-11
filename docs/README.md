# Taurus Documentation

This directory contains all documentation for the Taurus XML parser library.

## Directory Structure

```
docs/
├── api/                    # API documentation (auto-generated from headers)
│   ├── dom/               # DOM API reference
│   ├── xpath/             # XPath API reference
│   └── sax/               # SAX API reference
├── guide/                 # User guides
│   └── building.md        # Building and installation guide
├── developer/             # Developer documentation
│   ├── analysis/          # Feature analysis and design docs
│   ├── architecture.adoc  # Architecture overview
│   ├── performance/       # Performance analysis and optimization reports
│   ├── status/            # Development status reports
│   ├── testing/           # Testing documentation
│   └── archived/          # Historical documentation
└── research/              # External library research (libxml2, pugixml)
```

## Documentation for Users

If you're using the Taurus library, start with:
- **Building**: See `guide/building.md` for compilation and installation instructions
- **API Reference**: See `api/` for detailed API documentation

## Documentation for Developers

If you're contributing to Taurus development:
- **Architecture**: See `developer/architecture.adoc` for system architecture overview
- **Performance**: See `developer/performance/` for optimization strategies
- **Testing**: See `developer/testing/` for test suite documentation
- **Status**: See `developer/status/` for current development status

## Core Project Documentation

The following files are kept at the repository root:
- **README.adoc** - Main project README
- **CLAUDE.md** - Claude Code development guidelines
- **CHANGELOG.md** - Version history
- **LICENSE.md** - License information
- **VALIDATION.md** - W3C conformance validation results
- **CODE_OF_CONDUCT.md** - Community guidelines
- **RELEASE_NOTES_v*.md** - Release notes for each version
