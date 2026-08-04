//! What the engine refuses outright: modules it will not compile, will not
//! instantiate, or cannot find an entry point in — plus the linear-memory cap.
//!
//! These are the sandbox's outer wall. Everything here fails the run rather than
//! returning a code to the guest, so each test reads the failure's message.

mod support;

use support::{
    FakeHost, ONE_PAGE, PLENTY_OF_GAS, failure, import, module, run, run_entry, run_with_gas,
};
use xrpl_wasm_vm::{MAX_MEMORY_PAGES, RunError};

/// Assert which stage a run failed at, because the caller maps the stages to
/// different outcomes. A stage is one `RunError` variant, so the expectation is a
/// pattern; the failure comes back out for the tests that also read its message.
macro_rules! assert_stage {
    ($failure:expr, $stage:pat) => {{
        let failure = $failure;
        assert!(
            matches!(failure.error, $stage),
            concat!("expected a ", stringify!($stage), " failure, got: {}"),
            failure
        );
        failure
    }};
}

// ---------------------------------------------------------------------------
// Linear memory
// ---------------------------------------------------------------------------

/// A module declaring more than the cap fails to instantiate — the limit applies
/// to the initial memory, not only to growth.
#[test]
fn an_initial_memory_past_the_cap_is_refused() {
    let host = FakeHost::new();

    let wat = module(
        &[&format!(
            r#"(memory (export "memory") {})"#,
            MAX_MEMORY_PAGES + 1
        )],
        "(i32.const 0)",
    );
    assert_stage!(failure(&wat, &host), RunError::Instantiate(_));
}

/// The cap itself is allowed.
#[test]
fn an_initial_memory_at_the_cap_is_allowed() {
    let host = FakeHost::new();

    let wat = module(
        &[&format!(r#"(memory (export "memory") {MAX_MEMORY_PAGES})"#)],
        "(i32.const 0)",
    );
    assert_eq!(run(&wat, &host).expect("should run").result, 0);
}

/// Growth up to the cap succeeds; growth past it traps rather than answering -1 as
/// `memory.grow` otherwise would, because the engine's limiter sets
/// `trap_on_grow_failure(true)`.
#[test]
fn growth_stops_at_the_cap() {
    let host = FakeHost::new();

    let wat = module(
        &[ONE_PAGE],
        &format!("(memory.grow (i32.const {}))", MAX_MEMORY_PAGES - 1),
    );
    assert_eq!(
        run(&wat, &host).expect("should run").result,
        1,
        "growing to exactly the cap answers the previous size"
    );

    let wat = module(
        &[ONE_PAGE],
        &format!("(memory.grow (i32.const {MAX_MEMORY_PAGES}))"),
    );
    assert_stage!(failure(&wat, &host), RunError::Trap(_));
}

/// A module may declare a maximum above the cap: the cap is enforced on the initial
/// memory and on growth, not on the memory type's declared bound.
#[test]
fn a_declared_maximum_past_the_cap_is_allowed_but_unreachable() {
    let host = FakeHost::new();
    let memory = format!(r#"(memory (export "memory") 1 {})"#, MAX_MEMORY_PAGES + 1);

    let wat = module(&[&memory], "(i32.const 0)");
    assert_eq!(run(&wat, &host).expect("should run").result, 0);

    let wat = module(
        &[&memory],
        &format!("(memory.grow (i32.const {MAX_MEMORY_PAGES}))"),
    );
    assert_stage!(failure(&wat, &host), RunError::Trap(_));
}

// ---------------------------------------------------------------------------
// Engine configuration
// ---------------------------------------------------------------------------

/// One row per feature `build_wasm_engine` turns off: the smallest module that uses
/// it, and the fragment of wasmi's refusal that names the feature. A row declaring
/// its own memory omits [`ONE_PAGE`], or it is refused for having two memories
/// instead.
fn disabled_features() -> Vec<(&'static str, Vec<&'static str>, &'static str, &'static str)> {
    vec![
        (
            "wasm_multi_value",
            vec![
                ONE_PAGE,
                "(func $two (result i32 i32) (i32.const 1) (i32.const 2))",
            ],
            "(call $two) (drop) (drop) (i32.const 0)",
            "multi-value",
        ),
        (
            "wasm_sign_extension",
            vec![ONE_PAGE],
            "(i32.extend8_s (i32.const 1))",
            "sign extension",
        ),
        (
            "wasm_bulk_memory",
            vec![ONE_PAGE],
            "(memory.fill (i32.const 0) (i32.const 0) (i32.const 1)) (i32.const 0)",
            "bulk memory",
        ),
        (
            "wasm_reference_types",
            vec![ONE_PAGE, "(table 1 externref)"],
            "(i32.const 0)",
            "reference types",
        ),
        // The proposal covers mutable globals crossing the module boundary; an
        // internal one is core wasm and stays allowed — see the test below.
        (
            "wasm_mutable_global",
            vec![ONE_PAGE, r#"(global (export "g") (mut i32) (i32.const 0))"#],
            "(i32.const 0)",
            "mutable global",
        ),
        (
            "wasm_tail_call",
            vec![ONE_PAGE, "(func $f (result i32) (i32.const 0))"],
            "(return_call $f)",
            "tail call",
        ),
        // Arithmetic in a constant initialiser. wasmi names the operator rather
        // than the proposal here.
        (
            "wasm_extended_const",
            vec![
                ONE_PAGE,
                "(global $g i32 (i32.add (i32.const 1) (i32.const 2)))",
            ],
            "(global.get $g)",
            "non-constant operator",
        ),
        (
            "wasm_multi_memory",
            vec![ONE_PAGE, "(memory 1)"],
            "(i32.const 0)",
            "multiple memories",
        ),
        (
            "wasm_memory64",
            vec![r#"(memory (export "memory") i64 1)"#],
            "(i32.const 0)",
            "memory64",
        ),
        (
            "wasm_custom_page_sizes",
            vec![r#"(memory (export "memory") 1 (pagesize 1))"#],
            "(i32.const 0)",
            "custom page sizes",
        ),
        (
            "wasm_wide_arithmetic",
            vec![ONE_PAGE],
            "(drop (i64.add128 (i64.const 1) (i64.const 2) (i64.const 3) (i64.const 4)))
             (i32.const 0)",
            "wide arithmetic",
        ),
        // Determinism across nodes is the reason floats are off.
        (
            "floats",
            vec![ONE_PAGE],
            "(drop (f64.add (f64.const 1) (f64.const 2))) (i32.const 0)",
            "floating-point",
        ),
    ]
}

/// Every feature the engine disables is refused, and refused for that reason.
///
/// `wasm_custom_page_sizes` and `wasm_wide_arithmetic` are off by default in wasmi
/// 1.1 (`engine/config.rs:72,74`), so their rows guard against wasmi changing that
/// default rather than against our own config.
#[test]
fn every_disabled_feature_is_refused_by_name() {
    let host = FakeHost::new();

    for (knob, parts, body, expected) in disabled_features() {
        let wat = module(&parts, body);
        let failure = assert_stage!(failure(&wat, &host), RunError::Compile(_)).to_string();

        assert!(
            failure.contains(expected),
            "{knob}: expected a refusal mentioning {expected:?}, got: {failure}"
        );
    }
}

/// The three knobs [`every_disabled_feature_is_refused_by_name`] cannot cover. The
/// engine is a process-wide `LazyLock`, so a test observes the one configuration we
/// build: a knob masked by another, or with no caller-visible effect, has no
/// distinguishing module.
#[test]
fn the_knobs_without_a_module_of_their_own() {
    let host = FakeHost::new();

    // `wasm_saturating_float_to_int(false)`: every saturating conversion takes a
    // float operand, so `floats(false)` refuses it first, as the message shows.
    let wat = module(&[ONE_PAGE], "(i32.trunc_sat_f32_s (f32.const 1))");
    let refusal = failure(&wat, &host).to_string();
    assert!(refusal.contains("floating-point"), "{refusal}");
    assert!(!refusal.contains("saturating"), "{refusal}");

    // `ignore_custom_sections(true)`: governs whether wasmi retains custom
    // sections, not accept/reject, so this pins only that one is harmless.
    let wat = module(
        &[ONE_PAGE, r#"(@custom "note" "ignored")"#],
        "(i32.const 0)",
    );
    assert_eq!(run(&wat, &host).expect("should run").result, 0);

    // `consume_fuel(true)`: with it off, `Store::set_fuel` fails and `run` returns
    // before instantiating, so every test in the suite fails.
    let wat = module(&[ONE_PAGE], "(i32.const 0)");
    assert!(run(&wat, &host).expect("should run").fuel_used > 0);
}

/// A mutable global the module keeps to itself is core wasm, so the disabled
/// proposal does not reach it: a guest can still have mutable state.
#[test]
fn an_internal_mutable_global_is_still_allowed() {
    let host = FakeHost::new();

    let wat = module(
        &[ONE_PAGE, "(global $g (mut i32) (i32.const 0))"],
        "(global.set $g (i32.const 7)) (global.get $g)",
    );
    assert_eq!(run(&wat, &host).expect("should run").result, 7);
}

/// Bytes that are not a wasm module at all.
#[test]
fn garbage_does_not_compile() {
    let host = FakeHost::new();

    for bytes in [b"".as_slice(), b"not wasm", &[0x00, 0x61, 0x73, 0x6d]] {
        let failure = xrpl_wasm_vm::run(bytes, PLENTY_OF_GAS, &host, support::ENTRY)
            .expect_err("garbage must not compile");
        assert_stage!(failure, RunError::Compile(_));
    }
}

/// The VM takes wasm binaries, and text is not one. wasmi's `wat` feature is on by
/// default and would have `Module::new` assemble text too, so the crate builds
/// wasmi without it; turning it back on would make this transaction blob valid.
#[test]
fn the_vm_refuses_a_text_format_module() {
    let host = FakeHost::new();
    let text = module(&[ONE_PAGE], "(i32.const 0)");

    let failure = xrpl_wasm_vm::run(text.as_bytes(), PLENTY_OF_GAS, &host, support::ENTRY)
        .expect_err("text must not compile as a module");
    assert_stage!(failure, RunError::Compile(_));

    // The same module, assembled first, runs: the text is sound and only the
    // format was refused.
    assert_eq!(run(&text, &host).expect("should run").result, 0);
}

// ---------------------------------------------------------------------------
// Imports
// ---------------------------------------------------------------------------

/// A module may import fewer host functions than are registered, but not more:
/// an import the linker does not define fails instantiation.
#[test]
fn an_unknown_import_fails_instantiation() {
    let host = FakeHost::new();

    let wat = module(
        &[
            r#"(import "host_lib" "no_such_function" (func $f (param i32) (result i32)))"#,
            ONE_PAGE,
        ],
        "(call $f (i32.const 0))",
    );
    assert_stage!(failure(&wat, &host), RunError::Instantiate(_));
}

/// Host functions are registered under one module name — `host_lib`, the name the
/// guest SDK and this repo's fixtures import from — and a guest naming a different
/// one does not link. `env` is in the list because that is what plain clang emits.
#[test]
fn the_import_module_name_must_match() {
    let host = FakeHost::new();

    for module_name in ["host", "env", ""] {
        let wat = module(
            &[
                &format!(
                    r#"(import "{module_name}" "ldgr_index" (func $f (param i32 i32) (result i32)))"#
                ),
                ONE_PAGE,
            ],
            "(call $f (i32.const 0) (i32.const 4))",
        );
        assert_stage!(failure(&wat, &host), RunError::Instantiate(_));
    }
}

/// An import spelled with the wrong signature does not link even under the right
/// name, which is what makes the registered signatures load-bearing.
#[test]
fn an_import_with_the_wrong_signature_fails_instantiation() {
    let host = FakeHost::new();

    for signature in [
        "(param i32) (result i32)",         // too few parameters
        "(param i32 i32 i32) (result i32)", // too many
        "(param i64 i64) (result i32)",     // wrong parameter types
        "(param i32 i32) (result i64)",     // wrong result type
        "(param i32 i32)",                  // no result
    ] {
        let wat = module(
            &[
                &format!(r#"(import "host_lib" "ldgr_index" (func $f {signature}))"#),
                ONE_PAGE,
            ],
            "(i32.const 0)",
        );
        assert_stage!(failure(&wat, &host), RunError::Instantiate(_));
    }
}

/// A module that imports a host function it never calls still has to link.
#[test]
fn an_unused_import_is_still_linked() {
    let host = FakeHost::new();

    let wat = module(
        &[import::LDGR_INDEX, import::TRACE, ONE_PAGE],
        "(i32.const 0)",
    );
    assert_eq!(run(&wat, &host).expect("should run").result, 0);
}

// ---------------------------------------------------------------------------
// The start section
// ---------------------------------------------------------------------------

/// A start section runs guest code during instantiation, before the entry point
/// is even looked up, and `set_fuel` and the memory limiter are both installed by
/// then — so it is metered like any other guest code, and a run it stops is
/// charged for what it burned.
///
/// Reported as a **trap**, not as a module that would not instantiate: a trap is the
/// guest's fault wherever it happens, and the stage a run stopped at is not what the
/// caller maps. Filing it under the stage would put a contract's own defect among the
/// faults a caller treats as the node's, and charge nothing for the instructions the
/// contract burned reaching it.
#[test]
fn a_trapping_start_section_is_a_guest_trap_and_is_charged() {
    let host = FakeHost::new();

    let wat = format!(
        r#"(module {ONE_PAGE}
             (func $init (unreachable))
             (start $init)
             (func (export "finish") (result i32) (i32.const 0)))"#
    );
    let failure = assert_stage!(
        run_with_gas(&wat, PLENTY_OF_GAS, &host)
            .expect_err("a start section that traps must not complete the run"),
        RunError::Trap(_)
    );
    assert!(
        failure.fuel_used > 0,
        "the start section's instructions are metered: {failure}"
    );
}

/// What `RunError::Instantiate` is left to mean: a module the linker or the store
/// would not accept, rather than one whose guest code failed. Its two shapes, so the
/// variant is not left standing for nothing.
#[test]
fn instantiation_failure_is_a_module_the_engine_will_not_accept() {
    let host = FakeHost::new();

    // The linker defines no such import.
    let wat = module(
        &[
            r#"(import "host_lib" "no_such_function" (func $f (result i32)))"#,
            ONE_PAGE,
        ],
        "(call $f)",
    );
    assert_stage!(failure(&wat, &host), RunError::Instantiate(_));

    // The store's limiter will not grant the memory, and does not trap to say so.
    let wat = module(
        &[&format!("(memory {})", MAX_MEMORY_PAGES + 1)],
        "(i32.const 0)",
    );
    assert_stage!(failure(&wat, &host), RunError::Instantiate(_));
}

/// A start section that runs out of gas is reported as out of gas, not as a module
/// that would not instantiate. The stage a run stopped at is not what the caller
/// maps — the reason is — and gas exhaustion is one outcome wherever the guest
/// reaches it.
#[test]
fn a_start_section_that_exhausts_gas_is_out_of_gas_not_an_instantiation_failure() {
    const GAS: u64 = 10_000;

    let host = FakeHost::new();
    let wat = format!(
        r#"(module {ONE_PAGE}
             (func $init (loop $l (br $l)))
             (start $init)
             (func (export "finish") (result i32) (i32.const 0)))"#
    );

    let failure = assert_stage!(
        run_with_gas(&wat, GAS, &host).expect_err("an endless start section must not instantiate"),
        RunError::OutOfGas
    );
    assert_eq!(
        failure.fuel_used, GAS,
        "a runaway start section burns the whole limit"
    );
}

/// A start section cannot make a host call that needs guest memory, even in a
/// module that exports one: the memory is resolved from the *instance's* exports,
/// and instantiation is what produces the instance, so a call made while it is
/// still running has no memory to work in and ends the run.
///
/// Not a choice: `Module::instantiate` is `pub(crate)` in wasmi, so instantiation
/// cannot be split from the start section to resolve the memory in between.
#[test]
fn a_start_section_cannot_make_a_host_call() {
    let host = FakeHost::new();

    let wat = format!(
        r#"(module {ldgr_index} {ONE_PAGE}
             (func $init (drop (call $ldgr_index (i32.const 0) (i32.const 4))))
             (start $init)
             (func (export "finish") (result i32) (i32.const 0)))"#,
        ldgr_index = import::LDGR_INDEX
    );

    let failure = assert_stage!(
        run_with_gas(&wat, PLENTY_OF_GAS, &host)
            .expect_err("a host call from a start section must not be served"),
        RunError::NoMemory
    );
    assert!(
        failure.fuel_used > 0,
        "the start section is metered up to the refused call: {failure}"
    );
}

// ---------------------------------------------------------------------------
// The entry point
// ---------------------------------------------------------------------------

#[test]
fn a_missing_entry_point_fails() {
    let host = FakeHost::new();

    let wat = r#"(module (memory (export "memory") 1) (func (export "other") (result i32) (i32.const 0)))"#;
    let failure = assert_stage!(
        run_with_gas(wat, PLENTY_OF_GAS, &host)
            .expect_err("a module without the entry point must not run"),
        RunError::EntryPoint(_)
    );
    assert!(
        failure.to_string().contains("no entry point 'finish'"),
        "{failure}"
    );
}

/// The entry point is looked up by the name the caller asks for.
#[test]
fn the_entry_point_is_the_name_the_caller_gives() {
    let host = FakeHost::new();

    let wat = r#"(module (memory (export "memory") 1) (func (export "other") (result i32) (i32.const 9)))"#;
    let outcome = run_entry(wat, &host, "other").expect("should run");
    assert_eq!(outcome.result, 9);
}

/// The entry point must take nothing and return an `i32`. A module that exports the
/// name with another signature is told so, rather than being told the export is
/// missing: wasmi answers both cases with one error, and "no entry point" would send
/// a contract author looking for a function they already have.
#[test]
fn an_entry_point_of_the_wrong_type_fails() {
    let host = FakeHost::new();

    for signature in ["(result i64)", "(param i32) (result i32)", ""] {
        let body = if signature.contains("result i64") {
            "(i64.const 0)"
        } else if signature.is_empty() {
            "(nop)"
        } else {
            "(i32.const 0)"
        };
        let wat = format!(
            r#"(module (memory (export "memory") 1) (func (export "finish") {signature} {body}))"#
        );
        let failure = assert_stage!(
            run_with_gas(&wat, PLENTY_OF_GAS, &host)
                .expect_err("a wrongly-typed entry point must not run"),
            RunError::EntryPoint(_)
        )
        .to_string();
        assert!(
            failure.contains("entry point 'finish' has the wrong signature"),
            "{signature}: {failure}"
        );
        assert!(
            !failure.contains("no entry point"),
            "a present export must not be reported as absent — {signature}: {failure}"
        );
    }
}

/// An export of the entry point's name that is not a function at all is a third
/// case, and named as such: nothing is missing and no signature is wrong.
#[test]
fn an_entry_point_that_is_not_a_function_fails() {
    let host = FakeHost::new();

    let wat =
        r#"(module (memory (export "memory") 1) (global (export "finish") i32 (i32.const 0)))"#;
    let failure = assert_stage!(
        run_with_gas(wat, PLENTY_OF_GAS, &host).expect_err("a non-function export must not run"),
        RunError::EntryPoint(_)
    )
    .to_string();
    assert!(
        failure.contains("export 'finish' is not a function"),
        "{failure}"
    );
}

/// A guest that traps fails the run rather than returning a value.
#[test]
fn a_trapping_guest_fails_the_run() {
    let host = FakeHost::new();

    let wat = module(&[ONE_PAGE], "(unreachable)");
    assert_stage!(failure(&wat, &host), RunError::Trap(_));

    // An out-of-bounds guest access is a trap too, caught by the engine rather
    // than anything the host is asked about.
    let wat = module(&[ONE_PAGE], "(i32.load (i32.const 100000))");
    assert_stage!(failure(&wat, &host), RunError::Trap(_));
}
