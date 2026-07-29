//! `host_functions!` must work outside the crate that declares the ABI, and must
//! not care what is in scope where it lands.

use xrpl_host_functions_macros::host_functions;

/// Shadows the name the expansion refers to, while the real one is never imported
/// here. Both are inert: the generated code names the type by absolute path, and a
/// bare `HostFnSpec` in the expansion would fail to compile against this one.
struct HostFnSpec;

host_functions! {
    /// Answers with the number it was given.
    #[gas = 7]
    #[wasm_name = "ping"]
    fn ping(&self, number: i32) -> i32;
}

struct Host;

impl HostFunctions for Host {
    fn ping(&self, number: i32) -> i32 {
        number
    }
}

#[test]
fn the_expansion_ignores_a_conflicting_local_type() {
    let _decoy = HostFnSpec;

    assert_eq!(HostFunctionSpec::ALL.len(), 1);
    assert_eq!(HostFunctionSpec::Ping.wasm_name(), "ping");
    assert_eq!(HostFunctionSpec::Ping.gas(), 7);
}

/// The generated trait is implementable from another crate, which is the point of
/// declaring the ABI in a library at all.
#[test]
fn the_generated_trait_is_implementable_here() {
    assert_eq!(Host.ping(3), 3);
}
