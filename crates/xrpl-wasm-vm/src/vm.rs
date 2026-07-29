use std::cell::Cell;
use std::sync::LazyLock;
use wasmi::{Config, Engine, Linker, Module, Store, StoreLimits, StoreLimitsBuilder};
use xrpl_host_functions::HostFunctions;

use crate::register::register_host_functions;

/// wasm linear-memory page size, fixed by the wasm spec (64 KiB).
const WASM_PAGE_BYTES: u32 = 64 * 1024;

/// Linear-memory page cap.
pub const MAX_MEMORY_PAGES: u32 = 128;

/// Byte form of [`MAX_MEMORY_PAGES`]: `128 * 65536 = 8_388_608` (8 MiB).
pub const MAX_MEMORY_BYTES: usize = (MAX_MEMORY_PAGES * WASM_PAGE_BYTES) as usize;

/// Per-run transfer-limit budget: total bytes that may cross the host/guest
/// boundary during one [`run`] invocation. Separate from gas.
pub const TRANSFER_LIMIT_BYTES: u64 = 1 << 20;

/// Size cap on any single value crossing the host/guest boundary, in either
/// direction. A value over it is refused with `DataFieldTooLarge`.
///
/// Mirrors `kMaxWasmDataLength = 1 * 1024` in
/// `include/xrpl/protocol/Protocol.h:261`, enforced there by `getDataSlice` /
/// `setData` (`src/libxrpl/tx/wasm/HostFuncWrapper.cpp`).
pub const MAX_FIELD_BYTES: usize = 1024;

/// State threaded through every host call, stored in the wasmi [`Store`].
pub struct VmState<'h> {
    pub(crate) host: &'h dyn HostFunctions,
    /// Enforces [`MAX_MEMORY_BYTES`] via `Store::limiter`. It lives here because
    /// the limiter callback wasmi holds has to produce a `&mut` into it from
    /// `&mut VmState`.
    pub(crate) mem_limits: StoreLimits,
    /// Remaining transfer-limit budget for this run ([`TRANSFER_LIMIT_BYTES`]),
    /// decremented in `abi.rs` by the bytes actually moved.
    ///
    /// A `Cell` because the read path holds only a shared `&Caller` while the
    /// write path holds `&mut Caller`, and both decrement it. One thread per
    /// invocation touches the store, so `Cell`'s lack of `Sync` is no issue.
    ///
    /// TODO: the C++ `unalignedGas` alignment-copy charge
    /// (`HostFuncWrapper.cpp:44,390-397`) has no `FieldLocator` host function
    /// here to attach to.
    pub(crate) transfer_budget: Cell<u64>,
}

/// Outcome of running an escrow contract to completion.
#[derive(Debug)]
pub struct RunOutcome {
    /// The value returned by the exported entry point (`finish`): `> 0` means
    /// allow the escrow to finish.
    pub result: i32,
    /// Fuel (gas) consumed by the whole invocation — guest instructions plus
    /// the per-call host charges.
    pub fuel_used: u64,
}

/// The process-wide wasmi engine, built once on first use.
///
/// The configuration is consensus-fixed and identical for every invocation, and
/// an [`Engine`] is an internally `Arc`ed `Send + Sync` handle, so one shared
/// engine serves concurrent [`run`] calls.
pub fn wasm_engine() -> &'static Engine {
    static ENGINE: LazyLock<Engine> = LazyLock::new(build_wasm_engine);
    &ENGINE
}

/// Build the wasmi engine the escrow VM requires: deterministic, minimal
/// features, fuel metering on.
fn build_wasm_engine() -> Engine {
    let mut config = Config::default();
    config.consume_fuel(true);
    config.ignore_custom_sections(true);
    config.wasm_mutable_global(false);
    config.wasm_multi_value(false);
    config.wasm_sign_extension(false);
    config.wasm_saturating_float_to_int(false);
    config.wasm_bulk_memory(false);
    config.wasm_reference_types(false);
    config.wasm_tail_call(false);
    config.wasm_extended_const(false);
    config.floats(false);
    config.wasm_multi_memory(false);
    config.wasm_custom_page_sizes(false);
    config.wasm_memory64(false);
    config.wasm_wide_arithmetic(false);
    // TODO: enable option to reject wasm code containing start section after next wasmi release
    Engine::new(&config)
}

/// Run a contract: compile `wasm`, give it `gas` fuel, service its host
/// calls through `host`, and call the exported `function_name`.
pub fn run<'h>(
    wasm: &[u8],
    gas: u64,
    host: &'h dyn HostFunctions,
    function_name: &str,
) -> Result<RunOutcome, String> {
    let engine = wasm_engine();
    let module = Module::new(engine, wasm).map_err(|e| format!("compile: {e}"))?;

    let mem_limits = StoreLimitsBuilder::new()
        .memory_size(MAX_MEMORY_BYTES)
        .trap_on_grow_failure(true)
        .build();
    let mut store = Store::new(
        engine,
        VmState {
            host,
            mem_limits,
            transfer_budget: Cell::new(TRANSFER_LIMIT_BYTES),
        },
    );
    store.set_fuel(gas).map_err(|e| format!("set_fuel: {e}"))?;
    // The memory-page cap applies at instantiation too: an initial memory
    // declared past it fails to instantiate, as a `memory.grow` past it traps.
    store.limiter(|state| &mut state.mem_limits);

    let mut linker = Linker::<VmState<'h>>::new(engine);
    register_host_functions(&mut linker)?;

    let instance = linker
        .instantiate_and_start(&mut store, &module)
        .map_err(|e| format!("instantiate: {e}"))?;
    let finish = instance
        .get_typed_func::<(), i32>(&store, function_name)
        .map_err(|e| format!("no entry point '{function_name}': {e}"))?;

    let result = finish
        .call(&mut store, ())
        .map_err(|e| format!("trap: {e}"))?;

    let remaining = store.get_fuel().unwrap_or(0);
    Ok(RunOutcome {
        result,
        fuel_used: gas.saturating_sub(remaining),
    })
}

#[cfg(test)]
mod tests {
    use super::*;

    /// The engine is built once and shared, so two invocations must not compile
    /// their modules against different engines.
    #[test]
    fn the_engine_is_one_engine() {
        assert!(Engine::same(wasm_engine(), wasm_engine()));
    }

    /// The four protocol limits against the C++ values they mirror: a deliberate
    /// change-detector, and the only place these numbers appear as literals —
    /// every other test derives from the constants. The C++ names make the parity
    /// greppable against `include/xrpl/protocol/Protocol.h`.
    #[test]
    fn the_limits_are_the_protocol_limits() {
        assert_eq!(MAX_MEMORY_PAGES, 128, "maxPages");
        assert_eq!(MAX_MEMORY_BYTES, 8 * 1024 * 1024, "maxPages, in bytes");
        assert_eq!(MAX_FIELD_BYTES, 1024, "kMaxWasmDataLength");
        assert_eq!(TRANSFER_LIMIT_BYTES, 1 << 20, "kWasmTransferLimit");
    }
}
