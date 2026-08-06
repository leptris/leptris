# lib/taurus/node.rb — Taurus::Node wraps TaurusNodeRef for generic
# tree traversal (all node types, not just elements).

module Taurus
  class Node
    attr_reader :ptr

    def initialize(ptr, document)
      @ptr = ptr
      @document = document
    end

    def type
      Taurus.taurus_node_get_type(@ptr)
    end

    def element?
      type == Taurus::NODE_ELEMENT
    end

    def text?
      type == Taurus::NODE_TEXT
    end

    def comment?
      type == Taurus::NODE_COMMENT
    end

    def cdata?
      type == Taurus::NODE_CDATA
    end

    def pi?
      type == Taurus::NODE_PI
    end

    def content
      case type
      when Taurus::NODE_TEXT     then Taurus.taurus_text_node_get_content(@ptr)
      when Taurus::NODE_COMMENT  then Taurus.taurus_comment_node_get_content(@ptr)
      when Taurus::NODE_CDATA    then Taurus.taurus_cdata_node_get_content(@ptr)
      else nil
      end
    end

    def first_child
      c = Taurus.taurus_node_first_child(@ptr)
      return nil if c.nil? || c.null?
      Node.new(c, @document)
    end

    def next_sibling
      s = Taurus.taurus_node_next_sibling(@ptr)
      return nil if s.nil? || s.null?
      Node.new(s, @document)
    end

    def child_count
      Taurus.taurus_node_child_count(@ptr)
    end

    def as_element
      return nil unless element?
      elem = Taurus.taurus_node_as_element(@ptr)
      return nil if elem.nil? || elem.null?
      Element.new(elem, @document)
    end

    def inspect
      "<Taurus::Node type=#{type}>"
    end
  end
end
