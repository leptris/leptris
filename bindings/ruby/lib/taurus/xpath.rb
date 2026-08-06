# lib/taurus/xpath.rb — Taurus::XPath evaluates XPath expressions.
#
# Results are typed: number, string, boolean, or nodeset.  Nodeset
# results return Element objects; scalar results return the native
# Ruby type.

module Taurus
  module XPath
    RESULT_NODESET  = 0
    RESULT_BOOLEAN  = 1
    RESULT_NUMBER   = 2
    RESULT_STRING   = 3

    def self.evaluate(document, context_element, expression)
      ctx_ptr = context_element ? context_element.ptr : nil
      result = Taurus.taurus_xpath_eval(document.ptr, ctx_ptr, expression)
      raise Error, 'XPath evaluation failed' if result.nil? || result.null?

      type = Taurus.taurus_xpath_result_type(result)
      value = case type
              when RESULT_NUMBER  then Taurus.taurus_xpath_result_number(result)
              when RESULT_STRING
                ptr = Taurus.taurus_xpath_result_string(result)
                str = ptr.read_string
                Taurus.taurus_free_string(ptr)
                str
              when RESULT_BOOLEAN
                # Boolean is encoded as number internally.
                Taurus.taurus_xpath_result_number(result) != 0
              when RESULT_NODESET
                count = Taurus.taurus_xpath_result_count(result)
                (0...count).map do |i|
                  elem = Taurus.taurus_xpath_result_get(result, i)
                  Element.new(elem, document)
                end
              else
                nil
              end

      Taurus.taurus_xpath_result_free(result)
      value
    end
  end
end
