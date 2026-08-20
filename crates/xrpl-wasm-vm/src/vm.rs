use std::cell::Cell;
use std::fmt;
use std::sync::LazyLock;
use wasmi::{
    Config, Engine, Export, Linker, Memory, Module, Store, StoreLimits, StoreLimitsBuilder,
    TrapCode,
};
use xrpl_host_functions::HostFunctions;

use crate::abi::{FatalHostError, Fault};
use crate::preflight::entry_point_fault;
use crate::register::register_host_functions;

/// wasm linear-memory page size, fixed by the wasm spec (64 KiB).
const WASM_PAGE_BYTES: u32 = 64 * 1024;

/// Linear-memory page cap.
pub const MAX_MEMORY_PAGES: u32 = 128;

/// [`MAX_MEMORY_PAGES`] in bytes: 8 MiB.
pub const MAX_MEMORY_BYTES: usize = (MAX_MEMORY_PAGES * WASM_PAGE_BYTES) as usize;

/// Total bytes that may cross the host/guest boundary in one [`run`], separate
/// from gas.
pub const TRANSFER_LIMIT_BYTES: u64 = 1 << 20;

/// Size cap on any single value crossing the boundary, in either direction; over
/// it is `DataFieldTooLarge`.
///
/// A protocol limit: `kMaxWasmDataLength` in `include/xrpl/protocol/Protocol.h`.
pub const MAX_FIELD_BYTES: usize = 1024;

/// State threaded through every host call, stored in the wasmi [`Store`].
pub(crate) struct VmState<'h> {
    pub(crate) host: &'h dyn HostFunctions,
    /// Enforces [`MAX_MEMORY_BYTES`] via `Store::limiter`, which needs a `&mut`
    /// into it from `&mut VmState` — hence a field rather than a local.
    pub(crate) mem_limits: StoreLimits,
    /// Remaining transfer budget for this run ([`TRANSFER_LIMIT_BYTES`]).
    ///
    /// A `Cell` because it is decremented from a shared `&Caller`. One thread per
    /// invocation touches the store, so the lack of `Sync` costs nothing.
    ///
    /// TODO: the extra charge for an unaligned field copy has nothing to attach to
    /// until this ABI gains a `FieldLocator` host function.
    pub(crate) transfer_budget: Cell<u64>,
    /// The guest's linear memory, resolved once by [`run`] after instantiation so
    /// no host call pays for an export lookup.
    ///
    /// Caching the handle is sound because a [`Memory`] is an arena index, not a
    /// pointer to the bytes: it survives `memory.grow`, and `data`/`data_mut`
    /// re-derive the slice per call.
    ///
    /// The handle is scoped to one store, so this assumes **one module, one
    /// instance, one store per `run`**. Module linking or nested execution would
    /// have to resolve per instance: a cached handle would serve a call against the
    /// wrong instance's memory, which is a wrong answer rather than an error.
    pub(crate) memory: Option<Memory>,
    /// Where a host writes a value before [`crate::abi::write_buffered`] copies it
    /// to the guest. One buffer per run, so no call zero-fills one of its own.
    ///
    /// Inline rather than boxed: the store's data is built once and then only
    /// borrowed, so a kilobyte in it costs a move where a `Box` costs an
    /// allocation. A local would cost neither, but `forbid(unsafe_code)` means a
    /// stack buffer is zero-filled — per call, which is the cost this removes.
    pub(crate) out_buffer: [u8; MAX_FIELD_BYTES],
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
    /// The module compiled but the engine would not accept it: an import the
    /// linker does not define, or an initial memory past the page cap. Not guest
    /// code failing — a start section that traps is [`RunError::Trap`].
    Instantiate(String),
    /// No export named `function_name` with signature `() -> i32`: absent, not a
    /// function, or a function of another type — which the detail tells apart.
    EntryPoint(String),
    /// Gas exhausted — by the guest's own instructions or by a host call's
    /// charge. [`RunFailure::fuel_used`] is the whole limit.
    OutOfGas,
    /// The host could not serve a call.
    Internal,
    /// A host call had no linear memory to work in: the module exports none, or
    /// the call came from a start section, which runs before there is an instance
    /// to resolve the memory from.
    NoMemory,
    /// The guest trapped: `unreachable`, division by zero, an out-of-bounds
    /// access, or `memory.grow` past the page cap. Wherever the guest was
    /// executing, including a start section during instantiation.
    Trap(String),
}

impl fmt::Display for RunError {
    fn fmt(&self, f: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            RunError::Compile(detail) => write!(f, "compile: {detail}"),
            RunError::Instantiate(detail) => write!(f, "instantiate: {detail}"),
            // The detail says which of the entry point's failures this is, since
            // "no entry point" would be wrong for an export of the wrong type.
            RunError::EntryPoint(detail) => write!(f, "{detail}"),
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
    /// A failure with no fuel accounted: it stopped the run at or before the guest's
    /// first instruction, or under a store with no meter to read.
    fn owing_nothing(error: RunError) -> RunFailure {
        RunFailure {
            error,
            fuel_used: 0,
        }
    }
}

/// Fuel spent out of `gas`: the one place a run's cost is measured, so success,
/// trap and refusal all report it the same way.
///
/// `Store::get_fuel` fails only on a store without fuel metering, which
/// [`build_wasm_engine`] rules out and `run`'s `set_fuel` would already have
/// caught — so a failure here is a defect in this crate. It must not become a
/// number: `0` forgives a run its whole cost, `gas` charges an untouched one for
/// everything. [`RunError::Internal`] instead.
fn fuel_used(store: &Store<VmState<'_>>, gas: u64) -> Result<u64, RunError> {
    store
        .get_fuel()
        .map(|remaining| gas.saturating_sub(remaining))
        .map_err(|_| RunError::Internal)
}

/// Report `error` with the run's cost attached. A cost that cannot be read replaces
/// the outcome rather than being invented — see [`fuel_used`].
fn failed(store: &Store<VmState<'_>>, gas: u64, error: RunError) -> RunFailure {
    match fuel_used(store, gas) {
        Ok(fuel_used) => RunFailure { error, fuel_used },
        Err(unmetered) => RunFailure::owing_nothing(unmetered),
    }
}

/// The outcome a `wasmi::Error` names for itself, if any, rather than leaving it to
/// the stage that raised it.
///
/// Two ways a run halts mid-flight: a host call that could not be served, which
/// carries a [`FatalHostError`] saying which condition it was, and the guest's own
/// instructions exhausting the meter, which wasmi raises as `OutOfFuel`.
///
/// Both can happen anywhere the guest executes — including a start section, which
/// is guest code running during instantiation — so every stage from there on asks
/// this before naming a failure after itself.
fn guest_halted(error: &wasmi::Error) -> Option<RunError> {
    if let Some(fatal) = error.downcast_ref::<FatalHostError>() {
        return Some(fatal.0.into());
    }
    (error.as_trap_code() == Some(TrapCode::OutOfFuel)).then_some(RunError::OutOfGas)
}

/// Why instantiation failed, once [`guest_halted`] has ruled out the two conditions
/// that can arise anywhere.
///
/// A start section is guest code, so it can trap on its own — `unreachable`, a
/// division by zero, an out-of-bounds access — and a trap is the guest's fault
/// wherever it happens. Naming that after the *stage* would file it beside the
/// module faults a caller treats as its own defect, and charge nothing for
/// instructions the contract burned. What is left for [`RunError::Instantiate`] is a
/// module the linker or the store would not accept at all.
fn instantiation_failure(error: &wasmi::Error) -> RunError {
    match error.as_trap_code() {
        Some(_) => RunError::Trap(error.to_string()),
        None => RunError::Instantiate(error.to_string()),
    }
}

/// The outcome a [`Fault`] is: the one place a stopped call becomes a stopped run.
///
/// Total and one arm each, because a `Fault` is only ever a condition that stops the
/// run — the guest-visible codes cannot reach here, which is what
/// [`crate::abi::CallError`] buys. A fault added later has no arm and does not
/// compile.
impl From<Fault> for RunError {
    fn from(fault: Fault) -> RunError {
        match fault {
            Fault::OutOfGas => RunError::OutOfGas,
            Fault::Internal => RunError::Internal,
            Fault::NoMemory => RunError::NoMemory,
        }
    }
}

/// The process-wide wasmi engine, built once on first use.
///
/// The configuration is consensus-fixed and identical for every invocation, and an
/// [`Engine`] is an internally `Arc`ed `Send + Sync` handle, so one shared engine
/// serves concurrent [`run`] calls.
pub(crate) fn wasm_engine() -> &'static Engine {
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
    config.allow_start_fn(false);
    Engine::new(&config)
}

/// Compile `wasm` for this engine.
///
/// The one path to a [`Module`]: the configuration is what decides whether a
/// contract is valid at all, so [`run`] and [`crate::check`] must not be able to
/// compile against different ones.
pub(crate) fn compile(wasm: &[u8]) -> Result<Module, String> {
    Module::new(wasm_engine(), wasm).map_err(|e| e.to_string())
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
    let module =
        compile(wasm).map_err(|detail| RunFailure::owing_nothing(RunError::Compile(detail)))?;

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
            memory: None,
            out_buffer: [0u8; MAX_FIELD_BYTES],
        },
    );

    store
        .set_fuel(gas)
        .map_err(|_| RunFailure::owing_nothing(RunError::Internal))?;
    store.limiter(|state| &mut state.mem_limits);

    let mut linker = Linker::<VmState<'h>>::new(engine);
    register_host_functions(&mut linker)
        .map_err(|_| RunFailure::owing_nothing(RunError::Internal))?;

    let instance = match linker.instantiate_and_start(&mut store, &module) {
        Ok(instance) => instance,
        Err(e) => {
            let error = guest_halted(&e).unwrap_or_else(|| instantiation_failure(&e));
            return Err(failed(&store, gas, error));
        }
    };
    store.data_mut().memory = instance.exports(&store).find_map(Export::into_memory);

    let function = match instance.get_typed_func::<(), i32>(&store, function_name) {
        Ok(function) => function,
        Err(e) => {
            let found = instance
                .get_export(&store, function_name)
                .map(|export| export.ty(&store));
            let error =
                RunError::EntryPoint(format!("{}: {e}", entry_point_fault(found, function_name)));
            return Err(failed(&store, gas, error));
        }
    };

    let result = match function.call(&mut store, ()) {
        Ok(result) => result,
        Err(e) => {
            let error = guest_halted(&e).unwrap_or_else(|| RunError::Trap(e.to_string()));
            return Err(failed(&store, gas, error));
        }
    };

    let fuel_used = fuel_used(&store, gas).map_err(RunFailure::owing_nothing)?;
    Ok(RunOutcome { result, fuel_used })
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    fn the_engine_is_one_engine() {
        assert!(Engine::same(wasm_engine(), wasm_engine()));
    }

    /// The only place these numbers appear as literals; every other test derives
    /// them from the constants.
    #[test]
    fn the_limits_are_the_protocol_limits() {
        assert_eq!(MAX_MEMORY_PAGES, 128, "linear-memory page cap");
        assert_eq!(MAX_MEMORY_BYTES, 8 * 1024 * 1024, "page cap in bytes");
        assert_eq!(MAX_FIELD_BYTES, 1024, "kMaxWasmDataLength");
        assert_eq!(TRANSFER_LIMIT_BYTES, 1 << 20, "kWasmTransferLimit");
    }
}
