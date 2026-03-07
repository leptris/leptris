# Plan: Documentation Site for Taurus XML Library

**Status:** Planning
**Created:** 2026-03-07
**Branch:** feature/compact-element-structure

---

## Executive Summary

Create a professional documentation site for Taurus using a **hybrid architecture**:
- **MkDocs + Material theme** for guides, tutorials, and landing pages
- **Doxygen** for auto-generated API reference from header comments

This approach gives us:
- Beautiful, modern documentation site
- Comprehensive API reference extracted from existing header comments
- Easy-to-write guides in Markdown
- Fast builds and excellent search
- Mobile-friendly responsive design
- Free hosting on GitHub Pages

---

## Documentation Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                    Documentation Site                            │
│                    (MkDocs + Material)                           │
├─────────────────────────────────────────────────────────────────┤
│  Landing Page                                                    │
│  ├── Features overview with code examples                        │
│  ├── Performance highlights (vs libxml2/pugixml)                │
│  ├── Quick start guide                                           │
│  └── Installation badges                                         │
├─────────────────────────────────────────────────────────────────┤
│  Getting Started                                                 │
│  ├── Installation (vcpkg, CMake, package managers)               │
│  ├── Building from Source                                        │
│  ├── Quick Start Tutorial                                        │
│  └── Integration Examples                                        │
├─────────────────────────────────────────────────────────────────┤
│  User Guides                                                     │
│  ├── XML Parsing Guide                                           │
│  │   ├── Copy mode vs Inplace mode                               │
│  │   ├── Error handling                                          │
│  │   └── Character encoding                                      │
│  ├── XPath Query Guide                                           │
│  │   ├── Basic queries                                           │
│  │   ├── Functions (27 functions documented)                     │
│  │   ├── Axes (13 axes documented)                               │
│  │   └── Variables                                               │
│  ├── SAX Streaming Guide                                         │
│  │   ├── Event handlers                                          │
│  │   ├── Memory-efficient parsing                                │
│  │   └── Incremental parsing                                     │
│  ├── StAX Writer Guide                                           │
│  │   ├── Basic usage                                             │
│  │   ├── Pretty-printing                                         │
│  │   ├── Namespaces                                              │
│  │   └── Performance tips                                        │
│  └── Memory Management                                           │
│      ├── Pool allocation                                         │
│      ├── Document lifecycle                                      │
│      └── Thread safety                                           │
├─────────────────────────────────────────────────────────────────┤
│  Performance                                                     │
│  ├── Benchmarks                                                  │
│  │   ├── vs libxml2 (parsing, XPath, writing)                    │
│  │   └── vs pugixml (DOM operations)                             │
│  ├── Optimization Tips                                           │
│  └── Memory Footprint                                            │
├─────────────────────────────────────────────────────────────────┤
│  API Reference (Doxygen generated)                               │
│  ├── /api/dom/                                                   │
│  │   ├── Document API                                            │
│  │   ├── Element API                                             │
│  │   └── Node API                                                │
│  ├── /api/xpath/                                                 │
│  │   ├── Query API                                               │
│  │   ├── Variables API                                           │
│  │   └── Functions reference                                     │
│  ├── /api/sax/                                                   │
│  │   └── SAX Parser API                                          │
│  ├── /api/writer/                                                │
│  │   └── StAX Writer API                                         │
│  └── /api/types/                                                 │
│      └── Type definitions                                        │
├─────────────────────────────────────────────────────────────────┤
│  Reference                                                       │
│  ├── CLI Tool                                                    │
│  ├── Changelog                                                   │
│  ├── Migration Guide (from libxml2)                              │
│  └── FAQ                                                         │
└─────────────────────────────────────────────────────────────────┘
```

---

## File Structure

```
docs/
├── mkdocs.yml                    # MkDocs configuration
├── requirements.txt              # Python dependencies
│
├── docs/                         # MkDocs content
│   ├── index.md                  # Landing page
│   │
│   ├── getting-started/
│   │   ├── index.md              # Getting started overview
│   │   ├── installation.md       # Installation methods
│   │   ├── building.md           # Building from source
│   │   ├── quickstart.md         # 5-minute tutorial
│   │   └── integration.md        # CMake integration
│   │
│   ├── guides/
│   │   ├── index.md              # Guides overview
│   │   ├── parsing.md            # XML parsing guide
│   │   ├── xpath.md              # XPath query guide
│   │   ├── sax-streaming.md      # SAX parser guide
│   │   ├── stax-writer.md        # StAX writer guide
│   │   ├── memory.md             # Memory management
│   │   ├── namespaces.md         # Namespace handling
│   │   └── error-handling.md     # Error handling
│   │
│   ├── performance/
│   │   ├── index.md              # Performance overview
│   │   ├── benchmarks.md         # Benchmark results
│   │   ├── optimization.md       # Optimization tips
│   │   └── memory-footprint.md   # Memory analysis
│   │
│   ├── reference/
│   │   ├── cli.md                # CLI tool reference
│   │   ├── changelog.md          # Version history
│   │   ├── migration.md          # Migration from libxml2
│   │   └── faq.md                # Frequently asked questions
│   │
│   └── assets/
│       ├── images/               # Diagrams, screenshots
│       └── stylesheets/          # Custom CSS
│
├── api/                          # Doxygen output (generated)
│   └── html/
│
└── Doxyfile                      # Doxygen configuration
```

---

## Implementation Phases

### Phase 1: MkDocs Foundation

**Tasks:**
1. Create `docs/` directory structure
2. Create `mkdocs.yml` configuration
3. Create `requirements.txt` for Python dependencies
4. Create landing page (`docs/index.md`)
5. Set up Material theme with custom colors

**Configuration:**

```yaml
# mkdocs.yml
site_name: Taurus XML Library
site_description: High-performance XML parser and XPath engine in C
site_url: https://lutaml.github.io/taurus/
repo_url: https://github.com/lutaml/taurus
repo_name: lutaml/taurus

theme:
  name: material
  palette:
    - media: "(prefers-color-scheme: light)"
      scheme: default
      primary: indigo
      accent: indigo
      toggle:
        icon: material/brightness-7
        name: Switch to dark mode
    - media: "(prefers-color-scheme: dark)"
      scheme: slate
      primary: indigo
      accent: indigo
      toggle:
        icon: material/brightness-4
        name: Switch to light mode
  features:
    - navigation.instant
    - navigation.tracking
    - navigation.tabs
    - navigation.sections
    - navigation.expand
    - search.suggest
    - search.highlight
    - content.code.copy

nav:
  - Home: index.md
  - Getting Started:
    - getting-started/index.md
    - Installation: getting-started/installation.md
    - Building: getting-started/building.md
    - Quick Start: getting-started/quickstart.md
  - Guides:
    - guides/index.md
    - XML Parsing: guides/parsing.md
    - XPath Queries: guides/xpath.md
    - SAX Streaming: guides/sax-streaming.md
    - StAX Writer: guides/stax-writer.md
    - Memory Management: guides/memory.md
  - Performance:
    - performance/index.md
    - Benchmarks: performance/benchmarks.md
  - API Reference: api/html/index.html
  - CLI Reference: reference/cli.md

markdown_extensions:
  - pymdownx.highlight:
      anchor_linenums: true
  - pymdownx.inlinehilite
  - pymdownx.snippets
  - pymdownx.superfences
  - pymdownx.tabbed:
      alternate_style: true
  - admonition
  - pymdownx.details
  - attr_list
  - md_in_html
  - toc:
      permalink: true
```

### Phase 2: Doxygen Integration

**Tasks:**
1. Create `Doxyfile` configuration
2. Configure input directories (`src/include/taurus/`)
3. Configure output directory (`docs/api/html/`)
4. Enable HTML navigation panels
5. Enable cross-references
6. Test generation

**Configuration:**

```ini
# Doxyfile (key settings)
PROJECT_NAME = "Taurus XML Library"
PROJECT_NUMBER = 0.3.0
OUTPUT_DIRECTORY = docs/api
INPUT = src/include/taurus
RECURSIVE = YES
GENERATE_HTML = YES
GENERATE_LATEX = NO
HTML_OUTPUT = html
HTML_NAVIGATION = YES
HAVE_DOT = YES
CALL_GRAPH = YES
CALLER_GRAPH = YES
EXTRACT_ALL = NO
EXTRACT_PRIVATE = NO
EXTRACT_STATIC = YES
SOURCE_BROWSER = YES
INLINE_SOURCES = NO
```

### Phase 3: Content Migration

**Tasks:**
1. Extract Getting Started content from README.adoc
2. Create XML Parsing guide
3. Create XPath Query guide
4. Create SAX Streaming guide
5. Create StAX Writer guide (from README content)
6. Create Performance/Benchmarks page
7. Create CLI Reference

**Content Sources:**
- README.adoc → Multiple guides
- Header comments → API reference (via Doxygen)
- TODO.stax-implementation.md → StAX Writer guide

### Phase 4: GitHub Pages Deployment

**Tasks:**
1. Create `.github/workflows/docs.yml`
2. Configure GitHub Pages for `gh-pages` branch
3. Test deployment
4. Configure custom domain (optional)

**Workflow:**

```yaml
# .github/workflows/docs.yml
name: Documentation

on:
  push:
    branches: [main]
  pull_request:
    branches: [main]

permissions:
  contents: read
  pages: write
  id-token: write

jobs:
  build:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: Set up Python
        uses: actions/setup-python@v5
        with:
          python-version: '3.11'

      - name: Install MkDocs
        run: |
          pip install -r docs/requirements.txt

      - name: Install Doxygen
        run: sudo apt-get install -y doxygen graphviz

      - name: Generate API docs
        run: |
          cd docs
          doxygen Doxyfile

      - name: Build MkDocs
        run: |
          cd docs
          mkdocs build --strict

      - name: Upload artifact
        uses: actions/upload-pages-artifact@v3
        with:
          path: docs/site

  deploy:
    environment:
      name: github-pages
      url: ${{ steps.deployment.outputs.page_url }}
    runs-on: ubuntu-latest
    needs: build
    if: github.event_name == 'push' && github.ref == 'refs/heads/main'
    steps:
      - name: Deploy to GitHub Pages
        id: deployment
        uses: actions/deploy-pages@v4
```

---

## Local Development

```bash
# Install dependencies
cd docs
pip install -r requirements.txt

# Install Doxygen (macOS)
brew install doxygen graphviz

# Generate API docs
doxygen Doxyfile

# Run local server
mkdocs serve

# Build static site
mkdocs build
```

---

## Design Guidelines

### Code Examples

All code examples should be:
- Complete, compilable programs (not snippets)
- Include necessary `#include` statements
- Show error handling
- Follow consistent formatting

Example:

```c
#include <taurus.h>
#include <stdio.h>

int main(void) {
    const char* xml = "<root><item>Hello</item></root>";

    // Parse XML
    TaurusDocument doc = taurus_parse_string(xml, strlen(xml), NULL);
    if (!doc) {
        fprintf(stderr, "Failed to parse XML\n");
        return 1;
    }

    // Get root element
    TaurusElement root = taurus_document_root(doc);
    printf("Root element: %s\n", taurus_element_name(root));

    // Clean up
    taurus_document_free(doc);
    return 0;
}
```

### Performance Comparisons

Present benchmarks with clear tables:

| Operation | Taurus | libxml2 | Speedup |
|-----------|--------|---------|---------|
| Parse 1MB | 12ms | 28ms | 2.3x |

### Navigation Structure

- Max 3 levels deep
- Clear section names
- Consistent naming (gerunds: "Parsing", "Building")

---

## Success Metrics

- [ ] Landing page loads in < 2 seconds
- [ ] All API functions documented
- [ ] At least 5 working code examples
- [ ] Search functionality works
- [ ] Mobile-friendly (responsive)
- [ ] Dark mode support
- [ ] Automatic deployment on merge to main

---

## Timeline

| Phase | Duration | Deliverable |
|-------|----------|-------------|
| Phase 1: MkDocs Setup | 1 session | Working local site |
| Phase 2: Doxygen | 1 session | Generated API docs |
| Phase 3: Content | 2 sessions | All guides written |
| Phase 4: Deployment | 1 session | Live site on GitHub Pages |

**Total: ~5 sessions**

---

## Future Enhancements

1. **Interactive examples** - Embedded code playground
2. **Versioned docs** - Multiple versions (0.1, 0.2, 0.3)
3. **Search analytics** - Track what users search for
4. **Contributor docs** - How to contribute to Taurus
5. **Multi-language** - Translations (Japanese, Chinese)
6. **PDF export** - Downloadable PDF documentation
7. **Man pages** - CLI man page generation

---

## Reference Sites

- https://www.sqlite.org/docs.html - Simple, comprehensive
- https://curl.se/libcurl/c/ - Doxygen-based
- https://docs.ros.org/ - Sphinx + Doxygen
- https://facebook.github.io/zstd/ - MkDocs Material
- https://libxml2.github.io/ - Reference for comparison
