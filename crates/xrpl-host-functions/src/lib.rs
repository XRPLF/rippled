//! The wasm host ABI: the one place it is declared.
//!
//! `host_functions!` turns the declaration block at the bottom of this file into the
//! [`HostFunctions`] trait a host implements and the [`HostFunctionSpec`] table a
//! wasm engine registers from. Everything the expansion refers to — [`HostFnSpec`],
//! [`HostError`] — is written by hand here, and referred to by absolute path, so the
//! generated code never depends on what a caller happens to have imported.

#![no_std]
extern crate alloc;

use alloc::vec::Vec;

// Not re-exported: the ABI is declared once, here, and this is the only call site.
use xrpl_host_functions_macros::host_functions;

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

/// The wasm import name and base gas cost of one host function.
///
/// The same for every host function, so it is declared here rather than generated;
/// [`HostFunctionSpec::spec`] returns one of these per declaration.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub struct HostFnSpec {
    /// The name a guest imports the function under.
    pub name: &'static str,
    /// Gas charged before the call runs, independent of its arguments.
    pub gas: u64,
}

// Lets the generated code name this crate (`::xrpl_host_functions::HostFnSpec`)
// even though it is expanded here, inside the crate itself.
extern crate self as xrpl_host_functions;

host_functions! {
    #[gas = 60]
    #[wasm_name = "ldgr_index"]
    fn get_ledger_sqn(&self) -> [u8; 4];

    #[gas = 70]
    #[wasm_name = "home_le_field"]
    fn get_current_ledger_obj_field(&self, field: i32) -> Vec<u8>;

    #[gas = 2000]
    #[wasm_name = "sha512_half"]
    fn sha512_half(&self, data: &[u8]) -> [u8; 32];

    #[gas = 500]
    #[wasm_name = "trace"]
    fn trace(&self, msg: &str, data: &[u8], as_hex: bool);

    #[gas = 500]
    #[wasm_name = "trace_num"]
    fn trace_num(&self, msg: &str, number: i64);
}
