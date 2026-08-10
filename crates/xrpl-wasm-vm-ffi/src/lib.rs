//! The cxx bridge between the escrow wasm engine and xrpld.
//!
//! Three crossings. C++ calls `run_escrow` once per escrow finish; the engine's host
//! calls come back out through the C++ `HostContext`, which `CxxHost` presents to the
//! engine as an ordinary [`HostFunctions`] implementor. The ABI those calls speak is
//! declared once, in `xrpl-host-functions`, so neither side of this file gets to
//! restate a signature.
//!
//! `check_escrow` is the third, and it crosses in one direction only: screening a
//! module needs no host, so nothing comes back out.
//!
//! **Neither direction may unwind into the other**, and the two halves of that are
//! not symmetric:
//!
//! - A **Rust panic** is caught here, by `guarded`. Letting one reach C++ is
//!   undefined behaviour; `[profile.release]` turns overflow checks on, so this is a
//!   live path and not a formality.
//! - A **C++ exception** is stopped on the C++ side: every `HostContext` method is
//!   `noexcept` and reports failure as a negative `HostError` code. That is what
//!   makes `guarded` sufficient — see its documentation.
//!
//! Everything hand-written here is private, so the names above are code spans rather
//! than links, and `cargo doc` needs `--document-private-items` to show any of it.
//! That is also why this crate, unlike `xrpl-wasm-vm`, does not
//! `deny(unreachable_pub)`: cxx's expansion is `pub` throughout by necessity, leaving
//! the lint nothing but generated code to fire on.
#![deny(rustdoc::broken_intra_doc_links)]

use std::any::Any;
use std::panic::{AssertUnwindSafe, catch_unwind};
use xrpl_host_functions::{HostError, HostFunctions, HostResult};
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
        /// Every method is `noexcept` and answers with a code, so a host call cannot
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
        #[cxx_name = "sha512Half"]
        fn sha512_half(self: &HostContext, data: &[u8], out: &mut [u8]) -> i32;

        /// A call with no value to report answers `0`, or a negative `HostError`
        /// code.
        #[namespace = "xrpl"]
        fn trace(self: &HostContext, msg: &str, data: &[u8], as_hex: bool) -> i32;

        #[namespace = "xrpl"]
        #[cxx_name = "traceNum"]
        fn trace_num(self: &HostContext, msg: &str, number: i64) -> i32;
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
/// Named functions rather than `From` impls, and not by preference: every type
/// involved — `i32`, `Result`, `HostError` — is foreign to this crate, so the orphan
/// rule forbids the impl. Two readings of the same `i32` would want distinguishing
/// names here in any case.
fn bytes_written(n: i32) -> HostResult<usize> {
    usize::try_from(n).map_err(|_| HostError::from_code(n))
}

/// A call with nothing to report: any non-negative answer is success.
fn reported(n: i32) -> HostResult<()> {
    if n < 0 {
        return Err(HostError::from_code(n));
    }
    Ok(())
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

    fn sha512_half(&self, data: &[u8], out: &mut [u8]) -> HostResult<usize> {
        bytes_written(self.ctx.sha512_half(data, out))
    }

    fn trace(&self, msg: &str, data: &[u8], as_hex: bool) -> HostResult<()> {
        reported(self.ctx.trace(msg, data, as_hex))
    }

    fn trace_num(&self, msg: &str, number: i64) -> HostResult<()> {
        reported(self.ctx.trace_num(msg, number))
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
/// `HostContext` method is `noexcept` and answers with a code. So the only unwind
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

    #[test]
    fn a_negative_answer_is_an_error_code_and_a_length_is_a_length() {
        assert_eq!(bytes_written(32), Ok(32));
        assert_eq!(bytes_written(0), Ok(0));
        assert_eq!(bytes_written(-3), Err(HostError::BufferTooSmall));
        assert_eq!(reported(0), Ok(()));
        assert_eq!(reported(-14), Err(HostError::NoMemExported));
        assert_eq!(scalar(1), Ok(1));
        assert_eq!(scalar(0), Ok(0));
        assert_eq!(scalar(-2), Err(HostError::FieldNotFound));
    }

    /// An exception caught on the C++ side arrives as `-1`, which has to reach the
    /// engine as a *fatal* error so the run stops and the transaction is
    /// `tecINTERNAL` — not as a code handed to the contract to interpret.
    #[test]
    fn a_caught_cxx_exception_arrives_as_internal() {
        assert_eq!(bytes_written(-1), Err(HostError::Internal));
        assert_eq!(reported(-1), Err(HostError::Internal));
        assert_eq!(scalar(-1), Err(HostError::Internal));
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
