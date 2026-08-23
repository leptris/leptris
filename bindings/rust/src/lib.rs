//! Idiomatic Rust bindings for [libleptris](https://github.com/leptris/leptris),
//! a pure-C99 XML 1.0 parser with an XPath 1.0 engine and SAX front end.
//!
//! ```no_run
//! use leptris::Document;
//!
//! let doc = Document::parse(b"<root><a id='1'>hello</a></root>").unwrap();
//! let root = doc.root().unwrap();
//! assert_eq!(root.name(), "root");
//!
//! let hits = doc.xpath("//a").unwrap();
//! assert_eq!(hits.len(), 1);
//! assert_eq!(hits.element(0).unwrap().text(), Some("hello"));
//! ```
//!
//! The C library is resolved at link time: set `LEPTRIS_LIB_PATH`
//! (see `build.rs`) or install libleptris on the library path.

mod ffi;

use std::ffi::{CStr, CString};
use std::fmt;
use std::marker::PhantomData;
use std::os::raw::c_char;

/// Error surfaced by every fallible binding call. Mirrors the
/// `LeptrisStatus` codes from the C API.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum Error {
    Memory,
    Parse,
    Xpath,
    NullArg,
    InvalidArg,
    NotFound,
    Io,
    NotImplemented,
    Unknown(i32),
}

impl Error {
    fn from_status(status: i32) -> Error {
        match status {
            ffi::LEPTRIS_ERROR_MEMORY => Error::Memory,
            ffi::LEPTRIS_ERROR_PARSE => Error::Parse,
            ffi::LEPTRIS_ERROR_XPATH => Error::Xpath,
            ffi::LEPTRIS_ERROR_NULL_ARG => Error::NullArg,
            ffi::LEPTRIS_ERROR_INVALID_ARG => Error::InvalidArg,
            ffi::LEPTRIS_ERROR_NOT_FOUND => Error::NotFound,
            ffi::LEPTRIS_ERROR_IO => Error::Io,
            ffi::LEPTRIS_ERROR_NOT_IMPLEMENTED => Error::NotImplemented,
            other => Error::Unknown(other),
        }
    }

    /// The C error message for this status.
    pub fn message(&self) -> &'static str {
        let code = match self {
            Error::Memory => ffi::LEPTRIS_ERROR_MEMORY,
            Error::Parse => ffi::LEPTRIS_ERROR_PARSE,
            Error::Xpath => ffi::LEPTRIS_ERROR_XPATH,
            Error::NullArg => ffi::LEPTRIS_ERROR_NULL_ARG,
            Error::InvalidArg => ffi::LEPTRIS_ERROR_INVALID_ARG,
            Error::NotFound => ffi::LEPTRIS_ERROR_NOT_FOUND,
            Error::Io => ffi::LEPTRIS_ERROR_IO,
            Error::NotImplemented => ffi::LEPTRIS_ERROR_NOT_IMPLEMENTED,
            Error::Unknown(c) => *c,
        };
        unsafe {
            let msg = ffi::leptris_error_message(code);
            if msg.is_null() {
                "unknown error"
            } else {
                CStr::from_ptr(msg).to_str().unwrap_or("unknown error")
            }
        }
    }
}

impl fmt::Display for Error {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{} ({:?})", self.message(), self)
    }
}

impl std::error::Error for Error {}

pub type Result<T> = std::result::Result<T, Error>;

unsafe fn cstr_opt(ptr: *const c_char) -> Option<&'static str> {
    if ptr.is_null() {
        None
    } else {
        Some(CStr::from_ptr(ptr).to_str().unwrap_or(""))
    }
}

/// A parsed XML document. Owns the C document; freed on drop.
pub struct Document {
    raw: *mut ffi::LeptrisDocument,
}

impl Document {
    /// Parse XML from bytes. The bytes must be valid UTF-8 (XML 1.0).
    pub fn parse(xml: &[u8]) -> Result<Document> {
        let mut status: i32 = ffi::LEPTRIS_OK;
        let raw = unsafe {
            ffi::leptris_parse_string(xml.as_ptr() as *const c_char, xml.len(), &mut status)
        };
        if raw.is_null() {
            return Err(Error::from_status(status));
        }
        Ok(Document { raw })
    }

    /// The document element.
    pub fn root(&self) -> Option<Element<'_>> {
        let raw = unsafe { ffi::leptris_document_root(self.raw) };
        if raw.is_null() {
            None
        } else {
            Some(Element {
                raw,
                doc: PhantomData,
            })
        }
    }

    /// Evaluate an XPath 1.0 expression against the whole document.
    pub fn xpath(&self, expr: &str) -> Result<XPathResult<'_>> {
        let c_expr = CString::new(expr).map_err(|_| Error::InvalidArg)?;
        let raw =
            unsafe { ffi::leptris_xpath_eval(self.raw, std::ptr::null_mut(), c_expr.as_ptr()) };
        if raw.is_null() {
            return Err(Error::Xpath);
        }
        Ok(XPathResult {
            raw,
            doc: PhantomData,
        })
    }

    /// Serialize the document back to an owned XML string.
    pub fn serialize(&self) -> Result<String> {
        let raw = unsafe { ffi::leptris_document_serialize(self.raw, std::ptr::null_mut()) };
        if raw.is_null() {
            return Err(Error::Unknown(0));
        }
        let s = unsafe {
            let s = CStr::from_ptr(raw).to_string_lossy().into_owned();
            ffi::leptris_free_string(raw);
            s
        };
        Ok(s)
    }
}

impl Drop for Document {
    fn drop(&mut self) {
        unsafe { ffi::leptris_document_free(self.raw) };
    }
}

/// An element, borrowed from its document for its whole lifetime.
#[derive(Clone, Copy)]
pub struct Element<'doc> {
    raw: *mut ffi::LeptrisElement,
    doc: PhantomData<&'doc Document>,
}

impl<'doc> Element<'doc> {
    /// The element's name.
    pub fn name(&self) -> &'doc str {
        unsafe { cstr_opt(ffi::leptris_element_name(self.raw)).unwrap_or("") }
    }

    /// The element's text content (direct text children).
    pub fn text(&self) -> Option<&'doc str> {
        unsafe { cstr_opt(ffi::leptris_element_text(self.raw)) }
    }

    /// Number of child elements.
    pub fn child_count(&self) -> usize {
        unsafe { ffi::leptris_element_child_count(self.raw) }
    }

    /// First child element with the given name, if any.
    pub fn child(&self, name: &str) -> Option<Element<'doc>> {
        let c_name = CString::new(name).ok()?;
        let raw = unsafe { ffi::leptris_element_first_child(self.raw, c_name.as_ptr()) };
        if raw.is_null() {
            None
        } else {
            Some(Element {
                raw,
                doc: PhantomData,
            })
        }
    }

    /// First child element of any name.
    pub fn first_child(&self) -> Option<Element<'doc>> {
        let raw = unsafe { ffi::leptris_element_first_child_any(self.raw) };
        if raw.is_null() {
            None
        } else {
            Some(Element {
                raw,
                doc: PhantomData,
            })
        }
    }

    /// Next sibling element of any name.
    pub fn next_sibling(&self) -> Option<Element<'doc>> {
        let raw = unsafe { ffi::leptris_element_next_sibling_any(self.raw) };
        if raw.is_null() {
            None
        } else {
            Some(Element {
                raw,
                doc: PhantomData,
            })
        }
    }

    /// Iterate this element's child elements.
    pub fn children(&self) -> ElementChildren<'doc> {
        ElementChildren {
            next: self.first_child(),
        }
    }

    /// Iterate this element's attributes as (name, value) pairs.
    pub fn attributes(&self) -> Attributes<'doc> {
        Attributes {
            elem: *self,
            next: unsafe {
                let first = ffi::leptris_element_first_attribute(self.raw);
                if first.is_null() {
                    None
                } else {
                    Some(first)
                }
            },
        }
    }

    /// Look up one attribute value by name.
    pub fn attribute(&self, name: &str) -> Option<&'doc str> {
        self.attributes().find(|(n, _)| *n == name).map(|(_, v)| v)
    }
}

/// Iterator over child elements.
pub struct ElementChildren<'doc> {
    next: Option<Element<'doc>>,
}

impl<'doc> Iterator for ElementChildren<'doc> {
    type Item = Element<'doc>;

    fn next(&mut self) -> Option<Element<'doc>> {
        let cur = self.next?;
        self.next = cur.next_sibling();
        Some(cur)
    }
}

/// Iterator over an element's attributes.
pub struct Attributes<'doc> {
    elem: Element<'doc>,
    next: Option<*mut ffi::LeptrisAttribute>,
}

impl<'doc> Iterator for Attributes<'doc> {
    type Item = (&'doc str, &'doc str);

    fn next(&mut self) -> Option<Self::Item> {
        let attr = self.next?;
        unsafe {
            let name = cstr_opt(ffi::leptris_attribute_get_name(attr))?;
            let value = cstr_opt(ffi::leptris_attribute_get_value(self.elem.raw, attr))?;
            self.next = {
                let n = ffi::leptris_attribute_next(attr);
                if n.is_null() {
                    None
                } else {
                    Some(n)
                }
            };
            Some((name, value))
        }
    }
}

/// The XPath node kind reported by [`XPathResult::kind`].
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum XPathNodeKind {
    Element,
    Attribute,
    Text,
    Other,
}

/// A live XPath result, borrowed from its document; freed on drop.
pub struct XPathResult<'doc> {
    raw: *mut ffi::LeptrisXPathResult,
    doc: PhantomData<&'doc Document>,
}

impl<'doc> XPathResult<'doc> {
    /// Number of nodes in the result (nodesets only).
    pub fn len(&self) -> usize {
        unsafe { ffi::leptris_xpath_result_count(self.raw) }
    }

    pub fn is_empty(&self) -> bool {
        self.len() == 0
    }

    /// The element at `index` (elements only; other kinds are skipped
    /// by the C API and yield `None`).
    pub fn element(&self, index: usize) -> Option<Element<'doc>> {
        let raw = unsafe { ffi::leptris_xpath_result_get(self.raw, index) };
        if raw.is_null() {
            None
        } else {
            Some(Element {
                raw,
                doc: PhantomData,
            })
        }
    }

    /// The kind of the node at `index` (mixed nodesets).
    pub fn kind(&self, index: usize) -> XPathNodeKind {
        match unsafe { ffi::leptris_xpath_result_node_kind(self.raw, index) } {
            ffi::LEPTRIS_XPATH_NODE_ELEMENT => XPathNodeKind::Element,
            ffi::LEPTRIS_XPATH_NODE_ATTRIBUTE => XPathNodeKind::Attribute,
            ffi::LEPTRIS_XPATH_NODE_TEXT => XPathNodeKind::Text,
            _ => XPathNodeKind::Other,
        }
    }

    /// The name of the node at `index` (elements and attributes).
    pub fn node_name(&self, index: usize) -> Option<&'doc str> {
        unsafe { cstr_opt(ffi::leptris_xpath_result_node_name(self.raw, index)) }
    }

    /// The string value of the node at `index` (text, comment, CDATA,
    /// attributes).
    pub fn node_value(&self, index: usize) -> Option<&'doc str> {
        unsafe { cstr_opt(ffi::leptris_xpath_result_node_value(self.raw, index)) }
    }

    /// Numeric result value.
    pub fn number(&self) -> f64 {
        unsafe { ffi::leptris_xpath_result_number(self.raw) }
    }

    /// String result value (owned copy).
    pub fn to_string(&self) -> String {
        unsafe {
            let raw = ffi::leptris_xpath_result_string(self.raw);
            if raw.is_null() {
                return String::new();
            }
            let s = CStr::from_ptr(raw).to_string_lossy().into_owned();
            ffi::leptris_free_string(raw);
            s
        }
    }
}

impl Drop for XPathResult<'_> {
    fn drop(&mut self) {
        unsafe { ffi::leptris_xpath_result_free(self.raw) };
    }
}

pub mod sax {
    //! Event-driven parsing with closures — a Rust-flavored veneer
    //! over `leptris_sax_parse`.

    use super::ffi;
    use std::ffi::CStr;
    use std::os::raw::{c_char, c_int};

    /// Callbacks for SAX events; every field is optional. The
    /// lifetime lets closures borrow local state for the duration of
    /// the parse.
    #[derive(Default)]
    pub struct Handler<'a> {
        pub start_document: Option<Box<dyn FnMut() + 'a>>,
        pub end_document: Option<Box<dyn FnMut() + 'a>>,
        pub start_element: Option<Box<dyn FnMut(&str, &[(String, String)]) + 'a>>,
        pub end_element: Option<Box<dyn FnMut(&str) + 'a>>,
        pub characters: Option<Box<dyn FnMut(&str) + 'a>>,
        pub comment: Option<Box<dyn FnMut(&str) + 'a>>,
        pub cdata: Option<Box<dyn FnMut(&str) + 'a>>,
        pub processing_instruction: Option<Box<dyn FnMut(&str, &str) + 'a>>,
        pub start_prefix_mapping: Option<Box<dyn FnMut(&str, &str) + 'a>>,
        pub end_prefix_mapping: Option<Box<dyn FnMut(&str) + 'a>>,
        pub error: Option<Box<dyn FnMut(&str, i32, i32) + 'a>>,
    }

    /* Trampolines: user_data is a *mut Handler. */
    unsafe fn ud<'h>(data: *mut core::ffi::c_void) -> &'h mut Handler<'h> {
        &mut *(data as *mut Handler)
    }
    fn s<'a>(p: *const c_char) -> &'a str {
        if p.is_null() {
            ""
        } else {
            unsafe { CStr::from_ptr(p).to_str().unwrap_or("") }
        }
    }

    unsafe extern "C" fn tr_start_document(data: *mut core::ffi::c_void) {
        if let Some(f) = ud(data).start_document.as_mut() {
            f()
        }
    }
    unsafe extern "C" fn tr_end_document(data: *mut core::ffi::c_void) {
        if let Some(f) = ud(data).end_document.as_mut() {
            f()
        }
    }
    unsafe extern "C" fn tr_start_element(
        data: *mut core::ffi::c_void,
        name: *const c_char,
        attrs: *const *const c_char,
    ) {
        let mut pairs = Vec::new();
        let mut p = attrs;
        while !(*p).is_null() {
            let k = CStr::from_ptr(*p).to_string_lossy().into_owned();
            let v = CStr::from_ptr(*p.add(1)).to_string_lossy().into_owned();
            pairs.push((k, v));
            p = p.add(2);
        }
        if let Some(f) = ud(data).start_element.as_mut() {
            f(s(name), &pairs)
        }
    }
    unsafe extern "C" fn tr_end_element(data: *mut core::ffi::c_void, name: *const c_char) {
        if let Some(f) = ud(data).end_element.as_mut() {
            f(s(name))
        }
    }
    unsafe extern "C" fn tr_characters(
        data: *mut core::ffi::c_void,
        text: *const c_char,
        len: usize,
    ) {
        if let Some(f) = ud(data).characters.as_mut() {
            let slice = std::slice::from_raw_parts(text as *const u8, len);
            f(&String::from_utf8_lossy(slice))
        }
    }
    unsafe extern "C" fn tr_comment(data: *mut core::ffi::c_void, comment: *const c_char) {
        if let Some(f) = ud(data).comment.as_mut() {
            f(s(comment))
        }
    }
    unsafe extern "C" fn tr_cdata(data: *mut core::ffi::c_void, cdata: *const c_char) {
        if let Some(f) = ud(data).cdata.as_mut() {
            f(s(cdata))
        }
    }
    unsafe extern "C" fn tr_pi(
        data: *mut core::ffi::c_void,
        target: *const c_char,
        d: *const c_char,
    ) {
        if let Some(f) = ud(data).processing_instruction.as_mut() {
            f(s(target), s(d))
        }
    }
    unsafe extern "C" fn tr_start_ns(
        data: *mut core::ffi::c_void,
        prefix: *const c_char,
        uri: *const c_char,
    ) {
        if let Some(f) = ud(data).start_prefix_mapping.as_mut() {
            f(s(prefix), s(uri))
        }
    }
    unsafe extern "C" fn tr_end_ns(data: *mut core::ffi::c_void, prefix: *const c_char) {
        if let Some(f) = ud(data).end_prefix_mapping.as_mut() {
            f(s(prefix))
        }
    }
    unsafe extern "C" fn tr_error(
        data: *mut core::ffi::c_void,
        message: *const c_char,
        line: c_int,
        column: c_int,
    ) {
        if let Some(f) = ud(data).error.as_mut() {
            f(s(message), line, column)
        }
    }

    /// Parse `xml` with the given callbacks. Returns `Err` on a
    /// parse failure (the `error` callback, when set, also fires).
    pub fn parse(xml: &[u8], handler: &mut Handler<'_>) -> super::Result<()> {
        let c_handler = ffi::LeptrisSAXHandler {
            start_document: if handler.start_document.is_some() {
                Some(tr_start_document)
            } else {
                None
            },
            end_document: if handler.end_document.is_some() {
                Some(tr_end_document)
            } else {
                None
            },
            start_element: if handler.start_element.is_some() {
                Some(tr_start_element)
            } else {
                None
            },
            end_element: if handler.end_element.is_some() {
                Some(tr_end_element)
            } else {
                None
            },
            characters: if handler.characters.is_some() {
                Some(tr_characters)
            } else {
                None
            },
            comment: if handler.comment.is_some() {
                Some(tr_comment)
            } else {
                None
            },
            cdata: if handler.cdata.is_some() {
                Some(tr_cdata)
            } else {
                None
            },
            processing_instruction: if handler.processing_instruction.is_some() {
                Some(tr_pi)
            } else {
                None
            },
            start_prefix_mapping: if handler.start_prefix_mapping.is_some() {
                Some(tr_start_ns)
            } else {
                None
            },
            end_prefix_mapping: if handler.end_prefix_mapping.is_some() {
                Some(tr_end_ns)
            } else {
                None
            },
            error: if handler.error.is_some() {
                Some(tr_error)
            } else {
                None
            },
        };
        let rc = unsafe {
            ffi::leptris_sax_parse(
                xml.as_ptr() as *const c_char,
                xml.len(),
                &c_handler as *const ffi::LeptrisSAXHandler as *mut ffi::LeptrisSAXHandler,
                handler as *mut Handler<'_> as *mut core::ffi::c_void,
            )
        };
        if rc == 0 {
            Ok(())
        } else {
            Err(super::Error::Parse)
        }
    }
}
