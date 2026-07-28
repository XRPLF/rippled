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
/// boundary (via the `read_bytes` / `write_into` helpers in `abi.rs`) during
/// one [`run_escrow`] invocation. A budget separate from gas.
pub const TRANSFER_LIMIT_BYTES: u64 = 1 << 20;

/// State threaded through every host call, stored in the wasmi [`Store`].
pub struct VmState<'h> {
    pub(crate) host: &'h dyn HostFunctions,
    /// Enforces [`MAX_MEMORY_BYTES`] via `Store::limiter` (see `run_escrow`).
    /// Lives in `VmState` (rather than as a standalone local) because the
    /// limiter callback wasmi holds must be able to produce a `&mut` into it
    /// from `&mut VmState`.
    pub(crate) mem_limits: StoreLimits,
    /// Remaining transfer-limit budget for this run (see
    /// [`TRANSFER_LIMIT_BYTES`]); decremented in `abi.rs`'s `read_bytes` /
    /// `write_into` by the number of bytes actually moved.
    ///
    /// A `Cell`, not a plain `u64`: `AbiArg::read` (the guest -> host read
    /// path) only has a shared `&Caller`, while `write_into` (the host ->
    /// guest write path) has `&mut Caller` — both need to decrement this
    /// counter, so it can't be an ordinary field mutated only through
    /// `&mut`. The store (and this counter) is only ever touched from one
    /// thread per invocation, so `Cell`'s lack of `Sync` is not an issue.
    ///
    /// NOTE: the C++ `unalignedGas`/`FieldLocator` alignment-copy charge
    /// (`HostFuncWrapper.cpp:44,390-397`) is deferred — the PoC has no
    /// `FieldLocator` host functions yet to attach it to.
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
/// The engine's configuration is consensus-fixed and identical for every
/// invocation, so there is no reason to rebuild it per finish. A wasmi
/// [`Engine`] is an `Arc` internally (cheap to share, `Send + Sync`), and
/// modules compiled against it are per-invocation, so a single shared engine is
/// safe to reuse across concurrent [`run_escrow`] calls.
pub fn wasm_engine() -> &'static Engine {
    static ENGINE: LazyLock<Engine> = LazyLock::new(build_wasm_engine);
    &ENGINE
}

/// Build the wasmi engine with the sandboxing knobs the escrow VM requires.
/// (Unchanged from the original skeleton: a deterministic, minimal-feature
/// configuration with fuel metering on.)
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
    // Registers the memory-page cap; also applied at instantiation time (an
    // initial memory declared past the cap fails instantiation, same as a
    // `memory.grow` past it traps at runtime).
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
