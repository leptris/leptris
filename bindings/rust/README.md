# leptris (Rust)

Idiomatic Rust bindings for [libleptris](https://github.com/leptris/leptris) —
pure-C99 XML 1.0 parsing, a W3C-conformant XPath 1.0 engine, and SAX.

## Linking

The crate links against a prebuilt `libleptris` shared library:

```sh
# Build the C library once (from the repository root):
cmake -B build-shared -S . -DCMAKE_BUILD_TYPE=Release \
  -DLEPTRIS_BUILD_SHARED=ON -DLEPTRIS_BUILD_STATIC=OFF \
  -DBUILD_TESTING=OFF -DLEPTRIS_BUILD_CLI=OFF
cmake --build build-shared

# Then build/test the crate against it:
LEPTRIS_LIB_PATH=build-shared/src cargo test --manifest-path bindings/rust/Cargo.toml
```

`LEPTRIS_LIB_PATH` accepts the library file or its directory. Without
it the crate falls back to the system linker search (`-lleptris`).

## Usage

```rust
use leptris::{Document, XPathNodeKind};

let doc = Document::parse(b"<catalog><book id='b1'><title>Alpha</title></book></catalog>")?;
let root = doc.root().unwrap();
assert_eq!(root.name(), "catalog");

for book in root.children() {
    println!("{} — {}", book.attribute("id").unwrap(), book.child("title").unwrap().text().unwrap());
}

let hits = doc.xpath("//book[@id='b1']/title")?;
assert_eq!(hits.element(0).unwrap().text(), Some("Alpha"));

// Mixed nodesets (text, comments, attributes):
let nodes = doc.xpath("//node()")?;
assert_eq!(nodes.kind(1), XPathNodeKind::Element);
```

SAX with closures:

```rust
use leptris::sax;

let mut h = sax::Handler::default();
h.start_element = Some(Box::new(|name, attrs| println!("<{name}> {attrs:?}")));
h.characters = Some(Box::new(|t| print!("{t}")));
sax::parse(b"<r><a>hello</a></r>", &mut h)?;
```

## Layout

Mirrors the Ruby (`leptris-ruby`) and Python (`pyleptris`) bindings:
a hand-maintained FFI layer (`src/ffi.rs`) mirroring the public
headers, with safe wrappers above it. The FFI drift gate keeps the
mirror in sync with the C surface.

## Workspace: the shared descriptor-ABI crate

`bindings/rust` is a Cargo workspace. Besides the `leptris crate
(unchanged name and shape — links-only, resolved via
`LEPTRIS_LIB_PATH`), it contains the `descriptor` member, published
as **`leptris-descriptor`**: the `no_std` `#[repr(C)]` mirror of the
tree-shaped descriptor-plan ABI (`src/include/leptris/descriptor.h`).
It is the one record layout the whole engine family targets — the
per-engine crates (`leptris` XML, `yeptris` YAML, `teptris`
TOML/JSON — topology decided 2026-10-03, leptris-ruby#294) — so
hosts build plan rows once for every engine. The `link` feature
declares the engine FFI for the descriptor surface; enable it on
exactly one crate in the graph.

The authoritative layout gate is `tests/descriptor_abi.rs` in the
`leptris` crate: it compares Rust `size_of` against the engine's own
`leptris_plan_*_struct_size` introspection and walks a plan built
from the mirrored rows.
