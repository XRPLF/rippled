//! The wasm host ABI: the one place it is declared.
//!
//! `host_functions!` turns the declaration block at the bottom of this file into the
//! [`HostFunctions`] trait a host implements and the [`HostFunctionSpec`] table a
//! wasm engine registers from.
//!
//! The split: hand-written here is the vocabulary the declarations are written in —
//! [`HostError`], [`HostResult`], [`HASH_LEN`] — and everything derived from the
//! declarations is generated. The expansion names nothing this file does not, so the
//! two sides meet only in the block below.

#![no_std]

// Not re-exported: the ABI is declared once, here, and this is the only call site.
use xrpl_host_functions_macros::host_functions;

/// Declares [`HostError`] from one list: the variants, [`HostError::ALL`] and
/// [`HostError::from_code`]'s table all expand from the codes below.
///
/// One list is what makes `ALL` complete. Rust cannot enumerate an enum's
/// variants — an exhaustive `match` forces an arm per variant but gives nothing to
/// iterate — so a hand-written `ALL` beside a hand-written enum could only be kept
/// in step by review, and `ALL`'s whole purpose is to be the set a test can trust.
/// A code added below gains its `ALL` entry and its `from_code` arm by
/// construction. `HostFunctionSpec::ALL` is complete the same way, from the
/// `host_functions!` block.
macro_rules! host_errors {
    ($($variant:ident = $code:literal,)+) => {
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
            $($variant = $code,)+
        }

        impl HostError {
            /// Every error a host function may return, in code order.
            ///
            /// The complete set, and complete by construction: a wasm engine's
            /// split between the codes it hands the guest and the conditions it
            /// traps on is a decision per variant, so the test that checks the
            /// split iterates this and a code added to the ABI cannot slip past it.
            pub const ALL: &'static [HostError] = &[$(HostError::$variant,)+];

            /// Reconstruct a `HostError` from its wire code; unknown/positive values
            /// map to `Internal`.
            pub const fn from_code(code: i32) -> HostError {
                match code {
                    $($code => HostError::$variant,)+
                    _ => HostError::Internal,
                }
            }
        }
    };
}

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

impl HostError {
    /// The negative wire value the guest sees as the function's return code.
    #[inline]
    pub const fn code(self) -> i32 {
        self as i32
    }
}

/// Convenience alias for the trait's fallible returns.
pub type HostResult<T> = Result<T, HostError>;

/// A `sha512Half` digest: the first 32 bytes of a SHA-512, as XRPL uses it.
pub const HASH_LEN: usize = 32;

host_functions! {
    /// The sequence number of the ledger being built, as 4 little-endian bytes.
    #[gas = 60]
    #[wasm_name = "ldgr_index"]
    fn get_ledger_sqn(&self, out: &mut [u8]) -> HostResult<usize>;

    /// The close time of the parent (last-closed) ledger, as 4 little-endian bytes.
    #[gas = 60]
    #[wasm_name = "parent_ldgr_time"]
    fn get_parent_ledger_time(&self, out: &mut [u8]) -> HostResult<usize>;

    /// The hash of the parent (last-closed) ledger, as 32 bytes.
    #[gas = 60]
    #[wasm_name = "parent_ldgr_hash"]
    fn get_parent_ledger_hash(&self, out: &mut [u8]) -> HostResult<usize>;

    /// The base fee of the ledger being built, in drops, as 4 little-endian bytes.
    #[gas = 60]
    #[wasm_name = "base_fee"]
    fn get_base_fee(&self, out: &mut [u8]) -> HostResult<usize>;

    /// Whether an amendment is enabled. The input is either its 32-byte id or its
    /// name; the answer is `1` if enabled and `0` if not. Unlike the getters, this
    /// reads an input region and returns the flag directly rather than writing bytes.
    #[gas = 100]
    #[wasm_name = "amendment_enabled"]
    fn is_amendment_enabled(&self, amendment: &[u8]) -> HostResult<i32>;

    /// Load the ledger object with the given 32-byte id into a cache slot, so later
    /// calls can read its fields. `cache_idx` selects the slot (1-based); `0` asks the
    /// host to assign a free one. Returns the slot used, or a negative error.
    #[gas = 5000]
    #[wasm_name = "cache_le"]
    fn cache_ledger_obj(&self, obj_id: &[u8], cache_idx: i32) -> HostResult<i32>;

    /// The serialized bytes of one field of the transaction being executed, selected
    /// by its `SField` code.
    #[gas = 70]
    #[wasm_name = "tx_field"]
    fn get_tx_field(&self, field: i32, out: &mut [u8]) -> HostResult<usize>;

    /// The serialized bytes of one field of the current (escrow) ledger object.
    #[gas = 70]
    #[wasm_name = "home_le_field"]
    fn get_current_ledger_obj_field(&self, field: i32, out: &mut [u8]) -> HostResult<usize>;

    /// The serialized bytes of one field of a previously cached ledger object,
    /// selected by its cache slot and the field's `SField` code.
    #[gas = 70]
    #[wasm_name = "le_field"]
    fn get_ledger_obj_field(&self, cache_idx: i32, field: i32, out: &mut [u8]) -> HostResult<usize>;

    /// The serialized bytes of a nested field of the transaction, reached by a
    /// `locator`: a path of little-endian `i32` steps (so its byte length is a
    /// non-zero multiple of 4). Reads the locator region and writes the field bytes.
    #[gas = 110]
    #[wasm_name = "tx_inner"]
    fn get_tx_nested_field(&self, locator: &[u8], out: &mut [u8]) -> HostResult<usize>;

    /// The serialized bytes of a nested field of the current (escrow) ledger object,
    /// reached by a `locator`, as with [`Self::get_tx_nested_field`].
    #[gas = 110]
    #[wasm_name = "home_le_inner"]
    fn get_current_ledger_obj_nested_field(
        &self,
        locator: &[u8],
        out: &mut [u8],
    ) -> HostResult<usize>;

    /// The XRPL `sha512Half` of `data`: the first [`HASH_LEN`] bytes of its SHA-512.
    #[gas = 2000]
    #[wasm_name = "sha512_half"]
    fn sha512_half(&self, data: &[u8], out: &mut [u8]) -> HostResult<usize>;

    /// Writes `msg` and `data` to the trace log, `data` in hex if `as_hex`.
    #[gas = 500]
    #[wasm_name = "trace"]
    fn trace(&self, msg: &str, data: &[u8], as_hex: bool) -> HostResult<()>;

    /// Writes `msg` and `number` to the trace log.
    #[gas = 500]
    #[wasm_name = "trace_num"]
    fn trace_num(&self, msg: &str, number: i64) -> HostResult<()>;
}
