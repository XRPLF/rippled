use std::cell::Cell;
use std::fmt;
use std::sync::LazyLock;
use wasmi::{Config, Engine, Linker, Module, Store, StoreLimits, StoreLimitsBuilder, TrapCode};
use xrpl_host_functions::{HostError, HostFunctions};

use crate::abi::FatalHostError;
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

/// Why a run produced no result. Each variant is one outcome for the caller to
/// map to a TER.
#[derive(Debug)]
pub enum RunError {
    /// `wasm` is not a valid module under this engine's configuration.
    Compile(String),
    /// The module compiled but would not instantiate: an import the linker does
    /// not define, an initial memory past the page cap, a trapping start section.
    Instantiate(String),
    /// No export named `function_name` with signature `() -> i32`.
    EntryPoint(String),
    /// Gas exhausted — by the guest's own instructions or by a host call's
    /// charge. [`RunFailure::fuel_used`] is the whole limit.
    OutOfGas,
    /// The host could not serve a call.
    Internal,
    /// The module exports no linear memory, so no host call can be served.
    NoMemory,
    /// The guest trapped: `unreachable`, division by zero, an out-of-bounds
    /// access, or `memory.grow` past the page cap.
    Trap(String),
}

impl fmt::Display for RunError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            RunError::Compile(detail) => write!(f, "compile: {detail}"),
            RunError::Instantiate(detail) => write!(f, "instantiate: {detail}"),
            RunError::EntryPoint(detail) => write!(f, "no entry point {detail}"),
            RunError::OutOfGas => write!(f, "out of gas"),
            RunError::Internal => write!(f, "internal error"),
            RunError::NoMemory => write!(f, "no exported memory"),
            RunError::Trap(detail) => write!(f, "trap: {detail}"),
        }
    }
}

/// A failed run, with the gas it still owes: a contract that traps or exhausts
/// its gas is charged for what it burned.
#[derive(Debug)]
pub struct RunFailure {
    pub error: RunError,
    /// Fuel consumed before the failure. The whole limit when gas ran out; `0`
    /// when the module never ran.
    pub fuel_used: u64,
}

impl fmt::Display for RunFailure {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        write!(f, "{} (fuel used: {})", self.error, self.fuel_used)
    }
}

impl RunFailure {
    /// A failure the guest cannot have burned fuel before, because it stopped the
    /// run at or before the point the guest first gets to execute.
    fn owing_nothing(error: RunError) -> RunFailure {
        RunFailure {
            error,
            fuel_used: 0,
        }
    }
}

/// Fuel spent out of `gas`: the one place a run's cost is measured, so success,
/// trap and refusal all report it the same way.
fn fuel_used(store: &Store<VmState<'_>>, gas: u64) -> u64 {
    gas.saturating_sub(store.get_fuel().unwrap_or(0))
}

/// The outcome a `wasmi::Error` names for itself, if it names one, rather than
/// leaving it to the stage that raised it.
///
/// A host call the host could not serve traps with a [`FatalHostError`] payload,
/// which says which condition it was, so check for that before treating the error
/// as the guest's own doing. wasmi raises `OutOfFuel` when the guest's
/// *instructions* exhaust the meter — the same outcome by a different route, and
/// `as_trap_code` reports it whichever error kind carried it.
///
/// Both arise anywhere the guest executes, and a start section is guest code
/// running during instantiation, so every stage from there on asks this before
/// naming a failure after itself.
fn guest_halted(error: &wasmi::Error) -> Option<RunError> {
    if let Some(fatal) = error.downcast_ref::<FatalHostError>() {
        return Some(host_fatal(fatal.0));
    }
    (error.as_trap_code() == Some(TrapCode::OutOfFuel)).then_some(RunError::OutOfGas)
}

/// The outcome a host-fatal `HostError` is.
///
/// Exhaustive over `HostError` rather than closed with a wildcard, so a variant
/// added to the ABI has to be placed here before this compiles. Moving an
/// existing variant into [`crate::abi::is_fatal`]'s set is not caught that way —
/// it lands in the soft arm and reports `Internal` — so the two are read
/// together. The soft arm is otherwise unreachable: a guest-visible error is a
/// return code and never becomes a trap for [`guest_halted`] to unwrap.
fn host_fatal(error: HostError) -> RunError {
    match error {
        HostError::OutOfGas => RunError::OutOfGas,
        HostError::Internal => RunError::Internal,
        HostError::NoMemExported => RunError::NoMemory,
        HostError::FieldNotFound
        | HostError::BufferTooSmall
        | HostError::NoArray
        | HostError::NotLeafField
        | HostError::LocatorMalformed
        | HostError::SlotOutRange
        | HostError::SlotsFull
        | HostError::EmptySlot
        | HostError::LedgerObjNotFound
        | HostError::Decoding
        | HostError::DataFieldTooLarge
        | HostError::PointerOutOfBounds
        | HostError::InvalidParams
        | HostError::InvalidAccount
        | HostError::InvalidField
        | HostError::IndexOutOfBounds
        | HostError::FloatInputMalformed
        | HostError::FloatComputationError
        | HostError::NoRuntime
        | HostError::OutOfTransferLimit => RunError::Internal,
    }
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
) -> Result<RunOutcome, RunFailure> {
    let engine = wasm_engine();
    let module = Module::new(engine, wasm)
        .map_err(|e| RunFailure::owing_nothing(RunError::Compile(e.to_string())))?;

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
    // A store that will not take fuel, or imports that will not register, are
    // defects in the engine configuration or in this crate, not in the module:
    // nothing the contract did could have caused either.
    store
        .set_fuel(gas)
        .map_err(|_| RunFailure::owing_nothing(RunError::Internal))?;
    // The memory-page cap applies at instantiation too: an initial memory
    // declared past it fails to instantiate, as a `memory.grow` past it traps.
    store.limiter(|state| &mut state.mem_limits);

    let mut linker = Linker::<VmState<'h>>::new(engine);
    register_host_functions(&mut linker)
        .map_err(|_| RunFailure::owing_nothing(RunError::Internal))?;

    // Instantiation is the first point the guest can execute, through a start
    // section, so from here on the cost comes off the store rather than being
    // known to be nothing.
    let instance = match linker.instantiate_and_start(&mut store, &module) {
        Ok(instance) => instance,
        Err(e) => {
            return Err(RunFailure {
                error: guest_halted(&e).unwrap_or_else(|| RunError::Instantiate(e.to_string())),
                fuel_used: fuel_used(&store, gas),
            });
        }
    };
    let finish = match instance.get_typed_func::<(), i32>(&store, function_name) {
        Ok(finish) => finish,
        Err(e) => {
            return Err(RunFailure {
                error: RunError::EntryPoint(format!("'{function_name}': {e}")),
                fuel_used: fuel_used(&store, gas),
            });
        }
    };

    let result = match finish.call(&mut store, ()) {
        Ok(result) => result,
        Err(e) => {
            return Err(RunFailure {
                error: guest_halted(&e).unwrap_or_else(|| RunError::Trap(e.to_string())),
                fuel_used: fuel_used(&store, gas),
            });
        }
    };

    Ok(RunOutcome {
        result,
        fuel_used: fuel_used(&store, gas),
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
