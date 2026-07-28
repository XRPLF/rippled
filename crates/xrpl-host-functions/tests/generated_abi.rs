//! Exercises what `host_functions!` generates: the trait is implementable and
//! the spec table agrees with the declarations in `src/lib.rs`.

use xrpl_host_functions::{HASH_LEN, HostFnSpec, HostFunctionSpec, HostFunctions};

/// Records what it was asked to do; enough to prove the trait is usable.
#[derive(Default)]
struct FakeHost {
    traced: Vec<String>,
}

impl HostFunctions for FakeHost {
    fn get_ledger_sqn(&mut self) -> [u8; 4] {
        7u32.to_le_bytes()
    }

    fn get_current_ledger_obj_field(&mut self, field: i32) -> Vec<u8> {
        vec![field as u8]
    }

    fn sha512_half(&mut self, data: &[u8]) -> [u8; HASH_LEN] {
        let mut digest = [0; HASH_LEN];
        digest[0] = data.len() as u8;
        digest
    }

    fn trace(&mut self, msg: &str, data: &[u8], as_hex: bool) {
        self.traced.push(format!("{msg}/{}/{as_hex}", data.len()));
    }

    fn trace_num(&mut self, msg: &str, number: i64) {
        self.traced.push(format!("{msg}={number}"));
    }
}

#[test]
fn the_trait_is_implementable() {
    let mut host = FakeHost::default();

    assert_eq!(host.get_ledger_sqn(), [7, 0, 0, 0]);
    assert_eq!(host.get_current_ledger_obj_field(3), vec![3]);
    assert_eq!(host.sha512_half(b"abc")[0], 3);
    host.trace("hello", b"xy", true);
    host.trace_num("count", -1);

    assert_eq!(host.traced, ["hello/2/true", "count=-1"]);
}

#[test]
fn the_spec_table_matches_the_declarations() {
    assert_eq!(HostFunctionSpec::ALL.len(), 5);
    assert_eq!(
        HostFunctionSpec::GetLedgerSqn.spec(),
        HostFnSpec {
            name: "ldgr_index",
            gas: 60
        }
    );
    assert_eq!(HostFunctionSpec::Sha512Half.gas(), 2000);
    assert_eq!(
        HostFunctionSpec::GetCurrentLedgerObjField.wasm_name(),
        "home_le_field"
    );
}

/// `ALL` is what a wasm engine iterates to register imports, so it must be complete.
#[test]
fn every_variant_appears_in_all_exactly_once() {
    let mut names: Vec<&str> = HostFunctionSpec::ALL
        .iter()
        .map(|function| function.wasm_name())
        .collect();
    names.sort_unstable();

    assert_eq!(
        names,
        [
            "home_le_field",
            "ldgr_index",
            "sha512_half",
            "trace",
            "trace_num"
        ]
    );
}

/// The generated `spec` is `const`, so gas costs are available at compile time.
#[test]
fn the_table_is_usable_in_const_context() {
    const TRACE_GAS: u64 = HostFunctionSpec::Trace.gas();
    assert_eq!(TRACE_GAS, 500);
}
