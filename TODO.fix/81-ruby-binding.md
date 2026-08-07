# TODO 81: Ruby binding (ruby-ffi)

**Priority**: P2 (binding — Ruby ecosystem fit)
**Status**: Design
**Effort**: M

## Why Ruby first

The user works in Ruby (`lutaml-model`, `coradoc` cited in global
rules).  A Ruby binding lets libtaurus be the XML backend for Ruby
projects that need C-level performance.

## Approach: ruby-ffi

Ruby's `ffi` gem is the standard way to bind C libraries without
writing C extension code.  Pure Ruby; no compilation per-platform.

```ruby
require 'ffi'

module Taurus
  extend FFI::Library
  ffi_lib 'libtaurus'

  # Opaque handles — typed pointers
  class Document < FFI::Struct; end
  class Element < FFI::Struct; end

  # Status enum
  TAURUS_OK = 0

  # Function signatures
  attach_function :taurus_parse_string,
                  [:string, :size_t, :pointer], :pointer
  attach_function :taurus_document_root, [:pointer], :pointer
  attach_function :taurus_element_name,   [:pointer], :string
  attach_function :taurus_document_free,  [:pointer], :void
  attach_function :taurus_free_string,    [:pointer], :void

  # High-level wrapper
  class Doc
    def initialize(xml)
      status = FFI::MemoryPointer.new(:int)
      @ptr = Taurus.taurus_parse_string(xml, xml.bytesize, status)
      raise "parse failed" if @ptr.null?
    end

    def root
      Element.new(Taurus.taurus_document_root(@ptr))
    end

    def free
      Taurus.taurus_document_free(@ptr)
      @ptr = nil
    end
  end
end
```

## Packaging

- Gem: `taurus-ffi` (separate from a future pure-Ruby `taurus` if any).
- Depends on `ffi` gem and libtaurus system install.
- CI: build gem, install via `gem install`, smoke-test.

## Tests

`test/bindings/ruby/test_parse.rb`:

```ruby
require 'taurus'

doc = Taurus::Doc.new("<root><item>hi</item></root>")
assert_equal "root", doc.root.name
doc.free
```

## Architecture notes

The pool-ownership model maps cleanly to Ruby:

- `Doc.new` → `taurus_parse_string` (Ruby owns the doc handle).
- `Doc#free` → `taurus_document_free` (one call releases everything).
- Could use Ruby's `ObjectSpace.define_finalizer` for auto-free, but
  explicit `free` is safer (finalizer ordering is non-deterministic).

The Ruby binding is a thin layer — no business logic, just FFI
declarations and ergonomic wrappers.  All real work stays in C.
