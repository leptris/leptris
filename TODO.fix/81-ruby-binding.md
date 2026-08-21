# TODO 81: Ruby binding (ruby-ffi)

**Priority**: P2 (binding — Ruby ecosystem fit)
**Status**: Design
**Effort**: M

## Why Ruby first

The user works in Ruby (`lutaml-model`, `coradoc` cited in global
rules).  A Ruby binding lets libleptris be the XML backend for Ruby
projects that need C-level performance.

## Approach: ruby-ffi

Ruby's `ffi` gem is the standard way to bind C libraries without
writing C extension code.  Pure Ruby; no compilation per-platform.

```ruby
require 'ffi'

module Leptris
  extend FFI::Library
  ffi_lib 'libleptris'

  # Opaque handles — typed pointers
  class Document < FFI::Struct; end
  class Element < FFI::Struct; end

  # Status enum
  LEPTRIS_OK = 0

  # Function signatures
  attach_function :leptris_parse_string,
                  [:string, :size_t, :pointer], :pointer
  attach_function :leptris_document_root, [:pointer], :pointer
  attach_function :leptris_element_name,   [:pointer], :string
  attach_function :leptris_document_free,  [:pointer], :void
  attach_function :leptris_free_string,    [:pointer], :void

  # High-level wrapper
  class Doc
    def initialize(xml)
      status = FFI::MemoryPointer.new(:int)
      @ptr = Leptris.leptris_parse_string(xml, xml.bytesize, status)
      raise "parse failed" if @ptr.null?
    end

    def root
      Element.new(Leptris.leptris_document_root(@ptr))
    end

    def free
      Leptris.leptris_document_free(@ptr)
      @ptr = nil
    end
  end
end
```

## Packaging

- Gem: `leptris-ffi` (separate from a future pure-Ruby `leptris` if any).
- Depends on `ffi` gem and libleptris system install.
- CI: build gem, install via `gem install`, smoke-test.

## Tests

`test/bindings/ruby/test_parse.rb`:

```ruby
require 'leptris'

doc = Leptris::Doc.new("<root><item>hi</item></root>")
assert_equal "root", doc.root.name
doc.free
```

## Architecture notes

The pool-ownership model maps cleanly to Ruby:

- `Doc.new` → `leptris_parse_string` (Ruby owns the doc handle).
- `Doc#free` → `leptris_document_free` (one call releases everything).
- Could use Ruby's `ObjectSpace.define_finalizer` for auto-free, but
  explicit `free` is safer (finalizer ordering is non-deterministic).

The Ruby binding is a thin layer — no business logic, just FFI
declarations and ergonomic wrappers.  All real work stays in C.
