# lib/leptris.rb — Ruby FFI binding for libleptris XML parser.
#
# Architecture:
#   - Leptris module is the umbrella namespace.
#   - Subclasses (Document, Element, XPath, SAX) are autoloaded from
#     their files under lib/leptris/.  No require_relative.
#   - FFI declarations live here (the parent namespace file) so
#     autoloaded children can reference them without circular deps.
#   - The C library is loaded via FFI; the user must have libleptris
#     installed (homebrew, vcpkg, or build from source).

require 'ffi'

module Leptris
  extend FFI::Library

  # Try common install locations.  Users can override by setting
  # LEPTRIS_LIB_PATH before requiring this file.
  if ENV['LEPTRIS_LIB_PATH']
    ffi_lib ENV['LEPTRIS_LIB_PATH']
  else
    ffi_lib FFI::Library::LIBC if FFI::Platform.mac?
    begin
      ffi_lib 'leptris'
    rescue LoadError
      # Try build directory as fallback for development.
      ffi_lib File.expand_path('../../../build/src/libleptris', __dir__)
    end
  end

  # ---- Opaque handle typedefs ----
  typedef :pointer, :document
  typedef :pointer, :element
  typedef :pointer, :node_ref
  typedef :pointer, :xpath_result
  typedef :pointer, :sax_parser

  # ---- Status codes ----
  LEPTRIS_OK = 0

  # ---- Document lifecycle ----
  attach_function :leptris_parse_string,
    [:string, :size_t, :pointer], :document
  attach_function :leptris_document_free, [:document], :void
  attach_function :leptris_document_root, [:document], :element
  attach_function :leptris_document_serialize,
    [:document, :pointer], :pointer
  attach_function :leptris_xinclude_process,
    [:document, :string], :int

  # ---- Node type enum ----
  NODE_ELEMENT  = 0
  NODE_TEXT     = 1
  NODE_COMMENT  = 2
  NODE_CDATA    = 3
  NODE_PI       = 4
  NODE_DOCTYPE  = 5

  # ---- Node navigation ----
  attach_function :leptris_node_get_type, [:node_ref], :int
  attach_function :leptris_node_first_child, [:node_ref], :node_ref
  attach_function :leptris_node_next_sibling, [:node_ref], :node_ref
  attach_function :leptris_node_previous_sibling, [:node_ref], :node_ref
  attach_function :leptris_node_child_count, [:node_ref], :size_t
  attach_function :leptris_node_as_element, [:node_ref], :element
  attach_function :leptris_element_as_node, [:element], :node_ref

  # ---- Element queries ----
  attach_function :leptris_element_name, [:element], :string
  attach_function :leptris_element_text, [:element], :string
  attach_function :leptris_element_first_child_any, [:element], :element
  attach_function :leptris_element_parent, [:element], :element
  attach_function :leptris_element_attribute, [:element, :string], :string
  attach_function :leptris_element_next_sibling_any, [:element], :element

  # ---- Attribute iteration (TODO.remaining/06) ----
  typedef :pointer, :attribute
  attach_function :leptris_element_attribute_count, [:element], :size_t
  attach_function :leptris_element_first_attribute, [:element], :attribute
  attach_function :leptris_attribute_next, [:attribute], :attribute
  attach_function :leptris_attribute_get_name, [:attribute], :string
  attach_function :leptris_attribute_get_value, [:element, :attribute], :string

  # ---- Text / Comment / CDATA / PI ----
  attach_function :leptris_text_node_get_content, [:node_ref], :string
  attach_function :leptris_comment_node_get_content, [:node_ref], :string
  attach_function :leptris_cdata_node_get_content, [:node_ref], :string
  attach_function :leptris_pi_node_get_target, [:node_ref], :string
  attach_function :leptris_pi_node_get_data, [:node_ref], :string

  # ---- XPath ----
  attach_function :leptris_xpath_eval,
    [:document, :element, :string], :xpath_result
  attach_function :leptris_xpath_result_free, [:xpath_result], :void
  attach_function :leptris_xpath_result_type, [:xpath_result], :int
  attach_function :leptris_xpath_result_number, [:xpath_result], :double
  attach_function :leptris_xpath_result_string, [:xpath_result], :pointer
  attach_function :leptris_xpath_result_count, [:xpath_result], :size_t
  attach_function :leptris_xpath_result_get, [:xpath_result, :size_t], :element

  # ---- Free helper ----
  attach_function :leptris_free_string, [:pointer], :void
  attach_function :leptris_xpath_result_boolean, [:xpath_result], :int

  # ---- SAX (TODO 118 Phase B: full callback surface) ----
  # Callback typedefs mirror struct LeptrisSAXHandler (sax.h).
  # characters' text is NOT NUL-terminated: pointer + length.
  callback :sax_start_document,  [:pointer], :void
  callback :sax_end_document,    [:pointer], :void
  callback :sax_start_element,   [:pointer, :string, :pointer], :void
  callback :sax_end_element,     [:pointer, :string], :void
  callback :sax_characters,      [:pointer, :pointer, :size_t], :void
  callback :sax_comment,         [:pointer, :string], :void
  callback :sax_cdata,           [:pointer, :string], :void
  callback :sax_pi,              [:pointer, :string, :string], :void
  callback :sax_prefix_mapping,  [:pointer, :string, :string], :void
  callback :sax_end_prefix,      [:pointer, :string], :void
  callback :sax_error,           [:pointer, :string, :int, :int], :void

  attach_function :leptris_sax_parse,
    [:string, :size_t, :pointer, :pointer], :int
  attach_function :leptris_sax_parser_create,
    [:pointer, :pointer], :sax_parser
  attach_function :leptris_sax_parser_feed,
    [:sax_parser, :string, :size_t, :int], :int
  attach_function :leptris_sax_parser_set_streaming,
    [:sax_parser, :int], :int
  attach_function :leptris_sax_parser_free, [:sax_parser], :void

  # Autoload children from their files.  No require_relative.
  autoload :Document, 'leptris/document'
  autoload :Element,  'leptris/element'
  autoload :Node,     'leptris/node'
  autoload :XPath,    'leptris/xpath'
  autoload :SAX,      'leptris/sax'
  autoload :Error,    'leptris/error'
end
