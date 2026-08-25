use crate::region::Region;
use crate::vm::{MAX_FIELD_BYTES, VmState};
use core::ops::Range;
use wasmi::{Caller, Memory};
use xrpl_host_functions::{HostError, HostFunctionSpec, HostFunctions, HostResult};

/// A condition that stops the run. It is a property of the run rather than an answer
/// to a call, so it reaches no guest and carries no wire code — which is why it is
/// not a [`HostError`]: no host can report one and no contract can read one.
///
/// The three are the outcomes a host call can end a run with, and
/// `From<Fault> for RunError` in `vm.rs` is where each gets its name.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum Fault {
    /// This call's charge would take the meter below zero. The guest exhausting the
    /// meter with its own instructions reaches [`crate::vm::RunError::OutOfGas`] by
    /// wasmi's `OutOfFuel` trap instead, never through here.
    OutOfGas,
    /// The call could not be served: either the host said so, or this engine's own
    /// fuel meter did not answer.
    Internal,
    /// There is no linear memory to work in — the module exports none, or the call
    /// came from a start section, which runs before there is an instance.
    NoMemory,
}

/// How a host call fails: with a code the guest reads off the return value, or with a
/// [`Fault`] that stops the run.
///
/// **The variant picks the channel.** [`to_wire`] reads it rather than asking a
/// predicate, so the two cannot disagree, and a [`FatalHostError`] cannot be built
/// around something a guest was supposed to see.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) enum CallError {
    Code(HostError),
    Fatal(Fault),
}

/// A host call's result inside the engine: [`HostResult`] plus the faults only the
/// engine can raise.
pub(crate) type CallResult<T> = Result<T, CallError>;

/// Which channel a host's answer takes, decided once, here.
///
/// Three codes stop the run instead of reaching the contract that asked. Each says the
/// call was not served at all — the host could not do it, it has not been wired, or
/// there is nowhere to put the answer — and a contract has no business interpreting
/// any of them, so it is told nothing and the run ends. Every other code is the
/// contract's to read.
impl From<HostError> for CallError {
    fn from(error: HostError) -> CallError {
        match error {
            HostError::InternalFatal => CallError::Fatal(Fault::Internal),
            HostError::Unimplemented => CallError::Fatal(Fault::Internal),
            HostError::NoMemExported => CallError::Fatal(Fault::NoMemory),
            code => CallError::Code(code),
        }
    }
}

/// The payload a trap carries so [`crate::vm::run`] can name the outcome without
/// parsing a message. Holds a [`Fault`], so by construction no guest-visible code can
/// leave through this channel.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub(crate) struct FatalHostError(pub(crate) Fault);

impl wasmi::errors::HostError for FatalHostError {}

impl core::fmt::Display for FatalHostError {
    fn fmt(&self, f: &mut core::fmt::Formatter<'_>) -> core::fmt::Result {
        write!(f, "host call refused: {:?}", self.0)
    }
}

/// Charge the call's gas, run its body, put the result on the wire. The one path
/// every registered closure takes, so gas cannot be forgotten.
pub(crate) fn charged(
    caller: &mut Caller<'_, VmState<'_>>,
    op: HostFunctionSpec,
    body: impl FnOnce(&mut Caller<'_, VmState<'_>>) -> CallResult<i32>,
) -> Result<i32, wasmi::Error> {
    to_wire(charge(caller, op.gas()).and_then(|()| body(caller)))
}

/// [`charged`] for a call the guest gets no answer from: its wasm function has no
/// result, so a soft error has nowhere to go and is dropped. The gas is charged first
/// and charged whatever happens after, so the cost is all such a call leaves behind.
///
/// Only `trace` takes this path.
pub(crate) fn charged_unreported(
    caller: &mut Caller<'_, VmState<'_>>,
    op: HostFunctionSpec,
    body: impl FnOnce(&mut Caller<'_, VmState<'_>>) -> CallResult<()>,
) -> Result<(), wasmi::Error> {
    dropped(charge(caller, op.gas()).and_then(|()| body(caller)))
}

/// [`to_wire`] for a call with no result: there is no return value to encode a code
/// in, so it is dropped. A [`Fault`] still stops the run — that is a property of the
/// run, not an answer to the call.
fn dropped(result: CallResult<()>) -> Result<(), wasmi::Error> {
    match result {
        Err(CallError::Fatal(fault)) => Err(wasmi::Error::host(FatalHostError(fault))),
        _ => Ok(()),
    }
}

fn to_wire(result: CallResult<i32>) -> Result<i32, wasmi::Error> {
    match result {
        Ok(value) => Ok(value),
        Err(CallError::Code(error)) => Ok(error.code()),
        Err(CallError::Fatal(fault)) => Err(wasmi::Error::host(FatalHostError(fault))),
    }
}

/// Deduct `cost` fuel; [`Fault::OutOfGas`] if it would go negative.
///
/// A meter that will not answer is this crate's own defect, not the contract's, so it
/// is [`Fault::Internal`] rather than a number a guest could act on.
fn charge<T>(caller: &mut Caller<'_, T>, cost: u64) -> CallResult<()> {
    let remaining = caller
        .get_fuel()
        .map_err(|_| CallError::Fatal(Fault::Internal))?;
    match remaining.checked_sub(cost) {
        Some(left) => caller
            .set_fuel(left)
            .map_err(|_| CallError::Fatal(Fault::Internal)),
        None => {
            let _ = caller.set_fuel(0);
            Err(CallError::Fatal(Fault::OutOfGas))
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

fn memory(caller: &Caller<'_, VmState<'_>>) -> CallResult<Memory> {
    caller
        .data()
        .memory
        .ok_or(CallError::Fatal(Fault::NoMemory))
}

/// [`Region::read`] of the guest's memory, for a call that reads and writes nothing
/// back (`trace`).
pub(crate) fn read_borrowed<'a>(
    caller: &'a Caller<'_, VmState<'_>>,
    input: Region,
) -> CallResult<&'a [u8]> {
    let mem = memory(caller)?;
    Ok(input.read(mem.data(caller))?)
}

/// Decode a guest `u32` argument — a keylet's sequence number or document id — from
/// its four little-endian bytes, carried on to the host as its `i32` bit pattern.
///
/// The ABI transports these as a 4-byte region rather than a wasm scalar (the guest
/// SDK passes `seq.to_le_bytes()`), so the region must be exactly four bytes;
/// `InvalidParams` otherwise.
pub(crate) fn read_u32_arg(bytes: &[u8]) -> HostResult<i32> {
    let arr: [u8; 4] = bytes.try_into().map_err(|_| HostError::InvalidParams)?;
    Ok(i32::from_le_bytes(arr))
}

/// Service a call whose answer is bytes, written straight into the guest's output
/// region.
///
/// **`fill` returns the value's true length, not what it wrote**: a host holding 64
/// bytes and offered room for 4 writes nothing and answers `64`, which is how the
/// guest learns the size to ask for. So `n` is bounded by neither the region, the
/// cap, nor the budget, and all three checks below are reachable.
pub(crate) fn write_into(
    caller: &mut Caller<'_, VmState<'_>>,
    out: Region,
    fill: impl FnOnce(&dyn HostFunctions, &mut [u8]) -> HostResult<usize>,
) -> CallResult<i32> {
    let range = out.range()?;
    let cap = range.len();
    let mem = memory(caller)?;
    let host: &dyn HostFunctions = caller.data().host;
    let budget = usize::try_from(caller.data().transfer_budget.get()).unwrap_or(usize::MAX);
    let buf = mem
        .data_mut(&mut *caller)
        .get_mut(range)
        .ok_or(HostError::PointerOutOfBounds)?;
    let buf = &mut buf[..cap.min(MAX_FIELD_BYTES).min(budget)];

    let n = fill(host, buf)?;

    if n > MAX_FIELD_BYTES {
        return Err(HostError::DataFieldTooLarge.into());
    }
    if n > cap {
        return Err(HostError::BufferTooSmall.into());
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
) -> CallResult<i32> {
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
        return Err(HostError::DataFieldTooLarge.into());
    }
    let buf = data.get_mut(range).ok_or(HostError::PointerOutOfBounds)?;
    if n > cap {
        return Err(HostError::BufferTooSmall.into());
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

/// The mantissa and exponent widths `float_to_mant_exp` writes: an `i64` and an `i32`.
/// Fixed by the ABI, not the guest, so the split is a constant rather than a reported
/// length.
const MANTISSA_BYTES: usize = 8;
const EXPONENT_BYTES: usize = 4;

fn check_fits(data: &[u8], range: &Range<usize>, width: usize) -> HostResult<()> {
    let region = data
        .get(range.clone())
        .ok_or(HostError::PointerOutOfBounds)?;
    if region.len() < width {
        return Err(HostError::BufferTooSmall);
    }
    Ok(())
}

/// Service `float_to_mant_exp`, the one call that writes two output regions: the host
/// fills the run's output buffer with the mantissa followed by the exponent, and each
/// is copied to its own guest region once every rule has passed.
///
/// Like [`write_buffered`], the host reads its input from the guest's memory and writes
/// to a scratch buffer, so the input stays borrowed rather than copied. The two output
/// regions are judged after the input, and the mantissa's region before the exponent's,
/// so the first fault reported is the leftmost.
///
/// The two widths are the ABI's rather than the guest's, so the length the host reports
/// is checked against their sum for equality rather than as a bound, and ahead of the
/// output regions: a wrong total means there is no answer to place, whatever the guest
/// declared. That is a fatal error and not a status, since the guest asked for nothing
/// wrong.
pub(crate) fn write_mant_exp(
    caller: &mut Caller<'_, VmState<'_>>,
    mantissa_out: Region,
    exponent_out: Region,
    call: impl FnOnce(&dyn HostFunctions, &[u8], &mut [u8], &mut [u8]) -> HostResult<usize>,
) -> CallResult<i32> {
    let mem = memory(caller)?;
    let (data, state) = mem.data_and_store_mut(&mut *caller);
    let host: &dyn HostFunctions = state.host;

    // The scratch buffer is split at the fixed mantissa width: the host fills the first
    // eight bytes with the mantissa and the next four with the exponent.
    let (mant_buf, exp_buf) = state.out_buffer.split_at_mut(MANTISSA_BYTES);
    let mant_buf = &mut mant_buf[..MANTISSA_BYTES];
    let exp_buf = &mut exp_buf[..EXPONENT_BYTES];

    let total = call(host, data, mant_buf, exp_buf)?;

    // Both buffers are fixed-width and were offered whole, so the only length the host
    // can correctly report is their sum. Anything else is the host contradicting the
    // ABI: with the widths in doubt, part of what would be copied out is whatever the
    // previous call left in the buffer, so none of it is copied.
    if total != MANTISSA_BYTES + EXPONENT_BYTES {
        return Err(HostError::InternalFatal.into());
    }

    let mant_range = mantissa_out.range()?;
    check_fits(data, &mant_range, MANTISSA_BYTES)?;
    let exp_range = exponent_out.range()?;
    check_fits(data, &exp_range, EXPONENT_BYTES)?;

    charge_transfer(state, MANTISSA_BYTES + EXPONENT_BYTES)?;

    let mant_dst = data
        .get_mut(mant_range)
        .ok_or(HostError::PointerOutOfBounds)?;
    mant_dst[..MANTISSA_BYTES].copy_from_slice(&state.out_buffer[..MANTISSA_BYTES]);
    let exp_dst = data
        .get_mut(exp_range)
        .ok_or(HostError::PointerOutOfBounds)?;
    exp_dst[..EXPONENT_BYTES]
        .copy_from_slice(&state.out_buffer[MANTISSA_BYTES..MANTISSA_BYTES + EXPONENT_BYTES]);

    #[expect(
        clippy::cast_possible_truncation,
        clippy::cast_possible_wrap,
        reason = "a total other than 12 returned above, and 12 is far inside i32"
    )]
    let total = total as i32;
    Ok(total)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::vm::TRANSFER_LIMIT_BYTES;
    use std::cell::Cell;
    use wasmi::StoreLimitsBuilder;
    use xrpl_host_functions::TraceDataType;

    /// `charge_transfer` takes the store data, which has to hold a host.
    struct UncalledHost;

    impl HostFunctions for UncalledHost {
        fn get_ledger_sqn(&self, _out: &mut [u8]) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn get_parent_ledger_time(&self, _out: &mut [u8]) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn get_parent_ledger_hash(&self, _out: &mut [u8]) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn get_base_fee(&self, _out: &mut [u8]) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn is_amendment_enabled(&self, _amendment: &[u8]) -> HostResult<i32> {
            unreachable!("no unit test in this module calls the host")
        }
        fn cache_ledger_obj(&self, _obj_id: &[u8], _cache_idx: i32) -> HostResult<i32> {
            unreachable!("no unit test in this module calls the host")
        }
        fn get_tx_field(&self, _field: i32, _out: &mut [u8]) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn get_current_ledger_obj_field(&self, _field: i32, _out: &mut [u8]) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn get_ledger_obj_field(
            &self,
            _cache_idx: i32,
            _field: i32,
            _out: &mut [u8],
        ) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn get_tx_nested_field(&self, _locator: &[u8], _out: &mut [u8]) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn get_current_ledger_obj_nested_field(
            &self,
            _locator: &[u8],
            _out: &mut [u8],
        ) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn get_ledger_obj_nested_field(
            &self,
            _cache_idx: i32,
            _locator: &[u8],
            _out: &mut [u8],
        ) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn get_tx_array_len(&self, _field: i32) -> HostResult<i32> {
            unreachable!("no unit test in this module calls the host")
        }
        fn get_current_ledger_obj_array_len(&self, _field: i32) -> HostResult<i32> {
            unreachable!("no unit test in this module calls the host")
        }
        fn get_ledger_obj_array_len(&self, _cache_idx: i32, _field: i32) -> HostResult<i32> {
            unreachable!("no unit test in this module calls the host")
        }
        fn get_tx_nested_array_len(&self, _locator: &[u8]) -> HostResult<i32> {
            unreachable!("no unit test in this module calls the host")
        }
        fn get_current_ledger_obj_nested_array_len(&self, _locator: &[u8]) -> HostResult<i32> {
            unreachable!("no unit test in this module calls the host")
        }
        fn get_ledger_obj_nested_array_len(
            &self,
            _cache_idx: i32,
            _locator: &[u8],
        ) -> HostResult<i32> {
            unreachable!("no unit test in this module calls the host")
        }
        fn check_signature(
            &self,
            _message: &[u8],
            _signature: &[u8],
            _pubkey: &[u8],
        ) -> HostResult<i32> {
            unreachable!("no unit test in this module calls the host")
        }
        fn account_keylet(&self, _account: &[u8], _out: &mut [u8]) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn amm_keylet(&self, _asset1: &[u8], _asset2: &[u8], _out: &mut [u8]) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn check_keylet(&self, _account: &[u8], _seq: i32, _out: &mut [u8]) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn credential_keylet(
            &self,
            _subject: &[u8],
            _issuer: &[u8],
            _credential_type: &[u8],
            _out: &mut [u8],
        ) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn delegate_keylet(
            &self,
            _account: &[u8],
            _authorize: &[u8],
            _out: &mut [u8],
        ) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn deposit_preauth_keylet(
            &self,
            _account: &[u8],
            _authorize: &[u8],
            _out: &mut [u8],
        ) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn did_keylet(&self, _account: &[u8], _out: &mut [u8]) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn escrow_keylet(&self, _account: &[u8], _seq: i32, _out: &mut [u8]) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn trust_line_keylet(
            &self,
            _account1: &[u8],
            _account2: &[u8],
            _currency: &[u8],
            _out: &mut [u8],
        ) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn mptoken_issuance_keylet(
            &self,
            _issuer: &[u8],
            _seq: i32,
            _out: &mut [u8],
        ) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn mptoken_keylet(
            &self,
            _mptid: &[u8],
            _holder: &[u8],
            _out: &mut [u8],
        ) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn nftoken_offer_keylet(
            &self,
            _account: &[u8],
            _seq: i32,
            _out: &mut [u8],
        ) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn offer_keylet(&self, _account: &[u8], _seq: i32, _out: &mut [u8]) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn oracle_keylet(
            &self,
            _account: &[u8],
            _doc_id: i32,
            _out: &mut [u8],
        ) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn paychannel_keylet(
            &self,
            _account: &[u8],
            _destination: &[u8],
            _seq: i32,
            _out: &mut [u8],
        ) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn permissioned_domain_keylet(
            &self,
            _account: &[u8],
            _seq: i32,
            _out: &mut [u8],
        ) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn signer_list_keylet(&self, _account: &[u8], _out: &mut [u8]) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn ticket_keylet(&self, _account: &[u8], _seq: i32, _out: &mut [u8]) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn vault_keylet(&self, _account: &[u8], _seq: i32, _out: &mut [u8]) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn sha512_half(&self, _data: &[u8], _out: &mut [u8]) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn trace(&self, _msg: &str, _data: &[u8], _data_type: TraceDataType) -> HostResult<()> {
            unreachable!("no unit test in this module calls the host")
        }
        fn update_data(&self, _data: &[u8]) -> HostResult<i32> {
            unreachable!("no unit test in this module calls the host")
        }
        fn get_nft(&self, _account: &[u8], _nft_id: &[u8], _out: &mut [u8]) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn get_nft_issuer(&self, _nft_id: &[u8], _out: &mut [u8]) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn get_nft_taxon(&self, _nft_id: &[u8], _out: &mut [u8]) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn get_nft_flags(&self, _nft_id: &[u8]) -> HostResult<i32> {
            unreachable!("no unit test in this module calls the host")
        }
        fn get_nft_transfer_fee(&self, _nft_id: &[u8]) -> HostResult<i32> {
            unreachable!("no unit test in this module calls the host")
        }
        fn get_nft_sequence(&self, _nft_id: &[u8], _out: &mut [u8]) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn float_from_int(&self, _x: i64, _mode: i32, _out: &mut [u8]) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn float_from_uint(&self, _x: &[u8], _mode: i32, _out: &mut [u8]) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn float_from_stamount(
            &self,
            _amount: &[u8],
            _mode: i32,
            _out: &mut [u8],
        ) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn float_from_stnumber(
            &self,
            _number: &[u8],
            _mode: i32,
            _out: &mut [u8],
        ) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn float_to_int(&self, _x: &[u8], _mode: i32, _out: &mut [u8]) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn float_to_mant_exp(
            &self,
            _x: &[u8],
            _mantissa_out: &mut [u8],
            _exponent_out: &mut [u8],
        ) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn float_from_mant_exp(
            &self,
            _mantissa: i64,
            _exponent: i32,
            _mode: i32,
            _out: &mut [u8],
        ) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn float_compare(&self, _x: &[u8], _y: &[u8]) -> HostResult<i32> {
            unreachable!("no unit test in this module calls the host")
        }
        fn float_add(
            &self,
            _x: &[u8],
            _y: &[u8],
            _mode: i32,
            _out: &mut [u8],
        ) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn float_subtract(
            &self,
            _x: &[u8],
            _y: &[u8],
            _mode: i32,
            _out: &mut [u8],
        ) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn float_multiply(
            &self,
            _x: &[u8],
            _y: &[u8],
            _mode: i32,
            _out: &mut [u8],
        ) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn float_divide(
            &self,
            _x: &[u8],
            _y: &[u8],
            _mode: i32,
            _out: &mut [u8],
        ) -> HostResult<usize> {
            unreachable!("no unit test in this module calls the host")
        }
        fn float_power(
            &self,
            _x: &[u8],
            _n: i32,
            _mode: i32,
            _out: &mut [u8],
        ) -> HostResult<usize> {
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
    fn wire(result: CallResult<i32>) -> i32 {
        to_wire(result)
            .unwrap_or_else(|trap| panic!("expected a guest-visible status, got a trap: {trap}"))
    }

    #[test]
    fn a_success_becomes_the_value_and_an_error_becomes_its_code() {
        assert_eq!(wire(Ok(0)), 0);
        assert_eq!(wire(Ok(32)), 32);
        assert_eq!(wire(Err(HostError::BufferTooSmall.into())), -3);
    }

    /// The codes a host may answer that a contract must not see, and the fault each
    /// becomes. Written out rather than derived from `From<HostError>`, which is what
    /// they are asserting.
    const STOPS_THE_RUN: [(HostError, Fault); 3] = [
        (HostError::InternalFatal, Fault::Internal),
        (HostError::Unimplemented, Fault::Internal),
        (HostError::NoMemExported, Fault::NoMemory),
    ];

    /// Every fault, so the two tests below are the whole set and not a sample.
    /// `From<Fault> for RunError` is what forces a fault added later to be
    /// considered; this is what forces it to be tested.
    const ALL_FAULTS: [Fault; 3] = [Fault::OutOfGas, Fault::Internal, Fault::NoMemory];

    #[test]
    fn a_code_that_stops_the_run_converts_to_its_fault() {
        for (error, fault) in STOPS_THE_RUN {
            assert_eq!(CallError::from(error), CallError::Fatal(fault), "{error:?}");
        }
    }

    /// Over `HostError::ALL`, so it is the whole ABI and not a sample: a code added
    /// to the ABI arrives already asserted to reach the guest as itself, and stopping
    /// the run on it is then a change someone has to come and make.
    ///
    /// `OutOfTransferLimit` is the row worth reading twice: the one budget a
    /// contract can be expected to handle, so it is told no rather than killed.
    #[test]
    fn every_other_code_reaches_the_guest_as_itself() {
        for &error in HostError::ALL {
            if STOPS_THE_RUN.iter().any(|&(stops, _)| stops == error) {
                continue;
            }
            assert_eq!(CallError::from(error), CallError::Code(error), "{error:?}");
            assert_eq!(wire(Err(error.into())), error.code(), "{error:?}");
        }
    }

    /// The trap carries the fault, so `run` can name the outcome without parsing a
    /// message.
    #[test]
    fn a_fault_becomes_a_trap_carrying_it() {
        for fault in ALL_FAULTS {
            let trap = to_wire(Err(CallError::Fatal(fault)))
                .expect_err("a fault must not reach the guest as a code");
            let payload = trap.downcast_ref::<FatalHostError>().unwrap_or_else(|| {
                panic!("{fault:?}: expected a FatalHostError payload, got: {trap}")
            });
            assert_eq!(*payload, FatalHostError(fault));
        }
    }

    /// The result-less path splits the same two channels differently: a fault still
    /// stops the run, and every code is dropped, since `trace` has no return value to
    /// carry it. Over `HostError::ALL` for the reason above — a code added to the ABI
    /// arrives asserted against both paths.
    #[test]
    fn a_call_with_no_result_drops_a_code_and_traps_on_a_fault() {
        assert!(dropped(Ok(())).is_ok());

        for &error in HostError::ALL {
            if let CallError::Code(code) = CallError::from(error) {
                assert!(
                    dropped(Err(CallError::Code(code))).is_ok(),
                    "{error:?} has no channel to the guest and must be dropped"
                );
            }
        }

        for fault in ALL_FAULTS {
            let trap =
                dropped(Err(CallError::Fatal(fault))).expect_err("a fault must stop the run");
            let payload = trap.downcast_ref::<FatalHostError>().unwrap_or_else(|| {
                panic!("{fault:?}: expected a FatalHostError payload, got: {trap}")
            });
            assert_eq!(*payload, FatalHostError(fault));
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
