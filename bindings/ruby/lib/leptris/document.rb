# lib/leptris/document.rb — Leptris::Document wraps LeptrisDocument.
#
# Documents own the entire DOM tree + memory pool.  Callers MUST call
# #free when done (or let the process exit clean up).  No GC finalizer
# is used because the FFI pointer can't distinguish "already freed"
# from "valid" in a finalizer proc, leading to double-free crashes.

module Leptris
  class Document
    attr_reader :ptr

    def self.parse(xml)
      raise ArgumentError, 'xml must be a String' unless xml.is_a?(String)
      status = FFI::MemoryPointer.new(:int)
      doc = Leptris.leptris_parse_string(xml, xml.bytesize, status)
      raise Error, "Parse failed (status=#{status.read_int})" if doc.nil? || doc.null?
      new(doc)
    end

    def initialize(ptr)
      @ptr = ptr
      @freed = false
    end

    def root
      element_ptr = Leptris.leptris_document_root(@ptr)
      return nil if element_ptr.nil? || element_ptr.null?
      Element.new(element_ptr, self)
    end

    def serialize(options = nil)
      result = Leptris.leptris_serialize_document(@ptr, nil)
      return nil if result.nil? || result.null?
      str = result.read_string
      Leptris.leptris_free_string(result)
      str
    end

    def process_xinclude(base_url = nil)
      rc = Leptris.leptris_xinclude_process(@ptr, base_url)
      raise Error, 'XInclude processing failed' if rc != Leptris::LEPTRIS_OK
      self
    end

    def xpath(expression, context = nil)
      XPath.evaluate(self, context, expression)
    end

    def free
      return if @freed
      Leptris.leptris_document_free(@ptr)
      @freed = true
      @ptr = nil
    end

    def freed?
      @freed
    end
  end
end
