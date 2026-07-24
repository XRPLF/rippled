#![no_std]
use xrpl_host_functions_macros::host_abi;

/// Error codes a host function may return.
///
/// The discriminants mirror `HostFunctionError` in
/// `include/xrpl/tx/wasm/WasmCommon.h`, so a negative `i32` crossing the wasm
/// boundary means the same thing to the guest, the Rust host, and the existing
/// C++ code. The full set is kept (not just the ones the PoC uses today) to
/// preserve that shared meaning.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
#[repr(i32)]
pub enum HostError {
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

impl HostError {
    /// The negative wire value the guest sees as the function's return code.
    #[inline]
    pub const fn code(self) -> i32 {
        self as i32
    }

    /// Reconstruct a `HostError` from its wire code; unknown/positive values map to `Internal`.
    pub const fn from_code(code: i32) -> HostError {
        match code {
            -1 => HostError::Internal,
            -2 => HostError::FieldNotFound,
            -3 => HostError::BufferTooSmall,
            -4 => HostError::NoArray,
            -5 => HostError::NotLeafField,
            -6 => HostError::LocatorMalformed,
            -7 => HostError::SlotOutRange,
            -8 => HostError::SlotsFull,
            -9 => HostError::EmptySlot,
            -10 => HostError::LedgerObjNotFound,
            -11 => HostError::Decoding,
            -12 => HostError::DataFieldTooLarge,
            -13 => HostError::PointerOutOfBounds,
            -14 => HostError::NoMemExported,
            -15 => HostError::InvalidParams,
            -16 => HostError::InvalidAccount,
            -17 => HostError::InvalidField,
            -18 => HostError::IndexOutOfBounds,
            -19 => HostError::FloatInputMalformed,
            -20 => HostError::FloatComputationError,
            -21 => HostError::NoRuntime,
            -22 => HostError::OutOfGas,
            -23 => HostError::OutOfTransferLimit,
            _ => HostError::Internal,
        }
    }
}

/// Convenience alias for the trait's fallible returns.
pub type HostResult<T> = Result<T, HostError>;

/// A `sha512Half` digest: the first 32 bytes of a SHA-512, as XRPL uses it.
pub const HASH_LEN: usize = 32;

/// Per-function ABI metadata: the wasm import name and the consensus-fixed base gas cost.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct HostFnSpec {
    pub name: &'static str,
    pub base_gas: u64,
}

host_functions! {
    #[gas = 60]
    #[wasm_name = "ldgr_index"]
    fn get_ledger_sqn() -> [u8; 4];

    #[gas = 70]
    #[wasm_name = "home_le_field"]
    fn get_current_ledger_obj_field(field: i32) -> Vec<u8>;

    #[gas = 2000]
    #[wasm_name = "sha512_half"]
    fn sha512_half(data: &[u8]) -> [u8; 32];

    #[gas = 500]
    #[wasm_name = "trace"]
    fn trace(msg: &str, data: &[u8], as_hex: bool);

    #[gas = 500]
    #[wasm_name = "trace_num"]
    fn trace_num(msg: &str, number: i64);
}
