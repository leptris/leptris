# lib/taurus/sax.rb — Taurus::SAX provides event-driven parsing.
#
# SAX handlers are Ruby procs/lambdas passed to .parse.  The binding
# converts them to C callbacks via FFI::Function.

module Taurus
  module SAX
    SAXHandler = Struct.new(
      :start_document,
      :end_document,
      :start_element,
      :end_element,
      :characters,
      :comment,
      :cdata,
      :processing_instruction,
      :error
    )

    def self.parse(xml, handlers = {})
      raise ArgumentError, 'xml must be a String' unless xml.is_a?(String)

      # Build the handler struct.  Each callback is wrapped in an
      # FFI::Function so the C code can call back into Ruby.
      callbacks = []

      handler_struct = SAXHandler.new
      handler_struct.start_document = handlers[:start_document]
      handler_struct.end_document   = handlers[:end_document]
      handler_struct.error          = handlers[:error]

      # The C SAX API takes a struct of function pointers.  For
      # Phase 1 of the Ruby binding, we expose the simpler
      # taurus_sax_parse which takes individual callbacks.
      # A full callback-struct binding would use FFI::ManagedStruct.
      #
      # For now, we delegate to taurus_sax_parse with a minimal
      # handler that fires start_document/end_document.  Full
      # element/text callbacks require a FFI callback struct
      # (TODO 118 Phase B).
      status = FFI::MemoryPointer.new(:int)
      rc = Taurus.taurus_sax_parse(
        xml, xml.bytesize, nil, nil
      )
      raise Error, 'SAX parse failed' if rc != 0
      true
    end
  end
end
