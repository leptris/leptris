//! Descriptor-ABI verification against the linked engine.
//!
//! The `leptris-descriptor` crate mirrors the C record layout; this
//! test is the authoritative gate — it compares Rust `size_of`
//! against the engine's own row-size introspection and runs a
//! build/walk round trip through the mirrored types. A layout drift
//! anywhere between descriptor.h and the mirror fails here.

use leptris_descriptor as descriptor;
use leptris_descriptor::ffi as plan_ffi;

// Reference the linking crate: cargo only applies the leptris
// build script's -lleptris directives when the crate is actually
// used by this binary — without an anchor the engine symbols in
// the extern blocks below stay unresolved.
#[allow(dead_code)]
fn link_anchor() {
    let _ = leptris::Document::parse;
}
use std::ffi::{c_char, CStr, CString};
use std::mem::size_of;
use std::ptr;

const DOC: &[u8] = b"<catalog><book id='b1'><title>Alpha</title></book>\
                     <book id='b2'><title>Beta</title></book></catalog>";

/* Local opaque handles: the crate's `ffi` module is private, and
 * the engine's document API only needs pointer identity here. */
#[repr(C)]
struct DocH {
    _p: [u8; 0],
}
#[repr(C)]
struct ElemH {
    _p: [u8; 0],
}

extern "C" {
    fn leptris_parse_string(src: *const c_char, len: usize, status: *mut i32) -> *mut DocH;
    fn leptris_document_free(doc: *mut DocH);
    fn leptris_document_root(doc: *mut DocH) -> *mut ElemH;
}

#[test]
fn row_sizes_match_engine_introspection() {
    unsafe {
        assert_eq!(
            plan_ffi::leptris_plan_spec_struct_size(),
            size_of::<descriptor::PlanSpec>()
        );
        assert_eq!(
            plan_ffi::leptris_plan_element_row_size(),
            size_of::<descriptor::ElementPlan>()
        );
        assert_eq!(
            plan_ffi::leptris_plan_child_row_size(),
            size_of::<descriptor::ChildPlan>()
        );
        assert_eq!(
            plan_ffi::leptris_plan_attr_row_size(),
            size_of::<descriptor::AttrPlan>()
        );
        assert_eq!(
            plan_ffi::leptris_plan_predicate_row_size(),
            size_of::<descriptor::AttrPredicate>()
        );
        assert_eq!(plan_ffi::leptris_plan_abi_version(), 1);
        assert_eq!(descriptor::PLAN_ABI_VERSION, 1);
    }
}

#[test]
fn plan_build_and_walk_round_trip() {
    let title = CString::new("title").unwrap();
    let book = CString::new("book").unwrap();
    let catalog = CString::new("catalog").unwrap();
    let id = CString::new("id").unwrap();

    let attr_row = descriptor::AttrPlan {
        wire_name: id.as_ptr(),
        kind: descriptor::PLAN_KIND_SCALAR,
        type_tag: 7,
        predicate_count: 0,
        pad_pred: 0,
        predicates: ptr::null(),
        ns_form: descriptor::PLAN_NS_NONE,
        pad_ns: 0,
        ns_uri: ptr::null(),
        ns_prefix: ptr::null(),
    };
    let child_row = descriptor::ChildPlan {
        wire_name: title.as_ptr(),
        kind: descriptor::PLAN_KIND_SCALAR,
        type_tag: 9,
        child_plan_index: -1,
        ns_form: descriptor::PLAN_NS_NONE,
        pad0: 0,
        ns_uri: ptr::null(),
        predicate_count: 0,
        pad_pred: 0,
        predicates: ptr::null(),
        ns_prefix: ptr::null(),
    };
    let book_plan = descriptor::ElementPlan {
        element_name: book.as_ptr(),
        ns_form: descriptor::PLAN_NS_NONE,
        pad0: 0,
        ns_uri: ptr::null(),
        attribute_count: 1,
        child_count: 1,
        attribute_plans: &attr_row,
        child_plans: &child_row,
        flags: 0,
        pad1: 0,
        ns_prefix: ptr::null(),
    };
    let root_child = descriptor::ChildPlan {
        wire_name: book.as_ptr(),
        kind: descriptor::PLAN_KIND_NESTED,
        type_tag: 5,
        /* plans[0] is the ROOT plan; the book plan is plans[1]
         * (NESTED recurses into it for each match). */
        child_plan_index: 1,
        ns_form: descriptor::PLAN_NS_NONE,
        pad0: 0,
        ns_uri: ptr::null(),
        predicate_count: 0,
        pad_pred: 0,
        predicates: ptr::null(),
        ns_prefix: ptr::null(),
    };
    let root_plan = descriptor::ElementPlan {
        element_name: catalog.as_ptr(),
        ns_form: descriptor::PLAN_NS_NONE,
        pad0: 0,
        ns_uri: ptr::null(),
        attribute_count: 0,
        child_count: 1,
        attribute_plans: ptr::null(),
        child_plans: &root_child,
        flags: 0,
        pad1: 0,
        ns_prefix: ptr::null(),
    };
    let plans = [root_plan, book_plan];
    let spec = descriptor::PlanSpec {
        abi_version: descriptor::PLAN_ABI_VERSION,
        plan_count: 2,
        plans: plans.as_ptr(),
    };

    unsafe {
        let mut status: i32 = 0;
        let plan = plan_ffi::leptris_plan_build(&spec, &mut status);
        assert_eq!(status, 0, "plan_build status");
        assert!(!plan.is_null());

        let doc = leptris_parse_string(DOC.as_ptr() as *const c_char, DOC.len(), &mut status);
        assert!(!doc.is_null());
        let root = leptris_document_root(doc);

        let result = plan_ffi::leptris_plan_walk(
            doc as plan_ffi::DocumentHandle,
            root as plan_ffi::ElementHandle,
            plan,
            &mut status,
        );
        assert_eq!(status, 0, "plan_walk status");
        assert!(!result.is_null());

        // Root binds two nested book values (one per match).
        assert_eq!(
            plan_ffi::leptris_plan_value_kind(result),
            descriptor::PLAN_VALUE_ELEMENT
        );
        assert_eq!(plan_ffi::leptris_plan_value_count(result), 2);

        // Each book: the id attribute row and the title scalar.
        let b1 = plan_ffi::leptris_plan_value_at(result, 0);
        assert_eq!(plan_ffi::leptris_plan_value_type_tag(b1), 5);
        let id_v = plan_ffi::leptris_plan_value_attribute(b1, id.as_ptr());
        assert_eq!(CStr::from_ptr(id_v).to_bytes(), b"b1");
        assert_eq!(plan_ffi::leptris_plan_value_count(b1), 1);
        let title_v = plan_ffi::leptris_plan_value_at(b1, 0);
        assert_eq!(plan_ffi::leptris_plan_value_type_tag(title_v), 9);
        assert_eq!(
            CStr::from_ptr(plan_ffi::leptris_plan_value_string(title_v)).to_bytes(),
            b"Alpha"
        );

        let b2 = plan_ffi::leptris_plan_value_at(result, 1);
        let id_v2 = plan_ffi::leptris_plan_value_attribute(b2, id.as_ptr());
        assert_eq!(CStr::from_ptr(id_v2).to_bytes(), b"b2");

        plan_ffi::leptris_plan_result_free(result);
        leptris_document_free(doc);
        plan_ffi::leptris_plan_free(plan);
    }
}
