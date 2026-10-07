//! Shared descriptor-plan ABI for the leptris engine family.
//!
//! One crate per engine (`leptris` XML, `yeptris` YAML, `teptris`
//! TOML/JSON) targets this record layout, so hosts build plan rows
//! once and every engine's compiled-descriptor walk speaks the same
//! shape. The types mirror `src/include/leptris/descriptor.h` in the
//! leptris engine — field order is the ABI; NEVER renumber or
//! reorder. Additive trailing fields only, guarded by an ABI version
//! bump.
//!
//! Layout is verified at runtime against the engine's own
//! introspection (`leptris_plan_*_struct_size`) by the linking
//! binding crate's tests.
//!
//! This crate is `no_std`: types and constants only, plus the FFI
//! declarations behind the `link` feature.

#![no_std]
#![allow(non_camel_case_types, non_snake_case)]

use core::ffi::c_char;

/// Must equal `leptris_plan_abi_version()` reported by the engine.
pub const PLAN_ABI_VERSION: u32 = 1;

/// Child / attribute plan kinds. Append-only.
pub type PlanKind = u8;
pub const PLAN_KIND_SCALAR: PlanKind = 1;
pub const PLAN_KIND_COLLECTION: PlanKind = 2;
pub const PLAN_KIND_NESTED: PlanKind = 3;
pub const PLAN_KIND_RAW: PlanKind = 4;
pub const PLAN_KIND_CONTENT: PlanKind = 5;
pub const PLAN_KIND_CALLBACK: PlanKind = 6;
/// #1552: catch-all row — binds every element child no named
/// sibling row bound; emits one COLLECTION echoing the row
/// wire_name/type_tag; `child_plan_index >= 0` walks members
/// through that plan, else members are RAW serialized subtrees.
pub const PLAN_KIND_WILDCARD: PlanKind = 7;

/// Element plan flags.
pub const PLAN_FLAG_MIXED_CONTENT: u32 = 0x1;
pub const PLAN_FLAG_ORDERED: u32 = 0x2;
pub const PLAN_FLAG_CDATA: u32 = 0x4;
pub const PLAN_FLAG_NS_LENIENT: u32 = 0x8;
pub const PLAN_FLAG_EMIT_ORDER_SPINE: u32 = 0x10;

/// Namespace matching form for binding an element or attribute.
pub type PlanNsForm = u8;
pub const PLAN_NS_NONE: PlanNsForm = 0;
pub const PLAN_NS_EXACT: PlanNsForm = 1;
pub const PLAN_NS_ANY: PlanNsForm = 2;
/// #1560: matches the UNWRITTEN spelling — no written prefix,
/// regardless of the effective namespace URI. An unprefixed child
/// binds under both a namespace-less document and a default-xmlns
/// document; prefixed spellings never bind. Element rows only
/// (unprefixed attributes have no namespace by XML rules).
pub const PLAN_NS_UNQUALIFIED: PlanNsForm = 3;

/// Result node kinds (the value tree a walk returns).
pub type PlanValueKind = i32;
pub const PLAN_VALUE_ELEMENT: PlanValueKind = 0;
pub const PLAN_VALUE_SCALAR: PlanValueKind = 1;
pub const PLAN_VALUE_COLLECTION: PlanValueKind = 2;
pub const PLAN_VALUE_RAW: PlanValueKind = 3;
pub const PLAN_VALUE_CALLBACK: PlanValueKind = 4;

/// Attribute-match row: requires every (name, expected value) pair.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct AttrPredicate {
    /// XML attribute name as it appears on the wire.
    pub wire_name: *const c_char,
    /// String-equal match (non-null); host-owned.
    pub expected_value: *const c_char,
}

/// Attribute binding row.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct AttrPlan {
    /// XML attribute name as it appears on the wire.
    pub wire_name: *const c_char,
    /// `PlanKind`: SCALAR | COLLECTION | CALLBACK.
    pub kind: u8,
    /// Host-defined; echoed back verbatim.
    pub type_tag: u8,
    /// Number of `predicates` rows (0 = historical behavior).
    pub predicate_count: u16,
    pub pad_pred: u16,
    pub predicates: *const AttrPredicate,
    /// Attribute-level namespace form; `wire_name` is the LOCAL
    /// name when set. NONE (0) keeps wire-name lookup.
    pub ns_form: u8,
    pub pad_ns: u8,
    /// `PLAN_NS_EXACT` only.
    pub ns_uri: *const c_char,
    /// #1551: intended wire prefix for serialized output
    /// (`leptris_plan_serialize`); meaningful with EXACT.
    pub ns_prefix: *const c_char,
}

/// Child binding row.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct ChildPlan {
    /// XML element name as it appears on the wire: the prefixed
    /// form ("w:b") binds that literal prefix+local pair; the bare
    /// local form ("b") binds by local name and lets `ns_form` do
    /// the namespace work.
    pub wire_name: *const c_char,
    pub kind: u8,
    pub type_tag: u8,
    /// NESTED: index into `PlanSpec::plans`; -1 otherwise.
    pub child_plan_index: i32,
    /// Rule-level namespace form, consulted at child-match time.
    pub ns_form: u8,
    pub pad0: u8,
    /// `PLAN_NS_EXACT` only.
    pub ns_uri: *const c_char,
    /// Element-side predicates; same semantics as `AttrPredicate`.
    pub predicate_count: u16,
    pub pad_pred: u16,
    pub predicates: *const AttrPredicate,
    /// #1551: intended wire prefix for serialized output
    /// (`leptris_plan_serialize`); meaningful with EXACT.
    pub ns_prefix: *const c_char,
}

/// One element plan: the attribute and child rows bound to a name.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct ElementPlan {
    /// Local name this plan binds; matching uses the CHILD row's
    /// `wire_name`.
    pub element_name: *const c_char,
    pub ns_form: u8,
    pub pad0: u8,
    /// `PLAN_NS_EXACT` only.
    pub ns_uri: *const c_char,
    pub attribute_count: u32,
    pub child_count: u32,
    pub attribute_plans: *const AttrPlan,
    pub child_plans: *const ChildPlan,
    /// `PLAN_FLAG_*`.
    pub flags: u16,
    pub pad1: u16,
    /// #1551: the root plan's intended wire prefix for serialized
    /// output (`leptris_plan_serialize`); meaningful with EXACT.
    pub ns_prefix: *const c_char,
}

/// Top-level plan specification handed to the engine.
#[repr(C)]
#[derive(Clone, Copy)]
pub struct PlanSpec {
    /// Must equal the engine's `leptris_plan_abi_version()`.
    pub abi_version: u32,
    /// `plans[0]` is the root plan applied to the walk context.
    pub plan_count: u32,
    pub plans: *const ElementPlan,
}

#[cfg(feature = "link")]
pub mod ffi {
    //! Engine FFI for the descriptor surface. Enable this feature on
    //! exactly one crate in the graph (the binding that links an
    //! engine).

    use super::PlanSpec;
    use core::ffi::{c_char, c_void};

    /// Opaque engine document handle (leptris: `LeptrisDocument`).
    pub type DocumentHandle = *mut c_void;
    /// Opaque engine element handle (leptris: `LeptrisElement`).
    pub type ElementHandle = *mut c_void;
    /// Status out-parameter (leptris: `LeptrisStatus*`).
    pub type StatusPtr = *mut i32;
    /// Opaque compiled plan.
    pub type Plan = *mut c_void;
    /// Opaque walk result value.
    pub type PlanResult = *const c_void;

    extern "C" {
        pub fn leptris_plan_abi_version() -> u32;
        pub fn leptris_plan_spec_struct_size() -> usize;
        pub fn leptris_plan_element_row_size() -> usize;
        pub fn leptris_plan_child_row_size() -> usize;
        pub fn leptris_plan_attr_row_size() -> usize;
        pub fn leptris_plan_predicate_row_size() -> usize;

        pub fn leptris_plan_build(spec: *const PlanSpec, status: StatusPtr) -> Plan;
        pub fn leptris_plan_free(plan: Plan);

        pub fn leptris_plan_walk(
            doc: DocumentHandle,
            ctx: ElementHandle,
            plan: Plan,
            status: StatusPtr,
        ) -> PlanResult;
        pub fn leptris_plan_materialize(
            source: *const c_char,
            source_len: usize,
            plan: Plan,
            status: StatusPtr,
        ) -> PlanResult;
        pub fn leptris_plan_result_free(result: PlanResult);

        pub fn leptris_plan_value_kind(v: PlanResult) -> i32;
        pub fn leptris_plan_value_name(v: PlanResult) -> *const c_char;
        pub fn leptris_plan_value_type_tag(v: PlanResult) -> u8;
        pub fn leptris_plan_value_string(v: PlanResult) -> *const c_char;
        pub fn leptris_plan_value_length(v: PlanResult) -> usize;
        pub fn leptris_plan_value_position(v: PlanResult) -> usize;
        pub fn leptris_plan_value_node_kind(v: PlanResult) -> u8;
        pub fn leptris_plan_value_order_index(v: PlanResult) -> u32;
        pub fn leptris_plan_value_count(v: PlanResult) -> usize;
        pub fn leptris_plan_value_at(v: PlanResult, i: usize) -> PlanResult;
        pub fn leptris_plan_value_children_snapshot(
            v: PlanResult,
            names_blob: *mut c_char,
            blob_cap: usize,
            name_offsets: *mut usize,
            type_tags: *mut u8,
            child_handles: *mut PlanResult,
        ) -> usize;
        pub fn leptris_plan_value_attribute(
            v: PlanResult,
            wire_name: *const c_char,
        ) -> *const c_char;
        pub fn leptris_plan_value_int(v: PlanResult, out: *mut i64) -> i32;
        pub fn leptris_plan_value_float(v: PlanResult, out: *mut f64) -> i32;
        pub fn leptris_plan_value_bool(v: PlanResult, out: *mut i32) -> i32;
    }
}
