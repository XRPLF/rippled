//! What screening refuses, and that it refuses nothing a run would have served.
//!
//! `check` reaches its verdict from the compiled module alone, so these tests take
//! no host — except the ones that put the same module through `run` to compare the
//! two.

mod support;

use support::{ENTRY, FakeHost, ONE_PAGE, PLENTY_OF_GAS, assemble, import, module};
use xrpl_host_functions::HostFunctionSpec;
use xrpl_wasm_vm::{CheckError, MAX_MEMORY_PAGES, RunError};

/// Assert which stage screening refused a module at, because the caller maps the
/// stages separately. The error comes back out for the tests that also read its
/// message.
macro_rules! assert_stage {
    ($refusal:expr, $stage:pat) => {{
        let refusal = $refusal;
        assert!(
            matches!(refusal, $stage),
            concat!("expected a ", stringify!($stage), " refusal, got: {}"),
            refusal
        );
        refusal
    }};
}

/// Screens `wat`, which must assemble.
fn check(wat: &str) -> Result<(), CheckError> {
    xrpl_wasm_vm::check(&assemble(wat), ENTRY)
}

fn refusal(wat: &str) -> CheckError {
    check(wat).expect_err(&format!("expected this module to be refused:\n{wat}"))
}

fn passes(wat: &str) {
    if let Err(refusal) = check(wat) {
        panic!("expected this module to pass, but: {refusal}\n{wat}");
    }
}

// ---------------------------------------------------------------------------
// Compiling
// ---------------------------------------------------------------------------

/// A contract that imports a host function, exports its memory and exports the
/// entry point is what screening is looking for.
#[test]
fn a_runnable_contract_passes() {
    passes(&module(
        &[import::LDGR_INDEX, ONE_PAGE],
        "(call $ldgr_index (i32.const 0) (i32.const 4))",
    ));
}

/// Bytes that are not a wasm module at all.
#[test]
fn garbage_does_not_pass() {
    for bytes in [b"".as_slice(), b"not wasm", &[0x00, 0x61, 0x73, 0x6d]] {
        let refusal = xrpl_wasm_vm::check(bytes, ENTRY).expect_err("garbage must not pass");
        assert_stage!(refusal, CheckError::Compile(_));
    }
}

/// Screening takes wasm binaries, and text is not one — the same rule the VM
/// applies, from the same `wasmi` built without its `wat` feature. Turning that
/// feature on would make this transaction blob valid at both ends.
#[test]
fn a_text_format_module_does_not_pass() {
    let text = module(&[ONE_PAGE], "(i32.const 0)");

    let refusal =
        xrpl_wasm_vm::check(text.as_bytes(), ENTRY).expect_err("text must not pass as a module");
    assert_stage!(refusal, CheckError::Compile(_));

    // The same module, assembled first, passes: the text is sound and only the
    // format was refused.
    passes(&text);
}

/// A feature the engine disables is refused here too, because both stages compile
/// against the one engine. `vm_limits.rs` walks every disabled feature; this pins
/// that screening sees the same configuration.
#[test]
fn a_disabled_feature_does_not_pass() {
    let refusal = refusal(&module(
        &[ONE_PAGE],
        "(drop (f64.add (f64.const 1) (f64.const 2))) (i32.const 0)",
    ));
    let refusal = assert_stage!(refusal, CheckError::Compile(_)).to_string();
    assert!(refusal.contains("floating-point"), "{refusal}");
}

// ---------------------------------------------------------------------------
// Imports
// ---------------------------------------------------------------------------

/// Every host function the ABI declares, spelled as a guest imports it. The count
/// is asserted against the ABI so a function added to it cannot be left out here.
const ALL_IMPORTS: [&str; 62] = [
    import::LDGR_INDEX,
    import::PARENT_LDGR_TIME,
    import::PARENT_LDGR_HASH,
    import::BASE_FEE,
    import::AMENDMENT_ENABLED,
    import::CACHE_LE,
    import::TX_FIELD,
    import::HOME_LE_FIELD,
    import::LE_FIELD,
    import::TX_INNER,
    import::HOME_LE_INNER,
    import::LE_INNER,
    import::TX_ARR_LEN,
    import::HOME_LE_ARR_LEN,
    import::LE_ARR_LEN,
    import::TX_INNER_ARR_LEN,
    import::HOME_LE_INNER_ARR_LEN,
    import::LE_INNER_ARR_LEN,
    import::CHECK_SIG,
    import::ACCOUNTROOT_ID,
    import::AMM_ID,
    import::CHECK_ID,
    import::CREDENTIAL_ID,
    import::DELEGATE_ID,
    import::DEPOSIT_PREAUTH_ID,
    import::DID_ID,
    import::ESCROW_ID,
    import::TRUSTLINE_ID,
    import::MPT_ISSUANCE_ID,
    import::MPTOKEN_ID,
    import::NFT_OFFER_ID,
    import::OFFER_ID,
    import::ORACLE_ID,
    import::PAYCHAN_ID,
    import::PERMISSIONED_DOMAIN_ID,
    import::SIGNERS_ID,
    import::TICKET_ID,
    import::VAULT_ID,
    import::SHA512_HALF,
    import::TRACE,
    import::TRACE_NUM,
    import::SET_DATA,
    import::NFT_URI,
    import::NFT_ISSUER,
    import::NFT_TAXON,
    import::NFT_FLAGS,
    import::NFT_XFER_FEE,
    import::NFT_SERIAL,
    import::FLOAT_FROM_INT,
    import::FLOAT_FROM_UINT,
    import::FLOAT_FROM_STAMOUNT,
    import::FLOAT_FROM_STNUMBER,
    import::FLOAT_TO_INT,
    import::FLOAT_TO_MANT_EXP,
    import::FLOAT_FROM_MANT_EXP,
    import::FLOAT_CMP,
    import::FLOAT_ADD,
    import::FLOAT_SUB,
    import::FLOAT_MULT,
    import::FLOAT_DIV,
    import::FLOAT_ROOT,
    import::FLOAT_POW,
];

#[test]
fn every_declared_host_function_may_be_imported() {
    assert_eq!(
        ALL_IMPORTS.len(),
        HostFunctionSpec::ALL.len(),
        "the ABI gained a host function with no import declaration in this test"
    );

    let mut parts = ALL_IMPORTS.to_vec();
    parts.push(ONE_PAGE);
    passes(&module(&parts, "(i32.const 0)"));
}

/// A module may import fewer host functions than are registered, but not more.
#[test]
fn an_unknown_host_function_does_not_pass() {
    let refusal = refusal(&module(
        &[
            r#"(import "host_lib" "no_such_function" (func $f (param i32) (result i32)))"#,
            ONE_PAGE,
        ],
        "(call $f (i32.const 0))",
    ));
    let refusal = assert_stage!(refusal, CheckError::Import(_)).to_string();
    assert!(
        refusal.contains("no host function 'no_such_function'"),
        "{refusal}"
    );
}

/// Host functions live under one module name — `host_lib` — and an import naming
/// another is refused even when the function name is real. `env` is in the list
/// because that is what plain clang emits.
#[test]
fn an_import_from_another_module_does_not_pass() {
    for module_name in ["host", "env", ""] {
        let refusal = refusal(&module(
            &[
                &format!(
                    r#"(import "{module_name}" "ldgr_index" (func $f (param i32 i32) (result i32)))"#
                ),
                ONE_PAGE,
            ],
            "(call $f (i32.const 0) (i32.const 4))",
        ));
        let refusal = assert_stage!(refusal, CheckError::Import(_)).to_string();
        assert!(refusal.contains("is not from 'host_lib'"), "{refusal}");
    }
}

/// A host function's name imported as something other than a function. The engine
/// defines it as a function and nothing else, so this does not link either.
#[test]
fn a_host_function_imported_as_a_global_does_not_pass() {
    let refusal = refusal(&module(
        &[
            r#"(import "host_lib" "ldgr_index" (global $g i32))"#,
            ONE_PAGE,
        ],
        "(global.get $g)",
    ));
    let refusal = assert_stage!(refusal, CheckError::Import(_)).to_string();
    assert!(
        refusal.contains("'host_lib::ldgr_index' is not a function"),
        "{refusal}"
    );
}

/// A module faulty at two stages is refused by the earlier one — it imports what no
/// engine serves *and* exports no entry point. The imports are what the rest of the
/// module depends on, so that is the message worth having.
#[test]
fn the_earlier_stage_is_the_one_reported() {
    let refusal = refusal(
        r#"(module
             (import "host_lib" "no_such_function" (func $f (result i32)))
             (memory (export "memory") 1)
             (func (export "not_the_entry_point") (result i32) (call $f)))"#,
    );

    assert_stage!(refusal, CheckError::Import(_));
}

/// The signature is the one part of an import screening does not compare, so a
/// module that will not link can still pass. Recorded here because it is the gap
/// this stage leaves, not because it is wanted.
#[test]
fn an_import_with_the_wrong_signature_still_passes() {
    let wat = module(
        &[
            r#"(import "host_lib" "ldgr_index" (func $f (param i64 i64) (result i32)))"#,
            ONE_PAGE,
        ],
        "(i32.const 0)",
    );
    passes(&wat);

    let host = FakeHost::new();
    let failure = xrpl_wasm_vm::run(&assemble(&wat), PLENTY_OF_GAS, &host, ENTRY)
        .expect_err("a mistyped import must not link");
    assert!(
        matches!(failure.error, RunError::Instantiate(_)),
        "{failure}"
    );
}

// ---------------------------------------------------------------------------
// The entry point
// ---------------------------------------------------------------------------

#[test]
fn a_missing_entry_point_does_not_pass() {
    let refusal = refusal(
        r#"(module (memory (export "memory") 1)
                              (func (export "other") (result i32) (i32.const 0)))"#,
    );
    let refusal = assert_stage!(refusal, CheckError::EntryPoint(_)).to_string();
    assert_eq!(refusal, "no entry point 'finish'");
}

/// The entry point is looked up by the name the caller asks for, as a run looks it
/// up: screening a contract for one entry point says nothing about another.
#[test]
fn the_entry_point_is_the_name_the_caller_gives() {
    let wasm = assemble(
        r#"(module (memory (export "memory") 1)
             (func (export "other") (result i32) (i32.const 0)))"#,
    );

    assert!(xrpl_wasm_vm::check(&wasm, "other").is_ok());
    assert!(xrpl_wasm_vm::check(&wasm, ENTRY).is_err());
}

/// Both halves of the entry point's type are screened: a module returning the
/// wrong thing, or taking anything at all, would fail the run's typed lookup.
#[test]
fn an_entry_point_of_the_wrong_type_does_not_pass() {
    for (signature, body) in [
        ("(result i64)", "(i64.const 0)"),
        ("(param i32) (result i32)", "(i32.const 0)"),
        ("", "(nop)"),
    ] {
        let refusal = refusal(&format!(
            r#"(module (memory (export "memory") 1)
                 (func (export "finish") {signature} {body}))"#
        ));
        let refusal = assert_stage!(refusal, CheckError::EntryPoint(_)).to_string();
        assert_eq!(
            refusal, "entry point 'finish' has the wrong signature, expected '() -> i32'",
            "{signature}"
        );
    }
}

/// An export of the entry point's name that is not a function at all is a third
/// case, and named as such: nothing is missing and no signature is wrong.
#[test]
fn an_entry_point_that_is_not_a_function_does_not_pass() {
    let refusal = refusal(
        r#"(module (memory (export "memory") 1) (global (export "finish") i32 (i32.const 0)))"#,
    );
    let refusal = assert_stage!(refusal, CheckError::EntryPoint(_)).to_string();
    assert_eq!(refusal, "export 'finish' is not a function");
}

// ---------------------------------------------------------------------------
// Agreement with a run
// ---------------------------------------------------------------------------

/// A module with no linear memory to export passes. A contract that makes no host
/// call needs none, and one that does is refused at the call and charged — a
/// runtime fault, not a malformed module.
#[test]
fn a_module_exporting_no_memory_passes() {
    let wat = r#"(module (func (export "finish") (result i32) (i32.const 0)))"#;
    passes(wat);

    let host = FakeHost::new();
    assert_eq!(
        xrpl_wasm_vm::run(&assemble(wat), PLENTY_OF_GAS, &host, ENTRY)
            .expect("a module that calls no host function needs no memory")
            .result,
        0
    );
}

/// Modules spanning what screening decides, each also put through a run.
fn modules() -> Vec<(&'static str, String)> {
    vec![
        (
            "a runnable contract",
            module(&[import::LDGR_INDEX, ONE_PAGE], "(i32.const 0)"),
        ),
        (
            "a contract that traps",
            module(&[ONE_PAGE], "(unreachable)"),
        ),
        (
            "a disabled feature",
            module(&[ONE_PAGE], "(i32.extend8_s (i32.const 1))"),
        ),
        (
            "an unknown host function",
            module(
                &[
                    r#"(import "host_lib" "nope" (func $f (result i32)))"#,
                    ONE_PAGE,
                ],
                "(call $f)",
            ),
        ),
        (
            "an import from another module",
            module(
                &[
                    r#"(import "env" "ldgr_index" (func $f (param i32 i32) (result i32)))"#,
                    ONE_PAGE,
                ],
                "(i32.const 0)",
            ),
        ),
        (
            "a host function imported as a global",
            module(
                &[r#"(import "host_lib" "trace" (global $g i32))"#, ONE_PAGE],
                "(global.get $g)",
            ),
        ),
        (
            "no entry point",
            r#"(module (memory (export "memory") 1)
                 (func (export "other") (result i32) (i32.const 0)))"#
                .to_string(),
        ),
        (
            "an entry point of the wrong type",
            r#"(module (memory (export "memory") 1)
                 (func (export "finish") (result i64) (i64.const 0)))"#
                .to_string(),
        ),
    ]
}

/// Screening refuses a module exactly when a run would refuse it at one of the
/// three stages screening covers — nothing it rejects would have run, and nothing
/// it passes stops before the entry point is called. The exceptions are the ones
/// [`what_static_screening_cannot_see`] lists.
#[test]
fn screening_and_a_run_agree() {
    let host = FakeHost::new();

    for (label, wat) in modules() {
        let wasm = assemble(&wat);
        let refused_early = match xrpl_wasm_vm::run(&wasm, PLENTY_OF_GAS, &host, ENTRY) {
            Err(failure) => matches!(
                failure.error,
                RunError::Compile(_) | RunError::Instantiate(_) | RunError::EntryPoint(_)
            ),
            Ok(_) => false,
        };

        assert_eq!(
            xrpl_wasm_vm::check(&wasm, ENTRY).is_err(),
            refused_early,
            "{label}"
        );
    }
}

/// A module asking for more memory than the engine grants is refused, so the
/// contract that could never run does not reach the ledger. The cap itself passes.
#[test]
fn an_exported_memory_past_the_cap_does_not_pass() {
    let wat = module(
        &[&format!(
            r#"(memory (export "memory") {})"#,
            MAX_MEMORY_PAGES + 1
        )],
        "(i32.const 0)",
    );
    let refusal = assert_stage!(refusal(&wat), CheckError::Memory(_)).to_string();
    assert!(refusal.contains("past the 128-page cap"), "{refusal}");

    passes(&module(
        &[&format!(r#"(memory (export "memory") {MAX_MEMORY_PAGES})"#)],
        "(i32.const 0)",
    ));
}

/// A declared *maximum* past the cap is legal and simply unreachable, so screening
/// must not turn it away: `vm_limits` runs this very module to completion.
#[test]
fn a_declared_maximum_past_the_cap_still_passes() {
    passes(&module(
        &[&format!(
            r#"(memory (export "memory") 1 {})"#,
            MAX_MEMORY_PAGES + 1
        )],
        "(i32.const 0)",
    ));
}

/// The gap, listed rather than described, and now one entry long. A memory a module
/// keeps to itself is not in its exports, so this is the one module that passes
/// screening and then fails to *instantiate* — which is why a run's refusal at that
/// stage cannot be read as the node's fault.
///
/// A contract needs an exported memory to make any host call, so a module of this
/// shape can do nothing but compute; the SDK does not produce one.
#[test]
fn what_static_screening_cannot_see() {
    let host = FakeHost::new();
    let wat = format!(
        r#"(module (memory {})
             (func (export "finish") (result i32) (i32.const 0)))"#,
        MAX_MEMORY_PAGES + 1
    );

    passes(&wat);

    let failure = xrpl_wasm_vm::run(&assemble(&wat), PLENTY_OF_GAS, &host, ENTRY)
        .expect_err("the store's limiter must refuse the memory");
    assert!(
        matches!(failure.error, RunError::Instantiate(_)),
        "{failure}"
    );
}

/// A start section is guest code, so screening cannot see whether it traps — but it
/// no longer has to. A trap is the guest's fault wherever it happens, so the run
/// charges the contract for what it burned instead of reporting a module the node
/// should have screened.
#[test]
fn a_start_section_screening_cannot_see_is_charged_as_a_trap() {
    let host = FakeHost::new();
    let wat = format!(
        r#"(module {ONE_PAGE}
             (func $init (unreachable))
             (start $init)
             (func (export "finish") (result i32) (i32.const 0)))"#
    );

    passes(&wat);

    let failure = xrpl_wasm_vm::run(&assemble(&wat), PLENTY_OF_GAS, &host, ENTRY)
        .expect_err("a start section that traps must not complete the run");
    assert!(matches!(failure.error, RunError::Trap(_)), "{failure}");
    assert!(
        failure.fuel_used > 0,
        "charged for what it burned: {failure}"
    );
}
