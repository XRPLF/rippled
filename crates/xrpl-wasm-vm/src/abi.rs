use crate::vm::{MAX_FIELD_BYTES, VmState};
use wasmi::{Caller, Extern, Memory};
use xrpl_host_functions::{HostError, HostFunctionSpec, HostFunctions, HostResult};

// ---------------------------------------------------------------------------
// ABI marshaling: charge a call's gas at one point (`charged`) so every
// registered closure pays for itself exactly once, and hand the result to the
// engine on one of the two channels a host call answers on — a return code the
// guest reads, or a trap it cannot observe.
// ---------------------------------------------------------------------------

/// A host-fatal [`HostError`] on its way out of a host call as a wasmi trap.
///
/// wasmi takes an arbitrary payload out of a host function as long as it
/// implements `wasmi::errors::HostError`, a trait with no methods and no blanket
/// impl. Carrying the `HostError` itself is what lets [`crate::vm::run`] name the
/// condition with `downcast_ref` rather than string-comparing a message, as the
/// C++ path did with its `hfErrOutOfGas` trap strings.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct FatalHostError(pub(crate) HostError);

impl wasmi::errors::HostError for FatalHostError {}

impl core::fmt::Display for FatalHostError {
    /// A fixed prefix and the variant's name. wasmi folds this text into its own
    /// `Error`'s `Display`, which is the only place it surfaces, so keep it
    /// stable and greppable.
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        write!(f, "host call refused: {:?}", self.0)
    }
}

/// Whether a [`HostError`] is host-fatal: the host could not serve the call at
/// all, so the guest is stopped where it stands rather than handed a code it may
/// ignore. Everything else is guest-visible — `OutOfTransferLimit` included,
/// which was the one soft failure in C++ too.
///
/// Spelled variant by variant rather than as a range over `code()`, so which
/// channel a `HostError` added to the ABI later takes is a choice someone makes
/// here rather than one its number makes for it.
pub(crate) fn is_fatal(error: HostError) -> bool {
    matches!(
        error,
        HostError::OutOfGas | HostError::Internal | HostError::NoMemExported
    )
}

/// Charge a host call's gas from its spec, run its body, and hand the result to
/// the engine. Every registered closure goes through here, so gas cannot be
/// forgotten.
pub(crate) fn charged(
    caller: &mut Caller<'_, VmState<'_>>,
    op: HostFunctionSpec,
    body: impl FnOnce(&mut Caller<'_, VmState<'_>>) -> HostResult<i32>,
) -> Result<i32, wasmi::Error> {
    to_wire(charge(caller, op.gas()).and_then(|()| body(caller)))
}

/// Put a host-function result on one of the two channels a host call answers on.
///
/// A value, or an error the guest is meant to act on, is the `i32` the wasm
/// function returns: `>= 0` a value, `< 0` a [`HostError`] code. A host-fatal
/// error ([`is_fatal`]) leaves as a `wasmi::Error` instead, unwinding the guest
/// at the call — C++ threw for exactly these, and a guest handed `OutOfGas` as a
/// code runs on to the end of its current basic block, a stopping point wasmi's
/// `ConsumeFuel` placement decides rather than the protocol.
fn to_wire(result: HostResult<i32>) -> Result<i32, wasmi::Error> {
    match result {
        Ok(value) => Ok(value),
        Err(error) if is_fatal(error) => Err(wasmi::Error::host(FatalHostError(error))),
        Err(error) => Ok(error.code()),
    }
}

// ---------------------------------------------------------------------------
// Gas + memory helpers. Every guest access is a checked wasmi slice op.
// ---------------------------------------------------------------------------

/// Deduct `cost` fuel for a host call; `OutOfGas` if it would go negative.
fn charge<T>(caller: &mut Caller<'_, T>, cost: u64) -> Result<(), HostError> {
    let remaining = caller.get_fuel().map_err(|_| HostError::Internal)?;
    match remaining.checked_sub(cost) {
        Some(left) => caller.set_fuel(left).map_err(|_| HostError::Internal),
        None => {
            // Spending what is left makes the run's reported cost the whole gas
            // limit, as C++ reports it on out-of-gas. The store outlives the
            // trap `OutOfGas` becomes, and `run` reads the cost off it.
            let _ = caller.set_fuel(0);
            Err(HostError::OutOfGas)
        }
    }
}

/// Deduct `n` bytes from the per-run transfer-limit budget
/// ([`crate::vm::TRANSFER_LIMIT_BYTES`], separate from gas);
/// `OutOfTransferLimit` if it would go negative.
fn charge_transfer(state: &VmState<'_>, n: usize) -> Result<(), HostError> {
    let n = n as u64;
    let remaining = state.transfer_budget.get();
    match remaining.checked_sub(n) {
        Some(left) => {
            state.transfer_budget.set(left);
            Ok(())
        }
        None => Err(HostError::OutOfTransferLimit),
    }
}

/// The guest's exported linear memory.
fn memory<T>(caller: &Caller<'_, T>) -> Result<Memory, HostError> {
    match caller.get_export("memory") {
        Some(Extern::Memory(mem)) => Ok(mem),
        _ => Err(HostError::NoMemExported),
    }
}

/// Bounds-check `[ptr, ptr + len)` and return a `&[u8]` aliasing guest linear
/// memory — no allocation, no copy. The slice borrows `caller`, so it lives only
/// as long as the host call it feeds; host functions never re-enter the guest and
/// move its memory.
///
/// Checks params validity, the [`MAX_FIELD_BYTES`] cap (`DataFieldTooLarge`) and
/// the transfer budget, in that order, before the slice is formed.
pub(crate) fn read_borrowed<'a>(
    caller: &'a Caller<'_, VmState<'_>>,
    ptr: i32,
    len: i32,
) -> HostResult<&'a [u8]> {
    if ptr < 0 || len < 0 {
        return Err(HostError::InvalidParams);
    }
    let (ptr, len) = (ptr as usize, len as usize);
    if len > MAX_FIELD_BYTES {
        return Err(HostError::DataFieldTooLarge);
    }
    charge_transfer(caller.data(), len)?;
    let end = ptr.checked_add(len).ok_or(HostError::PointerOutOfBounds)?;
    memory(caller)?
        .data(caller)
        .get(ptr..end)
        .ok_or(HostError::PointerOutOfBounds)
}

/// Service a "fill-the-caller's-buffer" host call: bounds-check the guest output
/// region `[dst, dst + cap)` and hand the host a `&mut [u8]` aliasing it, so the
/// host writes straight into guest linear memory. Returns the byte count.
///
/// `fill` reports the value's true length and writes only what fits, leaving the
/// engine the policy the guest observes: the [`MAX_FIELD_BYTES`] cap
/// (`DataFieldTooLarge`), the buffer fit (`BufferTooSmall`), then the transfer
/// budget — the order the C++ `setData` path uses. Those checks follow `fill`,
/// since the length is unknown before it runs, so a refused value may leave bytes
/// in the guest's own buffer; a negative status tells the guest not to read it.
pub(crate) fn write_into(
    caller: &mut Caller<'_, VmState<'_>>,
    dst: i32,
    cap: i32,
    fill: impl FnOnce(&dyn HostFunctions, &mut [u8]) -> HostResult<usize>,
) -> HostResult<i32> {
    if dst < 0 || cap < 0 {
        return Err(HostError::InvalidParams);
    }
    let (dst, cap) = (dst as usize, cap as usize);
    let mem = memory(caller)?;
    // Copy the shared `&dyn HostFunctions` out of the store data (references are
    // Copy) so the data borrow ends before we borrow guest memory mutably.
    let host: &dyn HostFunctions = caller.data().host;
    let end = dst.checked_add(cap).ok_or(HostError::PointerOutOfBounds)?;
    let out = mem
        .data_mut(&mut *caller)
        .get_mut(dst..end)
        .ok_or(HostError::PointerOutOfBounds)?;

    let n = fill(host, out)?;

    if n > MAX_FIELD_BYTES {
        return Err(HostError::DataFieldTooLarge);
    }
    if n > cap {
        return Err(HostError::BufferTooSmall);
    }
    charge_transfer(caller.data(), n)?;
    // The cap check above bounds `n`, so the count reaches the wire whole: an
    // `i32` the guest reads as a byte count, never a truncation of a larger one.
    Ok(n as i32)
}

// The input buffer in `read_write` lives on the stack, sized to the field cap.
// Guard the assumption that the cap stays small enough for that to be fine.
const _: () = assert!(
    MAX_FIELD_BYTES <= 8 * 1024,
    "read_write's input buffer is a stack array; keep MAX_FIELD_BYTES small"
);

/// Service a host call that reads one region of guest memory and writes another
/// (e.g. `sha512_half`).
///
/// The input is copied into a stack buffer bounded by [`MAX_FIELD_BYTES`], so it
/// stays valid while [`write_into`] borrows guest memory mutably for the output —
/// no aliasing reasoning, at the price of zero-filling the buffer each call. The
/// output half is [`write_into`], so it obeys the same policy as a plain write.
pub(crate) fn read_write(
    caller: &mut Caller<'_, VmState<'_>>,
    src: i32,
    src_len: i32,
    dst: i32,
    cap: i32,
    call: impl FnOnce(&dyn HostFunctions, &[u8], &mut [u8]) -> HostResult<usize>,
) -> HostResult<i32> {
    if src < 0 || src_len < 0 {
        return Err(HostError::InvalidParams);
    }
    let len = src_len as usize;
    if len > MAX_FIELD_BYTES {
        return Err(HostError::DataFieldTooLarge);
    }
    charge_transfer(caller.data(), len)?;

    // Copy the input out before `write_into` borrows guest memory mutably.
    let mut buf = [0u8; MAX_FIELD_BYTES];
    memory(caller)?
        .read(&*caller, src as usize, &mut buf[..len])
        .map_err(|_| HostError::PointerOutOfBounds)?;
    let input = &buf[..len];

    write_into(caller, dst, cap, |host, out| call(host, input, out))
}

// ---------------------------------------------------------------------------
// Unit tests
//
// A `Caller` exists only for the duration of a host call, so `read_borrowed`,
// `write_into`, `read_write` and `memory` are unreachable from here; `tests/`
// covers them by running real modules against a fake host.
// ---------------------------------------------------------------------------

#[cfg(test)]
mod tests {
    use super::*;
    use crate::vm::TRANSFER_LIMIT_BYTES;
    use std::cell::Cell;
    use wasmi::StoreLimitsBuilder;

    /// A host no test here calls; `charge_transfer` takes the store data, which
    /// has to hold one.
    struct UncalledHost;

    impl HostFunctions for UncalledHost {
        fn get_ledger_sqn(&self, _out: &mut [u8]) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn get_current_ledger_obj_field(&self, _field: i32, _out: &mut [u8]) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn sha512_half(&self, _data: &[u8], _out: &mut [u8]) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn trace(&self, _msg: &str, _data: &[u8], _as_hex: bool) -> HostResult<()> {
            unreachable!("no unit test in this module calls the host")
        }
        fn trace_num(&self, _msg: &str, _number: i64) -> HostResult<()> {
            unreachable!("no unit test in this module calls the host")
        }
    }

    /// A `VmState` whose transfer budget starts at `budget`.
    fn state(budget: u64) -> VmState<'static> {
        VmState {
            host: &UncalledHost,
            mem_limits: StoreLimitsBuilder::new().build(),
            transfer_budget: Cell::new(budget),
        }
    }

    /// The status a result reaches the guest as. `wasmi::Error` is not `PartialEq`,
    /// so a test that expects the guest-visible channel says so here.
    fn wire(result: HostResult<i32>) -> i32 {
        to_wire(result)
            .unwrap_or_else(|trap| panic!("expected a guest-visible status, got a trap: {trap}"))
    }

    /// The guest-visible channel: a value passes through, and a soft error
    /// arrives as its negative wire code — a call the engine served either way,
    /// because the guest is the one who decides what to do about it.
    #[test]
    fn a_success_becomes_the_value_and_an_error_becomes_its_code() {
        assert_eq!(wire(Ok(0)), 0);
        assert_eq!(wire(Ok(32)), 32);
        assert_eq!(wire(Err(HostError::BufferTooSmall)), -3);
    }

    /// The three conditions the host cannot serve a call under. Named once, so the
    /// two tests below are one statement about the same set.
    const FATAL: [HostError; 3] = [
        HostError::OutOfGas,
        HostError::Internal,
        HostError::NoMemExported,
    ];

    /// The fatal channel: a trap, carrying the condition so `run` can name the
    /// outcome without parsing a message.
    #[test]
    fn a_host_fatal_error_becomes_a_trap_carrying_it() {
        for error in FATAL {
            let trap =
                to_wire(Err(error)).expect_err("a fatal error must not reach the guest as a code");
            let payload = trap.downcast_ref::<FatalHostError>().unwrap_or_else(|| {
                panic!("{error:?}: expected a FatalHostError payload, got: {trap}")
            });
            assert_eq!(*payload, FatalHostError(error));
        }
    }

    /// Which errors take which channel, as a deliberate change-detector: the
    /// three in [`FATAL`] trap, and everything else is a code the guest acts on.
    ///
    /// `OutOfTransferLimit` is the row worth reading twice. It is the one budget
    /// a contract can be expected to handle — C++ made it the single soft failure
    /// among these, and this fork keeps that — so a guest asking for more than
    /// the run's remaining 1 MiB is told no, not killed.
    #[test]
    fn only_the_host_fatal_errors_trap() {
        for error in FATAL {
            assert!(is_fatal(error), "{error:?} must stop the run");
        }

        for error in [
            HostError::OutOfTransferLimit,
            HostError::DataFieldTooLarge,
            HostError::BufferTooSmall,
            HostError::PointerOutOfBounds,
            HostError::InvalidParams,
            HostError::FieldNotFound,
            HostError::Decoding,
            HostError::NoRuntime,
        ] {
            assert!(!is_fatal(error), "{error:?} must reach the guest as a code");
            assert_eq!(wire(Err(error)), error.code());
        }
    }

    #[test]
    fn a_transfer_spends_the_budget() {
        let state = state(100);

        assert_eq!(charge_transfer(&state, 30), Ok(()));
        assert_eq!(state.transfer_budget.get(), 70);
        assert_eq!(charge_transfer(&state, 70), Ok(()));
        assert_eq!(state.transfer_budget.get(), 0);
    }

    /// The budget bounds the total, so the last transfer that fits is allowed and
    /// the one that would overrun is refused whole — never partially charged.
    #[test]
    fn a_transfer_past_the_budget_is_refused_and_charges_nothing() {
        let state = state(100);

        assert_eq!(
            charge_transfer(&state, 101),
            Err(HostError::OutOfTransferLimit)
        );
        assert_eq!(
            state.transfer_budget.get(),
            100,
            "a refusal must not charge"
        );
        assert_eq!(charge_transfer(&state, 100), Ok(()));
        assert_eq!(
            charge_transfer(&state, 1),
            Err(HostError::OutOfTransferLimit)
        );
    }

    #[test]
    fn transferring_nothing_costs_nothing() {
        let state = state(0);

        assert_eq!(charge_transfer(&state, 0), Ok(()));
        assert_eq!(state.transfer_budget.get(), 0);
    }

    /// The field cap holds any one call to a small share of the run's budget, so
    /// the budget bounds a run rather than a call. An inequality, not the two
    /// values: those are pinned in `vm.rs`.
    #[test]
    fn no_single_value_can_exhaust_the_run_budget() {
        assert!(
            (MAX_FIELD_BYTES as u64) * 64 <= TRANSFER_LIMIT_BYTES,
            "one {MAX_FIELD_BYTES}-byte value against a {TRANSFER_LIMIT_BYTES}-byte budget"
        );
    }
}
