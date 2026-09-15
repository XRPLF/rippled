//! `host_functions!` must work outside the crate that declares the ABI: the only
//! names its expansion needs are `WasmValType` and the ones the declarations
//! themselves spell.
//!
//! That this crate compiles is also what shows the emitted `wasmi_glue!` costs
//! nothing to carry: its body names an engine throughout, there is no engine
//! here, and nobody here expands it.

use xrpl_host_functions::{HostResult, WasmValType};
use xrpl_host_functions_macros::host_functions;

host_functions! {
    /// Answers with the number it was given.
    #[gas = 7]
    #[wasm_name = "ping"]
    fn ping(&self, number: i32) -> HostResult<i32>;
}

struct Host;

impl HostFunctions for Host {
    fn ping(&self, number: i32) -> HostResult<i32> {
        Ok(number)
    }
}

#[test]
fn the_generated_table_stands_on_its_own() {
    assert_eq!(HostFunctionSpec::ALL.len(), 1);
    assert_eq!(HostFunctionSpec::Ping.wasm_name(), "ping");
    assert_eq!(HostFunctionSpec::Ping.gas(), 7);
    assert_eq!(HostFunctionSpec::Ping.wasm_params(), &[WasmValType::I32]);
    assert_eq!(HostFunctionSpec::Ping.wasm_result(), Some(WasmValType::I32));
}

/// The generated trait is implementable from another crate, which is the point of
/// declaring the ABI in a library at all.
#[test]
fn the_generated_trait_is_implementable_here() {
    assert_eq!(Host.ping(3), Ok(3));
}
