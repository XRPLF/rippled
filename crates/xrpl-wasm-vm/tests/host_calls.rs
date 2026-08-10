//! What each registered host function passes in each direction: the scalars the
//! guest supplies reach the host unchanged, and the bytes the host produces land
//! where the guest asked for them.

mod support;

use support::{FakeHost, ONE_PAGE, Trace, code, import, module, run, status};
use xrpl_host_functions::{HASH_LEN, HostError};

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

/// A second scalar getter travels the same path: the value the host supplies lands
/// where the guest asked, and the status is the byte count. The default parent
/// ledger time is distinct from the sequence number, so this cannot pass by reading
/// the wrong one.
#[test]
fn parent_ldgr_time_writes_the_close_time_where_the_guest_asked() {
    let host = FakeHost::new();

    let wat = module(
        &[import::PARENT_LDGR_TIME, ONE_PAGE],
        "(drop (call $parent_ldgr_time (i32.const 64) (i32.const 4)))
         (i32.load (i32.const 64))",
    );
    assert_eq!(status(&wat, &host), 9, "the 4 LE bytes the host wrote");

    let wat = module(
        &[import::PARENT_LDGR_TIME, ONE_PAGE],
        "(call $parent_ldgr_time (i32.const 64) (i32.const 4))",
    );
    assert_eq!(status(&wat, &host), 4, "the byte count");
}

/// A 32-byte value (a ledger hash) travels the same getter path as the 4-byte
/// scalars: every byte lands where the guest asked, and the status is the length.
#[test]
fn parent_ldgr_hash_writes_all_32_bytes_where_the_guest_asked() {
    let host = FakeHost::new();

    let wat = module(
        &[import::PARENT_LDGR_HASH, ONE_PAGE],
        "(call $parent_ldgr_hash (i32.const 64) (i32.const 32))",
    );
    assert_eq!(status(&wat, &host), 32, "the byte count");

    // The default hash is 0, 1, 2, ..., so its first four bytes load as 0x03020100.
    let wat = module(
        &[import::PARENT_LDGR_HASH, ONE_PAGE],
        "(drop (call $parent_ldgr_hash (i32.const 64) (i32.const 32)))
         (i32.load (i32.const 64))",
    );
    assert_eq!(
        status(&wat, &host),
        0x03020100,
        "the first four bytes the host wrote"
    );
}

/// A third scalar getter, to pin the pattern rather than a single instance of it.
#[test]
fn base_fee_writes_the_fee_where_the_guest_asked() {
    let host = FakeHost::new();

    let wat = module(
        &[import::BASE_FEE, ONE_PAGE],
        "(drop (call $base_fee (i32.const 64) (i32.const 4)))
         (i32.load (i32.const 64))",
    );
    assert_eq!(status(&wat, &host), 10, "the 4 LE bytes the host wrote");

    let wat = module(
        &[import::BASE_FEE, ONE_PAGE],
        "(call $base_fee (i32.const 64) (i32.const 4))",
    );
    assert_eq!(status(&wat, &host), 4, "the byte count");
}

/// A call that reads an input region and returns a scalar flag, rather than writing
/// bytes to an output region: the amendment reaches the host, and its verdict comes
/// back as the call's status.
#[test]
fn amendment_enabled_reads_the_input_and_returns_the_flag() {
    let host = FakeHost::new(); // enabled by default

    let wat = module(
        &[import::AMENDMENT_ENABLED, ONE_PAGE],
        "(call $amendment_enabled (i32.const 64) (i32.const 32))",
    );
    assert_eq!(status(&wat, &host), 1, "the enabled flag");
    assert_eq!(
        *host.amendments_asked.borrow(),
        [vec![0u8; 32]],
        "the 32-byte region reached the host"
    );

    // A host that reports the amendment disabled answers 0 — a value, not an error.
    let host = FakeHost::new().answering_amendment_enabled(Ok(0));
    let wat = module(
        &[import::AMENDMENT_ENABLED, ONE_PAGE],
        "(call $amendment_enabled (i32.const 0) (i32.const 32))",
    );
    assert_eq!(status(&wat, &host), 0, "the disabled flag");
}

/// A call that reads an input region and takes a second scalar arg: both the object
/// id and the requested slot reach the host, and the slot it chose comes back as the
/// status.
#[test]
fn cache_le_passes_the_object_id_and_slot_through() {
    let host = FakeHost::new().answering_cache_slot(Ok(4));

    let wat = module(
        &[import::CACHE_LE, ONE_PAGE],
        "(call $cache_le (i32.const 64) (i32.const 32) (i32.const 7))",
    );
    assert_eq!(status(&wat, &host), 4, "the slot the host chose");
    assert_eq!(
        *host.cached.borrow(),
        [(vec![0u8; 32], 7)],
        "the id region and the requested slot reached the host"
    );
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

/// A field getter over the transaction: the selector reaches the host, and the bytes
/// it answers land where the guest asked. It has its own answer set, distinct from
/// the current-object field getter's.
#[test]
fn tx_field_passes_the_selector_and_writes_the_field() {
    let host = FakeHost::new().answering_tx_field(17, support::Answer::bytes([0xab, 0xcd]));

    let wat = module(
        &[import::TX_FIELD, ONE_PAGE],
        "(call $tx_field (i32.const 17) (i32.const 0) (i32.const 64))",
    );
    assert_eq!(status(&wat, &host), 2);
    assert_eq!(*host.tx_fields_asked.borrow(), vec![17]);
}

/// A field getter over a cached object: both the slot and the selector reach the
/// host, keyed together, and the answered bytes land where the guest asked.
#[test]
fn le_field_passes_the_slot_and_selector_through() {
    let host =
        FakeHost::new().answering_le_field(2, 17, support::Answer::bytes([0xab, 0xcd, 0xef]));

    let wat = module(
        &[import::LE_FIELD, ONE_PAGE],
        "(call $le_field (i32.const 2) (i32.const 17) (i32.const 0) (i32.const 64))",
    );
    assert_eq!(status(&wat, &host), 3);
    assert_eq!(*host.le_fields_asked.borrow(), vec![(2, 17)]);
}

/// A nested-field getter: the locator is read from one region and the answer written
/// to another — the read-input-write-output path. The guest lays the locator down in
/// memory, and the bytes the host answers land where it asked.
#[test]
fn tx_inner_reads_the_locator_and_writes_the_field() {
    // An eight-byte, two-step locator, as it lands in little-endian guest memory.
    let locator = vec![17u8, 0, 0, 0, 2, 0, 0, 0];
    let host =
        FakeHost::new().answering_tx_nested(locator.clone(), support::Answer::bytes([0xaa, 0xbb]));

    let wat = module(
        &[import::TX_INNER, ONE_PAGE],
        "(i32.store (i32.const 0) (i32.const 17))
         (i32.store (i32.const 4) (i32.const 2))
         (call $tx_inner (i32.const 0) (i32.const 8) (i32.const 64) (i32.const 64))",
    );
    assert_eq!(status(&wat, &host), 2, "the field bytes the host wrote");
    assert_eq!(*host.tx_nested_asked.borrow(), vec![locator]);
}

/// The same read-input-write-output path over the current object, with its own
/// answer set distinct from the transaction's nested getter.
#[test]
fn home_le_inner_reads_the_locator_and_writes_the_field() {
    let locator = vec![5u8, 0, 0, 0];
    let host = FakeHost::new()
        .answering_home_le_nested(locator.clone(), support::Answer::bytes([0xcc, 0xdd, 0xee]));

    let wat = module(
        &[import::HOME_LE_INNER, ONE_PAGE],
        "(i32.store (i32.const 0) (i32.const 5))
         (call $home_le_inner (i32.const 0) (i32.const 4) (i32.const 64) (i32.const 64))",
    );
    assert_eq!(status(&wat, &host), 3, "the field bytes the host wrote");
    assert_eq!(*host.home_le_nested_asked.borrow(), vec![locator]);
}

/// The nested getter over a cached object: the slot leads, the locator is read from
/// memory, and the two reach the host keyed together.
#[test]
fn le_inner_reads_the_slot_and_locator_and_writes_the_field() {
    let locator = vec![5u8, 0, 0, 0];
    let host = FakeHost::new().answering_le_nested(
        3,
        locator.clone(),
        support::Answer::bytes([0x11, 0x22]),
    );

    let wat = module(
        &[import::LE_INNER, ONE_PAGE],
        "(i32.store (i32.const 0) (i32.const 5))
         (call $le_inner (i32.const 3) (i32.const 0) (i32.const 4) (i32.const 64) (i32.const 64))",
    );
    assert_eq!(status(&wat, &host), 2, "the field bytes the host wrote");
    assert_eq!(*host.le_nested_asked.borrow(), vec![(3, locator)]);
}

/// A scalar-in, scalar-out call — no memory regions at all: the field selector
/// reaches the host and the array length comes back as the status.
#[test]
fn tx_arr_len_passes_the_selector_and_returns_the_count() {
    let host = FakeHost::new().answering_tx_arr_len(17, 5);

    let wat = module(
        &[import::TX_ARR_LEN, ONE_PAGE],
        "(call $tx_arr_len (i32.const 17))",
    );
    assert_eq!(status(&wat, &host), 5, "the array length");
    assert_eq!(*host.tx_arr_lens_asked.borrow(), vec![17]);
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

/// `trace` reads two regions and a flag, and yields a status of 0.
#[test]
fn trace_passes_its_message_data_and_flag_through() {
    let host = FakeHost::new();

    let wat = module(
        &[
            import::TRACE,
            ONE_PAGE,
            r#"(data (i32.const 0) "note")"#,
            r#"(data (i32.const 16) "\01\02\03")"#,
        ],
        "(call $trace (i32.const 0) (i32.const 4) (i32.const 16) (i32.const 3) (i32.const 1))",
    );
    assert_eq!(status(&wat, &host), 0, "trace yields a status of 0");
    assert_eq!(
        host.traces(),
        vec![Trace::Message {
            msg: "note".to_owned(),
            data: vec![1, 2, 3],
            as_hex: true,
        }]
    );
}

/// The flag is `bool` in the declaration and `i32` on the wire: nonzero is true.
#[test]
fn any_nonzero_flag_is_true() {
    for (flag, expected) in [("0", false), ("1", true), ("2", true), ("-1", true)] {
        let host = FakeHost::new();
        let wat = module(
            &[import::TRACE, ONE_PAGE],
            &format!(
                "(call $trace (i32.const 0) (i32.const 0) (i32.const 0) (i32.const 0) (i32.const {flag}))"
            ),
        );
        assert_eq!(status(&wat, &host), 0);
        let Some(Trace::Message { as_hex, .. }) = host.traces().first().cloned() else {
            panic!("expected one traced message");
        };
        assert_eq!(as_hex, expected, "flag {flag}");
    }
}

/// An `i64` parameter crosses as an `i64`, full width.
#[test]
fn trace_num_carries_a_full_width_i64() {
    for number in [0, 1, -1, i64::MAX, i64::MIN] {
        let host = FakeHost::new();
        let wat = module(
            &[import::TRACE_NUM, ONE_PAGE],
            &format!("(call $trace_num (i32.const 0) (i32.const 0) (i64.const {number}))"),
        );
        assert_eq!(status(&wat, &host), 0);
        assert_eq!(
            host.traces(),
            vec![Trace::Number {
                msg: String::new(),
                number,
            }]
        );
    }
}

/// A `&str` parameter is a byte region the engine validates: the host is handed
/// a `&str`, so bytes that are not UTF-8 cannot be passed on.
#[test]
fn a_message_that_is_not_utf8_is_refused() {
    let host = FakeHost::new();

    let wat = module(
        &[
            import::TRACE_NUM,
            ONE_PAGE,
            r#"(data (i32.const 0) "\ff\fe")"#,
        ],
        "(call $trace_num (i32.const 0) (i32.const 2) (i64.const 0))",
    );
    assert_eq!(status(&wat, &host), code(HostError::Decoding));
    assert!(host.traces().is_empty(), "the host must not be called");
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
