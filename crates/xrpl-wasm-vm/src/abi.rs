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
///
/// The budget counts bytes that **cross the boundary as copies**: host→guest
/// writes (C++'s `setData`, here [`write_into`], this function's one call site)
/// and typed reads that materialize a host object out of guest bytes (C++ charged
/// uint256, AccountID, Currency, Asset). Plain borrowed reads are not charged,
/// because nothing is copied — the host is handed a slice aliasing guest memory.
/// This ABI has no typed reads yet; the rule is here for the ones that arrive,
/// which will charge the object they materialize.
///
/// What bounds how many reads a run can make is gas, charged per host call before
/// its body runs ([`charged`]) — the same property C++ relied on.
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
/// Checks params validity then the [`MAX_FIELD_BYTES`] cap
/// (`DataFieldTooLarge`), in that order, before the slice is formed. The transfer
/// budget is not among them: there are no copied bytes to charge, which is why
/// C++ left plain slice/string reads (`trace`'s msg/data, `sha512_half`'s input)
/// free of it — see [`charge_transfer`].
pub(crate) fn read_borrowed<'a>(
    caller: &'a Caller<'_, VmState<'_>>,
    ptr: i32,
    len: i32,
) -> HostResult<&'a [u8]> {
    // A guest's pointer and length are `i32` on the wire and indices here, so the
    // conversion is the validity check: it fails on exactly the negative values.
    let (Ok(ptr), Ok(len)) = (usize::try_from(ptr), usize::try_from(len)) else {
        return Err(HostError::InvalidParams);
    };
    if len > MAX_FIELD_BYTES {
        return Err(HostError::DataFieldTooLarge);
    }
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
/// **`fill`'s `usize` is the value's true length, not the number of bytes it
/// wrote.** A host holding a 64-byte value, handed a 4-byte region, writes nothing
/// and answers `64` — which is how the guest learns the size to ask for next time.
/// The count is therefore bounded by neither the region nor the cap, and that is
/// what makes both checks below reachable.
///
/// The engine owns the policy the guest observes: the [`MAX_FIELD_BYTES`] cap
/// (`DataFieldTooLarge`), the buffer fit (`BufferTooSmall`), then the transfer
/// budget — the order the C++ `setData` path uses. Those checks follow `fill`,
/// since the length is unknown before it runs, which is why the region `fill`
/// receives is clamped to the cap: the checks decide the *status*, and the clamp
/// is what keeps an over-cap value's bytes out of guest memory regardless.
///
/// A refusal says nothing about what is in the guest's buffer, and the guest must
/// not read it on a negative status. Over the cap, the clamp does bound what could
/// have landed. Under it the clamp is a no-op — `fill` holds exactly the region the
/// guest asked for — so whether a host that cannot fit a value leaves a prefix
/// behind is that host's choice, not something the engine can enforce.
///
/// The bounds check covers the guest's whole declared `cap`, not the clamped
/// length, so a buffer running past memory is `PointerOutOfBounds` even when its
/// first [`MAX_FIELD_BYTES`] bytes would have been in bounds — the guest is told
/// its pointer is wrong rather than being served a truncated prefix of it.
pub(crate) fn write_into(
    caller: &mut Caller<'_, VmState<'_>>,
    dst: i32,
    cap: i32,
    fill: impl FnOnce(&dyn HostFunctions, &mut [u8]) -> HostResult<usize>,
) -> HostResult<i32> {
    // As in `read_borrowed`: the conversion to an index is the validity check.
    let (Ok(dst), Ok(cap)) = (usize::try_from(dst), usize::try_from(cap)) else {
        return Err(HostError::InvalidParams);
    };
    let mem = memory(caller)?;
    // Copy the shared `&dyn HostFunctions` out of the store data (references are
    // Copy) so the data borrow ends before we borrow guest memory mutably.
    let host: &dyn HostFunctions = caller.data().host;
    let end = dst.checked_add(cap).ok_or(HostError::PointerOutOfBounds)?;
    // The guest's whole declared region, so the bounds rule is about what it asked
    // for…
    let out = mem
        .data_mut(&mut *caller)
        .get_mut(dst..end)
        .ok_or(HostError::PointerOutOfBounds)?;
    // …of which at most the field cap is writable. Narrowed here rather than at the
    // call below, so no call can put more than MAX_FIELD_BYTES into guest memory
    // whatever the guest declared, and the wider slice cannot be reached again.
    let out = &mut out[..cap.min(MAX_FIELD_BYTES)];

    let n = fill(host, out)?;

    // Not subsumed by the clamp: `fill` reports the value's *true* length, which
    // can exceed the region it was offered, and this is how the guest learns the
    // value was too large rather than merely unwritten. The clamp bounds the
    // bytes; this bounds the status.
    if n > MAX_FIELD_BYTES {
        return Err(HostError::DataFieldTooLarge);
    }
    if n > cap {
        return Err(HostError::BufferTooSmall);
    }
    charge_transfer(caller.data(), n)?;
    // The cap check above bounds `n`, so the count reaches the wire whole: an
    // `i32` the guest reads as a byte count, never a truncation of a larger one.
    #[expect(
        clippy::cast_possible_truncation,
        clippy::cast_possible_wrap,
        reason = "`n > MAX_FIELD_BYTES` returned above, and the cap is far inside i32"
    )]
    let n = n as i32;
    Ok(n)
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
/// copy is host-private scratch rather than a value crossing the boundary, so it
/// costs no transfer budget, as C++'s `sha512_half` input did not
/// ([`charge_transfer`]). The output half is [`write_into`], so it obeys the same
/// policy as a plain write — including the charge.
pub(crate) fn read_write(
    caller: &mut Caller<'_, VmState<'_>>,
    src: i32,
    src_len: i32,
    dst: i32,
    cap: i32,
    call: impl FnOnce(&dyn HostFunctions, &[u8], &mut [u8]) -> HostResult<usize>,
) -> HostResult<i32> {
    // As in `read_borrowed`: the conversion to an index is the validity check.
    let (Ok(src), Ok(len)) = (usize::try_from(src), usize::try_from(src_len)) else {
        return Err(HostError::InvalidParams);
    };
    if len > MAX_FIELD_BYTES {
        return Err(HostError::DataFieldTooLarge);
    }

    // Copy the input out before `write_into` borrows guest memory mutably.
    let mut buf = [0u8; MAX_FIELD_BYTES];
    memory(caller)?
        .read(&*caller, src, &mut buf[..len])
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

    /// The three conditions the host cannot serve a call under, as the tests
    /// *expect* them rather than as [`is_fatal`] reports them — deriving this from
    /// `is_fatal` would make both tests below vacuous, since a condition wrongly
    /// classified as soft would simply be skipped. Named once, so the two are one
    /// statement about the same set.
    const MUST_TRAP: [HostError; 3] = [
        HostError::OutOfGas,
        HostError::Internal,
        HostError::NoMemExported,
    ];

    /// The fatal channel: a trap, carrying the condition so `run` can name the
    /// outcome without parsing a message.
    #[test]
    fn a_host_fatal_error_becomes_a_trap_carrying_it() {
        for error in MUST_TRAP {
            let trap =
                to_wire(Err(error)).expect_err("a fatal error must not reach the guest as a code");
            let payload = trap.downcast_ref::<FatalHostError>().unwrap_or_else(|| {
                panic!("{error:?}: expected a FatalHostError payload, got: {trap}")
            });
            assert_eq!(*payload, FatalHostError(error));
        }
    }

    /// Which errors take which channel, as a deliberate change-detector: the
    /// three in [`MUST_TRAP`] trap, and everything else is a code the guest acts on.
    ///
    /// Over `HostError::ALL`, so it is the whole ABI and not a sample: a code
    /// added to the ABI arrives here already asserted to be guest-visible, and
    /// making it fatal is then a change someone has to come and make.
    ///
    /// `OutOfTransferLimit` is the row worth reading twice. It is the one budget
    /// a contract can be expected to handle — C++ made it the single soft failure
    /// among these, and this fork keeps that — so a guest asking for more than
    /// the run's remaining 1 MiB is told no, not killed.
    #[test]
    fn only_the_host_fatal_errors_trap() {
        for &error in HostError::ALL {
            if MUST_TRAP.contains(&error) {
                assert!(is_fatal(error), "{error:?} must stop the run");
            } else {
                assert!(!is_fatal(error), "{error:?} must reach the guest as a code");
                assert_eq!(wire(Err(error)), error.code());
            }
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
