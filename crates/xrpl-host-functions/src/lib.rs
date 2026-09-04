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
//! Three items cross that split the other way — no declaration mentions any of them,
//! and the expansion names all three. [`WasmValType`] is what the wasm signature
//! derived from a declaration is spelled in; `FromWasmRegion` and `FromWasmScalar`
//! are what `wasmi_glue!` builds a marshalled argument through, and so are the shape
//! half of that macro's contract with the engine it expands in.
//!
//! So this file is lists — error codes, trace data types, functions. The `macro_rules!`
//! that expand the first two into enums live in `macros.rs`.

#![no_std]

#[macro_use]
mod macros;

// Not re-exported: the ABI is declared once, here, and this is the only call site.
use xrpl_host_functions_macros::host_functions;

host_errors! {
    Unimplemented = -1,
    FieldNotFound = -2,
    BufferTooSmall = -3,
    NoArray = -4,
    NotLeafField = -5,
    LocatorMalformed = -6,
    SlotOutRange = -7,
    SlotsFull = -8,
    EmptySlot = -9,
    LedgerObjNotFound = -10,
    OutOfTransferLimit = -11,
    DataFieldTooLarge = -12,
    PointerOutOfBounds = -13,
    NoMemExported = -14,
    InvalidParams = -15,
    InvalidAccount = -16,
    InvalidField = -17,
    IndexOutOfBounds = -18,
    FloatInputMalformed = -19,
    FloatComputationError = -20,
    /// Internal fatal error.
    /// User code will never see this error but keep it reserved to not rely on the value.
    InternalFatal = -2147483648,
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

/// The wasm module name a guest imports these functions under:
/// `(import "host_lib" "ldgr_index" …)`.
///
/// Part of the ABI rather than of any engine — it is half of every import's
/// name, so a guest that spells it differently links against nothing. An engine
/// registers under it and a screening stage refuses anything else.
pub const HOST_MODULE: &str = "host_lib";

/// A wasm value type, as much of them as this ABI uses.
///
/// The vocabulary the generated wasm signatures are spelled in: a declaration's
/// parameters and its result are these, and an engine maps them to its own value
/// types once. Both positions use the same enum, since a wasm value type is one
/// thing wherever it stands.
#[derive(Debug, Clone, Copy, PartialEq, Eq)]
pub enum WasmValType {
    I32,
    I64,
}

/// Builds the argument an engine marshals a `(ptr, len)` region into, from the
/// two wasm parameters the region arrived as: a declared `&[u8]`, `&str`, `u32`
/// or `&mut [u8]`.
///
/// This and [`FromWasmScalar`] are the shapes half of what `wasmi_glue!` needs of
/// an engine — the module it is handed says *which* type marshals each declared
/// one, and these say what the expansion may do with them. Nothing is checked
/// here: the pair arrives as the guest sent it, and where a malformed region is
/// refused is the engine's business.
///
/// **Kept apart from [`FromWasmScalar`]** rather than folded into one trait with
/// an associated wasm type, and the reason is the diagnostic. The mistake worth
/// catching is an arity one — a declared `u32` is a region, not a code — and as
/// two traits that lands as an unsatisfied bound pointing at the offending
/// argument type, where one trait would make it an `i32`-against-`(i32, i32)`
/// mismatch pointing at the macro call.
#[cfg(feature = "wasmi_glue")]
pub trait FromWasmRegion {
    fn from_wasm(ptr: i32, len: i32) -> Self;
}

/// Builds the argument an engine marshals a single `i32` code into: the declared
/// `TraceDataType`.
///
/// [`FromWasmRegion`] is the other shape, and says why the two are separate
/// traits.
#[cfg(feature = "wasmi_glue")]
pub trait FromWasmScalar {
    fn from_wasm(code: i32) -> Self;
}

// Two rules hold over every declaration below, and neither is visible at any one of
// them. They are what lets the wasm signature be read off the declaration.
//
// **Declaration order is wasm parameter order.** A parameter is at the same place on
// the wire as it is here, so a reader of a declaration is reading the import the guest
// links against. Where that forced a choice — `mode` after `out` in the float
// functions, `data_type` between `trace`'s two regions — the wire won and the
// declaration moved.
//
// **`i32` and `i64` are the wasm scalars, spelled as themselves; every other type is
// marshalled.** `&[u8]`/`&str` and `&mut [u8]` are `(ptr, len)` pairs, `TraceDataType`
// is an `i32` code the engine names before a host sees it, and **`u32` is four
// little-endian bytes in a region**, not a scalar — that is how the guest SDK passes a
// sequence number. So `i32` is also the spelling for a raw scalar whose signedness the
// ABI does not fix.
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

    /// Whether an amendment is enabled. The input is either its 32-byte id or its name;
    /// the answer is `1` if enabled and `0` if not.
    #[gas = 100]
    #[wasm_name = "amendment_enabled"]
    fn is_amendment_enabled(&self, amendment: &[u8]) -> HostResult<i32>;

    /// Load the ledger object with the given 32-byte id into a cache slot, so later
    /// calls can read its fields. `cache_idx` selects the slot (1-based); `0` asks the
    /// host to assign a free one. Answers the slot used.
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
    /// `locator`: a path of little-endian `i32` steps (so its byte length is a non-zero
    /// multiple of 4).
    #[gas = 110]
    #[wasm_name = "tx_inner"]
    fn get_tx_nested_field(&self, locator: &[u8], out: &mut [u8]) -> HostResult<usize>;

    /// The serialized bytes of a nested field of the current (escrow) ledger object,
    /// reached by a `locator`, as with [`HostFunctions::get_tx_nested_field`].
    #[gas = 110]
    #[wasm_name = "home_le_inner"]
    fn get_current_ledger_obj_nested_field(
        &self,
        locator: &[u8],
        out: &mut [u8],
    ) -> HostResult<usize>;

    /// The serialized bytes of a nested field of a previously cached ledger object,
    /// selected by its cache slot and reached by a `locator`.
    #[gas = 110]
    #[wasm_name = "le_inner"]
    fn get_ledger_obj_nested_field(
        &self,
        cache_idx: i32,
        locator: &[u8],
        out: &mut [u8],
    ) -> HostResult<usize>;

    /// The number of elements in an array field of the transaction, selected by its
    /// `SField` code. Answers the count directly; `NoArray` if the field is not an array.
    #[gas = 40]
    #[wasm_name = "tx_arr_len"]
    fn get_tx_array_len(&self, field: i32) -> HostResult<i32>;

    /// The number of elements in an array field of the current (escrow) ledger
    /// object, as with [`HostFunctions::get_tx_array_len`].
    #[gas = 40]
    #[wasm_name = "home_le_arr_len"]
    fn get_current_ledger_obj_array_len(&self, field: i32) -> HostResult<i32>;

    /// The number of elements in an array field of a previously cached ledger object,
    /// selected by its cache slot and `SField` code.
    #[gas = 40]
    #[wasm_name = "le_arr_len"]
    fn get_ledger_obj_array_len(&self, cache_idx: i32, field: i32) -> HostResult<i32>;

    /// The number of elements in a nested array field of the transaction, reached by a
    /// `locator`.
    #[gas = 70]
    #[wasm_name = "tx_inner_arr_len"]
    fn get_tx_nested_array_len(&self, locator: &[u8]) -> HostResult<i32>;

    /// The number of elements in a nested array field of the current (escrow) ledger
    /// object, reached by a `locator`, as with [`HostFunctions::get_tx_nested_array_len`].
    #[gas = 70]
    #[wasm_name = "home_le_inner_arr_len"]
    fn get_current_ledger_obj_nested_array_len(&self, locator: &[u8]) -> HostResult<i32>;

    /// The number of elements in a nested array field of a previously cached ledger
    /// object, selected by its cache slot and reached by a `locator`.
    #[gas = 70]
    #[wasm_name = "le_inner_arr_len"]
    fn get_ledger_obj_nested_array_len(&self, cache_idx: i32, locator: &[u8]) -> HostResult<i32>;

    /// Verify `signature` over `message` under `pubkey`. Answers `1` if the signature
    /// is valid, `0` if not, or a negative error.
    #[gas = 300]
    #[wasm_name = "check_sig"]
    fn check_signature(
        &self,
        message: &[u8],
        signature: &[u8],
        pubkey: &[u8],
    ) -> HostResult<i32>;

    /// The 32-byte ledger key (keylet) of an account's `AccountRoot`, computed from a
    /// 20-byte account id.
    #[gas = 350]
    #[wasm_name = "accountroot_id"]
    fn account_keylet(&self, account: &[u8], out: &mut [u8]) -> HostResult<usize>;

    /// The 32-byte keylet of an AMM, computed from its two assets. Each asset is a byte
    /// slice whose length selects its kind (24 = MPT, 20 = XRP, 40 = issued currency +
    /// issuer).
    #[gas = 450]
    #[wasm_name = "amm_id"]
    fn amm_keylet(&self, asset1: &[u8], asset2: &[u8], out: &mut [u8]) -> HostResult<usize>;

    /// The 32-byte keylet of a `Check`, computed from a 20-byte account id and its
    /// sequence number.
    #[gas = 350]
    #[wasm_name = "check_id"]
    fn check_keylet(&self, account: &[u8], seq: u32, out: &mut [u8]) -> HostResult<usize>;

    /// The 32-byte keylet of a `Credential`, computed from the 20-byte subject and
    /// issuer account ids and a credential-type byte string.
    #[gas = 350]
    #[wasm_name = "credential_id"]
    fn credential_keylet(
        &self,
        subject: &[u8],
        issuer: &[u8],
        credential_type: &[u8],
        out: &mut [u8],
    ) -> HostResult<usize>;

    /// The 32-byte keylet of a `Delegate` object, computed from the 20-byte account and
    /// the account it authorizes.
    #[gas = 350]
    #[wasm_name = "delegate_id"]
    fn delegate_keylet(
        &self,
        account: &[u8],
        authorize: &[u8],
        out: &mut [u8],
    ) -> HostResult<usize>;

    /// The 32-byte keylet of a `DepositPreauth`, computed from the 20-byte account and
    /// the account it authorizes to deposit.
    #[gas = 350]
    #[wasm_name = "deposit_preauth_id"]
    fn deposit_preauth_keylet(
        &self,
        account: &[u8],
        authorize: &[u8],
        out: &mut [u8],
    ) -> HostResult<usize>;

    /// The 32-byte keylet of an account's `DID`, computed from its 20-byte account id.
    #[gas = 350]
    #[wasm_name = "did_id"]
    fn did_keylet(&self, account: &[u8], out: &mut [u8]) -> HostResult<usize>;

    /// The 32-byte keylet of an `Escrow`, computed from the 20-byte owner account and
    /// its sequence number.
    #[gas = 350]
    #[wasm_name = "escrow_id"]
    fn escrow_keylet(&self, account: &[u8], seq: u32, out: &mut [u8]) -> HostResult<usize>;

    /// The 32-byte keylet of a `RippleState` (trust line), computed from two 20-byte
    /// account ids and a 20-byte currency.
    #[gas = 400]
    #[wasm_name = "trustline_id"]
    fn trust_line_keylet(
        &self,
        account1: &[u8],
        account2: &[u8],
        currency: &[u8],
        out: &mut [u8],
    ) -> HostResult<usize>;

    /// The 32-byte keylet of an `MPTokenIssuance`, computed from the 20-byte issuer
    /// account and its sequence number.
    #[gas = 350]
    #[wasm_name = "mpt_issuance_id"]
    fn mptoken_issuance_keylet(
        &self,
        issuer: &[u8],
        seq: u32,
        out: &mut [u8],
    ) -> HostResult<usize>;

    /// The 32-byte keylet of an `MPToken`, computed from a 24-byte MPT issuance id and
    /// the 20-byte holder account.
    #[gas = 500]
    #[wasm_name = "mptoken_id"]
    fn mptoken_keylet(&self, mptid: &[u8], holder: &[u8], out: &mut [u8]) -> HostResult<usize>;

    /// The 32-byte keylet of an `NFTokenOffer`, computed from the 20-byte owner account
    /// and its sequence number.
    #[gas = 350]
    #[wasm_name = "nft_offer_id"]
    fn nftoken_offer_keylet(
        &self,
        account: &[u8],
        seq: u32,
        out: &mut [u8],
    ) -> HostResult<usize>;

    /// The 32-byte keylet of an `Offer`, computed from the 20-byte owner account and
    /// its sequence number.
    #[gas = 350]
    #[wasm_name = "offer_id"]
    fn offer_keylet(&self, account: &[u8], seq: u32, out: &mut [u8]) -> HostResult<usize>;

    /// The 32-byte keylet of an `Oracle`, computed from the 20-byte owner account and
    /// its document id.
    #[gas = 350]
    #[wasm_name = "oracle_id"]
    fn oracle_keylet(&self, account: &[u8], doc_id: u32, out: &mut [u8]) -> HostResult<usize>;

    /// The 32-byte keylet of a `PayChannel`, computed from the 20-byte source account,
    /// the 20-byte destination account, and the channel's sequence number.
    #[gas = 350]
    #[wasm_name = "paychan_id"]
    fn paychannel_keylet(
        &self,
        account: &[u8],
        destination: &[u8],
        seq: u32,
        out: &mut [u8],
    ) -> HostResult<usize>;

    /// The 32-byte keylet of a `PermissionedDomain`, computed from the 20-byte owner
    /// account and its sequence number.
    #[gas = 350]
    #[wasm_name = "permissioned_domain_id"]
    fn permissioned_domain_keylet(
        &self,
        account: &[u8],
        seq: u32,
        out: &mut [u8],
    ) -> HostResult<usize>;

    /// The 32-byte keylet of a `SignerList`, computed from its 20-byte owner account.
    #[gas = 350]
    #[wasm_name = "signers_id"]
    fn signer_list_keylet(&self, account: &[u8], out: &mut [u8]) -> HostResult<usize>;

    /// The 32-byte keylet of a `Ticket`, computed from the 20-byte owner account and
    /// its ticket sequence number.
    #[gas = 350]
    #[wasm_name = "ticket_id"]
    fn ticket_keylet(&self, account: &[u8], seq: u32, out: &mut [u8]) -> HostResult<usize>;

    /// The 32-byte keylet of a `Vault`, computed from the 20-byte owner account and its
    /// sequence number.
    #[gas = 350]
    #[wasm_name = "vault_id"]
    fn vault_keylet(&self, account: &[u8], seq: u32, out: &mut [u8]) -> HostResult<usize>;

    /// The XRPL `sha512Half` of `data`: the first [`HASH_LEN`] bytes of its SHA-512.
    #[gas = 2000]
    #[wasm_name = "sha512_half"]
    fn sha512_half(&self, data: &[u8], out: &mut [u8]) -> HostResult<usize>;

    /// Writes `msg` to the trace log, followed by `data` rendered as `data_type` says.
    ///
    /// The one declaration whose wasm function has **no result**: this node's own log
    /// is its only effect, so a guest is told nothing. An `Err` from a host therefore
    /// reaches it in no form, and only the host-fatal ones do anything at all.
    #[gas = 30]
    #[wasm_name = "trace"]
    fn trace(&self, msg: &str, data_type: TraceDataType, data: &[u8]) -> HostResult<()>;

    /// Stores `data` as the current object's data field, replacing whatever was there,
    /// and returns the number of bytes stored; `DataFieldTooLarge` if it exceeds the
    /// host's limit.
    #[gas = 1000]
    #[wasm_name = "set_data"]
    fn update_data(&self, data: &[u8]) -> HostResult<i32>;

    /// The URI of the `NFToken` with id `nft_id` (32 bytes) held by the 20-byte
    /// `account`.
    #[gas = 5000]
    #[wasm_name = "nft_uri"]
    fn get_nft(&self, account: &[u8], nft_id: &[u8], out: &mut [u8]) -> HostResult<usize>;

    /// The 20-byte issuer account encoded in the `NFToken` id `nft_id` (32 bytes).
    #[gas = 70]
    #[wasm_name = "nft_issuer"]
    fn get_nft_issuer(&self, nft_id: &[u8], out: &mut [u8]) -> HostResult<usize>;

    /// The taxon encoded in the `NFToken` id `nft_id` (32 bytes), as four little-endian
    /// bytes.
    #[gas = 60]
    #[wasm_name = "nft_taxon"]
    fn get_nft_taxon(&self, nft_id: &[u8], out: &mut [u8]) -> HostResult<usize>;

    /// The flags encoded in the `NFToken` id `nft_id` (32 bytes).
    #[gas = 60]
    #[wasm_name = "nft_flags"]
    fn get_nft_flags(&self, nft_id: &[u8]) -> HostResult<i32>;

    /// The transfer fee encoded in the `NFToken` id `nft_id` (32 bytes).
    #[gas = 60]
    #[wasm_name = "nft_xfer_fee"]
    fn get_nft_transfer_fee(&self, nft_id: &[u8]) -> HostResult<i32>;

    /// The sequence number encoded in the `NFToken` id `nft_id` (32 bytes), as four
    /// little-endian bytes.
    #[gas = 60]
    #[wasm_name = "nft_serial"]
    fn get_nft_sequence(&self, nft_id: &[u8], out: &mut [u8]) -> HostResult<usize>;

    // A "float" here is an XRPL `Number` in its serialized form: a byte blob the guest
    // holds opaquely and hands back to these functions. Inputs and outputs that are
    // floats are byte regions; `mode` is the rounding mode, a scalar the guest chooses.

    /// A float built from the signed integer `x` under rounding `mode`.
    #[gas = 100]
    #[wasm_name = "float_from_int"]
    fn float_from_int(&self, x: i64, out: &mut [u8], mode: i32) -> HostResult<usize>;

    /// A float built from the unsigned integer in the 8-byte region `x` under rounding
    /// `mode`.
    #[gas = 130]
    #[wasm_name = "float_from_uint"]
    fn float_from_uint(&self, x: &[u8], out: &mut [u8], mode: i32) -> HostResult<usize>;

    /// A float built from the serialized `STAmount` in `amount` under rounding `mode`.
    #[gas = 150]
    #[wasm_name = "float_from_stamount"]
    fn float_from_stamount(&self, amount: &[u8], out: &mut [u8], mode: i32) -> HostResult<usize>;

    /// A float built from the serialized `STNumber` in `number` under rounding `mode`.
    #[gas = 150]
    #[wasm_name = "float_from_stnumber"]
    fn float_from_stnumber(&self, number: &[u8], out: &mut [u8], mode: i32) -> HostResult<usize>;

    /// The float `x` rounded to a signed integer under rounding `mode`, as eight
    /// little-endian bytes.
    #[gas = 130]
    #[wasm_name = "float_to_int"]
    fn float_to_int(&self, x: &[u8], out: &mut [u8], mode: i32) -> HostResult<usize>;

    /// The float `x` split into its mantissa (eight little-endian bytes) and its exponent
    /// (four little-endian bytes), each written to its own output region.
    #[gas = 130]
    #[wasm_name = "float_to_mant_exp"]
    fn float_to_mant_exp(
        &self,
        x: &[u8],
        mantissa_out: &mut [u8],
        exponent_out: &mut [u8],
    ) -> HostResult<usize>;

    /// A float built from `mantissa` and `exponent` under rounding `mode`.
    #[gas = 100]
    #[wasm_name = "float_from_mant_exp"]
    fn float_from_mant_exp(
        &self,
        mantissa: i64,
        exponent: i32,
        out: &mut [u8],
        mode: i32,
    ) -> HostResult<usize>;

    /// Compares floats `x` and `y`, returning a negative, zero, or positive scalar as
    /// `x` is less than, equal to, or greater than `y`.
    #[gas = 80]
    #[wasm_name = "float_cmp"]
    fn float_compare(&self, x: &[u8], y: &[u8]) -> HostResult<i32>;

    /// The float sum `x + y` under rounding `mode`.
    #[gas = 160]
    #[wasm_name = "float_add"]
    fn float_add(&self, x: &[u8], y: &[u8], out: &mut [u8], mode: i32) -> HostResult<usize>;

    /// The float difference `x - y` under rounding `mode`.
    #[gas = 160]
    #[wasm_name = "float_sub"]
    fn float_subtract(&self, x: &[u8], y: &[u8], out: &mut [u8], mode: i32) -> HostResult<usize>;

    /// The float product `x * y` under rounding `mode`.
    #[gas = 300]
    #[wasm_name = "float_mult"]
    fn float_multiply(&self, x: &[u8], y: &[u8], out: &mut [u8], mode: i32) -> HostResult<usize>;

    /// The float quotient `x / y` under rounding `mode`.
    #[gas = 300]
    #[wasm_name = "float_div"]
    fn float_divide(&self, x: &[u8], y: &[u8], out: &mut [u8], mode: i32) -> HostResult<usize>;

    /// The float `x` raised to the power `n` under rounding `mode`.
    #[gas = 5500]
    #[wasm_name = "float_pow"]
    fn float_power(&self, x: &[u8], n: i32, out: &mut [u8], mode: i32) -> HostResult<usize>;
}
