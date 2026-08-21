# lib/leptris/element.rb — Leptris::Element wraps LeptrisElement.
#
# Elements are owned by their parent Document.  They are never freed
# directly — freeing the Document frees the entire tree.

module Leptris
  class Element
    attr_reader :ptr

    def initialize(ptr, document)
      @ptr = ptr
      @document = document
    end

    def name
      Leptris.leptris_element_name(@ptr)
    end

    def text
      Leptris.leptris_element_text(@ptr)
    end

    def [](attr_name)
      Leptris.leptris_element_attribute(@ptr, attr_name, nil)
    end

    def attribute_count
      Leptris.leptris_element_attribute_count(@ptr)
    end

    def parent
      p = Leptris.leptris_element_parent(@ptr)
      return nil if p.nil? || p.null?
      Element.new(p, @document)
    end

    def first_child_element
      c = Leptris.leptris_element_first_child_any(@ptr)
      return nil if c.nil? || c.null?
      Element.new(c, @document)
    end

    def child_count
      Leptris.leptris_element_child_count(@ptr)
    end

    def each_child_element
      return enum_for(:each_child_element) unless block_given?
      child = first_child_element
      while child
        yield child
        child = child.next_sibling_element
      end
    end

    def next_sibling_element
      node = Leptris.leptris_element_as_node(@ptr)
      sibling = Leptris.leptris_node_next_sibling(node)
      return nil if sibling.nil? || sibling.null?
      elem = Leptris.leptris_node_as_element(sibling)
      return nil if elem.nil? || elem.null?
      Element.new(elem, @document)
    end

    def to_node
      Node.new(Leptris.leptris_element_as_node(@ptr), @document)
    end

    def inspect
      "<Leptris::Element #{name}>"
    end
  end
end
