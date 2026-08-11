//! What each registered host function passes in each direction: the scalars the
//! guest supplies reach the host unchanged, and the bytes the host produces land
//! where the guest asked for them.

mod support;

use support::{
    COMPLETED, EMPTY_REGION, FakeHost, ONE_PAGE, Trace, code, failure, import, module, run, status,
    traced,
};
use xrpl_host_functions::{HASH_LEN, HostError, TraceDataType};
use xrpl_wasm_vm::RunError;

/// A value the host writes must be readable by the guest at the pointer it gave,
/// and the call's status is the byte count.
#[test]
fn ldgr_index_writes_the_sequence_number_where_the_guest_asked() {
    let host = FakeHost::new();

    let wat = module(
        &[import::LDGR_INDEX, ONE_PAGE],
        "(drop (call $ldgr_index (i32.const 64) (i32.const 4)))
         (i32.load (i32.const 64))",
    );
    assert_eq!(status(&wat, &host), 7, "the 4 LE bytes the host wrote");

    let wat = module(
        &[import::LDGR_INDEX, ONE_PAGE],
        "(call $ldgr_index (i32.const 64) (i32.const 4))",
    );
    assert_eq!(status(&wat, &host), 4, "the byte count");
}

/// The output region is wherever the guest points, not a fixed address.
#[test]
fn the_output_region_is_the_pointer_the_guest_gave() {
    let host = FakeHost::new();

    for offset in [0, 1, 7, 4096, 65532] {
        let wat = module(
            &[import::LDGR_INDEX, ONE_PAGE],
            &format!(
                "(drop (call $ldgr_index (i32.const {offset}) (i32.const 4)))
                 (i32.load (i32.const {offset}))"
            ),
        );
        assert_eq!(status(&wat, &host), 7, "at offset {offset}");
    }
}

/// A leading scalar parameter reaches the host as declared.
#[test]
fn home_le_field_passes_the_field_selector_through() {
    let host = FakeHost::new().answering_field(17, support::Answer::bytes([0xab, 0xcd]));

    let wat = module(
        &[import::HOME_LE_FIELD, ONE_PAGE],
        "(call $home_le_field (i32.const 17) (i32.const 0) (i32.const 64))",
    );
    assert_eq!(status(&wat, &host), 2);
    assert_eq!(*host.fields_asked.borrow(), vec![17]);
}

/// A host error reaches the guest as its negative wire code, and the output
/// region is left as the guest had it.
#[test]
fn a_host_error_becomes_its_wire_code() {
    let host = FakeHost::new();
    const UNTOUCHED: i32 = 7;

    // Field 99 is unanswered, so the host returns `FieldNotFound`. The guest
    // stamps its buffer first, then checks the byte survived the failed call.
    let wat = module(
        &[import::HOME_LE_FIELD, ONE_PAGE],
        &format!(
            "(i32.store8 (i32.const 0) (i32.const {UNTOUCHED}))
             (drop (call $home_le_field (i32.const 99) (i32.const 0) (i32.const 64)))
             (i32.load8_u (i32.const 0))"
        ),
    );
    assert_eq!(status(&wat, &host), UNTOUCHED, "nothing was written");

    let wat = module(
        &[import::HOME_LE_FIELD, ONE_PAGE],
        "(call $home_le_field (i32.const 99) (i32.const 0) (i32.const 64))",
    );
    assert_eq!(status(&wat, &host), code(HostError::FieldNotFound));
}

/// `sha512_half` reads one region and writes another in the same call.
#[test]
fn sha512_half_carries_bytes_in_and_out() {
    const MARKER: u8 = 99;

    let host = FakeHost::new().answering_digest(support::Answer::bytes([MARKER; HASH_LEN]));

    let wat = module(
        &[
            import::SHA512_HALF,
            ONE_PAGE,
            r#"(data (i32.const 0) "hello wasm")"#,
        ],
        &format!(
            "(drop (call $sha512_half (i32.const 0) (i32.const 10)
                                      (i32.const 128) (i32.const {HASH_LEN})))
             (i32.load8_u (i32.const 128))"
        ),
    );
    assert_eq!(
        status(&wat, &host),
        i32::from(MARKER),
        "the first digest byte"
    );
    assert_eq!(
        *host.digested.borrow(),
        vec![b"hello wasm".to_vec()],
        "the input the host saw"
    );
}

/// An empty input region is a legal read, not an error.
#[test]
fn sha512_half_accepts_an_empty_input() {
    let host = FakeHost::new();

    let wat = module(
        &[import::SHA512_HALF, ONE_PAGE],
        "(call $sha512_half (i32.const 0) (i32.const 0) (i32.const 128) (i32.const 32))",
    );
    assert_eq!(status(&wat, &host), 32);
    assert_eq!(*host.digested.borrow(), vec![Vec::<u8>::new()]);
}

/// `trace` reads two regions and a type, and hands the guest back nothing — the
/// module returns a constant of its own, which is what a completed run looks like.
#[test]
fn trace_passes_its_message_type_and_data_through() {
    let host = FakeHost::new();

    let wat = module(
        &[
            import::TRACE,
            ONE_PAGE,
            r#"(data (i32.const 0) "note")"#,
            r#"(data (i32.const 16) "\01\02\03")"#,
        ],
        &traced(
            TraceDataType::AsHex,
            "(i32.const 0) (i32.const 4)",
            "(i32.const 16) (i32.const 3)",
        ),
    );
    assert_eq!(status(&wat, &host), COMPLETED);
    assert_eq!(
        host.traces(),
        vec![Trace {
            msg: "note".to_owned(),
            data_type: TraceDataType::AsHex,
            data: vec![1, 2, 3],
        }]
    );
}

/// The type is the guest's to choose and the host's to act on, so every code the
/// ABI names has to arrive as the type it names.
#[test]
fn every_data_type_reaches_the_host_as_declared() {
    for &data_type in TraceDataType::ALL {
        let host = FakeHost::new();
        let wat = module(
            &[import::TRACE, ONE_PAGE],
            &traced(data_type, EMPTY_REGION, EMPTY_REGION),
        );
        assert_eq!(status(&wat, &host), COMPLETED, "{data_type:?}");
        assert_eq!(
            host.traces().first().map(|t| t.data_type),
            Some(data_type),
            "{data_type:?}"
        );
    }
}

/// A code no type carries is the guest's mistake, and there is no channel to tell it
/// so: the call is dropped and the run carries on.
#[test]
fn a_code_that_names_no_data_type_drops_the_call() {
    for code in [0, -1, 8] {
        let host = FakeHost::new();
        let wat = module(
            &[import::TRACE, ONE_PAGE],
            &format!(
                "(call $trace (i32.const 0) (i32.const 0) (i32.const {code}) (i32.const 0) (i32.const 0))
                 (i32.const {COMPLETED})"
            ),
        );
        assert_eq!(status(&wat, &host), COMPLETED, "code {code}");
        assert!(
            host.traces().is_empty(),
            "code {code}: the host is not called"
        );
    }
}

/// A `&str` parameter is a byte region the engine validates: the host is handed
/// a `&str`, so bytes that are not UTF-8 cannot be passed on.
#[test]
fn a_message_that_is_not_utf8_is_refused() {
    let host = FakeHost::new();

    let wat = module(
        &[import::TRACE, ONE_PAGE, r#"(data (i32.const 0) "\ff\fe")"#],
        &traced(
            TraceDataType::AsText,
            "(i32.const 0) (i32.const 2)",
            EMPTY_REGION,
        ),
    );
    assert_eq!(status(&wat, &host), COMPLETED);
    assert!(host.traces().is_empty(), "the host must not be called");
}

/// The error a host with no result to report may still return: a soft one is the
/// engine's to drop, since there is nowhere to put it and the contract asked
/// nothing.
#[test]
fn a_soft_error_from_a_call_with_no_result_is_dropped() {
    let host = FakeHost::new().failing_trace(HostError::InvalidParams);

    let wat = module(
        &[import::TRACE, ONE_PAGE],
        &traced(TraceDataType::AsText, EMPTY_REGION, EMPTY_REGION),
    );
    assert_eq!(status(&wat, &host), COMPLETED);
    assert_eq!(host.traces().len(), 1, "the host was called and failed");
}

/// A host-fatal error is not an answer to the call, so having no answer to give
/// changes nothing: the run stops.
#[test]
fn a_fatal_error_from_a_call_with_no_result_still_stops_the_run() {
    let host = FakeHost::new().failing_trace(HostError::Internal);

    let wat = module(
        &[import::TRACE, ONE_PAGE],
        &traced(TraceDataType::AsText, EMPTY_REGION, EMPTY_REGION),
    );
    assert!(
        matches!(failure(&wat, &host).error, RunError::Internal),
        "a fatal host error must stop the run"
    );
}

/// Several host calls in one run each see their own arguments: the two fields answer
/// with distinct marker bytes and `finish` returns their sum, so a value landing in
/// the wrong place gives a different total.
#[test]
fn calls_do_not_bleed_into_each_other() {
    const FIRST: u8 = 11;
    const SECOND: u8 = 22;

    let host = FakeHost::new()
        .answering_field(1, support::Answer::bytes([FIRST]))
        .answering_field(2, support::Answer::bytes([SECOND, SECOND]));

    let wat = module(
        &[import::HOME_LE_FIELD, ONE_PAGE],
        "(drop (call $home_le_field (i32.const 1) (i32.const 0) (i32.const 64)))
         (drop (call $home_le_field (i32.const 2) (i32.const 64) (i32.const 64)))
         (i32.add (i32.load8_u (i32.const 0)) (i32.load8_u (i32.const 64)))",
    );
    assert_eq!(status(&wat, &host), i32::from(FIRST) + i32::from(SECOND));
    assert_eq!(*host.fields_asked.borrow(), vec![1, 2]);
}

/// The run's outcome carries the entry point's return value, and that value is
/// the guest's own — the engine does not interpret it.
#[test]
fn the_outcome_carries_whatever_the_guest_returned() {
    let host = FakeHost::new();

    for value in [0, 1, -1, i32::MAX, i32::MIN] {
        let wat = module(&[ONE_PAGE], &format!("(i32.const {value})"));
        let outcome = run(&wat, &host).expect("the module should run");
        assert_eq!(outcome.result, value);
    }
}
