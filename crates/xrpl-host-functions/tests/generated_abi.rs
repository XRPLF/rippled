//! Exercises what `host_functions!` generates: the trait is implementable and
//! the spec table agrees with the declarations in `src/lib.rs`.

use std::cell::RefCell;
use std::collections::HashSet;

use xrpl_host_functions::{HASH_LEN, HostError, HostFunctionSpec, HostFunctions, HostResult};

/// Records what it was asked to do; enough to prove the trait is usable.
///
/// Every method takes `&self`, so a host that records anything keeps it behind
/// interior mutability.
#[derive(Default)]
struct FakeHost {
    traced: RefCell<Vec<String>>,
}

/// The contract every byte-producing host function follows: write only if the
/// value fits, and report its true length either way, so the engine can turn a
/// value that doesn't fit into `BufferTooSmall` without the host knowing the
/// guest's buffer size.
fn put(out: &mut [u8], value: &[u8]) -> HostResult<usize> {
    if let Some(dst) = out.get_mut(..value.len()) {
        dst.copy_from_slice(value);
    }
    Ok(value.len())
}

impl HostFunctions for FakeHost {
    fn get_ledger_sqn(&self, out: &mut [u8]) -> HostResult<usize> {
        put(out, &7u32.to_le_bytes())
    }

    fn get_parent_ledger_time(&self, out: &mut [u8]) -> HostResult<usize> {
        put(out, &9u32.to_le_bytes())
    }

    fn get_parent_ledger_hash(&self, out: &mut [u8]) -> HostResult<usize> {
        put(out, &[0xab; HASH_LEN])
    }

    fn get_base_fee(&self, out: &mut [u8]) -> HostResult<usize> {
        put(out, &10u32.to_le_bytes())
    }

    /// Fails on a field it doesn't know, so the error channel is exercised too.
    fn get_current_ledger_obj_field(&self, field: i32, out: &mut [u8]) -> HostResult<usize> {
        if field < 0 {
            return Err(HostError::FieldNotFound);
        }
        put(out, &[field as u8])
    }

    fn sha512_half(&self, data: &[u8], out: &mut [u8]) -> HostResult<usize> {
        let mut digest = [0; HASH_LEN];
        digest[0] = data.len() as u8;
        put(out, &digest)
    }

    fn trace(&self, msg: &str, data: &[u8], as_hex: bool) -> HostResult<()> {
        self.traced
            .borrow_mut()
            .push(format!("{msg}/{}/{as_hex}", data.len()));
        Ok(())
    }

    fn trace_num(&self, msg: &str, number: i64) -> HostResult<()> {
        self.traced.borrow_mut().push(format!("{msg}={number}"));
        Ok(())
    }
}

#[test]
fn the_trait_is_implementable() {
    let host = FakeHost::default();
    let mut out = [0u8; HASH_LEN];

    assert_eq!(host.get_ledger_sqn(&mut out), Ok(4));
    assert_eq!(out[..4], [7, 0, 0, 0]);
    assert_eq!(host.get_parent_ledger_time(&mut out), Ok(4));
    assert_eq!(out[..4], [9, 0, 0, 0]);
    assert_eq!(host.get_parent_ledger_hash(&mut out), Ok(HASH_LEN));
    assert_eq!(out[0], 0xab);
    assert_eq!(host.get_base_fee(&mut out), Ok(4));
    assert_eq!(out[..4], [10, 0, 0, 0]);
    assert_eq!(host.get_current_ledger_obj_field(3, &mut out), Ok(1));
    assert_eq!(out[0], 3);
    assert_eq!(host.sha512_half(b"abc", &mut out), Ok(HASH_LEN));
    assert_eq!(out[0], 3);
    assert_eq!(host.trace("hello", b"xy", true), Ok(()));
    assert_eq!(host.trace_num("count", -1), Ok(()));

    assert_eq!(*host.traced.borrow(), ["hello/2/true", "count=-1"]);
}

/// The error channel every declaration carries: an `Err` the VM turns into the
/// wire's negative return code.
#[test]
fn a_failing_call_reports_its_error_code() {
    let host = FakeHost::default();
    let mut out = [0u8; 8];

    assert_eq!(
        host.get_current_ledger_obj_field(-1, &mut out),
        Err(HostError::FieldNotFound)
    );
    assert_eq!(HostError::FieldNotFound.code(), -2);
}

/// A host reports the value's true length even when it cannot write it, which is
/// what lets the engine answer `BufferTooSmall` on the guest's behalf.
#[test]
fn a_short_buffer_still_reports_the_true_length() {
    let host = FakeHost::default();
    let mut out = [0u8; 2];

    assert_eq!(host.get_ledger_sqn(&mut out), Ok(4));
    assert_eq!(
        out,
        [0, 0],
        "nothing is written when the value does not fit"
    );
}

/// The VM reaches the host as one shared trait object held in the wasmi `Store`,
/// which is what the `&self` receivers are for.
#[test]
fn the_trait_is_callable_through_a_shared_trait_object() {
    let fake = FakeHost::default();
    let host: &dyn HostFunctions = &fake;
    let mut out = [0u8; 4];

    assert_eq!(host.get_ledger_sqn(&mut out), Ok(4));
    assert_eq!(host.trace_num("count", 1), Ok(()));

    assert_eq!(*fake.traced.borrow(), ["count=1"]);
}

/// The whole table, written out: the one place the ABI's wire names and gas costs
/// appear as literals, and a deliberate change-detector, since both are consensus
/// input. Everything else reads `HostFunctionSpec::gas()` instead.
///
/// `ALL` is in declaration order, so comparing the whole vec pins the order and the
/// membership too.
#[test]
fn the_spec_table_matches_the_declarations() {
    let table: Vec<(&str, u64)> = HostFunctionSpec::ALL
        .iter()
        .map(|function| (function.wasm_name(), function.gas()))
        .collect();

    assert_eq!(
        table,
        [
            ("ldgr_index", 60),
            ("parent_ldgr_time", 60),
            ("parent_ldgr_hash", 60),
            ("base_fee", 60),
            ("home_le_field", 70),
            ("sha512_half", 2000),
            ("trace", 500),
            ("trace_num", 500),
        ]
    );
}

/// `ALL` is what a wasm engine iterates to register imports, so no two declarations
/// may collapse to the same wire name. The table above pins membership and order;
/// this adds only uniqueness, and restates nothing.
#[test]
fn every_variant_appears_in_all_exactly_once() {
    let names: HashSet<&str> = HostFunctionSpec::ALL
        .iter()
        .map(|function| function.wasm_name())
        .collect();

    assert_eq!(names.len(), HostFunctionSpec::ALL.len());
}

/// Both accessors are `const`, so an engine can build its import and gas tables at
/// compile time rather than on every invocation. The assertions sit in `const`
/// blocks so they are checked while compiling, which is the claim; the values
/// themselves are pinned above.
#[test]
fn the_table_is_usable_in_const_context() {
    const NAME: &str = HostFunctionSpec::Trace.wasm_name();
    const GAS: u64 = HostFunctionSpec::Trace.gas();

    const { assert!(!NAME.is_empty()) };
    const { assert!(GAS > 0) };
}
