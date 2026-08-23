use leptris::sax;
use leptris::{Document, Error, XPathNodeKind};

const DOC: &[u8] = b"<catalog><book id='b1'><title>Alpha</title></book>\
                     <book id='b2'><title>Beta</title></book></catalog>";

#[test]
fn parse_and_traverse() {
    let doc = Document::parse(DOC).unwrap();
    let root = doc.root().unwrap();
    assert_eq!(root.name(), "catalog");
    assert_eq!(root.child_count(), 2);

    let books: Vec<_> = root
        .children()
        .map(|b| (b.attribute("id").unwrap().to_string(), b.name().to_string()))
        .collect();
    assert_eq!(
        books,
        vec![
            ("b1".to_string(), "book".to_string()),
            ("b2".to_string(), "book".to_string())
        ]
    );

    let first = root.children().next().unwrap();
    let title = first.child("title").unwrap();
    assert_eq!(title.text(), Some("Alpha"));
}

#[test]
fn attribute_iteration() {
    let doc = Document::parse(b"<e a='1' b='two' c='3.5'/>").unwrap();
    let root = doc.root().unwrap();
    let attrs: Vec<_> = root
        .attributes()
        .map(|(k, v)| (k.to_string(), v.to_string()))
        .collect();
    assert_eq!(
        attrs,
        vec![
            ("a".to_string(), "1".to_string()),
            ("b".to_string(), "two".to_string()),
            ("c".to_string(), "3.5".to_string()),
        ]
    );
}

#[test]
fn xpath_queries() {
    let doc = Document::parse(DOC).unwrap();

    let books = doc.xpath("//book").unwrap();
    assert_eq!(books.len(), 2);
    assert_eq!(books.element(0).unwrap().name(), "book");

    let count = doc.xpath("count(//title)").unwrap();
    assert_eq!(count.len(), 0); // number result has no nodes
    assert_eq!(count.number(), 2.0);

    let title = doc.xpath("//book[@id='b2']/title").unwrap();
    assert_eq!(title.len(), 1);
    assert_eq!(title.element(0).unwrap().text(), Some("Beta"));
}

#[test]
fn xpath_mixed_nodeset() {
    let doc = Document::parse(b"<r><a id='x1'>hello</a><!-- c --></r>").unwrap();

    // Mixed kinds: root + element + text + comment.
    let nodes = doc.xpath("//node()").unwrap();
    assert_eq!(nodes.len(), 4);
    assert_eq!(nodes.kind(0), XPathNodeKind::Element);
    assert_eq!(nodes.kind(1), XPathNodeKind::Element);
    assert_eq!(nodes.kind(2), XPathNodeKind::Text);
    assert_eq!(nodes.kind(3), XPathNodeKind::Other);
    assert_eq!(nodes.node_name(1), Some("a"));
    assert_eq!(nodes.node_value(2), Some("hello"));
    assert_eq!(nodes.node_value(3), Some(" c "));

    // Attributes report kind + name + value.
    let attr = doc.xpath("//a/@id").unwrap();
    assert_eq!(attr.len(), 1);
    assert_eq!(attr.kind(0), XPathNodeKind::Attribute);
    assert_eq!(attr.node_name(0), Some("id"));
    assert_eq!(attr.node_value(0), Some("x1"));
}

#[test]
fn string_result() {
    let doc = Document::parse(DOC).unwrap();
    let s = doc.xpath("string(//book[1]/title)").unwrap();
    assert_eq!(s.to_string(), "Alpha");
}

#[test]
fn serialize_roundtrip() {
    let doc = Document::parse(b"<r><a>1</a></r>").unwrap();
    let xml = doc.serialize().unwrap();
    assert!(xml.contains("<r>"), "{xml}");
    assert!(xml.contains("<a>1</a>"), "{xml}");
}

#[test]
fn parse_error_maps_to_err() {
    assert!(matches!(Document::parse(b"<unclosed>"), Err(Error::Parse)));
}

#[test]
fn documents_free_cleanly() {
    // Drop path: many documents, no leaks (CI runs ASAN).
    for _ in 0..100 {
        let doc = Document::parse(DOC).unwrap();
        let _ = doc.xpath("//book").unwrap();
        let _ = doc.root().unwrap().children().count();
    }
}

#[test]
fn sax_events() {
    let (starts, texts, attrs, ends) = {
        let mut starts: Vec<String> = Vec::new();
        let mut texts = String::new();
        let mut attrs: Vec<(String, String)> = Vec::new();
        let mut ends: Vec<String> = Vec::new();

        let mut h = sax::Handler::default();
        h.start_element = Some(Box::new(|name, pairs| {
            for (k, v) in pairs {
                attrs.push((k.clone(), v.clone()));
            }
            starts.push(name.to_string());
        }));
        h.end_element = Some(Box::new(|name| ends.push(name.to_string())));
        h.characters = Some(Box::new(|t| texts.push_str(t)));

        sax::parse(b"<r lang='en'><a>xy</a><b/></r>", &mut h).unwrap();
        drop(h); // release the closures' borrows before moving the vecs
        (starts, texts, attrs, ends)
    };

    assert_eq!(starts, vec!["r", "a", "b"]);
    assert_eq!(ends, vec!["a", "b", "r"]);
    assert_eq!(attrs, vec![("lang".to_string(), "en".to_string())]);
    assert_eq!(texts, "xy");
}

#[test]
fn sax_parse_error() {
    let failed = {
        let mut h = sax::Handler::default();
        h.error = Some(Box::new(|_msg, _line, _col| {}));
        sax::parse(b"<broken>", &mut h).is_err()
    };
    assert!(failed);
}
