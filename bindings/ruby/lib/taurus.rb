# lib/taurus.rb — Ruby FFI binding for libtaurus XML parser.
#
# Architecture:
#   - Taurus module is the umbrella namespace.
#   - Subclasses (Document, Element, XPath, SAX) are autoloaded from
#     their files under lib/taurus/.  No require_relative.
#   - FFI declarations live here (the parent namespace file) so
#     autoloaded children can reference them without circular deps.
#   - The C library is loaded via FFI; the user must have libtaurus
#     installed (homebrew, vcpkg, or build from source).

require 'ffi'

module Taurus
  extend FFI::Library

  # Try common install locations.  Users can override by setting
  # TAURUS_LIB_PATH before requiring this file.
  if ENV['TAURUS_LIB_PATH']
    ffi_lib ENV['TAURUS_LIB_PATH']
  else
    ffi_lib FFI::Library::LIBC if FFI::Platform.mac?
    begin
      ffi_lib 'taurus'
    rescue LoadError
      # Try build directory as fallback for development.
      ffi_lib File.expand_path('../../../build/src/libtaurus', __dir__)
    end
  end

  # ---- Opaque handle typedefs ----
  typedef :pointer, :document
  typedef :pointer, :element
  typedef :pointer, :node_ref
  typedef :pointer, :xpath_result
  typedef :pointer, :sax_parser

  # ---- Status codes ----
  TAURUS_OK = 0

  # ---- Document lifecycle ----
  attach_function :taurus_parse_string,
    [:string, :size_t, :pointer], :document
  attach_function :taurus_document_free, [:document], :void
  attach_function :taurus_document_root, [:document], :element
  attach_function :taurus_serialize_document,
    [:document, :pointer], :pointer
  attach_function :taurus_xinclude_process,
    [:document, :string], :int

  # ---- Node type enum ----
  NODE_ELEMENT  = 0
  NODE_TEXT     = 1
  NODE_COMMENT  = 2
  NODE_CDATA    = 3
  NODE_PI       = 4
  NODE_DOCTYPE  = 5

  # ---- Node navigation ----
  attach_function :taurus_node_get_type, [:node_ref], :int
  attach_function :taurus_node_first_child, [:node_ref], :node_ref
  attach_function :taurus_node_next_sibling, [:node_ref], :node_ref
  attach_function :taurus_node_previous_sibling, [:node_ref], :node_ref
  attach_function :taurus_node_child_count, [:node_ref], :size_t
  attach_function :taurus_node_as_element, [:node_ref], :element
  attach_function :taurus_element_as_node, [:element], :node_ref

  # ---- Element queries ----
  attach_function :taurus_element_name, [:element], :string
  attach_function :taurus_element_text, [:element], :string
  attach_function :taurus_element_first_child_any, [:element], :element
  attach_function :taurus_element_parent, [:element], :element
  attach_function :taurus_element_attribute, [:element, :string, :string], :string
  attach_function :taurus_element_next_sibling_any, [:element], :element

  # ---- Text / Comment / CDATA / PI ----
  attach_function :taurus_text_node_get_content, [:node_ref], :string
  attach_function :taurus_comment_node_get_content, [:node_ref], :string
  attach_function :taurus_cdata_node_get_content, [:node_ref], :string
  attach_function :taurus_pi_node_get_target, [:node_ref], :string
  attach_function :taurus_pi_node_get_data, [:node_ref], :string

  # ---- XPath ----
  attach_function :taurus_xpath_eval,
    [:document, :element, :string], :xpath_result
  attach_function :taurus_xpath_result_free, [:xpath_result], :void
  attach_function :taurus_xpath_result_type, [:xpath_result], :int
  attach_function :taurus_xpath_result_number, [:xpath_result], :double
  attach_function :taurus_xpath_result_string, [:xpath_result], :pointer
  attach_function :taurus_xpath_result_count, [:xpath_result], :size_t
  attach_function :taurus_xpath_result_get, [:xpath_result, :size_t], :element

  # ---- Free helper ----
  attach_function :taurus_free_string, [:pointer], :void
  attach_function :taurus_xpath_result_boolean, [:xpath_result], :int

  # Autoload children from their files.  No require_relative.
  autoload :Document, 'taurus/document'
  autoload :Element,  'taurus/element'
  autoload :Node,     'taurus/node'
  autoload :XPath,    'taurus/xpath'
  autoload :Error,    'taurus/error'
end
