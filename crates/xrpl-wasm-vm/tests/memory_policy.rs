//! The bounds, field-cap and buffer-fit rules `abi.rs` enforces on every region
//! crossing the boundary. This is the policy the guest observes, so each rule is
//! pinned to the code it answers with.

mod support;

use support::{Answer, FakeHost, ONE_PAGE, code, failure, import, module, status};
use xrpl_host_functions::{HASH_LEN, HostError};
use xrpl_wasm_vm::{MAX_FIELD_BYTES, RunError};

/// One page, so anything at or past 65536 is out of bounds.
const PAGE: i64 = 64 * 1024;

/// The per-field size cap, as a wasm operand.
const CAP: i64 = MAX_FIELD_BYTES as i64;
/// One byte over the cap: the smallest value the engine must refuse.
const OVER_CAP: i64 = CAP + 1;

// ---------------------------------------------------------------------------
// Output regions (`write_into`)
// ---------------------------------------------------------------------------

/// The whole output region must be in bounds, not merely its start — the engine
/// checks `[dst, dst + cap)` before the host is allowed to write.
#[test]
fn an_output_region_running_past_memory_is_refused() {
    let host = FakeHost::new();

    for (dst, cap) in [(PAGE, 4), (PAGE - 3, 4), (PAGE + 1024, 4), (0, PAGE + 1)] {
        let wat = module(
            &[import::LDGR_INDEX, ONE_PAGE],
            &format!("(call $ldgr_index (i32.const {dst}) (i32.const {cap}))"),
        );
        assert_eq!(
            status(&wat, &host),
            code(HostError::PointerOutOfBounds),
            "dst {dst} cap {cap}"
        );
    }
}

/// A region ending exactly at the last byte of memory is in bounds.
#[test]
fn an_output_region_ending_at_the_last_byte_is_allowed() {
    let host = FakeHost::new();

    let wat = module(
        &[import::LDGR_INDEX, ONE_PAGE],
        &format!("(call $ldgr_index (i32.const {}) (i32.const 4))", PAGE - 4),
    );
    assert_eq!(status(&wat, &host), 4);
}

/// The wire carries `i32`, so a guest can present a negative pointer or length.
#[test]
fn a_negative_output_pointer_or_length_is_refused() {
    let host = FakeHost::new();

    for (dst, cap) in [(-1, 4), (0, -1), (-1, -1), (i32::MIN, 4)] {
        let wat = module(
            &[import::LDGR_INDEX, ONE_PAGE],
            &format!("(call $ldgr_index (i32.const {dst}) (i32.const {cap}))"),
        );
        assert_eq!(
            status(&wat, &host),
            code(HostError::InvalidParams),
            "dst {dst} cap {cap}"
        );
    }
}

/// The host reports a value's true length whether or not it fitted; a value that
/// did not fit is the guest's error, not the host's.
#[test]
fn a_value_larger_than_the_buffer_is_refused() {
    let host = FakeHost::new().answering_field(1, Answer::filler(64));

    let wat = module(
        &[import::HOME_LE_FIELD, ONE_PAGE],
        "(call $home_le_field (i32.const 1) (i32.const 0) (i32.const 63))",
    );
    assert_eq!(status(&wat, &host), code(HostError::BufferTooSmall));

    let wat = module(
        &[import::HOME_LE_FIELD, ONE_PAGE],
        "(call $home_le_field (i32.const 1) (i32.const 0) (i32.const 64))",
    );
    assert_eq!(status(&wat, &host), 64, "exactly enough room is enough");
}

/// A zero-length output region is in bounds and simply cannot hold anything.
#[test]
fn a_zero_length_output_region_is_in_bounds_but_too_small() {
    let host = FakeHost::new();

    let wat = module(
        &[import::LDGR_INDEX, ONE_PAGE],
        "(call $ldgr_index (i32.const 0) (i32.const 0))",
    );
    assert_eq!(status(&wat, &host), code(HostError::BufferTooSmall));
}

/// A host that reports more than the per-field cap is refused even when the
/// guest offered room for it: the cap is the engine's rule, not the buffer's.
#[test]
fn a_value_past_the_field_cap_is_refused() {
    let host = FakeHost::new()
        .answering_field(1, Answer::claiming(OVER_CAP as usize))
        .answering_field(2, Answer::claiming(MAX_FIELD_BYTES));

    let wat = module(
        &[import::HOME_LE_FIELD, ONE_PAGE],
        "(call $home_le_field (i32.const 1) (i32.const 0) (i32.const 4096))",
    );
    assert_eq!(status(&wat, &host), code(HostError::DataFieldTooLarge));

    let wat = module(
        &[import::HOME_LE_FIELD, ONE_PAGE],
        "(call $home_le_field (i32.const 2) (i32.const 0) (i32.const 4096))",
    );
    assert_eq!(status(&wat, &host), CAP as i32, "the cap itself is allowed");
}

/// **Pins current behaviour, not a decision.** `write_into` checks the field cap
/// after `fill` has written, so an over-cap value reaches the guest's own buffer
/// and is then refused. Finding A4 in `docs/claude/redesign_impl.md` says the write
/// should be clamped instead.
///
/// The host answers with a real over-cap value: [`Answer::claiming`] writes
/// nothing and so could not show the bytes landing.
#[test]
fn an_over_cap_value_is_written_before_it_is_refused() {
    let over_cap = vec![0xff; MAX_FIELD_BYTES + 1];
    let host = FakeHost::new().answering_field(1, Answer::bytes(over_cap));

    let wat = module(
        &[import::HOME_LE_FIELD, ONE_PAGE],
        "(drop (call $home_le_field (i32.const 1) (i32.const 0) (i32.const 4096)))
         (i32.load8_u (i32.const 0))",
    );
    // The status the guest sees, from a module that returns it directly.
    let refusing = module(
        &[import::HOME_LE_FIELD, ONE_PAGE],
        "(call $home_le_field (i32.const 1) (i32.const 0) (i32.const 4096))",
    );
    assert_eq!(
        status(&refusing, &host),
        code(HostError::DataFieldTooLarge),
        "the value is refused"
    );
    assert_eq!(
        status(&wat, &host),
        0xff,
        "but its bytes are already in guest memory"
    );
}

/// The field cap is checked before the buffer-fit rule, so a value that breaks both
/// is reported as over-cap. The guest branches on the code, and the two rules
/// answer different questions, so the order is worth pinning.
#[test]
fn the_field_cap_precedes_the_buffer_fit_check() {
    let host = FakeHost::new().answering_field(1, Answer::claiming(MAX_FIELD_BYTES + 1));

    // A 63-byte buffer: the value is both over the cap and far too big to fit.
    let wat = module(
        &[import::HOME_LE_FIELD, ONE_PAGE],
        "(call $home_le_field (i32.const 1) (i32.const 0) (i32.const 63))",
    );
    assert_eq!(status(&wat, &host), code(HostError::DataFieldTooLarge));
}

// ---------------------------------------------------------------------------
// Input regions (`read_borrowed`, via `trace`)
// ---------------------------------------------------------------------------

/// An input region is bounds-checked the same way an output region is. Every case
/// here stays within the field cap, which on an input is checked first.
#[test]
fn an_input_region_running_past_memory_is_refused() {
    let host = FakeHost::new();

    for (ptr, len) in [(PAGE, 1), (PAGE - 3, 4), (PAGE - 1, CAP)] {
        let wat = module(
            &[import::TRACE_NUM, ONE_PAGE],
            &format!("(call $trace_num (i32.const {ptr}) (i32.const {len}) (i64.const 0))"),
        );
        assert_eq!(
            status(&wat, &host),
            code(HostError::PointerOutOfBounds),
            "ptr {ptr} len {len}"
        );
        assert!(host.traces().is_empty(), "the host must not be called");
    }
}

#[test]
fn a_negative_input_pointer_or_length_is_refused() {
    let host = FakeHost::new();

    for (ptr, len) in [(-1, 1), (0, -1), (i32::MIN, 1)] {
        let wat = module(
            &[import::TRACE_NUM, ONE_PAGE],
            &format!("(call $trace_num (i32.const {ptr}) (i32.const {len}) (i64.const 0))"),
        );
        assert_eq!(
            status(&wat, &host),
            code(HostError::InvalidParams),
            "ptr {ptr} len {len}"
        );
    }
}

/// The field cap bounds what the guest may hand *in*, too.
#[test]
fn an_input_past_the_field_cap_is_refused() {
    let host = FakeHost::new();

    let wat = module(
        &[import::TRACE_NUM, ONE_PAGE],
        &format!("(call $trace_num (i32.const 0) (i32.const {OVER_CAP}) (i64.const 0))"),
    );
    assert_eq!(status(&wat, &host), code(HostError::DataFieldTooLarge));
    assert!(host.traces().is_empty());

    let wat = module(
        &[import::TRACE_NUM, ONE_PAGE],
        &format!("(call $trace_num (i32.const 0) (i32.const {CAP}) (i64.const 0))"),
    );
    assert_eq!(status(&wat, &host), 0, "the cap itself is allowed");
}

/// The two directions check in opposite orders: an input's length is known before
/// the read, so the cap comes first, while an output's region has to be resolved
/// before the host can produce a value, so bounds come first there.
#[test]
fn the_field_cap_precedes_the_bounds_check_on_an_input() {
    let host = FakeHost::new();

    let reading = module(
        &[import::TRACE_NUM, ONE_PAGE],
        &format!(
            "(call $trace_num (i32.const 0) (i32.const {}) (i64.const 0))",
            PAGE + 1
        ),
    );
    assert_eq!(status(&reading, &host), code(HostError::DataFieldTooLarge));

    let writing = module(
        &[import::LDGR_INDEX, ONE_PAGE],
        &format!("(call $ldgr_index (i32.const 0) (i32.const {}))", PAGE + 1),
    );
    assert_eq!(status(&writing, &host), code(HostError::PointerOutOfBounds));
}

/// `trace` reads two regions, and either one being bad refuses the call.
#[test]
fn both_of_traces_regions_are_checked() {
    let host = FakeHost::new();

    let bad_msg = module(
        &[import::TRACE, ONE_PAGE],
        &format!(
            "(call $trace (i32.const {PAGE}) (i32.const 1) (i32.const 0) (i32.const 1) (i32.const 0))"
        ),
    );
    assert_eq!(status(&bad_msg, &host), code(HostError::PointerOutOfBounds));

    let bad_data = module(
        &[import::TRACE, ONE_PAGE],
        &format!(
            "(call $trace (i32.const 0) (i32.const 1) (i32.const {PAGE}) (i32.const 1) (i32.const 0))"
        ),
    );
    assert_eq!(
        status(&bad_data, &host),
        code(HostError::PointerOutOfBounds)
    );
    assert!(host.traces().is_empty());
}

// ---------------------------------------------------------------------------
// Both at once (`read_write`, via `sha512_half`)
// ---------------------------------------------------------------------------

/// A call with an input and an output region checks the input first, so a bad
/// input is reported even when the output region is also bad.
#[test]
fn a_read_write_checks_its_input_before_its_output() {
    let host = FakeHost::new();
    let digest = |src: i64, src_len: i64, dst: i64| {
        module(
            &[import::SHA512_HALF, ONE_PAGE],
            &format!(
                "(call $sha512_half (i32.const {src}) (i32.const {src_len})
                                    (i32.const {dst}) (i32.const {HASH_LEN}))"
            ),
        )
    };

    let over_cap = digest(0, OVER_CAP, 0);
    assert_eq!(status(&over_cap, &host), code(HostError::DataFieldTooLarge));

    let out_of_bounds = digest(PAGE, 4, 0);
    assert_eq!(
        status(&out_of_bounds, &host),
        code(HostError::PointerOutOfBounds)
    );

    // A bad input and a bad output: the input's verdict is the one reported.
    let both_bad = digest(0, OVER_CAP, PAGE);
    assert_eq!(status(&both_bad, &host), code(HostError::DataFieldTooLarge));
    assert!(host.digested.borrow().is_empty(), "the host is not reached");
}

/// The output half of a read-write call obeys the same rules as a plain write.
#[test]
fn a_read_write_output_obeys_the_write_rules() {
    let host = FakeHost::new().answering_digest(Answer::filler(32));

    let wat = module(
        &[import::SHA512_HALF, ONE_PAGE],
        "(call $sha512_half (i32.const 0) (i32.const 4) (i32.const 0) (i32.const 31))",
    );
    assert_eq!(status(&wat, &host), code(HostError::BufferTooSmall));

    let wat = module(
        &[import::SHA512_HALF, ONE_PAGE],
        &format!(
            "(call $sha512_half (i32.const 0) (i32.const 4) (i32.const {PAGE}) (i32.const 32))"
        ),
    );
    assert_eq!(status(&wat, &host), code(HostError::PointerOutOfBounds));
}

/// An input region may overlap the output region: the engine copies the input out
/// of guest memory before the host writes back into it. The marker is any byte
/// distinct from the input's first (`a`), so `finish` returning it proves the write
/// landed.
#[test]
fn an_input_may_overlap_the_output() {
    const MARKER: u8 = 99;

    let host = FakeHost::new().answering_digest(Answer::bytes([MARKER; HASH_LEN]));

    let wat = module(
        &[
            import::SHA512_HALF,
            ONE_PAGE,
            r#"(data (i32.const 0) "abcd")"#,
        ],
        &format!(
            "(drop (call $sha512_half (i32.const 0) (i32.const 4)
                                      (i32.const 0) (i32.const {HASH_LEN})))
             (i32.load8_u (i32.const 0))"
        ),
    );
    assert_eq!(
        status(&wat, &host),
        i32::from(MARKER),
        "the output overwrote the input"
    );
    assert_eq!(
        *host.digested.borrow(),
        vec![b"abcd".to_vec()],
        "the host saw the input as it was"
    );
}

// ---------------------------------------------------------------------------
// The memory export itself
// ---------------------------------------------------------------------------

/// A host call with no memory to work in ends the run instead of answering the
/// guest: there is no buffer for a status to describe, and nothing the guest could
/// do about the answer — which is what puts this beside out-of-gas on the fatal
/// channel. What the guest burned getting there is still charged.
fn assert_no_memory(wat: &str, host: &FakeHost) {
    let failure = failure(wat, host);
    assert!(
        matches!(failure.error, RunError::NoMemory),
        "expected the run to end for want of a memory export, got: {failure}"
    );
    assert!(failure.fuel_used > 0, "{failure}");
}

/// Every region is relative to the guest's exported memory, so a module without
/// one cannot make a host call at all.
#[test]
fn a_module_that_exports_no_memory_cannot_call_the_host() {
    let host = FakeHost::new();

    let wat = module(
        &[import::LDGR_INDEX, "(memory 1)"],
        "(call $ldgr_index (i32.const 0) (i32.const 4))",
    );
    assert_no_memory(&wat, &host);
}

/// The export has to be named `memory`, and it has to *be* a memory — a global
/// under that name is not a near miss the engine tolerates.
#[test]
fn the_memory_export_must_be_a_memory_named_memory() {
    let host = FakeHost::new();

    // The right kind under the wrong name.
    let misnamed = module(
        &[import::LDGR_INDEX, r#"(memory (export "mem") 1)"#],
        "(call $ldgr_index (i32.const 0) (i32.const 4))",
    );
    assert_no_memory(&misnamed, &host);

    // The right name on the wrong kind, which is the other arm of the match.
    let wrong_kind = module(
        &[
            import::LDGR_INDEX,
            "(memory 1)",
            r#"(global (export "memory") i32 (i32.const 0))"#,
        ],
        "(call $ldgr_index (i32.const 0) (i32.const 4))",
    );
    assert_no_memory(&wrong_kind, &host);
}

/// Bounds follow the memory the module actually declared, not a fixed page.
#[test]
fn bounds_follow_the_declared_memory_size() {
    let host = FakeHost::new();

    let wat = module(
        &[import::LDGR_INDEX, r#"(memory (export "memory") 2)"#],
        &format!("(call $ldgr_index (i32.const {PAGE}) (i32.const 4))"),
    );
    assert_eq!(status(&wat, &host), 4, "the second page is in bounds");
}
