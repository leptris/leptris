//! Compile-time layout invariants for the descriptor ABI mirror.
//!
//! These check the crate's internal consistency (trailing-field
//! discipline, alignment) — the BINDING crate's integration test
//! verifies the sizes against the engine's own introspection, which
//! is the authoritative gate.

use leptris_descriptor::*;
use std::mem::{align_of, size_of};

#[test]
fn trailing_field_discipline() {
    // Trailing additive fields must not change the size when zeroed:
    // zero-extended aggregate initializers stay valid across ABI
    // additions. repr(C) + these sizes are the contract.
    assert_eq!(size_of::<PlanSpec>(), 4 + 4 + size_of::<*const u8>());
    assert_eq!(align_of::<PlanSpec>(), align_of::<*const u8>());
}

#[test]
fn kind_and_flag_constants_match_engine_headers() {
    assert_eq!(PLAN_ABI_VERSION, 1);
    assert_eq!(PLAN_KIND_SCALAR, 1);
    assert_eq!(PLAN_KIND_COLLECTION, 2);
    assert_eq!(PLAN_KIND_NESTED, 3);
    assert_eq!(PLAN_KIND_RAW, 4);
    assert_eq!(PLAN_KIND_CONTENT, 5);
    assert_eq!(PLAN_KIND_CALLBACK, 6);
    assert_eq!(PLAN_FLAG_MIXED_CONTENT, 0x1);
    assert_eq!(PLAN_FLAG_ORDERED, 0x2);
    assert_eq!(PLAN_FLAG_CDATA, 0x4);
    assert_eq!(PLAN_FLAG_NS_LENIENT, 0x8);
    assert_eq!(PLAN_FLAG_EMIT_ORDER_SPINE, 0x10);
    assert_eq!(PLAN_NS_NONE, 0);
    assert_eq!(PLAN_NS_EXACT, 1);
    assert_eq!(PLAN_NS_ANY, 2);
    assert_eq!(PLAN_VALUE_ELEMENT, 0);
    assert_eq!(PLAN_VALUE_SCALAR, 1);
    assert_eq!(PLAN_VALUE_COLLECTION, 2);
    assert_eq!(PLAN_VALUE_RAW, 3);
    assert_eq!(PLAN_VALUE_CALLBACK, 4);
}

#[test]
fn row_layouts_are_pointer_sized_aligned() {
    assert_eq!(align_of::<AttrPredicate>(), align_of::<*const u8>());
    assert_eq!(align_of::<AttrPlan>(), align_of::<*const u8>());
    assert_eq!(align_of::<ChildPlan>(), align_of::<*const u8>());
    assert_eq!(align_of::<ElementPlan>(), align_of::<*const u8>());
}
