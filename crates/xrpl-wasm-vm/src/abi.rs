use crate::region::Region;
use crate::vm::{MAX_FIELD_BYTES, VmState};
use wasmi::{Caller, Memory};
use xrpl_host_functions::{HostError, HostFunctionSpec, HostFunctions, HostResult};

#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct FatalHostError(pub(crate) HostError);

impl wasmi::errors::HostError for FatalHostError {}

impl core::fmt::Display for FatalHostError {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        write!(f, "host call refused: {:?}", self.0)
    }
}

/// Whether a [`HostError`] stops the run instead of reaching the guest as a code.
pub(crate) fn is_fatal(error: HostError) -> bool {
    matches!(
        error,
        HostError::OutOfGas | HostError::Internal | HostError::NoMemExported
    )
}

/// Charge the call's gas, run its body, put the result on the wire. The one path
/// every registered closure takes, so gas cannot be forgotten.
pub(crate) fn charged(
    caller: &mut Caller<'_, VmState<'_>>,
    op: HostFunctionSpec,
    body: impl FnOnce(&mut Caller<'_, VmState<'_>>) -> HostResult<i32>,
) -> Result<i32, wasmi::Error> {
    to_wire(charge(caller, op.gas()).and_then(|()| body(caller)))
}

fn to_wire(result: HostResult<i32>) -> Result<i32, wasmi::Error> {
    match result {
        Ok(value) => Ok(value),
        Err(error) if is_fatal(error) => Err(wasmi::Error::host(FatalHostError(error))),
        Err(error) => Ok(error.code()),
    }
}

/// Deduct `cost` fuel; `OutOfGas` if it would go negative.
fn charge<T>(caller: &mut Caller<'_, T>, cost: u64) -> Result<(), HostError> {
    let remaining = caller.get_fuel().map_err(|_| HostError::Internal)?;
    match remaining.checked_sub(cost) {
        Some(left) => caller.set_fuel(left).map_err(|_| HostError::Internal),
        None => {
            let _ = caller.set_fuel(0);
            Err(HostError::OutOfGas)
        }
    }
}

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

fn memory(caller: &Caller<'_, VmState<'_>>) -> Result<Memory, HostError> {
    caller.data().memory.ok_or(HostError::NoMemExported)
}

/// [`Region::read`] of the guest's memory, for a call that reads and writes nothing
/// back (`trace`, `trace_num`).
pub(crate) fn read_borrowed<'a>(
    caller: &'a Caller<'_, VmState<'_>>,
    input: Region,
) -> HostResult<&'a [u8]> {
    let mem = memory(caller)?;
    input.read(mem.data(caller))
}

/// Service a call whose answer is bytes, written straight into the guest's output
/// region.
///
/// **`fill` returns the value's true length, not what it wrote**: a host holding 64
/// bytes and offered room for 4 writes nothing and answers `64`, which is how the
/// guest learns the size to ask for. So `n` is bounded by neither the region nor the
/// cap, and both checks below are reachable.
pub(crate) fn write_into(
    caller: &mut Caller<'_, VmState<'_>>,
    out: Region,
    fill: impl FnOnce(&dyn HostFunctions, &mut [u8]) -> HostResult<usize>,
) -> HostResult<i32> {
    let range = out.range()?;
    let cap = range.len();
    let mem = memory(caller)?;
    let host: &dyn HostFunctions = caller.data().host;
    // Bounds-checked over the guest's whole declared region, so a buffer running
    // past memory is a wrong pointer rather than a truncated prefix being served…
    let buf = mem
        .data_mut(&mut *caller)
        .get_mut(range)
        .ok_or(HostError::PointerOutOfBounds)?;
    // …of which only the field cap is writable, so no call can exceed it whatever
    // the guest declared.
    let buf = &mut buf[..cap.min(MAX_FIELD_BYTES)];

    let n = fill(host, buf)?;

    if n > MAX_FIELD_BYTES {
        return Err(HostError::DataFieldTooLarge);
    }
    if n > cap {
        return Err(HostError::BufferTooSmall);
    }
    charge_transfer(caller.data(), n)?;
    #[expect(
        clippy::cast_possible_truncation,
        clippy::cast_possible_wrap,
        reason = "`n > MAX_FIELD_BYTES` returned above, and the cap is far inside i32"
    )]
    let n = n as i32;
    Ok(n)
}

/// Service a call that reads guest memory and writes bytes back to it: the host
/// fills the run's output buffer, which is copied to the guest once every rule has
/// passed.
///
/// `call` gets the guest's whole memory, so it can borrow any number of input
/// regions with [`Region::read`] — which a `&mut` view of that memory would forbid.
/// That is why the answer goes through a buffer instead of straight into the guest
/// as [`write_into`]'s does.
///
/// **The host is never told the guest's capacity**: it is offered the whole buffer
/// and reports the value's true length, so the fit is decided here, with nothing yet
/// in guest memory. A refused value therefore reaches it in no part.
///
/// The output is judged after the inputs, so a call with both bad reports the
/// input's verdict. `NoMemExported` precedes both: there is no memory to validate a
/// region against.
pub(crate) fn write_buffered(
    caller: &mut Caller<'_, VmState<'_>>,
    out: Region,
    call: impl FnOnce(&dyn HostFunctions, &[u8], &mut [u8]) -> HostResult<usize>,
) -> HostResult<i32> {
    let mem = memory(caller)?;
    // One borrow split in two: the guest's bytes for the inputs, the store data for
    // the output buffer. Taking them together is what keeps the inputs borrowed
    // rather than copied out.
    let (data, state) = mem.data_and_store_mut(&mut *caller);
    let host: &dyn HostFunctions = state.host;

    let n = call(host, data, &mut state.out_buffer[..])?;

    // `out` is checked here rather than before the call: the inputs are judged
    // first, so a call with both malformed reports the input's verdict.
    let range = out.range()?;
    let cap = range.len();
    if n > MAX_FIELD_BYTES {
        return Err(HostError::DataFieldTooLarge);
    }
    let buf = data.get_mut(range).ok_or(HostError::PointerOutOfBounds)?;
    if n > cap {
        return Err(HostError::BufferTooSmall);
    }
    charge_transfer(state, n)?;
    buf[..n].copy_from_slice(&state.out_buffer[..n]);
    #[expect(
        clippy::cast_possible_truncation,
        clippy::cast_possible_wrap,
        reason = "`n > MAX_FIELD_BYTES` returned above, and the cap is far inside i32"
    )]
    let n = n as i32;
    Ok(n)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::vm::TRANSFER_LIMIT_BYTES;
    use std::cell::Cell;
    use wasmi::StoreLimitsBuilder;

    /// `charge_transfer` takes the store data, which has to hold a host.
    struct UncalledHost;

    impl HostFunctions for UncalledHost {
        fn get_ledger_sqn(&self, _out: &mut [u8]) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn get_parent_ledger_time(&self, _out: &mut [u8]) -> HostResult<usize> {
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

    fn state(budget: u64) -> VmState<'static> {
        VmState {
            host: &UncalledHost,
            mem_limits: StoreLimitsBuilder::new().build(),
            transfer_budget: Cell::new(budget),
            memory: None,
            out_buffer: [0u8; MAX_FIELD_BYTES],
        }
    }

    /// `wasmi::Error` is not `PartialEq`, so a test expecting the guest-visible
    /// channel says so by going through here.
    fn wire(result: HostResult<i32>) -> i32 {
        to_wire(result)
            .unwrap_or_else(|trap| panic!("expected a guest-visible status, got a trap: {trap}"))
    }

    #[test]
    fn a_success_becomes_the_value_and_an_error_becomes_its_code() {
        assert_eq!(wire(Ok(0)), 0);
        assert_eq!(wire(Ok(32)), 32);
        assert_eq!(wire(Err(HostError::BufferTooSmall)), -3);
    }

    /// The fatal set as the tests *expect* it, not as [`is_fatal`] reports it:
    /// deriving it from `is_fatal` would make both tests below vacuous, since a
    /// condition wrongly classified as soft would simply be skipped.
    const MUST_TRAP: [HostError; 3] = [
        HostError::OutOfGas,
        HostError::Internal,
        HostError::NoMemExported,
    ];

    /// The trap carries the condition, so `run` can name the outcome without
    /// parsing a message.
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

    /// Over `HostError::ALL`, so it is the whole ABI and not a sample: a code added
    /// to the ABI arrives already asserted to be guest-visible, and making it fatal
    /// is then a change someone has to come and make.
    ///
    /// `OutOfTransferLimit` is the row worth reading twice: the one budget a
    /// contract can be expected to handle, so it is told no rather than killed.
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

    /// The budget bounds the total, so the transfer that would overrun it is
    /// refused whole rather than partially charged.
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

    /// The field cap holds one call to a small share of the run's budget, so the
    /// budget bounds a run rather than a call. An inequality, not the two values:
    /// those are pinned in `vm.rs`.
    #[test]
    fn no_single_value_can_exhaust_the_run_budget() {
        assert!(
            (MAX_FIELD_BYTES as u64) * 64 <= TRANSFER_LIMIT_BYTES,
            "one {MAX_FIELD_BYTES}-byte value against a {TRANSFER_LIMIT_BYTES}-byte budget"
        );
    }
}
