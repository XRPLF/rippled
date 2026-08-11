//! The wasm host ABI: the one place it is declared.
//!
//! `host_functions!` turns the declaration block at the bottom of this file into the
//! [`HostFunctions`] trait a host implements and the [`HostFunctionSpec`] table a
//! wasm engine registers from.
//!
//! The split: hand-written here is the vocabulary the declarations are written in —
//! [`HostError`], [`TraceDataType`], [`HostResult`], [`HASH_LEN`] — and everything
//! derived from the declarations is generated. The expansion names nothing this file
//! does not, so the two sides meet only in the block below.
//!
//! So this file is lists — error codes, trace data types, functions. The `macro_rules!`
//! that expand the first two into enums live in `macros.rs`.

#![no_std]

#[macro_use]
mod macros;

// Not re-exported: the ABI is declared once, here, and this is the only call site.
use xrpl_host_functions_macros::host_functions;

host_errors! {
    Internal = -1,
    FieldNotFound = -2,
    BufferTooSmall = -3,
    NoArray = -4,
    NotLeafField = -5,
    LocatorMalformed = -6,
    SlotOutRange = -7,
    SlotsFull = -8,
    EmptySlot = -9,
    LedgerObjNotFound = -10,
    Decoding = -11,
    DataFieldTooLarge = -12,
    PointerOutOfBounds = -13,
    NoMemExported = -14,
    InvalidParams = -15,
    InvalidAccount = -16,
    InvalidField = -17,
    IndexOutOfBounds = -18,
    FloatInputMalformed = -19,
    FloatComputationError = -20,
    NoRuntime = -21,
    OutOfGas = -22,
    OutOfTransferLimit = -23,
}

/// Convenience alias for the trait's fallible returns.
pub type HostResult<T> = Result<T, HostError>;

/// A `sha512Half` digest: the first 32 bytes of a SHA-512, as XRPL uses it.
pub const HASH_LEN: usize = 32;

trace_data_types! {
    /// 8 little-endian bytes, rendered as a signed decimal.
    Int64 = 1,
    /// 8 little-endian bytes, rendered as an unsigned decimal.
    Uint64 = 2,
    /// A serialized XRPL float: 12 bytes, mantissa then exponent.
    Xfloat = 3,
    /// A 20-byte account ID, rendered as base58.
    Account = 4,
    /// A serialized `STAmount`.
    Amount = 5,
    /// Raw bytes, hex-encoded.
    AsHex = 6,
    /// Bytes rendered verbatim as text.
    AsText = 7,
}

host_functions! {
    /// The sequence number of the ledger being built, as 4 little-endian bytes.
    #[gas = 60]
    #[wasm_name = "ldgr_index"]
    fn get_ledger_sqn(&self, out: &mut [u8]) -> HostResult<usize>;

    /// The serialized bytes of one field of the current (escrow) ledger object.
    #[gas = 70]
    #[wasm_name = "home_le_field"]
    fn get_current_ledger_obj_field(&self, field: i32, out: &mut [u8]) -> HostResult<usize>;

    /// The XRPL `sha512Half` of `data`: the first [`HASH_LEN`] bytes of its SHA-512.
    #[gas = 2000]
    #[wasm_name = "sha512_half"]
    fn sha512_half(&self, data: &[u8], out: &mut [u8]) -> HostResult<usize>;

    /// Writes `msg` to the trace log, followed by `data` rendered as `data_type` says.
    ///
    /// The one declaration whose wasm function has **no result**: this node's own log
    /// is its only effect, so a guest is told nothing. An `Err` from a host therefore
    /// reaches it in no form, and only the host-fatal ones do anything at all.
    ///
    /// It is also the one declaration that is **not** the wasm parameter order.
    /// `data_type` is the third wasm parameter, between the two regions, because that
    /// is where xrpld's `trace_proto` and the guest stdlib put it; `register.rs` takes
    /// the arguments in wasm order and calls this in declaration order.
    #[gas = 30]
    #[wasm_name = "trace"]
    fn trace(&self, msg: &str, data: &[u8], data_type: TraceDataType) -> HostResult<()>;
}
