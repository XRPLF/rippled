//! The cxx bridge between the escrow wasm engine and xrpld.
//!
//! Three crossings:
//!
//! - **In:** C++ calls `run_escrow`, once per escrow finish.
//! - **Back out:** that run's host calls leave through the C++ `HostContext`, which
//!   `CxxHost` presents to the engine as an ordinary [`HostFunctions`] implementor.
//! - **In only:** C++ screens a module with `check_escrow`. Screening needs no host,
//!   so nothing comes back out.
//!
//! The ABI the host calls speak is declared once, in `xrpl-host-functions`, so neither
//! side of this file gets to restate a signature.
//!
//! **Neither language may unwind into the other**, and the two halves of that are
//! not symmetric:
//!
//! - A **Rust panic** is caught here, by `guarded`. Letting one reach C++ is
//!   undefined behaviour; `[profile.release]` turns overflow checks on, so this is a
//!   live path and not a formality.
//! - A **C++ exception** is stopped on the C++ side: every `HostContext` method is
//!   `noexcept` and catches its own. That is what makes `guarded` sufficient — see
//!   its documentation.
//!
//! Everything hand-written here is private, so the names above are code spans rather
//! than links, and `cargo doc` needs `--document-private-items` to show any of it.
//! That is also why this crate, unlike `xrpl-wasm-vm`, does not
//! `deny(unreachable_pub)`: cxx's expansion is `pub` throughout by necessity, leaving
//! the lint nothing but generated code to fire on.
#![deny(rustdoc::broken_intra_doc_links)]

use std::any::Any;
use std::panic::{AssertUnwindSafe, catch_unwind};
use xrpl_host_functions::{HostError, HostFunctions, HostResult, TraceDataType};
use xrpl_wasm_vm::{CheckError, RunError, RunFailure, RunOutcome, check, run};

/// [`guarded`] must be able to stop an unwind. Under `panic = "abort"` it cannot,
/// and every arithmetic overflow in the engine becomes a node crash instead of a
/// `tecINTERNAL`.
#[cfg(panic = "abort")]
compile_error!(
    "xrpl-wasm-vm-ffi requires panic=unwind: run_escrow catches panics rather than \
     letting them cross into C++"
);

#[cxx::bridge(namespace = "rs::wasm_vm")]
mod ffi {
    /// Which outcome a run had — one variant per way [`run`] can end, so the caller
    /// maps a status to a TER rather than reading a message.
    #[derive(Debug, Hash)]
    #[repr(i32)]
    enum RunStatus {
        /// The entry point returned.
        Ok,
        /// `wasm` is not a valid module under this engine's configuration.
        Compile,
        /// The module would not instantiate.
        Instantiate,
        /// No export of that name with signature `() -> i32`.
        EntryPoint,
        /// Gas exhausted, by the guest's instructions or a host call's charge.
        OutOfGas,
        /// The host could not serve a call, including any exception it caught.
        Internal,
        /// A host call had no linear memory to work in.
        NoMemory,
        /// The guest trapped.
        Trap,
        /// The engine panicked. A defect in this crate or the one below it.
        Panic,
    }

    /// A run's outcome, flattened: cxx enums carry no payload, so the status, the
    /// cost and the description travel side by side.
    struct RunResult {
        status: RunStatus,
        /// What the entry point returned. Meaningful only when `status` is `Ok`.
        result: i32,
        /// Gas consumed. The whole limit when gas ran out; `0` when the module never
        /// ran, or when the cost could not be trusted (`Internal`, `Panic`).
        gas_used: u64,
        /// The engine's own description of the outcome, for the log. Empty on `Ok`.
        detail: String,
    }

    /// Why a module cannot be run — one variant per way [`check`] can refuse it,
    /// so the caller maps a status to a TER rather than reading a message.
    #[derive(Debug, Hash)]
    #[repr(i32)]
    enum CheckStatus {
        /// The module compiles, imports only what the engine serves, and exports
        /// the entry point as `() -> i32`.
        Ok,
        /// `wasm` is not a valid module under this engine's configuration.
        Compile,
        /// An import the engine does not define: another module namespace, a name
        /// that is not a host function, or one imported as something else.
        Import,
        /// No export of that name with signature `() -> i32`.
        EntryPoint,
        /// The module asks for more linear memory than the engine grants.
        Memory,
        /// The module asks for a larger table than the engine grants.
        Table,
        /// The engine panicked. A defect in this crate or the one below it, and
        /// not a fault in the module — which is why it is a status of its own
        /// rather than one more way a contract can be malformed.
        Panic,
    }

    /// A check's verdict. No cost, because nothing was executed.
    struct CheckResult {
        status: CheckStatus,
        /// The engine's own description of the refusal, for the log. Empty on
        /// `Ok`.
        detail: String,
    }

    /// How `HostContext::trace` is to read its data buffer.
    ///
    /// **Declared here so that C++ does not declare it.** A shared enum is emitted into
    /// the generated header as `xrpl::TraceDataType`, which is the definition
    /// `HostContext.cpp` switches on — so the variants and their wire values are
    /// written once, in Rust, for both languages.
    ///
    /// It is not the same type as [`xrpl_host_functions::TraceDataType`], and cannot
    /// be: the ABI crate is `no_std` with no dependencies so that it also links into
    /// the guest, and `cxx` is neither. [`crossed`] converts, in a `match` that is
    /// exhaustive over the ABI's enum — so a data type added there fails to compile
    /// until it is added here, which is the drift check the hand-written C++ copy
    /// never had.
    #[namespace = "xrpl"]
    #[derive(Debug, Hash)]
    #[repr(i32)]
    enum TraceDataType {
        Int64 = 1,
        Uint64 = 2,
        Xfloat = 3,
        Account = 4,
        Amount = 5,
        AsHex = 6,
        AsText = 7,
    }

    extern "Rust" {
        /// Run `wasm`'s `function_name` export with `gas` fuel, servicing host calls
        /// through `host`.
        ///
        /// Reports every outcome as a [`RunStatus`] and **never throws**: an
        /// exception is a poor interface for a condition the caller has to turn into
        /// a TER anyway, and a panic reaching C++ would be undefined behaviour.
        ///
        /// `gas` is the run's whole budget. `0` is a run that cannot execute an
        /// instruction; the C++ front refuses it as `temBAD_AMOUNT` before calling
        /// here, so it is not given a status of its own.
        fn run_escrow(host: &HostContext, wasm: &[u8], gas: u64, function_name: &str) -> RunResult;

        /// Screen `wasm` before it can reach the ledger: whether [`run_escrow`]
        /// would refuse it before the guest's first instruction.
        ///
        /// Takes no host, no gas and no store — the verdict comes from the
        /// compiled module alone, which is what makes it callable from a
        /// transaction's preflight, where there is no ledger to serve a host call
        /// from. **Never throws**, for the same reason [`run_escrow`] does not.
        fn check_escrow(wasm: &[u8], function_name: &str) -> CheckResult;
    }

    unsafe extern "C++" {
        include!("xrpl/tx/wasm/HostContext.h");

        /// The C++ side of the ABI: one method per host function, forwarding to
        /// `xrpl::HostFunctions`.
        ///
        /// Every method is `noexcept` and catches everything, so a host call cannot
        /// unwind into the engine.
        ///
        /// `cxx_name` on each method below is not cosmetic: the declarations keep the
        /// ABI's names here and rippled's camelBack over there, so neither side has
        /// to spell the other's convention.
        #[namespace = "xrpl"]
        type HostContext;

        /// A byte-producing call is handed `out` and returns the value's **true
        /// length**, writing it only if the whole value fits. Returning a length past
        /// `out` is how a guest learns the size to ask for; the engine turns it into
        /// `BufferTooSmall`, so C++ never needs to know the guest's capacity.
        ///
        /// A negative return is a `HostError` code.
        #[namespace = "xrpl"]
        #[cxx_name = "getLedgerSqn"]
        fn get_ledger_sqn(self: &HostContext, out: &mut [u8]) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "getParentLedgerTime"]
        fn get_parent_ledger_time(self: &HostContext, out: &mut [u8]) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "getParentLedgerHash"]
        fn get_parent_ledger_hash(self: &HostContext, out: &mut [u8]) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "getBaseFee"]
        fn get_base_fee(self: &HostContext, out: &mut [u8]) -> i32;

        /// Reads the amendment (id or name) and answers `1`/`0`, or a negative
        /// `HostError` code.
        #[namespace = "xrpl"]
        #[cxx_name = "isAmendmentEnabled"]
        fn is_amendment_enabled(self: &HostContext, amendment: &[u8]) -> i32;

        /// Caches the object with `obj_id` in slot `cache_idx` (`0` = pick one) and
        /// answers the slot used, or a negative `HostError` code.
        #[namespace = "xrpl"]
        #[cxx_name = "cacheLedgerObj"]
        fn cache_ledger_obj(self: &HostContext, obj_id: &[u8], cache_idx: i32) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "getTxField"]
        fn get_tx_field(self: &HostContext, field: i32, out: &mut [u8]) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "getCurrentLedgerObjField"]
        fn get_current_ledger_obj_field(self: &HostContext, field: i32, out: &mut [u8]) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "getLedgerObjField"]
        fn get_ledger_obj_field(
            self: &HostContext,
            cache_idx: i32,
            field: i32,
            out: &mut [u8],
        ) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "getTxNestedField"]
        fn get_tx_nested_field(self: &HostContext, locator: &[u8], out: &mut [u8]) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "getCurrentLedgerObjNestedField"]
        fn get_current_ledger_obj_nested_field(
            self: &HostContext,
            locator: &[u8],
            out: &mut [u8],
        ) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "getLedgerObjNestedField"]
        fn get_ledger_obj_nested_field(
            self: &HostContext,
            cache_idx: i32,
            locator: &[u8],
            out: &mut [u8],
        ) -> i32;

        /// Answers the array's element count directly, or a negative `HostError` code.
        #[namespace = "xrpl"]
        #[cxx_name = "getTxArrayLen"]
        fn get_tx_array_len(self: &HostContext, field: i32) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "getCurrentLedgerObjArrayLen"]
        fn get_current_ledger_obj_array_len(self: &HostContext, field: i32) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "getLedgerObjArrayLen"]
        fn get_ledger_obj_array_len(self: &HostContext, cache_idx: i32, field: i32) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "getTxNestedArrayLen"]
        fn get_tx_nested_array_len(self: &HostContext, locator: &[u8]) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "getCurrentLedgerObjNestedArrayLen"]
        fn get_current_ledger_obj_nested_array_len(self: &HostContext, locator: &[u8]) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "getLedgerObjNestedArrayLen"]
        fn get_ledger_obj_nested_array_len(
            self: &HostContext,
            cache_idx: i32,
            locator: &[u8],
        ) -> i32;

        /// Answers `1`/`0` for a valid/invalid signature, or a negative `HostError`.
        #[namespace = "xrpl"]
        #[cxx_name = "checkSignature"]
        fn check_signature(
            self: &HostContext,
            message: &[u8],
            signature: &[u8],
            pubkey: &[u8],
        ) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "accountKeylet"]
        fn account_keylet(self: &HostContext, account: &[u8], out: &mut [u8]) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "ammKeylet"]
        fn amm_keylet(self: &HostContext, asset1: &[u8], asset2: &[u8], out: &mut [u8]) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "checkKeylet"]
        fn check_keylet(self: &HostContext, account: &[u8], seq: i32, out: &mut [u8]) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "credentialKeylet"]
        fn credential_keylet(
            self: &HostContext,
            subject: &[u8],
            issuer: &[u8],
            credential_type: &[u8],
            out: &mut [u8],
        ) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "delegateKeylet"]
        fn delegate_keylet(
            self: &HostContext,
            account: &[u8],
            authorize: &[u8],
            out: &mut [u8],
        ) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "depositPreauthKeylet"]
        fn deposit_preauth_keylet(
            self: &HostContext,
            account: &[u8],
            authorize: &[u8],
            out: &mut [u8],
        ) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "didKeylet"]
        fn did_keylet(self: &HostContext, account: &[u8], out: &mut [u8]) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "escrowKeylet"]
        fn escrow_keylet(self: &HostContext, account: &[u8], seq: i32, out: &mut [u8]) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "trustLineKeylet"]
        fn trust_line_keylet(
            self: &HostContext,
            account1: &[u8],
            account2: &[u8],
            currency: &[u8],
            out: &mut [u8],
        ) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "mptokenIssuanceKeylet"]
        fn mptoken_issuance_keylet(
            self: &HostContext,
            issuer: &[u8],
            seq: i32,
            out: &mut [u8],
        ) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "mptokenKeylet"]
        fn mptoken_keylet(self: &HostContext, mptid: &[u8], holder: &[u8], out: &mut [u8]) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "nftokenOfferKeylet"]
        fn nftoken_offer_keylet(
            self: &HostContext,
            account: &[u8],
            seq: i32,
            out: &mut [u8],
        ) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "offerKeylet"]
        fn offer_keylet(self: &HostContext, account: &[u8], seq: i32, out: &mut [u8]) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "oracleKeylet"]
        fn oracle_keylet(self: &HostContext, account: &[u8], doc_id: i32, out: &mut [u8]) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "paychannelKeylet"]
        fn paychannel_keylet(
            self: &HostContext,
            account: &[u8],
            destination: &[u8],
            seq: i32,
            out: &mut [u8],
        ) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "permissionedDomainKeylet"]
        fn permissioned_domain_keylet(
            self: &HostContext,
            account: &[u8],
            seq: i32,
            out: &mut [u8],
        ) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "signerListKeylet"]
        fn signer_list_keylet(self: &HostContext, account: &[u8], out: &mut [u8]) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "ticketKeylet"]
        fn ticket_keylet(self: &HostContext, account: &[u8], seq: i32, out: &mut [u8]) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "vaultKeylet"]
        fn vault_keylet(self: &HostContext, account: &[u8], seq: i32, out: &mut [u8]) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "sha512Half"]
        fn sha512_half(self: &HostContext, data: &[u8], out: &mut [u8]) -> i32;

        /// Renders `data` as `data_type` says and writes it to this node's log with
        /// `msg`. Answers nothing at all: the guest's wasm function has no result, and
        /// C++ swallows a malformed buffer rather than reporting it, so there is no
        /// failure for this side to encode.
        ///
        /// The engine has already refused a code that names no type, so what crosses
        /// here is always one of the variants.
        #[namespace = "xrpl"]
        fn trace(self: &HostContext, msg: &str, data: &[u8], data_type: TraceDataType);

        #[namespace = "xrpl"]
        #[cxx_name = "updateData"]
        fn update_data(self: &HostContext, data: &[u8]) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "getNFT"]
        fn get_nft(self: &HostContext, account: &[u8], nft_id: &[u8], out: &mut [u8]) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "getNFTIssuer"]
        fn get_nft_issuer(self: &HostContext, nft_id: &[u8], out: &mut [u8]) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "getNFTTaxon"]
        fn get_nft_taxon(self: &HostContext, nft_id: &[u8], out: &mut [u8]) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "getNFTFlags"]
        fn get_nft_flags(self: &HostContext, nft_id: &[u8]) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "getNFTTransferFee"]
        fn get_nft_transfer_fee(self: &HostContext, nft_id: &[u8]) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "getNFTSequence"]
        fn get_nft_sequence(self: &HostContext, nft_id: &[u8], out: &mut [u8]) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "floatFromInt"]
        fn float_from_int(self: &HostContext, x: i64, mode: i32, out: &mut [u8]) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "floatFromUint"]
        fn float_from_uint(self: &HostContext, x: &[u8], mode: i32, out: &mut [u8]) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "floatFromSTAmount"]
        fn float_from_stamount(self: &HostContext, amount: &[u8], mode: i32, out: &mut [u8])
        -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "floatFromSTNumber"]
        fn float_from_stnumber(self: &HostContext, number: &[u8], mode: i32, out: &mut [u8])
        -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "floatToInt"]
        fn float_to_int(self: &HostContext, x: &[u8], mode: i32, out: &mut [u8]) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "floatToMantExp"]
        fn float_to_mant_exp(
            self: &HostContext,
            x: &[u8],
            mantissa_out: &mut [u8],
            exponent_out: &mut [u8],
        ) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "floatFromMantExp"]
        fn float_from_mant_exp(
            self: &HostContext,
            mantissa: i64,
            exponent: i32,
            mode: i32,
            out: &mut [u8],
        ) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "floatCompare"]
        fn float_compare(self: &HostContext, x: &[u8], y: &[u8]) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "floatAdd"]
        fn float_add(self: &HostContext, x: &[u8], y: &[u8], mode: i32, out: &mut [u8]) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "floatSubtract"]
        fn float_subtract(self: &HostContext, x: &[u8], y: &[u8], mode: i32, out: &mut [u8])
        -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "floatMultiply"]
        fn float_multiply(self: &HostContext, x: &[u8], y: &[u8], mode: i32, out: &mut [u8])
        -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "floatDivide"]
        fn float_divide(self: &HostContext, x: &[u8], y: &[u8], mode: i32, out: &mut [u8]) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "floatPower"]
        fn float_power(self: &HostContext, x: &[u8], n: i32, mode: i32, out: &mut [u8]) -> i32;
    }
}

/// Sized carrier for the [`HostFunctions`] implementation.
///
/// [`ffi::HostContext`] is an opaque C++ type and therefore `!Sized`, so it cannot
/// be coerced to `&dyn HostFunctions` itself.
struct CxxHost<'a> {
    ctx: &'a ffi::HostContext,
}

/// A byte-producing call's answer: the value's true length, or its error code.
///
/// The conversion *is* the sign test — it fails on exactly the negative values — so
/// there is no cast to argue about.
///
/// A named function rather than a `From` impl, and not by preference: every type
/// involved — `i32`, `Result`, `HostError` — is foreign to this crate, so the orphan
/// rule forbids the impl.
fn bytes_written(n: i32) -> HostResult<usize> {
    usize::try_from(n).map_err(|_| HostError::from_code(n))
}

/// The ABI's data type as the shared enum C++ was given a definition of.
///
/// A `match` rather than a cast through `code()`: the cast would compile for a variant
/// nobody added to [`ffi::TraceDataType`] and hand C++ a value its `switch` does not
/// name. This is the whole reason the two lists cannot drift.
fn crossed(data_type: TraceDataType) -> ffi::TraceDataType {
    match data_type {
        TraceDataType::Int64 => ffi::TraceDataType::Int64,
        TraceDataType::Uint64 => ffi::TraceDataType::Uint64,
        TraceDataType::Xfloat => ffi::TraceDataType::Xfloat,
        TraceDataType::Account => ffi::TraceDataType::Account,
        TraceDataType::Amount => ffi::TraceDataType::Amount,
        TraceDataType::AsHex => ffi::TraceDataType::AsHex,
        TraceDataType::AsText => ffi::TraceDataType::AsText,
    }
}

/// A call whose answer is a scalar the guest reads directly (a flag, a slot index):
/// a non-negative value is that answer, a negative one its error code.
fn scalar(n: i32) -> HostResult<i32> {
    if n < 0 {
        return Err(HostError::from_code(n));
    }
    Ok(n)
}

impl HostFunctions for CxxHost<'_> {
    fn get_ledger_sqn(&self, out: &mut [u8]) -> HostResult<usize> {
        bytes_written(self.ctx.get_ledger_sqn(out))
    }

    fn get_parent_ledger_time(&self, out: &mut [u8]) -> HostResult<usize> {
        bytes_written(self.ctx.get_parent_ledger_time(out))
    }

    fn get_parent_ledger_hash(&self, out: &mut [u8]) -> HostResult<usize> {
        bytes_written(self.ctx.get_parent_ledger_hash(out))
    }

    fn get_base_fee(&self, out: &mut [u8]) -> HostResult<usize> {
        bytes_written(self.ctx.get_base_fee(out))
    }

    fn is_amendment_enabled(&self, amendment: &[u8]) -> HostResult<i32> {
        scalar(self.ctx.is_amendment_enabled(amendment))
    }

    fn cache_ledger_obj(&self, obj_id: &[u8], cache_idx: i32) -> HostResult<i32> {
        scalar(self.ctx.cache_ledger_obj(obj_id, cache_idx))
    }

    fn get_tx_field(&self, field: i32, out: &mut [u8]) -> HostResult<usize> {
        bytes_written(self.ctx.get_tx_field(field, out))
    }

    fn get_current_ledger_obj_field(&self, field: i32, out: &mut [u8]) -> HostResult<usize> {
        bytes_written(self.ctx.get_current_ledger_obj_field(field, out))
    }

    fn get_ledger_obj_field(
        &self,
        cache_idx: i32,
        field: i32,
        out: &mut [u8],
    ) -> HostResult<usize> {
        bytes_written(self.ctx.get_ledger_obj_field(cache_idx, field, out))
    }

    fn get_tx_nested_field(&self, locator: &[u8], out: &mut [u8]) -> HostResult<usize> {
        bytes_written(self.ctx.get_tx_nested_field(locator, out))
    }

    fn get_current_ledger_obj_nested_field(
        &self,
        locator: &[u8],
        out: &mut [u8],
    ) -> HostResult<usize> {
        bytes_written(self.ctx.get_current_ledger_obj_nested_field(locator, out))
    }

    fn get_ledger_obj_nested_field(
        &self,
        cache_idx: i32,
        locator: &[u8],
        out: &mut [u8],
    ) -> HostResult<usize> {
        bytes_written(
            self.ctx
                .get_ledger_obj_nested_field(cache_idx, locator, out),
        )
    }

    fn get_tx_array_len(&self, field: i32) -> HostResult<i32> {
        scalar(self.ctx.get_tx_array_len(field))
    }

    fn get_current_ledger_obj_array_len(&self, field: i32) -> HostResult<i32> {
        scalar(self.ctx.get_current_ledger_obj_array_len(field))
    }

    fn get_ledger_obj_array_len(&self, cache_idx: i32, field: i32) -> HostResult<i32> {
        scalar(self.ctx.get_ledger_obj_array_len(cache_idx, field))
    }

    fn get_tx_nested_array_len(&self, locator: &[u8]) -> HostResult<i32> {
        scalar(self.ctx.get_tx_nested_array_len(locator))
    }

    fn get_current_ledger_obj_nested_array_len(&self, locator: &[u8]) -> HostResult<i32> {
        scalar(self.ctx.get_current_ledger_obj_nested_array_len(locator))
    }

    fn get_ledger_obj_nested_array_len(&self, cache_idx: i32, locator: &[u8]) -> HostResult<i32> {
        scalar(self.ctx.get_ledger_obj_nested_array_len(cache_idx, locator))
    }

    fn check_signature(&self, message: &[u8], signature: &[u8], pubkey: &[u8]) -> HostResult<i32> {
        scalar(self.ctx.check_signature(message, signature, pubkey))
    }

    fn account_keylet(&self, account: &[u8], out: &mut [u8]) -> HostResult<usize> {
        bytes_written(self.ctx.account_keylet(account, out))
    }

    fn amm_keylet(&self, asset1: &[u8], asset2: &[u8], out: &mut [u8]) -> HostResult<usize> {
        bytes_written(self.ctx.amm_keylet(asset1, asset2, out))
    }

    // `cast_signed` on a keylet's `seq`/`doc_id`, here and in the nine below: the ABI
    // declares the guest's `u32`, while `HostContext` takes rippled's `std::int32_t`.
    // The bit pattern is what both sides mean, so the crossing is a reinterpretation
    // rather than a conversion, and no value is out of range on either side.
    fn check_keylet(&self, account: &[u8], seq: u32, out: &mut [u8]) -> HostResult<usize> {
        bytes_written(self.ctx.check_keylet(account, seq.cast_signed(), out))
    }

    fn credential_keylet(
        &self,
        subject: &[u8],
        issuer: &[u8],
        credential_type: &[u8],
        out: &mut [u8],
    ) -> HostResult<usize> {
        bytes_written(
            self.ctx
                .credential_keylet(subject, issuer, credential_type, out),
        )
    }

    fn delegate_keylet(
        &self,
        account: &[u8],
        authorize: &[u8],
        out: &mut [u8],
    ) -> HostResult<usize> {
        bytes_written(self.ctx.delegate_keylet(account, authorize, out))
    }

    fn deposit_preauth_keylet(
        &self,
        account: &[u8],
        authorize: &[u8],
        out: &mut [u8],
    ) -> HostResult<usize> {
        bytes_written(self.ctx.deposit_preauth_keylet(account, authorize, out))
    }

    fn did_keylet(&self, account: &[u8], out: &mut [u8]) -> HostResult<usize> {
        bytes_written(self.ctx.did_keylet(account, out))
    }

    fn escrow_keylet(&self, account: &[u8], seq: u32, out: &mut [u8]) -> HostResult<usize> {
        bytes_written(self.ctx.escrow_keylet(account, seq.cast_signed(), out))
    }

    fn trust_line_keylet(
        &self,
        account1: &[u8],
        account2: &[u8],
        currency: &[u8],
        out: &mut [u8],
    ) -> HostResult<usize> {
        bytes_written(
            self.ctx
                .trust_line_keylet(account1, account2, currency, out),
        )
    }

    fn mptoken_issuance_keylet(
        &self,
        issuer: &[u8],
        seq: u32,
        out: &mut [u8],
    ) -> HostResult<usize> {
        bytes_written(
            self.ctx
                .mptoken_issuance_keylet(issuer, seq.cast_signed(), out),
        )
    }

    fn mptoken_keylet(&self, mptid: &[u8], holder: &[u8], out: &mut [u8]) -> HostResult<usize> {
        bytes_written(self.ctx.mptoken_keylet(mptid, holder, out))
    }

    fn nftoken_offer_keylet(&self, account: &[u8], seq: u32, out: &mut [u8]) -> HostResult<usize> {
        bytes_written(
            self.ctx
                .nftoken_offer_keylet(account, seq.cast_signed(), out),
        )
    }

    fn offer_keylet(&self, account: &[u8], seq: u32, out: &mut [u8]) -> HostResult<usize> {
        bytes_written(self.ctx.offer_keylet(account, seq.cast_signed(), out))
    }

    fn oracle_keylet(&self, account: &[u8], doc_id: u32, out: &mut [u8]) -> HostResult<usize> {
        bytes_written(self.ctx.oracle_keylet(account, doc_id.cast_signed(), out))
    }

    fn paychannel_keylet(
        &self,
        account: &[u8],
        destination: &[u8],
        seq: u32,
        out: &mut [u8],
    ) -> HostResult<usize> {
        bytes_written(
            self.ctx
                .paychannel_keylet(account, destination, seq.cast_signed(), out),
        )
    }

    fn permissioned_domain_keylet(
        &self,
        account: &[u8],
        seq: u32,
        out: &mut [u8],
    ) -> HostResult<usize> {
        bytes_written(
            self.ctx
                .permissioned_domain_keylet(account, seq.cast_signed(), out),
        )
    }

    fn signer_list_keylet(&self, account: &[u8], out: &mut [u8]) -> HostResult<usize> {
        bytes_written(self.ctx.signer_list_keylet(account, out))
    }

    fn ticket_keylet(&self, account: &[u8], seq: u32, out: &mut [u8]) -> HostResult<usize> {
        bytes_written(self.ctx.ticket_keylet(account, seq.cast_signed(), out))
    }

    fn vault_keylet(&self, account: &[u8], seq: u32, out: &mut [u8]) -> HostResult<usize> {
        bytes_written(self.ctx.vault_keylet(account, seq.cast_signed(), out))
    }

    fn sha512_half(&self, data: &[u8], out: &mut [u8]) -> HostResult<usize> {
        bytes_written(self.ctx.sha512_half(data, out))
    }

    fn trace(&self, msg: &str, data_type: TraceDataType, data: &[u8]) -> HostResult<()> {
        self.ctx.trace(msg, data, crossed(data_type));
        Ok(())
    }

    fn update_data(&self, data: &[u8]) -> HostResult<i32> {
        scalar(self.ctx.update_data(data))
    }

    fn get_nft(&self, account: &[u8], nft_id: &[u8], out: &mut [u8]) -> HostResult<usize> {
        bytes_written(self.ctx.get_nft(account, nft_id, out))
    }

    fn get_nft_issuer(&self, nft_id: &[u8], out: &mut [u8]) -> HostResult<usize> {
        bytes_written(self.ctx.get_nft_issuer(nft_id, out))
    }

    fn get_nft_taxon(&self, nft_id: &[u8], out: &mut [u8]) -> HostResult<usize> {
        bytes_written(self.ctx.get_nft_taxon(nft_id, out))
    }

    fn get_nft_flags(&self, nft_id: &[u8]) -> HostResult<i32> {
        scalar(self.ctx.get_nft_flags(nft_id))
    }

    fn get_nft_transfer_fee(&self, nft_id: &[u8]) -> HostResult<i32> {
        scalar(self.ctx.get_nft_transfer_fee(nft_id))
    }

    fn get_nft_sequence(&self, nft_id: &[u8], out: &mut [u8]) -> HostResult<usize> {
        bytes_written(self.ctx.get_nft_sequence(nft_id, out))
    }

    fn float_from_int(&self, x: i64, out: &mut [u8], mode: i32) -> HostResult<usize> {
        bytes_written(self.ctx.float_from_int(x, mode, out))
    }

    fn float_from_uint(&self, x: &[u8], out: &mut [u8], mode: i32) -> HostResult<usize> {
        bytes_written(self.ctx.float_from_uint(x, mode, out))
    }

    fn float_from_stamount(&self, amount: &[u8], out: &mut [u8], mode: i32) -> HostResult<usize> {
        bytes_written(self.ctx.float_from_stamount(amount, mode, out))
    }

    fn float_from_stnumber(&self, number: &[u8], out: &mut [u8], mode: i32) -> HostResult<usize> {
        bytes_written(self.ctx.float_from_stnumber(number, mode, out))
    }

    fn float_to_int(&self, x: &[u8], out: &mut [u8], mode: i32) -> HostResult<usize> {
        bytes_written(self.ctx.float_to_int(x, mode, out))
    }

    fn float_to_mant_exp(
        &self,
        x: &[u8],
        mantissa_out: &mut [u8],
        exponent_out: &mut [u8],
    ) -> HostResult<usize> {
        bytes_written(self.ctx.float_to_mant_exp(x, mantissa_out, exponent_out))
    }

    fn float_from_mant_exp(
        &self,
        mantissa: i64,
        exponent: i32,
        out: &mut [u8],
        mode: i32,
    ) -> HostResult<usize> {
        bytes_written(self.ctx.float_from_mant_exp(mantissa, exponent, mode, out))
    }

    fn float_compare(&self, x: &[u8], y: &[u8]) -> HostResult<i32> {
        scalar(self.ctx.float_compare(x, y))
    }

    fn float_add(&self, x: &[u8], y: &[u8], out: &mut [u8], mode: i32) -> HostResult<usize> {
        bytes_written(self.ctx.float_add(x, y, mode, out))
    }

    fn float_subtract(&self, x: &[u8], y: &[u8], out: &mut [u8], mode: i32) -> HostResult<usize> {
        bytes_written(self.ctx.float_subtract(x, y, mode, out))
    }

    fn float_multiply(&self, x: &[u8], y: &[u8], out: &mut [u8], mode: i32) -> HostResult<usize> {
        bytes_written(self.ctx.float_multiply(x, y, mode, out))
    }

    fn float_divide(&self, x: &[u8], y: &[u8], out: &mut [u8], mode: i32) -> HostResult<usize> {
        bytes_written(self.ctx.float_divide(x, y, mode, out))
    }

    fn float_power(&self, x: &[u8], n: i32, out: &mut [u8], mode: i32) -> HostResult<usize> {
        bytes_written(self.ctx.float_power(x, n, mode, out))
    }
}

fn run_escrow(
    host: &ffi::HostContext,
    wasm: &[u8],
    gas: u64,
    function_name: &str,
) -> ffi::RunResult {
    guarded(
        || {
            let host = CxxHost { ctx: host };
            run(wasm, gas, &host, function_name).into()
        },
        ffi::RunResult::panicked,
    )
}

fn check_escrow(wasm: &[u8], function_name: &str) -> ffi::CheckResult {
    guarded(
        || check(wasm, function_name).into(),
        ffi::CheckResult::panicked,
    )
}

impl ffi::RunResult {
    /// A run the engine panicked in.
    ///
    /// The cost is not reported: a panicking run's meter is not evidence of
    /// anything, and `0` says "unknown" where a number would say "this is what it
    /// owed".
    fn panicked(detail: String) -> ffi::RunResult {
        ffi::RunResult {
            status: ffi::RunStatus::Panic,
            result: 0,
            gas_used: 0,
            detail,
        }
    }
}

impl ffi::CheckResult {
    /// A check the engine panicked in.
    fn panicked(detail: String) -> ffi::CheckResult {
        ffi::CheckResult {
            status: ffi::CheckStatus::Panic,
            detail,
        }
    }
}

/// Run `body`, handing a panic to `panicked` rather than letting it unwind into
/// C++.
///
/// **Why catching here is enough.** An unwind can only be caught where every frame
/// between the panic and the catch is Rust, and every frame here is: the engine and
/// wasmi are Rust, and a host call cannot start a C++ unwind because each
/// `HostContext` method is `noexcept` and catches everything. So the only unwind
/// that can reach this frame started in Rust, and this stops it.
///
/// [`AssertUnwindSafe`] is sound because nothing survives to be observed in a torn
/// state: the store, the linker and the host wrapper are all dropped on the way out,
/// and the one thing that outlives the call — the C++ `HostContext` — is only ever
/// touched through those `noexcept` methods, which either complete or report.
///
/// Generic over the result so both crossings share the one catch: the two answer
/// with different structs, and a second `catch_unwind` is the last thing this file
/// should have two of.
fn guarded<T>(body: impl FnOnce() -> T, panicked: impl FnOnce(String) -> T) -> T {
    catch_unwind(AssertUnwindSafe(body)).unwrap_or_else(|payload| panicked(panic_detail(&*payload)))
}

/// The panic's message, for the log.
///
/// A `panic!` payload is a `&str` or a `String`; anything else is a `panic_any` that
/// nothing below this crate makes, and it still has to produce a line.
fn panic_detail(payload: &(dyn Any + Send)) -> String {
    let message = payload
        .downcast_ref::<&str>()
        .copied()
        .or_else(|| payload.downcast_ref::<String>().map(String::as_str))
        .unwrap_or("payload is not a string");
    format!("panicked: {message}")
}

/// The engine's two-channel result on the one struct cxx can carry.
///
/// A `From` rather than a named function because the mapping is total and there is
/// only one of it: every field of the wire struct is decided by the outcome, so
/// there is no second reading for a name to distinguish.
impl From<Result<RunOutcome, RunFailure>> for ffi::RunResult {
    fn from(result: Result<RunOutcome, RunFailure>) -> ffi::RunResult {
        match result {
            Ok(RunOutcome { result, fuel_used }) => ffi::RunResult {
                status: ffi::RunStatus::Ok,
                result,
                gas_used: fuel_used,
                detail: String::new(),
            },
            // `fuel_used` is carried on both channels by construction, so a failed
            // run reports its cost here without this having to decide what one is.
            Err(RunFailure { error, fuel_used }) => ffi::RunResult {
                status: ffi::RunStatus::from(&error),
                result: 0,
                gas_used: fuel_used,
                detail: error.to_string(),
            },
        }
    }
}

/// The status a [`RunError`] crosses as.
///
/// Exhaustive rather than closed with a wildcard: an outcome added to the engine has
/// to be given a status — and therefore a TER on the far side — before this compiles.
impl From<&RunError> for ffi::RunStatus {
    fn from(error: &RunError) -> ffi::RunStatus {
        match error {
            RunError::Compile(_) => ffi::RunStatus::Compile,
            RunError::Instantiate(_) => ffi::RunStatus::Instantiate,
            RunError::EntryPoint(_) => ffi::RunStatus::EntryPoint,
            RunError::OutOfGas => ffi::RunStatus::OutOfGas,
            RunError::Internal => ffi::RunStatus::Internal,
            RunError::NoMemory => ffi::RunStatus::NoMemory,
            RunError::Trap(_) => ffi::RunStatus::Trap,
        }
    }
}

/// A verdict on the wire. No cost to carry, so `Ok` is the empty description.
impl From<Result<(), CheckError>> for ffi::CheckResult {
    fn from(result: Result<(), CheckError>) -> ffi::CheckResult {
        match result {
            Ok(()) => ffi::CheckResult {
                status: ffi::CheckStatus::Ok,
                detail: String::new(),
            },
            Err(error) => ffi::CheckResult {
                status: ffi::CheckStatus::from(&error),
                detail: error.to_string(),
            },
        }
    }
}

/// The status a [`CheckError`] crosses as, exhaustive for the same reason
/// [`ffi::RunStatus`]'s conversion is.
impl From<&CheckError> for ffi::CheckStatus {
    fn from(error: &CheckError) -> ffi::CheckStatus {
        match error {
            CheckError::Compile(_) => ffi::CheckStatus::Compile,
            CheckError::Import(_) => ffi::CheckStatus::Import,
            CheckError::EntryPoint(_) => ffi::CheckStatus::EntryPoint,
            CheckError::Memory(_) => ffi::CheckStatus::Memory,
            CheckError::Table(_) => ffi::CheckStatus::Table,
        }
    }
}

/// These tests reach none of the `extern "C++"` methods, which is what lets the test
/// binary link at all: the C++ side of the bridge exists only in the CMake build, so
/// a test that called one would fail to link rather than fail.
#[cfg(test)]
mod tests {
    use super::*;

    fn ok(result: i32, fuel_used: u64) -> ffi::RunResult {
        let outcome: Result<RunOutcome, RunFailure> = Ok(RunOutcome { result, fuel_used });
        outcome.into()
    }

    fn failed(error: RunError, fuel_used: u64) -> ffi::RunResult {
        let outcome: Result<RunOutcome, RunFailure> = Err(RunFailure { error, fuel_used });
        outcome.into()
    }

    #[test]
    fn a_completed_run_carries_its_value_and_its_cost() {
        let crossed = ok(5, 1234);

        assert_eq!(crossed.status, ffi::RunStatus::Ok);
        assert_eq!(crossed.result, 5);
        assert_eq!(crossed.gas_used, 1234);
        assert_eq!(crossed.detail, "", "a completed run has nothing to explain");
    }

    /// The cost is the point: a contract that burns its gas and traps is charged.
    #[test]
    fn a_failed_run_carries_its_cost_and_the_engines_own_words() {
        let crossed = failed(RunError::Trap("unreachable".to_string()), 900);

        assert_eq!(crossed.status, ffi::RunStatus::Trap);
        assert_eq!(crossed.gas_used, 900);
        assert_eq!(crossed.detail, "trap: unreachable");
        assert_eq!(crossed.result, 0, "a failed run returned no value");
    }

    /// The `RunError` set as the test *expects* it, not as the conversion reports it:
    /// deriving it from the code under test would make the assertion vacuous.
    fn every_run_error() -> Vec<RunError> {
        vec![
            RunError::Compile(String::new()),
            RunError::Instantiate(String::new()),
            RunError::EntryPoint(String::new()),
            RunError::OutOfGas,
            RunError::Internal,
            RunError::NoMemory,
            RunError::Trap(String::new()),
        ]
    }

    /// Distinct statuses, because the TER map on the far side reads nothing else. Two
    /// outcomes sharing one status would silently collapse two TERs into one.
    #[test]
    fn every_run_error_crosses_as_a_status_of_its_own() {
        let mut seen = Vec::new();
        for error in every_run_error() {
            let status = ffi::RunStatus::from(&error);
            assert!(
                !seen.contains(&status),
                "{error:?} shares {status:?} with an earlier outcome"
            );
            seen.push(status);
        }
    }

    /// `Ok` is the one status no failure may take: the far side reads it as "the
    /// contract returned", and would then read `result` off a run that produced none.
    #[test]
    fn no_failure_crosses_as_success() {
        for error in every_run_error() {
            assert_ne!(
                ffi::RunStatus::from(&error),
                ffi::RunStatus::Ok,
                "{error:?}"
            );
        }
    }

    #[test]
    fn a_panic_becomes_a_status_instead_of_an_unwind() {
        let crossed = guarded(|| panic!("the engine came apart"), ffi::RunResult::panicked);

        assert_eq!(crossed.status, ffi::RunStatus::Panic);
        assert_eq!(crossed.detail, "panicked: the engine came apart");
        assert_eq!(crossed.gas_used, 0, "a panicking run reports no cost");
    }

    /// A formatted `panic!` payload is a `String` rather than a `&str`, so both
    /// downcasts are load-bearing.
    #[test]
    fn a_formatted_panic_keeps_its_message() {
        let overflowed = 3;
        let crossed = guarded(
            || panic!("gas underflowed by {overflowed}"),
            ffi::RunResult::panicked,
        );

        assert_eq!(crossed.detail, "panicked: gas underflowed by 3");
    }

    #[test]
    fn a_panic_with_no_message_still_reports_one() {
        let crossed = guarded(|| std::panic::panic_any(7u32), ffi::RunResult::panicked);

        assert_eq!(crossed.status, ffi::RunStatus::Panic);
        assert_eq!(crossed.detail, "panicked: payload is not a string");
    }

    #[test]
    fn a_run_that_does_not_panic_is_untouched() {
        let crossed = guarded(|| ok(1, 2), ffi::RunResult::panicked);

        assert_eq!(crossed.status, ffi::RunStatus::Ok);
        assert_eq!(crossed.result, 1);
        assert_eq!(crossed.gas_used, 2);
    }

    /// [`crossed`] being exhaustive makes the two lists hold the same *variants*;
    /// this makes them hold the same *numbers*, which is what actually crosses. A
    /// `match` arm pointed at the wrong variant would pass the compiler and fail
    /// here.
    ///
    /// Over `TraceDataType::ALL`, so it is the whole set rather than a sample: a data
    /// type added to the ABI arrives already asserted against the shared enum.
    #[test]
    fn every_data_type_crosses_as_the_same_wire_value() {
        for &data_type in TraceDataType::ALL {
            assert_eq!(
                crossed(data_type).repr,
                data_type.code(),
                "{data_type:?} crosses as a different value than the ABI gives it"
            );
        }
    }

    #[test]
    fn a_negative_answer_is_an_error_code_and_a_length_is_a_length() {
        assert_eq!(bytes_written(32), Ok(32));
        assert_eq!(bytes_written(0), Ok(0));
        assert_eq!(bytes_written(-3), Err(HostError::BufferTooSmall));
        assert_eq!(bytes_written(-14), Err(HostError::NoMemExported));
        assert_eq!(scalar(1), Ok(1));
        assert_eq!(scalar(0), Ok(0));
        assert_eq!(scalar(-2), Err(HostError::FieldNotFound));
    }

    #[test]
    fn a_caught_cxx_exception_arrives_as_internal_fatal() {
        assert_eq!(bytes_written(i32::MIN), Err(HostError::InternalFatal));
    }

    /// A code the ABI does not define goes the same way, so a C++ list this crate has
    /// not caught up with stops the run rather than reaching the guest.
    #[test]
    fn an_undefined_code_arrives_as_internal_fatal() {
        assert_eq!(bytes_written(-21), Err(HostError::InternalFatal));
    }

    // -----------------------------------------------------------------------
    // The check crossing
    //
    // `check_escrow` takes no host, so unlike `run_escrow` it can be called
    // outright here — the modules are hand-written bytes because this crate has
    // no assembler and needs none for two of them.
    // -----------------------------------------------------------------------

    /// The smallest valid module: the eight-byte header and nothing else. It
    /// compiles and imports nothing, so it reaches the entry-point stage.
    const EMPTY_MODULE: [u8; 8] = [0x00, 0x61, 0x73, 0x6d, 0x01, 0x00, 0x00, 0x00];

    #[test]
    fn a_module_that_does_not_compile_crosses_as_compile() {
        let crossed = check_escrow(b"not wasm", "escrow_finish");

        assert_eq!(crossed.status, ffi::CheckStatus::Compile);
        assert!(
            crossed.detail.starts_with("compile: "),
            "{}",
            crossed.detail
        );
    }

    /// The whole crossing, end to end: a real module through the real engine, with
    /// the refusal the C++ side will log.
    #[test]
    fn a_module_without_the_entry_point_crosses_as_entry_point() {
        let crossed = check_escrow(&EMPTY_MODULE, "escrow_finish");

        assert_eq!(crossed.status, ffi::CheckStatus::EntryPoint);
        assert_eq!(crossed.detail, "no entry point 'escrow_finish'");
    }

    /// The `CheckError` set as the test *expects* it, not as the conversion reports
    /// it: deriving it from the code under test would make the assertion vacuous.
    fn every_check_error() -> Vec<CheckError> {
        vec![
            CheckError::Compile(String::new()),
            CheckError::Import(String::new()),
            CheckError::EntryPoint(String::new()),
            CheckError::Memory(String::new()),
            CheckError::Table(String::new()),
        ]
    }

    /// Distinct statuses, because the TER map on the far side reads nothing else.
    #[test]
    fn every_check_error_crosses_as_a_status_of_its_own() {
        let mut seen = Vec::new();
        for error in every_check_error() {
            let status = ffi::CheckStatus::from(&error);
            assert!(
                !seen.contains(&status),
                "{error:?} shares {status:?} with an earlier refusal"
            );
            seen.push(status);
        }
    }

    /// `Ok` is the one status no refusal may take: the far side reads it as
    /// `tesSUCCESS` and would let the module through.
    #[test]
    fn no_refusal_crosses_as_success() {
        for error in every_check_error() {
            assert_ne!(
                ffi::CheckStatus::from(&error),
                ffi::CheckStatus::Ok,
                "{error:?}"
            );
        }
    }

    /// A panic during a check is its own status rather than one more malformed
    /// module: the far side answers a node-local failure, not `temBAD_WASM`.
    #[test]
    fn a_panic_during_a_check_becomes_a_status_instead_of_an_unwind() {
        let crossed = guarded(
            || panic!("the checker came apart"),
            ffi::CheckResult::panicked,
        );

        assert_eq!(crossed.status, ffi::CheckStatus::Panic);
        assert_eq!(crossed.detail, "panicked: the checker came apart");
    }
}
