# lib/leptris/sax.rb — Leptris::SAX provides event-driven parsing.
#
# Two surfaces over the C SAX API (leptris/sax/sax.h):
#
#   Leptris::SAX.parse(xml, handlers)   — one-shot parse of a String.
#   Leptris::SAX::Parser                — incremental feed() for streams.
#
# handlers is a Hash of procs keyed by event name. All keys are
# optional; unset callbacks are not passed to C at all:
#
#   :start_document          -> ()
#   :end_document            -> ()
#   :start_element           -> (name, attrs_hash)
#   :end_element             -> (name)
#   :characters              -> (text)
#   :comment                 -> (text)
#   :cdata                   -> (text)
#   :processing_instruction  -> (target, data)
#   :start_prefix_mapping    -> (prefix, uri)
#   :end_prefix_mapping      -> (prefix)
#   :error                   -> (message, line, column)
#
# All strings handed to Ruby are UTF-8 copies (read out of the C
# callback scope before it returns).

module Leptris
  module SAX
    # C struct LeptrisSAXHandler — 11 function pointers, same order
    # as sax.h.
    class HandlerStruct < FFI::Struct
      layout(
        :start_document,         Leptris.find_type(:sax_start_document),
        :end_document,           Leptris.find_type(:sax_end_document),
        :start_element,          Leptris.find_type(:sax_start_element),
        :end_element,            Leptris.find_type(:sax_end_element),
        :characters,             Leptris.find_type(:sax_characters),
        :comment,                Leptris.find_type(:sax_comment),
        :cdata,                  Leptris.find_type(:sax_cdata),
        :processing_instruction, Leptris.find_type(:sax_pi),
        :start_prefix_mapping,   Leptris.find_type(:sax_prefix_mapping),
        :end_prefix_mapping,     Leptris.find_type(:sax_end_prefix),
        :error,                  Leptris.find_type(:sax_error)
      )
    end

    # C-side argument lists after user_data, per event.
    C_ARGS = {
      start_document: [],
      end_document: [],
      start_element: %i[string pointer],
      end_element: [:string],
      characters: %i[pointer size_t],
      comment: [:string],
      cdata: [:string],
      processing_instruction: %i[string string],
      start_prefix_mapping: %i[string string],
      end_prefix_mapping: [:string],
      error: %i[string int int]
    }.freeze
    private_constant :C_ARGS

    # Bridges C callbacks into Ruby handler procs.
    #
    # user_data pattern: C hands back an opaque pointer we chose. We
    # pass the address of a small FFI::MemoryPointer unique per
    # bridge; REGISTRY maps that address back to this object. The
    # Ruby object itself is never exposed to C, so the GC can move
    # it freely. The registry entry (and with it the anchor and the
    # FFI::Function objects) stays alive exactly as long as C can
    # still fire callbacks — until #release.
    class Bridge
      REGISTRY = {}

      attr_reader :anchor

      def initialize(handlers)
        @handlers = handlers
        @anchor = FFI::MemoryPointer.new(:char, 1)
        @struct = HandlerStruct.new
        REGISTRY[@anchor.address] = self

        C_ARGS.each do |event, c_args|
          handler = handlers[event]
          next unless handler
          @struct[event] = FFI::Function.new(:void, [:pointer] + c_args) do |user_data, *args|
            bridge = REGISTRY[user_data.address]
            bridge.fire(event, handler, args) if bridge
          end
        end
      end

      # The struct pointer to hand to C as LeptrisSAXHandler*.
      def pointer
        @struct
      end

      def fire(event, handler, args)
        case event
        when :start_element
          handler.call(str(args[0]), Bridge.read_attrs(args[1]))
        when :characters
          handler.call(args[0].get_bytes(0, args[1]).force_encoding(Encoding::UTF_8))
        else
          handler.call(*args.map { |a| a.is_a?(String) ? str(a) : a })
        end
      end

      def str(s)
        s.force_encoding(Encoding::UTF_8)
      end

      # attrs is a NULL-terminated char** of name/value pairs:
      # [name1, value1, name2, value2, ..., NULL].
      def self.read_attrs(attrs)
        return {} if attrs.null?
        step = FFI.type_size(:pointer)
        out = {}
        i = 0
        while (name_p = attrs.get_pointer(i)) && !name_p.null?
          value_p = attrs.get_pointer(i + step)
          out[name_p.read_string.force_encoding(Encoding::UTF_8)] =
            value_p.null? ? nil : value_p.read_string.force_encoding(Encoding::UTF_8)
          i += 2 * step
        end
        out
      end

      def release
        REGISTRY.delete(@anchor.address)
      end
    end
    private_constant :Bridge

    # One-shot SAX parse of a String.
    #
    # Returns true on success. Raises Leptris::Error when parsing
    # fails; the :error handler (if any) observes the failure
    # before the raise.
    def self.parse(xml, handlers = {})
      raise ArgumentError, 'xml must be a String' unless xml.is_a?(String)

      bridge = Bridge.new(handlers)
      rc = Leptris.leptris_sax_parse(
        xml, xml.bytesize, bridge.pointer, bridge.anchor
      )
      bridge.release
      raise Error, 'SAX parse failed' if rc != 0
      true
    end

    # Incremental SAX parser for streaming input.
    #
    #   parser = Leptris::SAX::Parser.new(handlers, streaming: true)
    #   io.each_chunk { |chunk| parser.feed(chunk) }
    #   parser.feed('', final: true)
    #   parser.free
    #
    # streaming: true enables the constant-memory state machine
    # (leptris_sax_parser_set_streaming); the default matches the C
    # default (buffer chunks, parse on the final feed).
    class Parser
      def initialize(handlers = {}, streaming: false)
        @bridge = Bridge.new(handlers)
        @parser = Leptris.leptris_sax_parser_create(
          @bridge.pointer, @bridge.anchor
        )
        raise Error, 'SAX parser creation failed' if @parser.null?
        if streaming
          rc = Leptris.leptris_sax_parser_set_streaming(@parser, 1)
          raise Error, 'SAX streaming mode rejected' if rc != 0
        end
      end

      def feed(chunk, final: false)
        raise ArgumentError, 'chunk must be a String' unless chunk.is_a?(String)
        Leptris.leptris_sax_parser_feed(
          @parser, chunk, chunk.bytesize, final ? 1 : 0
        )
      end

      def free
        return unless @parser
        Leptris.leptris_sax_parser_free(@parser)
        @parser = nil
        @bridge.release
      end
    end
  end
end
