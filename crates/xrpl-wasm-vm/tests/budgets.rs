//! The two budgets a run spends: gas (fuel), and the transfer limit on bytes
//! crossing the boundary. Both are consensus input, so several of these tests
//! assert exact numbers.

mod support;

use support::{
    Answer, EMPTY_REGION, FakeHost, ONE_PAGE, PLENTY_OF_GAS, code, import, module, run,
    run_with_gas, trace_call,
};
use xrpl_host_functions::{HASH_LEN, HostError, HostFunctionSpec, TraceDataType};
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

/// wasmi's own fuel for a host call whose operands are all small constants: 15 per
/// `*.const`, and the `call` itself is free. Our gas sits on top.
///
/// This holds only while every operand is a small constant. wasmi widens a
/// constant's encoding past a threshold, and a wider const costs more, so a call
/// built with a large constant fails here. Every call in [`call_for`] keeps its
/// operands small for that reason.
fn wasmi_call_fuel(small_const_operands: u64) -> u64 {
    15 * small_const_operands
}

/// What wasmi charges on top of that for a call to a function with no result —
/// `trace`'s shape, and nothing else in the ABI. Per call, not per module. Measured
/// and pinned like the figures above.
const WASMI_NO_RESULT_FUEL: u64 = 15;

/// wasmi's fuel for one `(drop …)`, which is how a module makes more than one call
/// and keeps only the last result. Pinned like the two above.
const WASMI_DROP_FUEL: u64 = 22;

/// The wasm a test needs in order to call one host function: the `(import …)`
/// declaration, a call with small-constant operands, and how many it pushes.
struct Call {
    import: &'static str,
    call: &'static str,
    operands: u64,
    /// Whether the call leaves an `i32` behind. `trace` does not, which is why
    /// [`Call::body`] ends every module with a constant instead of the call.
    yields: bool,
}

impl Call {
    /// `n` calls in a row, leaving one `i32` for the module to return: the last
    /// answer where there is one, and a constant where the call has none.
    fn body(&self, n: usize) -> String {
        if self.yields {
            format!(
                "{}{}",
                format!("(drop {}) ", self.call).repeat(n - 1),
                self.call
            )
        } else {
            format!("{}(i32.const 0)", format!("{} ", self.call).repeat(n))
        }
    }

    /// What [`Call::body`] burns beside the calls' own gas and the module's floor:
    /// one `drop` between consecutive answers, or wasmi's own surcharge on a call
    /// that has none.
    fn overhead(&self, n: u64) -> u64 {
        if self.yields {
            (n - 1) * WASMI_DROP_FUEL
        } else {
            n * WASMI_NO_RESULT_FUEL
        }
    }
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
        HostFunctionSpec::CacheLedgerObj => (
            import::CACHE_LE,
            "(call $cache_le (i32.const 0) (i32.const 32) (i32.const 0))",
            3,
        ),
        HostFunctionSpec::GetTxField => (
            import::TX_FIELD,
            "(call $tx_field (i32.const 1) (i32.const 0) (i32.const 4))",
            3,
        ),
        HostFunctionSpec::GetCurrentLedgerObjField => (
            import::HOME_LE_FIELD,
            "(call $home_le_field (i32.const 1) (i32.const 0) (i32.const 4))",
            3,
        ),
        HostFunctionSpec::GetLedgerObjField => (
            import::LE_FIELD,
            "(call $le_field (i32.const 1) (i32.const 1) (i32.const 0) (i32.const 4))",
            4,
        ),
        HostFunctionSpec::GetTxNestedField => (
            import::TX_INNER,
            "(call $tx_inner (i32.const 0) (i32.const 4) (i32.const 8) (i32.const 4))",
            4,
        ),
        HostFunctionSpec::GetCurrentLedgerObjNestedField => (
            import::HOME_LE_INNER,
            "(call $home_le_inner (i32.const 0) (i32.const 4) (i32.const 8) (i32.const 4))",
            4,
        ),
        HostFunctionSpec::GetLedgerObjNestedField => (
            import::LE_INNER,
            "(call $le_inner (i32.const 1) (i32.const 0) (i32.const 4) (i32.const 8) (i32.const 4))",
            5,
        ),
        HostFunctionSpec::GetTxArrayLen => {
            (import::TX_ARR_LEN, "(call $tx_arr_len (i32.const 1))", 1)
        }
        HostFunctionSpec::GetCurrentLedgerObjArrayLen => (
            import::HOME_LE_ARR_LEN,
            "(call $home_le_arr_len (i32.const 1))",
            1,
        ),
        HostFunctionSpec::GetLedgerObjArrayLen => (
            import::LE_ARR_LEN,
            "(call $le_arr_len (i32.const 1) (i32.const 1))",
            2,
        ),
        HostFunctionSpec::GetTxNestedArrayLen => (
            import::TX_INNER_ARR_LEN,
            "(call $tx_inner_arr_len (i32.const 0) (i32.const 4))",
            2,
        ),
        HostFunctionSpec::GetCurrentLedgerObjNestedArrayLen => (
            import::HOME_LE_INNER_ARR_LEN,
            "(call $home_le_inner_arr_len (i32.const 0) (i32.const 4))",
            2,
        ),
        HostFunctionSpec::GetLedgerObjNestedArrayLen => (
            import::LE_INNER_ARR_LEN,
            "(call $le_inner_arr_len (i32.const 1) (i32.const 0) (i32.const 4))",
            3,
        ),
        HostFunctionSpec::CheckSignature => (
            import::CHECK_SIG,
            "(call $check_sig (i32.const 0) (i32.const 0) (i32.const 0) (i32.const 0) (i32.const 0) (i32.const 0))",
            6,
        ),
        HostFunctionSpec::AccountKeylet => (
            import::ACCOUNTROOT_ID,
            "(call $accountroot_id (i32.const 0) (i32.const 20) (i32.const 32) (i32.const 32))",
            4,
        ),
        HostFunctionSpec::AmmKeylet => (
            import::AMM_ID,
            "(call $amm_id (i32.const 0) (i32.const 20) (i32.const 24) (i32.const 40) (i32.const 0) (i32.const 32))",
            6,
        ),
        HostFunctionSpec::CheckKeylet => (
            import::CHECK_ID,
            "(call $check_id (i32.const 0) (i32.const 20) (i32.const 0) (i32.const 4) (i32.const 32) (i32.const 32))",
            6,
        ),
        HostFunctionSpec::CredentialKeylet => (
            import::CREDENTIAL_ID,
            "(call $credential_id (i32.const 0) (i32.const 20) (i32.const 20) (i32.const 20) (i32.const 40) (i32.const 4) (i32.const 44) (i32.const 20))",
            8,
        ),
        HostFunctionSpec::DelegateKeylet => (
            import::DELEGATE_ID,
            "(call $delegate_id (i32.const 0) (i32.const 20) (i32.const 20) (i32.const 20) (i32.const 40) (i32.const 32))",
            6,
        ),
        HostFunctionSpec::DepositPreauthKeylet => (
            import::DEPOSIT_PREAUTH_ID,
            "(call $deposit_preauth_id (i32.const 0) (i32.const 20) (i32.const 20) (i32.const 20) (i32.const 40) (i32.const 32))",
            6,
        ),
        HostFunctionSpec::DidKeylet => (
            import::DID_ID,
            "(call $did_id (i32.const 0) (i32.const 20) (i32.const 32) (i32.const 32))",
            4,
        ),
        HostFunctionSpec::EscrowKeylet => (
            import::ESCROW_ID,
            "(call $escrow_id (i32.const 0) (i32.const 20) (i32.const 0) (i32.const 4) (i32.const 32) (i32.const 32))",
            6,
        ),
        HostFunctionSpec::TrustLineKeylet => (
            import::TRUSTLINE_ID,
            "(call $trustline_id (i32.const 0) (i32.const 20) (i32.const 20) (i32.const 20) (i32.const 40) (i32.const 20) (i32.const 60) (i32.const 32))",
            8,
        ),
        HostFunctionSpec::MptokenIssuanceKeylet => (
            import::MPT_ISSUANCE_ID,
            "(call $mpt_issuance_id (i32.const 0) (i32.const 20) (i32.const 0) (i32.const 4) (i32.const 32) (i32.const 32))",
            6,
        ),
        HostFunctionSpec::MptokenKeylet => (
            import::MPTOKEN_ID,
            "(call $mptoken_id (i32.const 0) (i32.const 24) (i32.const 24) (i32.const 20) (i32.const 44) (i32.const 20))",
            6,
        ),
        HostFunctionSpec::NftokenOfferKeylet => (
            import::NFT_OFFER_ID,
            "(call $nft_offer_id (i32.const 0) (i32.const 20) (i32.const 0) (i32.const 4) (i32.const 32) (i32.const 32))",
            6,
        ),
        HostFunctionSpec::OfferKeylet => (
            import::OFFER_ID,
            "(call $offer_id (i32.const 0) (i32.const 20) (i32.const 0) (i32.const 4) (i32.const 32) (i32.const 32))",
            6,
        ),
        HostFunctionSpec::OracleKeylet => (
            import::ORACLE_ID,
            "(call $oracle_id (i32.const 0) (i32.const 20) (i32.const 0) (i32.const 4) (i32.const 32) (i32.const 32))",
            6,
        ),
        HostFunctionSpec::PaychannelKeylet => (
            import::PAYCHAN_ID,
            "(call $paychan_id (i32.const 0) (i32.const 20) (i32.const 20) (i32.const 20) (i32.const 0) (i32.const 4) (i32.const 40) (i32.const 20))",
            8,
        ),
        HostFunctionSpec::PermissionedDomainKeylet => (
            import::PERMISSIONED_DOMAIN_ID,
            "(call $permissioned_domain_id (i32.const 0) (i32.const 20) (i32.const 0) (i32.const 4) (i32.const 32) (i32.const 32))",
            6,
        ),
        HostFunctionSpec::SignerListKeylet => (
            import::SIGNERS_ID,
            "(call $signers_id (i32.const 0) (i32.const 20) (i32.const 32) (i32.const 32))",
            4,
        ),
        HostFunctionSpec::TicketKeylet => (
            import::TICKET_ID,
            "(call $ticket_id (i32.const 0) (i32.const 20) (i32.const 0) (i32.const 4) (i32.const 32) (i32.const 32))",
            6,
        ),
        HostFunctionSpec::VaultKeylet => (
            import::VAULT_ID,
            "(call $vault_id (i32.const 0) (i32.const 20) (i32.const 0) (i32.const 4) (i32.const 32) (i32.const 32))",
            6,
        ),
        HostFunctionSpec::Sha512Half => (
            import::SHA512_HALF,
            "(call $sha512_half (i32.const 0) (i32.const 4) (i32.const 0) (i32.const 32))",
            4,
        ),
        HostFunctionSpec::Trace => (
            import::TRACE,
            "(call $trace (i32.const 0) (i32.const 0) (i32.const 1) (i32.const 0) (i32.const 0))",
            5,
        ),
        HostFunctionSpec::UpdateData => (
            import::SET_DATA,
            "(call $set_data (i32.const 0) (i32.const 8))",
            2,
        ),
        HostFunctionSpec::GetNft => (
            import::NFT_URI,
            "(call $nft_uri (i32.const 0) (i32.const 20) (i32.const 20) (i32.const 32) (i32.const 52) (i32.const 12))",
            6,
        ),
        HostFunctionSpec::GetNftIssuer => (
            import::NFT_ISSUER,
            "(call $nft_issuer (i32.const 0) (i32.const 32) (i32.const 32) (i32.const 20))",
            4,
        ),
        HostFunctionSpec::GetNftTaxon => (
            import::NFT_TAXON,
            "(call $nft_taxon (i32.const 0) (i32.const 32) (i32.const 32) (i32.const 4))",
            4,
        ),
        HostFunctionSpec::GetNftFlags => (
            import::NFT_FLAGS,
            "(call $nft_flags (i32.const 0) (i32.const 32))",
            2,
        ),
        HostFunctionSpec::GetNftTransferFee => (
            import::NFT_XFER_FEE,
            "(call $nft_xfer_fee (i32.const 0) (i32.const 32))",
            2,
        ),
        HostFunctionSpec::GetNftSequence => (
            import::NFT_SERIAL,
            "(call $nft_serial (i32.const 0) (i32.const 32) (i32.const 32) (i32.const 4))",
            4,
        ),
        HostFunctionSpec::FloatFromInt => (
            import::FLOAT_FROM_INT,
            "(call $float_from_int (i64.const 0) (i32.const 0) (i32.const 8) (i32.const 0))",
            4,
        ),
        HostFunctionSpec::FloatFromUint => (
            import::FLOAT_FROM_UINT,
            "(call $float_from_uint (i32.const 0) (i32.const 8) (i32.const 8) (i32.const 8) (i32.const 0))",
            5,
        ),
        HostFunctionSpec::FloatFromStamount => (
            import::FLOAT_FROM_STAMOUNT,
            "(call $float_from_stamount (i32.const 0) (i32.const 8) (i32.const 8) (i32.const 8) (i32.const 0))",
            5,
        ),
        HostFunctionSpec::FloatFromStnumber => (
            import::FLOAT_FROM_STNUMBER,
            "(call $float_from_stnumber (i32.const 0) (i32.const 8) (i32.const 8) (i32.const 8) (i32.const 0))",
            5,
        ),
        HostFunctionSpec::FloatToInt => (
            import::FLOAT_TO_INT,
            "(call $float_to_int (i32.const 0) (i32.const 8) (i32.const 8) (i32.const 8) (i32.const 0))",
            5,
        ),
        HostFunctionSpec::FloatToMantExp => (
            import::FLOAT_TO_MANT_EXP,
            "(call $float_to_mant_exp (i32.const 0) (i32.const 8) (i32.const 8) (i32.const 8) (i32.const 16) (i32.const 4))",
            6,
        ),
        HostFunctionSpec::FloatFromMantExp => (
            import::FLOAT_FROM_MANT_EXP,
            "(call $float_from_mant_exp (i64.const 0) (i32.const 0) (i32.const 8) (i32.const 8) (i32.const 0))",
            5,
        ),
        HostFunctionSpec::FloatCompare => (
            import::FLOAT_CMP,
            "(call $float_cmp (i32.const 0) (i32.const 8) (i32.const 8) (i32.const 8))",
            4,
        ),
        HostFunctionSpec::FloatAdd => (
            import::FLOAT_ADD,
            "(call $float_add (i32.const 0) (i32.const 8) (i32.const 8) (i32.const 8) (i32.const 16) (i32.const 8) (i32.const 0))",
            7,
        ),
        HostFunctionSpec::FloatSubtract => (
            import::FLOAT_SUB,
            "(call $float_sub (i32.const 0) (i32.const 8) (i32.const 8) (i32.const 8) (i32.const 16) (i32.const 8) (i32.const 0))",
            7,
        ),
        HostFunctionSpec::FloatMultiply => (
            import::FLOAT_MULT,
            "(call $float_mult (i32.const 0) (i32.const 8) (i32.const 8) (i32.const 8) (i32.const 16) (i32.const 8) (i32.const 0))",
            7,
        ),
        HostFunctionSpec::FloatDivide => (
            import::FLOAT_DIV,
            "(call $float_div (i32.const 0) (i32.const 8) (i32.const 8) (i32.const 8) (i32.const 16) (i32.const 8) (i32.const 0))",
            7,
        ),
        HostFunctionSpec::FloatRoot => (
            import::FLOAT_ROOT,
            "(call $float_root (i32.const 0) (i32.const 8) (i32.const 2) (i32.const 8) (i32.const 8) (i32.const 0))",
            6,
        ),
        HostFunctionSpec::FloatPower => (
            import::FLOAT_POW,
            "(call $float_pow (i32.const 0) (i32.const 8) (i32.const 2) (i32.const 8) (i32.const 8) (i32.const 0))",
            6,
        ),
    };
    Call {
        import,
        call,
        operands,
        yields: !matches!(op, HostFunctionSpec::Trace),
    }
}

#[test]
fn an_empty_module_burns_a_fixed_amount_of_fuel() {
    let fuel = fuel_for("(i32.const 0)", &[ONE_PAGE], &FakeHost::new());
    assert_eq!(fuel, EMPTY_MODULE_FUEL);
}

/// Calling a host function `n` times costs `n` times its gas, to the unit. Every
/// other term is known — the module's floor, wasmi's fuel per call, one `drop` per
/// answered call — so the total is a closed form, with the gas read from the spec
/// table rather than restated. `n = 1` pins the charge, `n > 1` pins that it lands
/// on every call rather than once per run.
#[test]
fn a_host_call_costs_its_gas_every_time_it_is_called() {
    let host = FakeHost::new().answering_field(1, Answer::bytes([0xaa]));

    for &op in HostFunctionSpec::ALL {
        let call = call_for(op);
        let per_call = wasmi_call_fuel(call.operands) + op.gas();

        for n in 1..=3 {
            let body = call.body(n);
            let n = n as u64;

            assert_eq!(
                fuel_for(&body, &[call.import, ONE_PAGE], &host),
                EMPTY_MODULE_FUEL + n * per_call + call.overhead(n),
                "{n} x {}",
                call.call
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
    let call = call_for(op);
    let wat = module(&[call.import, ONE_PAGE], call.call);
    let cost = EMPTY_MODULE_FUEL + wasmi_call_fuel(call.operands) + op.gas();

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
    let call = call_for(HostFunctionSpec::Trace);
    let wat = module(&[call.import, ONE_PAGE], &call.body(1));

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
    // The cost break down is as follows:
    // 1. There is a function entry charge (finish function) which seems to be 63 units of fuel.
    // 2. Each iteration costs 2 units of fuel.
    // For a GAS amount of 100,000, we will be limited to burning an odd number of fuel.
    // So the way the test is written, the most fuel that will be used is 99,999 units.
    assert_eq!(
        failure.fuel_used,
        GAS - 1,
        "a runaway guest burns all but the last unit of the limit"
    );
}

/// A host call refused its gas stops the run: the guest never gets a chance to
/// ignore the refusal and carry on, and it is charged the whole limit.
///
/// The gas range is every amount that reaches the call and cannot pay for it, so
/// the case is the whole boundary rather than one number. `trace` is the call under
/// it because it is the one that could not report a refusal even if it wanted to:
/// stopping the run is the whole of what the guest sees.
#[test]
fn a_host_call_refused_its_gas_stops_the_run() {
    let host = FakeHost::new();
    let op = HostFunctionSpec::Trace;
    let call = call_for(op);
    let wat = module(&[call.import, ONE_PAGE], &call.body(1));
    // Measured rather than derived: the whole run's cost, less the call's own gas,
    // is the least a guest can be given and still reach the call. Below that the
    // meter stops the guest's own instructions instead, which is
    // `a_run_that_cannot_afford_itself_fails`'s case, not this one.
    let cost = run(&wat, &FakeHost::new())
        .expect("the module should run")
        .fuel_used;

    for gas in cost - op.gas()..cost {
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

/// A write the budget refuses is a write that did not happen. `float_to_mant_exp` is
/// the case worth pinning: its two regions are charged as one, so a call that cannot
/// pay for both must leave both alone rather than place the mantissa and refuse.
#[test]
fn a_write_the_budget_refuses_reaches_guest_memory_in_no_part() {
    let host = FakeHost::new()
        .answering_field(1, Answer::filler(MAX_FIELD_BYTES))
        .answering_float_mant_exp(vec![1, 2, 3, 4, 5, 6, 7, 8], vec![9, 10, 11, 12]);

    // Spend the budget on 1 KiB fields at offset 0, then ask for a mantissa and an
    // exponent at offsets well clear of them.
    let call = "(call $float_to_mant_exp (i32.const 0) (i32.const 8) (i32.const 2048) (i32.const 8) (i32.const 2064) (i32.const 4))";
    let spent = |tail: &str| {
        module(
            &[import::HOME_LE_FIELD, import::FLOAT_TO_MANT_EXP, ONE_PAGE],
            &format!(
                "(local $r i32)
                 (loop $l
                   (local.set $r (call $home_le_field (i32.const 1) (i32.const 0) (i32.const {MAX_FIELD_BYTES})))
                   (br_if $l {WHILE_POSITIVE}))
                 {tail}"
            ),
        )
    };

    let refused = run(&spent(call), &host).expect("the module should run");
    assert_eq!(refused.result, code(HostError::OutOfTransferLimit));

    let wat = spent(&format!(
        "(drop {call})
         (i32.or (i32.load8_u (i32.const 2048)) (i32.load8_u (i32.const 2064)))"
    ));
    let outcome = run(&wat, &host).expect("the module should run");
    assert_eq!(outcome.result, 0, "neither region should be written");
}

/// The same rule on the path that writes straight into guest memory: `write_into`
/// hands the host a slice *of the guest's own buffer*, so a value the budget cannot
/// pay for has to be kept out of that slice before the host fills it.
///
/// The probe region is one the spending loop never writes to, so anything found there
/// came from the refused call.
#[test]
fn a_straight_write_the_budget_refuses_reaches_guest_memory_in_no_part() {
    /// Clear of the offset the spending loop writes to.
    const PROBE: usize = 2048;
    /// Every byte of the value, so the fold sees a prefix as readily as the whole.
    const MARK: u8 = 0xff;

    let host = FakeHost::new().answering_field(1, Answer::bytes(vec![MARK; MAX_FIELD_BYTES]));
    let call = format!(
        "(call $home_le_field (i32.const 1) (i32.const {PROBE}) (i32.const {MAX_FIELD_BYTES}))"
    );

    // Every local the tails below use is declared here: wasm wants them all ahead of
    // the first instruction.
    let spent = |tail: &str| {
        module(
            &[import::HOME_LE_FIELD, ONE_PAGE],
            &format!(
                "(local $r i32) (local $i i32) (local $seen i32)
                 (loop $l
                   (local.set $r (call $home_le_field (i32.const 1) (i32.const 0) (i32.const {MAX_FIELD_BYTES})))
                   (br_if $l {WHILE_POSITIVE}))
                 {tail}"
            ),
        )
    };

    let refused = run(&spent(&call), &host).expect("the module should run");
    assert_eq!(refused.result, code(HostError::OutOfTransferLimit));

    // Guest memory starts zero-filled, so or-ing the region together reports whether
    // any byte of it was written.
    let wat = spent(&format!(
        "(drop {call})
         (loop $l
           (local.set $seen (i32.or (local.get $seen)
                                    (i32.load8_u (i32.add (i32.const {PROBE}) (local.get $i)))))
           (local.set $i (i32.add (local.get $i) (i32.const 1)))
           (br_if $l (i32.lt_u (local.get $i) (i32.const {MAX_FIELD_BYTES}))))
         (local.get $seen)"
    ));
    let outcome = run(&wat, &host).expect("the module should run");
    assert_eq!(outcome.result, 0, "not one byte should have been written");
}

/// What a write may deliver is what is *left* of the budget, to the byte.
///
/// The prologue spends all but `LEFT`, and field 3's host answers with as much as it
/// is offered — so the window `write_into` opened is what it reports and what it
/// leaves in guest memory, and both are read off as `LEFT`. A mark is a 1, so the
/// fold over the probe's whole buffer counts the bytes that reached it.
///
/// `LEFT` is under [`MAX_FIELD_BYTES`] and the buffer is wider than both probes'
/// values, so it is the budget answering and neither the field cap nor the guest's
/// capacity. Field 4 is the byte past it: a host whose value is one larger than what
/// is left, which no window can hold.
#[test]
fn a_write_may_deliver_what_is_left_of_the_budget_and_not_a_byte_more() {
    /// Full-cap writes, all the prologue can make without overshooting.
    const BULK: u64 = TRANSFER_LIMIT_BYTES / MAX_FIELD_BYTES as u64 - 1;
    /// What the prologue leaves unspent.
    const LEFT: usize = MAX_FIELD_BYTES / 2;
    /// The write that trims what [`BULK`] leaves down to [`LEFT`].
    const TRIM: usize = MAX_FIELD_BYTES - LEFT;
    /// Clear of the offset the prologue writes to.
    const PROBE: usize = 2048;
    const BUFFER: usize = MAX_FIELD_BYTES;
    /// One per byte written, so the fold below sums to how many there were.
    const MARK: u8 = 1;

    assert_eq!(
        BULK * MAX_FIELD_BYTES as u64 + TRIM as u64 + LEFT as u64,
        TRANSFER_LIMIT_BYTES,
        "the prologue must spend all but LEFT of the budget"
    );

    let host = FakeHost::new()
        .answering_field(1, Answer::filler(MAX_FIELD_BYTES))
        .answering_field(2, Answer::filler(TRIM))
        .answering_field(3, Answer::as_much_as_offered(MARK))
        .answering_field(4, Answer::claiming(LEFT + 1));

    let probe = |field: i32| {
        format!(
            "(call $home_le_field (i32.const {field}) (i32.const {PROBE}) (i32.const {BUFFER}))"
        )
    };
    // Every local the tails use, declared where wasm wants them.
    let after_prologue = |tail: String| {
        let wat = module(
            &[import::HOME_LE_FIELD, ONE_PAGE],
            &format!(
                "(local $i i32) (local $marks i32)
                 (loop $l
                   (drop (call $home_le_field (i32.const 1) (i32.const 0) (i32.const {MAX_FIELD_BYTES})))
                   (local.set $i (i32.add (local.get $i) (i32.const 1)))
                   (br_if $l (i32.lt_u (local.get $i) (i32.const {BULK}))))
                 (drop (call $home_le_field (i32.const 2) (i32.const 0) (i32.const {TRIM})))
                 (local.set $i (i32.const 0))
                 {tail}"
            ),
        );
        run(&wat, &host).expect("the module should run").result
    };

    assert_eq!(
        after_prologue(probe(3)),
        LEFT as i32,
        "the host should be offered exactly what is left"
    );

    // Guest memory starts zero-filled, so summing the probe's whole buffer counts the
    // marks in it.
    assert_eq!(
        after_prologue(format!(
            "(drop {})
             (loop $l
               (local.set $marks (i32.add (local.get $marks)
                                          (i32.load8_u (i32.add (i32.const {PROBE}) (local.get $i)))))
               (local.set $i (i32.add (local.get $i) (i32.const 1)))
               (br_if $l (i32.lt_u (local.get $i) (i32.const {BUFFER}))))
             (local.get $marks)",
            probe(3)
        )),
        LEFT as i32,
        "and that many marks, no more, should reach guest memory"
    );

    assert_eq!(
        after_prologue(probe(4)),
        code(HostError::OutOfTransferLimit),
        "a value one byte past what is left fits no window"
    );
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
    let read = trace_call(
        TraceDataType::AsHex,
        EMPTY_REGION,
        &format!("(i32.const 0) (i32.const {MAX_FIELD_BYTES})"),
    );
    let wat = module(
        &[import::TRACE, import::HOME_LE_FIELD, ONE_PAGE],
        &format!(
            "(local $i i32)
             (loop $l
               {read}
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

// cspell:disable
/// Measures each pinned fuel figure straight from wasmi and asserts the constant
/// still matches. This is what fails first when a wasmi upgrade shifts the fuel
/// table, and it prints every measured number so the constants can be re-derived:
///
///     cargo test -p xrpl-wasm-vm --test budgets probe_fuel -- --exact --nocapture
///
/// The behavioural tests above build totals out of these constants; this one ties
/// each constant back to the one measurement that defines it.
// cspell:enable
#[test]
fn probe_fuel() {
    let h = FakeHost::new().answering_field(1, Answer::bytes([0xaa]));

    // EMPTY_MODULE_FUEL: a module that only returns a constant.
    let empty = fuel_for("(i32.const 0)", &[ONE_PAGE], &h);
    eprintln!("EMPTY_MODULE_FUEL = {empty}");
    assert_eq!(empty, EMPTY_MODULE_FUEL, "EMPTY_MODULE_FUEL");

    // wasmi_call_fuel(operands) = fuel(1 call) - empty - gas, for every op. Asserting
    // it against the formula across all operand counts pins both slope and intercept.
    eprintln!("--- wasmi_call_fuel by operand count ---");
    for &op in HostFunctionSpec::ALL {
        let c = call_for(op);
        // A no-result op's body ends in a trailing constant, not the call's result,
        // so its total carries an extra push; it is pinned in the NO_RESULT section.
        if !c.yields {
            continue;
        }
        let one = fuel_for(&c.body(1), &[c.import, ONE_PAGE], &h);
        let measured = one - empty - op.gas();
        eprintln!(
            "operands={:2}  wasmi_call_fuel={measured:3}  {}",
            c.operands, c.call
        );
        assert_eq!(
            measured,
            wasmi_call_fuel(c.operands),
            "wasmi_call_fuel({}) for {}",
            c.operands,
            c.call
        );
    }

    // WASMI_DROP_FUEL: a second yielding call adds one call plus one drop.
    let g = call_for(HostFunctionSpec::GetLedgerSqn);
    let g1 = fuel_for(&g.body(1), &[g.import, ONE_PAGE], &h);
    let g2 = fuel_for(&g.body(2), &[g.import, ONE_PAGE], &h);
    let drop = (g2 - g1) - (wasmi_call_fuel(g.operands) + HostFunctionSpec::GetLedgerSqn.gas());
    eprintln!("WASMI_DROP_FUEL = {drop}");
    assert_eq!(drop, WASMI_DROP_FUEL, "WASMI_DROP_FUEL");

    // WASMI_NO_RESULT_FUEL: trace is the only no-result op; its module ends in a
    // trailing constant instead of the call's result.
    let t = call_for(HostFunctionSpec::Trace);
    let t1 = fuel_for(&t.body(1), &[t.import, ONE_PAGE], &h);
    let no_result = t1 - empty - wasmi_call_fuel(t.operands) - HostFunctionSpec::Trace.gas();
    eprintln!("WASMI_NO_RESULT_FUEL = {no_result}");
    assert_eq!(no_result, WASMI_NO_RESULT_FUEL, "WASMI_NO_RESULT_FUEL");
}
