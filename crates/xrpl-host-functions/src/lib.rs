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

            /// The negative wire value the guest sees as the function's return code.
            #[inline]
            pub const fn code(self) -> i32 {
                self as i32
            }

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

/// Convenience alias for the trait's fallible returns.
pub type HostResult<T> = Result<T, HostError>;

/// A `sha512Half` digest: the first 32 bytes of a SHA-512, as XRPL uses it.
pub const HASH_LEN: usize = 32;

/// Declares [`TraceDataType`] from one list, so [`TraceDataType::ALL`],
/// [`TraceDataType::code`] and [`TraceDataType::from_code`] cannot fall behind the
/// variants — the reason `host_errors!` above is written this way.
macro_rules! trace_data_types {
    ($($(#[$doc:meta])* $variant:ident = $code:literal,)+) => {
        /// How [`HostFunctions::trace`] is to read its data buffer.
        ///
        /// The discriminants are wire values shared with the guest stdlib: append only,
        /// never renumber. They start at 1, so a zeroed argument names no type rather
        /// than the first one.
        ///
        /// This is the declaration a guest and a host both compile against. The host
        /// side needs a second one — `cxx` cannot be a dependency here, since this
        /// crate also links into the guest — so `xrpl-wasm-vm-ffi` declares a shared
        /// enum for C++ and converts, exhaustively, from this.
        #[derive(Debug, Clone, Copy, PartialEq, Eq)]
        #[repr(i32)]
        pub enum TraceDataType {
            $($(#[$doc])* $variant = $code,)+
        }

        impl TraceDataType {
            /// Every data type a guest may name, in code order.
            pub const ALL: &'static [TraceDataType] = &[$(TraceDataType::$variant,)+];

            /// The wire value a guest passes to name this type.
            #[inline]
            pub const fn code(self) -> i32 {
                self as i32
            }

            /// The type `code` names, or `None`: the engine drops a call it cannot
            /// read rather than guessing at a rendering the guest did not ask for.
            pub const fn from_code(code: i32) -> Option<TraceDataType> {
                match code {
                    $($code => Some(TraceDataType::$variant),)+
                    _ => None,
                }
            }
        }
    };
}

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
