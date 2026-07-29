use crate::vm::VmState;
use wasmi::{Caller, Extern, Memory};
use xrpl_host_functions::{HostError, HostFunctionSpec, HostFunctions, HostResult};

// ---------------------------------------------------------------------------
// ABI marshaling traits: decode a host-function argument from wasm scalars +
// guest memory (`AbiArg`), encode a result back into guest memory and a wasm
// return status (`AbiRet`), and a single-point gas-charging wrapper
// (`charged`) so every registered closure pays for its call exactly once.
// ---------------------------------------------------------------------------

/// Encode a *scalar or unit* host-function result into the status the wasm fn
/// returns (>= 0 success — a value; < 0 a HostError code, via `to_wasm_*`).
/// `Out` is the extra wasm scalars for output — always `()` here, since these
/// returns need no guest buffer.
///
/// Value-producing returns (`Vec<u8>` / `[u8; N]`) do *not* go through this
/// trait: they are serviced by [`write_into`], where the host writes straight
/// into guest linear memory with no owned buffer to encode.
pub(crate) trait AbiRet {
    type Out;
    fn write(self, caller: &mut Caller<'_, VmState<'_>>, out: Self::Out) -> HostResult<i64>;
}

impl AbiRet for () {
    type Out = ();
    fn write(self, _c: &mut Caller<'_, VmState<'_>>, _o: ()) -> HostResult<i64> {
        Ok(0)
    }
}
impl AbiRet for u32 {
    type Out = ();
    fn write(self, _c: &mut Caller<'_, VmState<'_>>, _o: ()) -> HostResult<i64> {
        Ok(self as i64)
    }
}

/// Charge a host call's gas once (from the enum's spec) then run its body.
/// Because every registered closure goes through here, gas can't be forgotten.
pub(crate) fn charged(
    caller: &mut Caller<'_, VmState<'_>>,
    op: HostFunctionSpec,
    body: impl FnOnce(&mut Caller<'_, VmState<'_>>) -> HostResult<i64>,
) -> HostResult<i64> {
    charge(caller, op.gas())?;
    body(caller)
}

pub(crate) fn to_wasm_i32(r: HostResult<i64>) -> i32 {
    match r {
        Ok(v) => v as i32,
        Err(e) => e.code(),
    }
}
#[allow(dead_code)]
pub(crate) fn to_wasm_i64(r: HostResult<i64>) -> i64 {
    match r {
        Ok(v) => v,
        Err(e) => e.code() as i64,
    }
}

// ---------------------------------------------------------------------------
// Gas + bounds-checked memory helpers (the crate's only "unsafe surface",
// concentrated and safe: every access is a checked wasmi slice op)
// ---------------------------------------------------------------------------

/// Per-field size cap for any single value crossing the host/guest boundary.
///
/// Mirrors `kMaxWasmDataLength = 1 * 1024` in
/// `include/xrpl/protocol/Protocol.h:261`, enforced by `getDataSlice`/
/// `setData` (`src/libxrpl/tx/wasm/HostFuncWrapper.cpp`) returning
/// `DataFieldTooLarge`.
const MAX_WASM_DATA_LEN: usize = 1024;

/// Deduct `cost` fuel for a host call; `OutOfGas` if it would go negative.
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

/// Deduct `n` bytes from the per-run transfer-limit budget (see
/// [`crate::vm::TRANSFER_LIMIT_BYTES`]); `OutOfTransferLimit` if it would go
/// negative. A separate budget from gas — see `VmState::transfer_budget`.
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

/// Bounds-check `[ptr, ptr + len)` and return a `&[u8]` **aliasing guest linear
/// memory** — no allocation, no copy. The read analog of [`write_into`]: where
/// `write_into` hands the host a `&mut [u8]` into guest memory, this hands it a
/// `&[u8]`, so a *read-only* host call touches the guest's bytes in place.
///
/// The returned slice borrows `caller`, so it is valid only for the duration of
/// the host call it feeds — the same leaf-call invariant `write_into` relies on
/// (our host functions don't re-enter the guest and move its memory).
///
/// Checks, in order: params validity, the [`MAX_WASM_DATA_LEN`] size cap
/// (`DataFieldTooLarge`), then the transfer-limit budget — all before the
/// slice is formed.
pub(crate) fn read_borrowed<'a>(
    caller: &'a Caller<'_, VmState<'_>>,
    ptr: i32,
    len: i32,
) -> HostResult<&'a [u8]> {
    if ptr < 0 || len < 0 {
        return Err(HostError::InvalidParams);
    }
    let (ptr, len) = (ptr as usize, len as usize);
    if len > MAX_WASM_DATA_LEN {
        return Err(HostError::DataFieldTooLarge);
    }
    charge_transfer(caller.data(), len)?;
    let end = ptr.checked_add(len).ok_or(HostError::PointerOutOfBounds)?;
    memory(caller)?
        .data(caller)
        .get(ptr..end)
        .ok_or(HostError::PointerOutOfBounds)
}

/// Service a "fill-the-caller's-buffer" host call: bounds-check the guest
/// output region `[dst, dst + cap)`, hand the host a `&mut [u8]` aliasing it,
/// and let the host write **straight into guest linear memory** — the single
/// copy, with no owned buffer intermediate (this is what removes the extra copy
/// the value-producing host functions used to pay: a `Vec<u8>` / `[u8; N]`
/// materialized on the host side, then copied into guest memory. The `CxxHost`
/// path additionally used to marshal C++ `Bytes` through a `rust::Vec` /
/// `HashResult`; that too is gone).
///
/// `fill` returns the value's *true* length (it writes only when the value fits
/// in `dst`), so the engine keeps ownership of the policy the guest observes:
/// the [`MAX_WASM_DATA_LEN`] field-size cap (`DataFieldTooLarge`), the
/// buffer-fit check (`BufferTooSmall`), and the transfer-limit budget — checked
/// here, in the same order as the C++ `setData` path (size cap precedes the
/// transfer charge). On success returns the byte count.
///
/// Ordering note: because the byte count isn't known until `fill` runs, the
/// transfer budget is charged *after* the write rather than before it (the
/// pre-write gas charge in [`charged`] still bounds how often this runs). A
/// value rejected for being over-cap/over-budget may leave bytes in the guest
/// buffer, but they sit within the guest's own bounds and the guest must treat
/// a negative status as "don't read the buffer".
pub(crate) fn write_into(
    caller: &mut Caller<'_, VmState<'_>>,
    dst: i32,
    cap: i32,
    fill: impl FnOnce(&dyn HostFunctions, &mut [u8]) -> HostResult<usize>,
) -> HostResult<i64> {
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

    if n > MAX_WASM_DATA_LEN {
        return Err(HostError::DataFieldTooLarge);
    }
    if n > cap {
        return Err(HostError::BufferTooSmall);
    }
    charge_transfer(caller.data(), n)?;
    Ok(n as i64)
}

// The input buffer in `read_write` lives on the stack, sized to the field cap.
// Guard the assumption that the cap stays small enough for that to be fine.
const _: () = assert!(
    MAX_WASM_DATA_LEN <= 8 * 1024,
    "read_write's input buffer is a stack array; keep MAX_WASM_DATA_LEN small"
);

/// Service a host call that reads an input region *and* writes an output region
/// of guest memory (e.g. `sha512_half`).
///
/// The input is copied into a fixed **stack** buffer — no heap allocation. It's
/// bounded by [`MAX_WASM_DATA_LEN`] (the 1 KiB field cap, checked before the
/// copy), so a plain `[u8; MAX_WASM_DATA_LEN]` array always fits; `&buf[..len]`
/// carries the length, so no wrapper type is needed. Keeping the input in a
/// stack local — rather than a borrow of the wasmi store — is what lets it
/// coexist with the output `&mut [u8]`: [`write_into`] can borrow guest memory
/// mutably for the output while `input` (borrowing the local) stays valid, with
/// no aliasing/split reasoning. The output half reuses [`write_into`] verbatim,
/// so the field-cap / buffer-fit / transfer policy is unchanged.
///
/// (The stack buffer is zero-initialized each call — one `memset` of the cap
/// size. That's the deliberately-simple PoC trade: it drops the per-call heap
/// allocation the old `Vec<u8>` path paid, at the price of a small fixed
/// zero-fill; a `MaybeUninit`/arrayvec buffer could drop that too.)
pub(crate) fn read_write(
    caller: &mut Caller<'_, VmState<'_>>,
    src: i32,
    src_len: i32,
    dst: i32,
    cap: i32,
    call: impl FnOnce(&dyn HostFunctions, &[u8], &mut [u8]) -> HostResult<usize>,
) -> HostResult<i64> {
    if src < 0 || src_len < 0 {
        return Err(HostError::InvalidParams);
    }
    let len = src_len as usize;
    if len > MAX_WASM_DATA_LEN {
        return Err(HostError::DataFieldTooLarge);
    }
    charge_transfer(caller.data(), len)?;

    // Copy the input into a stack buffer, then release the (shared) store
    // borrow before `write_into` takes it mutably for the output.
    let mut buf = [0u8; MAX_WASM_DATA_LEN];
    memory(caller)?
        .read(&*caller, src as usize, &mut buf[..len])
        .map_err(|_| HostError::PointerOutOfBounds)?;
    let input = &buf[..len];

    write_into(caller, dst, cap, |host, out| call(host, input, out))
}
