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

/// The same scalar-in, scalar-out count over the current object, with its own answer
/// set distinct from the transaction's.
#[test]
fn home_le_arr_len_passes_the_selector_and_returns_the_count() {
    let host = FakeHost::new().answering_home_le_arr_len(17, 8);

    let wat = module(
        &[import::HOME_LE_ARR_LEN, ONE_PAGE],
        "(call $home_le_arr_len (i32.const 17))",
    );
    assert_eq!(status(&wat, &host), 8, "the array length");
    assert_eq!(*host.home_le_arr_lens_asked.borrow(), vec![17]);
}

/// The scalar count over a cached object: the slot leads, and both it and the
/// selector reach the host keyed together.
#[test]
fn le_arr_len_passes_the_slot_and_selector_and_returns_the_count() {
    let host = FakeHost::new().answering_le_arr_len(2, 17, 9);

    let wat = module(
        &[import::LE_ARR_LEN, ONE_PAGE],
        "(call $le_arr_len (i32.const 2) (i32.const 17))",
    );
    assert_eq!(status(&wat, &host), 9, "the array length");
    assert_eq!(*host.le_arr_lens_asked.borrow(), vec![(2, 17)]);
}

/// A nested array-length getter: the locator is read from memory and the count comes
/// back as the status — read-input, scalar-out, no output buffer.
#[test]
fn tx_inner_arr_len_reads_the_locator_and_returns_the_count() {
    let locator = vec![5u8, 0, 0, 0];
    let host = FakeHost::new().answering_tx_nested_arr_len(locator.clone(), 6);

    let wat = module(
        &[import::TX_INNER_ARR_LEN, ONE_PAGE],
        "(i32.store (i32.const 0) (i32.const 5))
         (call $tx_inner_arr_len (i32.const 0) (i32.const 4))",
    );
    assert_eq!(status(&wat, &host), 6, "the array length");
    assert_eq!(*host.tx_nested_arr_lens_asked.borrow(), vec![locator]);
}

/// The same read-input, scalar-out count over the current object, with its own answer
/// set distinct from the transaction's.
#[test]
fn home_le_inner_arr_len_reads_the_locator_and_returns_the_count() {
    let locator = vec![5u8, 0, 0, 0];
    let host = FakeHost::new().answering_home_le_nested_arr_len(locator.clone(), 7);

    let wat = module(
        &[import::HOME_LE_INNER_ARR_LEN, ONE_PAGE],
        "(i32.store (i32.const 0) (i32.const 5))
         (call $home_le_inner_arr_len (i32.const 0) (i32.const 4))",
    );
    assert_eq!(status(&wat, &host), 7, "the array length");
    assert_eq!(*host.home_le_nested_arr_lens_asked.borrow(), vec![locator]);
}

/// The nested array-length getter over a cached object: the slot leads, the locator
/// is read from memory, and the two reach the host keyed together.
#[test]
fn le_inner_arr_len_reads_the_slot_and_locator_and_returns_the_count() {
    let locator = vec![5u8, 0, 0, 0];
    let host = FakeHost::new().answering_le_nested_arr_len(3, locator.clone(), 8);

    let wat = module(
        &[import::LE_INNER_ARR_LEN, ONE_PAGE],
        "(i32.store (i32.const 0) (i32.const 5))
         (call $le_inner_arr_len (i32.const 3) (i32.const 0) (i32.const 4))",
    );
    assert_eq!(status(&wat, &host), 8, "the array length");
    assert_eq!(*host.le_nested_arr_lens_asked.borrow(), vec![(3, locator)]);
}

/// A call that reads three input regions and returns a scalar verdict: the message,
/// signature, and pubkey all reach the host, and the verdict comes back as the status.
#[test]
fn check_sig_reads_all_three_regions_and_returns_the_verdict() {
    let host = FakeHost::new(); // valid by default

    // message @0 len 3, signature @8 len 4, pubkey @16 len 5 — memory is zeroed.
    let wat = module(
        &[import::CHECK_SIG, ONE_PAGE],
        "(call $check_sig
            (i32.const 0) (i32.const 3)
            (i32.const 8) (i32.const 4)
            (i32.const 16) (i32.const 5))",
    );
    assert_eq!(status(&wat, &host), 1, "the valid verdict");
    assert_eq!(
        *host.sigs_checked.borrow(),
        [(vec![0u8; 3], vec![0u8; 4], vec![0u8; 5])],
        "the three regions reached the host at their declared lengths"
    );

    // An invalid signature comes back as 0 — a value, not an error.
    let host = FakeHost::new().answering_check_sig(Ok(0));
    assert_eq!(status(&wat, &host), 0, "the invalid verdict");
}

/// A keylet getter: reads an account region and writes a 32-byte keylet back — the
/// read-input-write-output path. The account reaches the host and the keylet lands
/// where the guest asked.
#[test]
fn accountroot_id_reads_the_account_and_writes_the_keylet() {
    // Guest memory is zeroed, so a 20-byte account read is all zeros.
    let account = vec![0u8; 20];
    let host =
        FakeHost::new().answering_account_keylet(account.clone(), support::Answer::filler(32));

    let wat = module(
        &[import::ACCOUNTROOT_ID, ONE_PAGE],
        "(call $accountroot_id (i32.const 0) (i32.const 20) (i32.const 64) (i32.const 64))",
    );
    assert_eq!(status(&wat, &host), 32, "the 32-byte keylet length");
    assert_eq!(*host.account_keylets_asked.borrow(), vec![account]);

    // The keylet bytes land at the output pointer: filler is 0, 1, 2, ..., so the
    // first four load as 0x03020100.
    let wat = module(
        &[import::ACCOUNTROOT_ID, ONE_PAGE],
        "(drop (call $accountroot_id (i32.const 0) (i32.const 20) (i32.const 64) (i32.const 64)))
         (i32.load (i32.const 64))",
    );
    assert_eq!(
        status(&wat, &host),
        0x03020100,
        "the first four keylet bytes"
    );
}

/// A keylet getter that reads two input regions: both assets reach the host as a
/// pair, and the keylet lands where the guest asked.
#[test]
fn amm_id_reads_two_assets_and_writes_the_keylet() {
    // Two distinct all-zero assets of different lengths (20 and 40 bytes).
    let asset1 = vec![0u8; 20];
    let asset2 = vec![0u8; 40];
    let host = FakeHost::new().answering_amm_keylet(
        asset1.clone(),
        asset2.clone(),
        support::Answer::filler(32),
    );

    let wat = module(
        &[import::AMM_ID, ONE_PAGE],
        "(call $amm_id
            (i32.const 0) (i32.const 20)
            (i32.const 64) (i32.const 40)
            (i32.const 128) (i32.const 64))",
    );
    assert_eq!(status(&wat, &host), 32, "the 32-byte keylet length");
    assert_eq!(*host.amm_keylets_asked.borrow(), vec![(asset1, asset2)]);
}

/// A keylet getter that reads an account region and also takes a scalar seq: both
/// reach the host keyed together, and the keylet lands where the guest asked.
#[test]
fn check_id_reads_the_account_and_seq_and_writes_the_keylet() {
    // Guest memory is zeroed, so a 20-byte account read is all zeros.
    let account = vec![0u8; 20];
    let host =
        FakeHost::new().answering_check_keylet(account.clone(), 5, support::Answer::filler(32));

    let wat = module(
        &[import::CHECK_ID, ONE_PAGE],
        "(call $check_id (i32.const 0) (i32.const 20) (i32.const 5) (i32.const 64) (i32.const 64))",
    );
    assert_eq!(status(&wat, &host), 32, "the 32-byte keylet length");
    assert_eq!(*host.check_keylets_asked.borrow(), vec![(account, 5)]);
}

/// A keylet getter that reads three input regions — two accounts and a credential
/// type: all three reach the host keyed together, and the keylet lands where asked.
#[test]
fn credential_id_reads_subject_issuer_and_type() {
    // Guest memory is zeroed, so the two 20-byte accounts and the 4-byte type read
    // as zeros of their declared lengths.
    let subject = vec![0u8; 20];
    let issuer = vec![0u8; 20];
    let cred_type = vec![0u8; 4];
    let host = FakeHost::new().answering_credential_keylet(
        subject.clone(),
        issuer.clone(),
        cred_type.clone(),
        support::Answer::filler(32),
    );

    let wat = module(
        &[import::CREDENTIAL_ID, ONE_PAGE],
        "(call $credential_id
            (i32.const 0) (i32.const 20)
            (i32.const 20) (i32.const 20)
            (i32.const 40) (i32.const 4)
            (i32.const 64) (i32.const 64))",
    );
    assert_eq!(status(&wat, &host), 32, "the 32-byte keylet length");
    assert_eq!(
        *host.credential_keylets_asked.borrow(),
        vec![(subject, issuer, cred_type)]
    );
}

/// A two-account keylet getter: both accounts reach the host as a pair, and the
/// keylet lands where the guest asked.
#[test]
fn delegate_id_reads_both_accounts_and_writes_the_keylet() {
    let account = vec![0u8; 20];
    let authorize = vec![0u8; 20];
    let host = FakeHost::new().answering_delegate_keylet(
        account.clone(),
        authorize.clone(),
        support::Answer::filler(32),
    );

    let wat = module(
        &[import::DELEGATE_ID, ONE_PAGE],
        "(call $delegate_id
            (i32.const 0) (i32.const 20)
            (i32.const 20) (i32.const 20)
            (i32.const 64) (i32.const 64))",
    );
    assert_eq!(status(&wat, &host), 32, "the 32-byte keylet length");
    assert_eq!(
        *host.delegate_keylets_asked.borrow(),
        vec![(account, authorize)]
    );
}

/// The same two-account keylet shape as delegate, with its own answer set.
#[test]
fn deposit_preauth_id_reads_both_accounts_and_writes_the_keylet() {
    let account = vec![0u8; 20];
    let authorize = vec![0u8; 20];
    let host = FakeHost::new().answering_deposit_preauth_keylet(
        account.clone(),
        authorize.clone(),
        support::Answer::filler(32),
    );

    let wat = module(
        &[import::DEPOSIT_PREAUTH_ID, ONE_PAGE],
        "(call $deposit_preauth_id
            (i32.const 0) (i32.const 20)
            (i32.const 20) (i32.const 20)
            (i32.const 64) (i32.const 64))",
    );
    assert_eq!(status(&wat, &host), 32, "the 32-byte keylet length");
    assert_eq!(
        *host.deposit_preauth_keylets_asked.borrow(),
        vec![(account, authorize)]
    );
}

/// A single-account keylet getter (like accountroot), with its own answer set.
#[test]
fn did_id_reads_the_account_and_writes_the_keylet() {
    let account = vec![0u8; 20];
    let host = FakeHost::new().answering_did_keylet(account.clone(), support::Answer::filler(32));

    let wat = module(
        &[import::DID_ID, ONE_PAGE],
        "(call $did_id (i32.const 0) (i32.const 20) (i32.const 64) (i32.const 64))",
    );
    assert_eq!(status(&wat, &host), 32, "the 32-byte keylet length");
    assert_eq!(*host.did_keylets_asked.borrow(), vec![account]);
}

/// The account-and-sequence keylet shape (like check), with its own answer set.
#[test]
fn escrow_id_reads_the_account_and_seq_and_writes_the_keylet() {
    let account = vec![0u8; 20];
    let host =
        FakeHost::new().answering_escrow_keylet(account.clone(), 5, support::Answer::filler(32));

    let wat = module(
        &[import::ESCROW_ID, ONE_PAGE],
        "(call $escrow_id (i32.const 0) (i32.const 20) (i32.const 5) (i32.const 64) (i32.const 64))",
    );
    assert_eq!(status(&wat, &host), 32, "the 32-byte keylet length");
    assert_eq!(*host.escrow_keylets_asked.borrow(), vec![(account, 5)]);
}

/// A keylet getter reading three regions — two accounts and a currency: all three
/// reach the host as a triple, and the keylet lands where the guest asked.
#[test]
fn trustline_id_reads_two_accounts_and_a_currency() {
    let account1 = vec![0u8; 20];
    let account2 = vec![0u8; 20];
    let currency = vec![0u8; 20];
    let host = FakeHost::new().answering_trust_line_keylet(
        account1.clone(),
        account2.clone(),
        currency.clone(),
        support::Answer::filler(32),
    );

    let wat = module(
        &[import::TRUSTLINE_ID, ONE_PAGE],
        "(call $trustline_id
            (i32.const 0) (i32.const 20)
            (i32.const 20) (i32.const 20)
            (i32.const 40) (i32.const 20)
            (i32.const 64) (i32.const 64))",
    );
    assert_eq!(status(&wat, &host), 32, "the 32-byte keylet length");
    assert_eq!(
        *host.trust_line_keylets_asked.borrow(),
        vec![(account1, account2, currency)]
    );
}

/// The issuer-and-sequence keylet shape (like escrow), with its own answer set.
#[test]
fn mpt_issuance_id_reads_the_issuer_and_seq() {
    let issuer = vec![0u8; 20];
    let host = FakeHost::new().answering_mpt_issuance_keylet(
        issuer.clone(),
        5,
        support::Answer::filler(32),
    );

    let wat = module(
        &[import::MPT_ISSUANCE_ID, ONE_PAGE],
        "(call $mpt_issuance_id (i32.const 0) (i32.const 20) (i32.const 5) (i32.const 64) (i32.const 64))",
    );
    assert_eq!(status(&wat, &host), 32, "the 32-byte keylet length");
    assert_eq!(*host.mpt_issuance_keylets_asked.borrow(), vec![(issuer, 5)]);
}

/// A keylet from a 24-byte MPT id and a 20-byte holder: both reach the host as a
/// pair, and the keylet lands where the guest asked.
#[test]
fn mptoken_id_reads_the_mptid_and_holder() {
    let mptid = vec![0u8; 24];
    let holder = vec![0u8; 20];
    let host = FakeHost::new().answering_mptoken_keylet(
        mptid.clone(),
        holder.clone(),
        support::Answer::filler(32),
    );

    let wat = module(
        &[import::MPTOKEN_ID, ONE_PAGE],
        "(call $mptoken_id
            (i32.const 0) (i32.const 24)
            (i32.const 24) (i32.const 20)
            (i32.const 64) (i32.const 64))",
    );
    assert_eq!(status(&wat, &host), 32, "the 32-byte keylet length");
    assert_eq!(*host.mptoken_keylets_asked.borrow(), vec![(mptid, holder)]);
}

/// Another account-and-sequence keylet, with its own answer set.
#[test]
fn nft_offer_id_reads_the_account_and_seq() {
    let account = vec![0u8; 20];
    let host =
        FakeHost::new().answering_nft_offer_keylet(account.clone(), 5, support::Answer::filler(32));

    let wat = module(
        &[import::NFT_OFFER_ID, ONE_PAGE],
        "(call $nft_offer_id (i32.const 0) (i32.const 20) (i32.const 5) (i32.const 64) (i32.const 64))",
    );
    assert_eq!(status(&wat, &host), 32, "the 32-byte keylet length");
    assert_eq!(*host.nft_offer_keylets_asked.borrow(), vec![(account, 5)]);
}

/// A third account-and-sequence keylet, distinct from the NFT-offer set, to pin the
/// pattern rather than a single instance of it.
#[test]
fn offer_id_reads_the_account_and_seq() {
    let account = vec![0u8; 20];
    let host =
        FakeHost::new().answering_offer_keylet(account.clone(), 5, support::Answer::filler(32));

    let wat = module(
        &[import::OFFER_ID, ONE_PAGE],
        "(call $offer_id (i32.const 0) (i32.const 20) (i32.const 5) (i32.const 64) (i32.const 64))",
    );
    assert_eq!(status(&wat, &host), 32, "the 32-byte keylet length");
    assert_eq!(*host.offer_keylets_asked.borrow(), vec![(account, 5)]);
}

/// The account-and-scalar keylet, keyed on a document id rather than a sequence; its
/// own answer set, to keep it distinct from the other account-and-scalar getters.
#[test]
fn oracle_id_reads_the_account_and_doc_id() {
    let account = vec![0u8; 20];
    let host =
        FakeHost::new().answering_oracle_keylet(account.clone(), 5, support::Answer::filler(32));

    let wat = module(
        &[import::ORACLE_ID, ONE_PAGE],
        "(call $oracle_id (i32.const 0) (i32.const 20) (i32.const 5) (i32.const 64) (i32.const 64))",
    );
    assert_eq!(status(&wat, &host), 32, "the 32-byte keylet length");
    assert_eq!(*host.oracle_keylets_asked.borrow(), vec![(account, 5)]);
}

/// A keylet that reads two account regions and a scalar: both accounts and the
/// sequence reach the host, keyed together, and the answered bytes land where asked.
#[test]
fn paychan_id_reads_both_accounts_and_the_seq() {
    let account = vec![0u8; 20];
    let destination = vec![0u8; 20];
    let host = FakeHost::new().answering_paychannel_keylet(
        account.clone(),
        destination.clone(),
        5,
        support::Answer::filler(32),
    );

    let wat = module(
        &[import::PAYCHAN_ID, ONE_PAGE],
        "(call $paychan_id (i32.const 0) (i32.const 20) (i32.const 32) (i32.const 20) (i32.const 5) (i32.const 64) (i32.const 64))",
    );
    assert_eq!(status(&wat, &host), 32, "the 32-byte keylet length");
    assert_eq!(
        *host.paychannel_keylets_asked.borrow(),
        vec![(account, destination, 5)]
    );
}

/// Another account-and-sequence keylet, with its own answer set, for a permissioned
/// domain.
#[test]
fn permissioned_domain_id_reads_the_account_and_seq() {
    let account = vec![0u8; 20];
    let host = FakeHost::new().answering_permissioned_domain_keylet(
        account.clone(),
        5,
        support::Answer::filler(32),
    );

    let wat = module(
        &[import::PERMISSIONED_DOMAIN_ID, ONE_PAGE],
        "(call $permissioned_domain_id (i32.const 0) (i32.const 20) (i32.const 5) (i32.const 64) (i32.const 64))",
    );
    assert_eq!(status(&wat, &host), 32, "the 32-byte keylet length");
    assert_eq!(*host.domain_keylets_asked.borrow(), vec![(account, 5)]);
}

/// An account-only keylet: the account reaches the host and the answered bytes land
/// where the guest asked, with no scalar in the shape.
#[test]
fn signers_id_reads_the_account() {
    let account = vec![0u8; 20];
    let host =
        FakeHost::new().answering_signer_list_keylet(account.clone(), support::Answer::filler(32));

    let wat = module(
        &[import::SIGNERS_ID, ONE_PAGE],
        "(call $signers_id (i32.const 0) (i32.const 20) (i32.const 64) (i32.const 64))",
    );
    assert_eq!(status(&wat, &host), 32, "the 32-byte keylet length");
    assert_eq!(*host.signer_list_keylets_asked.borrow(), vec![account]);
}

/// Another account-and-sequence keylet, with its own answer set, for a ticket.
#[test]
fn ticket_id_reads_the_account_and_seq() {
    let account = vec![0u8; 20];
    let host =
        FakeHost::new().answering_ticket_keylet(account.clone(), 5, support::Answer::filler(32));

    let wat = module(
        &[import::TICKET_ID, ONE_PAGE],
        "(call $ticket_id (i32.const 0) (i32.const 20) (i32.const 5) (i32.const 64) (i32.const 64))",
    );
    assert_eq!(status(&wat, &host), 32, "the 32-byte keylet length");
    assert_eq!(*host.ticket_keylets_asked.borrow(), vec![(account, 5)]);
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
