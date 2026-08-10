//! The two budgets a run spends: gas (fuel), and the transfer limit on bytes
//! crossing the boundary. Both are consensus input, so several of these tests
//! assert exact numbers.

mod support;

use support::{Answer, FakeHost, ONE_PAGE, PLENTY_OF_GAS, code, import, module, run, run_with_gas};
use xrpl_host_functions::{HASH_LEN, HostError, HostFunctionSpec};
use xrpl_wasm_vm::{MAX_FIELD_BYTES, RunError, TRANSFER_LIMIT_BYTES};

// ---------------------------------------------------------------------------
// Gas
// ---------------------------------------------------------------------------

/// The fuel a module of `body` burns, given gas to spare.
fn fuel_for(body: &str, parts: &[&str], host: &FakeHost) -> u64 {
    let wat = module(parts, body);
    run(&wat, host).expect("the module should run").fuel_used
}

/// The fuel a module burns doing nothing but returning a constant; every figure
/// below builds on it. wasmi's number, pinned deliberately because wasmi's fuel
/// table is consensus input.
const EMPTY_MODULE_FUEL: u64 = 30;

/// wasmi's own fuel for a host call whose operands are all constants under 64: 14
/// per `*.const`, plus 1 for the call. Our gas sits on top.
///
/// The formula holds only under 64, because wasmi widens a constant's encoding
/// above that, each tier costing 7 more. Every call in [`call_for`] keeps its
/// operands small for that reason; one with a larger constant fails here by a
/// multiple of 7.
fn wasmi_call_fuel(small_const_operands: u64) -> u64 {
    14 * small_const_operands + 1
}

/// wasmi's fuel for one `(drop …)`, which is how a module makes more than one call
/// and keeps only the last result. Pinned like the two above.
const WASMI_DROP_FUEL: u64 = 21;

/// The wasm a test needs in order to call one host function: the `(import …)`
/// declaration, a call with small-constant operands, and how many it pushes.
struct Call {
    import: &'static str,
    call: &'static str,
    operands: u64,
}

/// The test wasm for each host function. The `match` is exhaustive, so a function
/// added to the ABI fails to compile until it has wasm here, and iterating
/// [`HostFunctionSpec::ALL`] then covers the whole ABI.
fn call_for(op: HostFunctionSpec) -> Call {
    let (import, call, operands) = match op {
        HostFunctionSpec::GetLedgerSqn => (
            import::LDGR_INDEX,
            "(call $ldgr_index (i32.const 0) (i32.const 4))",
            2,
        ),
        HostFunctionSpec::GetParentLedgerTime => (
            import::PARENT_LDGR_TIME,
            "(call $parent_ldgr_time (i32.const 0) (i32.const 4))",
            2,
        ),
        HostFunctionSpec::GetParentLedgerHash => (
            import::PARENT_LDGR_HASH,
            "(call $parent_ldgr_hash (i32.const 0) (i32.const 32))",
            2,
        ),
        HostFunctionSpec::GetBaseFee => (
            import::BASE_FEE,
            "(call $base_fee (i32.const 0) (i32.const 4))",
            2,
        ),
        HostFunctionSpec::IsAmendmentEnabled => (
            import::AMENDMENT_ENABLED,
            "(call $amendment_enabled (i32.const 0) (i32.const 32))",
            2,
        ),
        HostFunctionSpec::GetCurrentLedgerObjField => (
            import::HOME_LE_FIELD,
            "(call $home_le_field (i32.const 1) (i32.const 0) (i32.const 4))",
            3,
        ),
        HostFunctionSpec::Sha512Half => (
            import::SHA512_HALF,
            "(call $sha512_half (i32.const 0) (i32.const 4) (i32.const 0) (i32.const 32))",
            4,
        ),
        HostFunctionSpec::Trace => (
            import::TRACE,
            "(call $trace (i32.const 0) (i32.const 0) (i32.const 0) (i32.const 0) (i32.const 0))",
            5,
        ),
        HostFunctionSpec::TraceNum => (
            import::TRACE_NUM,
            "(call $trace_num (i32.const 0) (i32.const 0) (i64.const 0))",
            3,
        ),
    };
    Call {
        import,
        call,
        operands,
    }
}

#[test]
fn an_empty_module_burns_a_fixed_amount_of_fuel() {
    let fuel = fuel_for("(i32.const 0)", &[ONE_PAGE], &FakeHost::new());
    assert_eq!(fuel, EMPTY_MODULE_FUEL);
}

/// Calling a host function `n` times costs `n` times its gas, to the unit. Every
/// other term is known — the module's floor, wasmi's fuel per call, one `drop`
/// between consecutive calls — so the total is a closed form, with the gas read
/// from the spec table rather than restated. `n = 1` pins the charge, `n > 1` pins
/// that it lands on every call rather than once per run.
#[test]
fn a_host_call_costs_its_gas_every_time_it_is_called() {
    let host = FakeHost::new().answering_field(1, Answer::bytes([0xaa]));

    for &op in HostFunctionSpec::ALL {
        let Call {
            import,
            call,
            operands,
        } = call_for(op);
        let per_call = wasmi_call_fuel(operands) + op.gas();

        for n in 1..=3 {
            let body = format!("{}{call}", format!("(drop {call}) ").repeat(n - 1));
            let n = n as u64;

            assert_eq!(
                fuel_for(&body, &[import, ONE_PAGE], &host),
                EMPTY_MODULE_FUEL + n * per_call + (n - 1) * WASMI_DROP_FUEL,
                "{n} x {call}"
            );
        }
    }
}

/// The gas charge precedes the call's body, so a failing call costs exactly what a
/// successful one costs. Field 1 is answered and field 7 is not; the two modules
/// are otherwise identical, so their totals are comparable.
#[test]
fn a_failing_host_call_costs_exactly_what_a_successful_one_costs() {
    let host = FakeHost::new().answering_field(1, Answer::bytes([0xaa]));
    let call = |field: i32| {
        module(
            &[import::HOME_LE_FIELD, ONE_PAGE],
            &format!("(call $home_le_field (i32.const {field}) (i32.const 0) (i32.const 4))"),
        )
    };

    let answered = run(&call(1), &host).expect("the module should run");
    let refused = run(&call(7), &host).expect("the module should run");

    assert_eq!(answered.result, 1);
    assert_eq!(refused.result, code(HostError::FieldNotFound));
    assert_eq!(refused.fuel_used, answered.fuel_used);
}

/// `fuel_used` is `gas - remaining`: what the run spent, not what was left or what
/// it was handed. The gas figures are derived from the run's cost, so the boundary
/// — exactly enough, and one short — is among the cases.
#[test]
fn fuel_used_is_what_was_spent_not_what_was_supplied() {
    let host = FakeHost::new();
    let op = HostFunctionSpec::GetLedgerSqn;
    let Call {
        import,
        call,
        operands,
    } = call_for(op);
    let wat = module(&[import, ONE_PAGE], call);
    let cost = EMPTY_MODULE_FUEL + wasmi_call_fuel(operands) + op.gas();

    // Exactly its cost is enough, and no amount above it changes the figure. The
    // result is checked too, so the figure belongs to a run that did the work
    // rather than to one that was cut short.
    for gas in [cost, cost + 1, cost * 100, PLENTY_OF_GAS] {
        let outcome = run_with_gas(&wat, gas, &host).expect("should run");
        assert_eq!(
            outcome.result, 4,
            "gas {gas}: the call should have succeeded"
        );
        assert_eq!(outcome.fuel_used, cost, "gas {gas}");
    }

    // One fuel short: the run ends at the call it cannot pay for and still owes the
    // whole limit, because `charge` spends what is left.
    let short = run_with_gas(&wat, cost - 1, &host).expect_err("one fuel short must not complete");
    assert!(
        matches!(short.error, RunError::OutOfGas),
        "expected the run to end out of gas, got: {short}"
    );
    assert_eq!(short.fuel_used, cost - 1);
}

/// Fuel is metered, so the same module burns the same fuel every time — a
/// property consensus depends on.
#[test]
fn the_same_run_burns_the_same_fuel() {
    let wat = module(
        &[import::TRACE, ONE_PAGE],
        "(call $trace (i32.const 0) (i32.const 0) (i32.const 0) (i32.const 0) (i32.const 0))",
    );

    let first = run(&wat, &FakeHost::new()).expect("should run").fuel_used;
    for _ in 0..4 {
        assert_eq!(
            run(&wat, &FakeHost::new()).expect("should run").fuel_used,
            first
        );
    }
    assert!(first > HostFunctionSpec::Trace.gas());
}

/// Too little gas to finish stops the run: the meter refuses the guest's own
/// instructions before it ever reaches the host call.
#[test]
fn a_run_that_cannot_afford_itself_fails() {
    let host = FakeHost::new();
    let wat = module(
        &[import::LDGR_INDEX, ONE_PAGE],
        "(call $ldgr_index (i32.const 0) (i32.const 4))",
    );

    for gas in [0, 1, 10] {
        let Err(failure) = run_with_gas(&wat, gas, &host) else {
            panic!("gas {gas} should not have completed");
        };
        assert!(
            matches!(failure.error, RunError::OutOfGas),
            "gas {gas}: expected the run to end out of gas, got: {failure}"
        );
    }
}

/// A guest looping forever is stopped by gas rather than running away, and owes
/// the gas it burned doing it.
#[test]
fn an_endless_loop_is_stopped_by_gas() {
    const GAS: u64 = 100_000;

    let host = FakeHost::new();
    let wat = module(&[ONE_PAGE], "(loop $l (br $l)) (i32.const 0)");

    let failure = run_with_gas(&wat, GAS, &host).expect_err("an endless loop must not complete");
    assert!(
        matches!(failure.error, RunError::OutOfGas),
        "expected the meter to stop it, got: {failure}"
    );
    assert_eq!(
        failure.fuel_used, GAS,
        "a runaway guest burns the whole limit"
    );
}

/// A host call refused its gas stops the run: the guest never gets a chance to
/// ignore the refusal and carry on, and it is charged the whole limit.
///
/// The gas range is every amount that reaches the call and cannot pay for it, so
/// the case is the whole boundary rather than one number.
#[test]
fn a_host_call_refused_its_gas_stops_the_run() {
    let host = FakeHost::new();
    let op = HostFunctionSpec::TraceNum;
    let Call {
        import,
        call,
        operands,
    } = call_for(op);
    let wat = module(&[import, ONE_PAGE], call);
    // What the guest spends getting as far as the call. Below it the meter stops
    // the guest's own instructions instead, which is
    // `a_run_that_cannot_afford_itself_fails`'s case, not this one.
    let reaching_the_call = EMPTY_MODULE_FUEL + wasmi_call_fuel(operands);

    for gas in reaching_the_call..reaching_the_call + op.gas() {
        let Err(failure) = run_with_gas(&wat, gas, &host) else {
            panic!("gas {gas}: the run completed, so the guest was handed the refusal");
        };
        assert!(
            matches!(failure.error, RunError::OutOfGas),
            "gas {gas}: expected the run to end out of gas, got: {failure}"
        );
        assert_eq!(
            failure.fuel_used, gas,
            "gas {gas}: a call it cannot afford burns the whole limit"
        );
    }
    assert!(host.traces().is_empty(), "the host body must not have run");
}

// ---------------------------------------------------------------------------
// The transfer limit
// ---------------------------------------------------------------------------

/// A module that repeats `call` while `keep_going` holds, then returns the last
/// status, so a budget can be run to exhaustion inside one invocation.
fn until_refused(imports: &str, call: &str, keep_going: &str) -> String {
    module(
        &[imports, ONE_PAGE],
        &format!(
            "(local $r i32)
             (loop $l
               (local.set $r {call})
               (br_if $l {keep_going}))
             (local.get $r)"
        ),
    )
}

/// For a call whose success is a positive byte count.
const WHILE_POSITIVE: &str = "(i32.gt_s (local.get $r) (i32.const 0))";

/// Bytes written into guest memory are charged against the run's budget, and the
/// budget is a per-run total: 1 MiB of 1 KiB values exhausts it.
#[test]
fn writes_spend_the_transfer_budget() {
    let host = FakeHost::new().answering_field(1, Answer::filler(MAX_FIELD_BYTES));
    let wat = until_refused(
        import::HOME_LE_FIELD,
        &format!("(call $home_le_field (i32.const 1) (i32.const 0) (i32.const {MAX_FIELD_BYTES}))"),
        WHILE_POSITIVE,
    );

    let outcome = run(&wat, &host).expect("the module should run");
    assert_eq!(outcome.result, code(HostError::OutOfTransferLimit));
    assert_eq!(
        host.fields_asked.borrow().len() as u64,
        TRANSFER_LIMIT_BYTES / MAX_FIELD_BYTES as u64 + 1,
        "one call per 1 KiB of budget, plus the one that was refused"
    );
}

/// The budget is per run, not per call: a fresh run starts with a full budget.
#[test]
fn each_run_gets_its_own_budget() {
    let wat = until_refused(
        import::HOME_LE_FIELD,
        &format!("(call $home_le_field (i32.const 1) (i32.const 0) (i32.const {MAX_FIELD_BYTES}))"),
        WHILE_POSITIVE,
    );

    for _ in 0..2 {
        let host = FakeHost::new().answering_field(1, Answer::filler(MAX_FIELD_BYTES));
        let outcome = run(&wat, &host).expect("the module should run");
        assert_eq!(outcome.result, code(HostError::OutOfTransferLimit));
        assert_eq!(
            host.fields_asked.borrow().len() as u64,
            TRANSFER_LIMIT_BYTES / MAX_FIELD_BYTES as u64 + 1
        );
    }
}

/// A run well inside the budget never sees it.
#[test]
fn a_modest_run_never_meets_the_budget() {
    let host = FakeHost::new().answering_field(1, Answer::filler(MAX_FIELD_BYTES));
    let wat = module(
        &[import::HOME_LE_FIELD, ONE_PAGE],
        &format!("(call $home_le_field (i32.const 1) (i32.const 0) (i32.const {MAX_FIELD_BYTES}))"),
    );

    let outcome = run(&wat, &host).expect("the module should run");
    assert_eq!(outcome.result, MAX_FIELD_BYTES as i32);
}

/// Reads leave the budget alone: `read_borrowed` hands the host a slice *aliasing*
/// guest memory, so there are no copied bytes to charge. What bounds how many reads
/// a run can make is gas, which every host call pays before its body runs.
///
/// The observation is the write at the end, not the reads: the module reads four
/// times the whole budget first, so a rule that charged reads would have nothing
/// left, and the write would answer `OutOfTransferLimit` instead of a byte count.
#[test]
fn reads_do_not_spend_the_transfer_budget() {
    /// 1 KiB reads, four times over the budget.
    const READS: u64 = 4 * TRANSFER_LIMIT_BYTES / MAX_FIELD_BYTES as u64;

    let host = FakeHost::new().answering_field(1, Answer::filler(MAX_FIELD_BYTES));
    let wat = module(
        &[import::TRACE_NUM, import::HOME_LE_FIELD, ONE_PAGE],
        &format!(
            "(local $i i32)
             (loop $l
               (drop (call $trace_num (i32.const 0) (i32.const {MAX_FIELD_BYTES}) (i64.const 0)))
               (local.set $i (i32.add (local.get $i) (i32.const 1)))
               (br_if $l (i32.lt_u (local.get $i) (i32.const {READS}))))
             (call $home_le_field (i32.const 1) (i32.const 0) (i32.const {MAX_FIELD_BYTES}))"
        ),
    );

    let outcome = run(&wat, &host).expect("the module should run");
    assert_eq!(
        host.traces().len() as u64,
        READS,
        "every read should have been served"
    );
    assert_eq!(
        outcome.result, MAX_FIELD_BYTES as i32,
        "the write after {READS} reads of {MAX_FIELD_BYTES} bytes should still have its budget"
    );
}

/// Only the output half of a read-write call spends the budget. `sha512_half`'s
/// input is a borrowed read like any other, aliasing guest memory rather than
/// crossing the boundary, so a run may hash far more bytes than the budget holds as
/// long as the digests it writes fit inside it.
///
/// The two totals are asserted, so the arithmetic that makes the case is in the
/// test rather than in a comment: the inputs alone would overrun the budget, the
/// digests alone are a small fraction of it.
#[test]
fn only_the_output_half_of_a_read_write_spends_the_budget() {
    /// Enough 1 KiB inputs to overrun the budget twice over.
    const CALLS: u64 = 2 * TRANSFER_LIMIT_BYTES / MAX_FIELD_BYTES as u64;

    assert!(
        CALLS * MAX_FIELD_BYTES as u64 > TRANSFER_LIMIT_BYTES,
        "the inputs alone must overrun the budget"
    );
    assert!(
        CALLS * HASH_LEN as u64 <= TRANSFER_LIMIT_BYTES / 2,
        "the digests alone must stay well inside it"
    );

    let host = FakeHost::new().answering_digest(Answer::filler(HASH_LEN));
    let wat = module(
        &[import::SHA512_HALF, ONE_PAGE],
        &format!(
            "(local $i i32)
             (local $r i32)
             (loop $l
               (local.set $r (call $sha512_half (i32.const 0) (i32.const {MAX_FIELD_BYTES})
                                                (i32.const 0) (i32.const {HASH_LEN})))
               (local.set $i (i32.add (local.get $i) (i32.const 1)))
               (br_if $l (i32.lt_u (local.get $i) (i32.const {CALLS}))))
             (local.get $r)"
        ),
    );

    let outcome = run(&wat, &host).expect("the module should run");
    assert_eq!(
        host.digested.borrow().len() as u64,
        CALLS,
        "every call should have been served"
    );
    assert_eq!(
        outcome.result, HASH_LEN as i32,
        "only the digests are charged, and they fit"
    );
}
