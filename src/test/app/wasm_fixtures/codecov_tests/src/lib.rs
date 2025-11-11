#![cfg_attr(target_arch = "wasm32", no_std)]

#[cfg(not(target_arch = "wasm32"))]
extern crate std;

use core::panic;
use xrpl_std::core::current_tx::escrow_finish::{get_current_escrow_finish, EscrowFinish};
use xrpl_std::core::current_tx::traits::TransactionCommonFields;
use xrpl_std::core::locator::Locator;
use xrpl_std::core::types::issue::Issue;
use xrpl_std::core::types::issue::XrpIssue;
use xrpl_std::core::types::keylets;
use xrpl_std::core::types::mpt_id::MptId;
use xrpl_std::host;
use xrpl_std::host::error_codes;
use xrpl_std::host::trace::{trace, trace_num as trace_number};
use xrpl_std::sfield;

mod host_bindings_loose;
include!("host_bindings_loose.rs");

fn check_result(result: i32, expected: i32, test_name: &'static str) {
    match result {
        code if code == expected => {
            let _ = trace_number(test_name, code.into());
        }
        code if code >= 0 => {
            let _ = trace(test_name);
            let _ = trace_number("TEST FAILED", code.into());
            panic!("Unexpected success code: {}", code);
        }
        code => {
            let _ = trace(test_name);
            let _ = trace_number("TEST FAILED", code.into());
            panic!("Error code: {}", code);
        }
    }
}

fn with_buffer<const N: usize, F, R>(mut f: F) -> R
where
    F: FnMut(*mut u8, usize) -> R,
{
    let mut buf = [0u8; N];
    f(buf.as_mut_ptr(), buf.len())
}

#[unsafe(no_mangle)]
pub extern "C" fn finish() -> i32 {
    let _ = trace("$$$$$ STARTING WASM EXECUTION $$$$$");

    // ########################################
    // Step #1: Test all host function happy paths
    // Note: not testing all the keylet functions,
    // that's in a separate test file (all_keylets).
    // The float tests are also in a separate file (float_tests).
    // ########################################
    check_result(unsafe { host::get_ledger_sqn() }, 12345, "get_ledger_sqn");
    check_result(
        unsafe { host::get_parent_ledger_time() },
        67890,
        "get_parent_ledger_time",
    );
    with_buffer::<32, _, _>(|ptr, len| {
        check_result(
            unsafe { host::get_parent_ledger_hash(ptr, len) },
            32,
            "get_parent_ledger_hash",
        );
    });
    check_result(unsafe { host::get_base_fee() }, 10, "get_base_fee");
    let amendment_name: &[u8] = b"test_amendment";
    let amendment_id: [u8; 32] = [1; 32];
    check_result(
        unsafe { host::amendment_enabled(amendment_name.as_ptr(), amendment_name.len()) },
        1,
        "amendment_enabled",
    );
    check_result(
        unsafe { host::amendment_enabled(amendment_id.as_ptr(), amendment_id.len()) },
        1,
        "amendment_enabled",
    );
    let tx: EscrowFinish = get_current_escrow_finish();
    let account = tx.get_account().unwrap_or_panic(); // get_tx_field under the hood
    let keylet = keylets::account_keylet(&account).unwrap_or_panic(); // account_keylet under the hood
    check_result(
        unsafe { host::cache_ledger_obj(keylet.as_ptr(), keylet.len(), 0) },
        1,
        "cache_ledger_obj",
    );
    with_buffer::<20, _, _>(|ptr, len| {
        check_result(
            unsafe { host::get_current_ledger_obj_field(sfield::Account, ptr, len) },
            20,
            "get_current_ledger_obj_field",
        );
    });
    with_buffer::<20, _, _>(|ptr, len| {
        check_result(
            unsafe { host::get_ledger_obj_field(1, sfield::Account, ptr, len) },
            20,
            "get_ledger_obj_field",
        );
    });
    let mut locator = Locator::new();
    locator.pack(sfield::Account);
    with_buffer::<20, _, _>(|ptr, len| {
        check_result(
            unsafe { host::get_tx_nested_field(locator.as_ptr(), locator.len(), ptr, len) },
            20,
            "get_tx_nested_field",
        );
    });
    with_buffer::<20, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::get_current_ledger_obj_nested_field(locator.as_ptr(), locator.len(), ptr, len)
            },
            20,
            "get_current_ledger_obj_nested_field",
        );
    });
    with_buffer::<20, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::get_ledger_obj_nested_field(1, locator.as_ptr(), locator.len(), ptr, len)
            },
            20,
            "get_ledger_obj_nested_field",
        );
    });
    check_result(
        unsafe { host::get_tx_array_len(sfield::Memos) },
        32,
        "get_tx_array_len",
    );
    check_result(
        unsafe { host::get_current_ledger_obj_array_len(sfield::Memos) },
        32,
        "get_current_ledger_obj_array_len",
    );
    check_result(
        unsafe { host::get_ledger_obj_array_len(1, sfield::Memos) },
        32,
        "get_ledger_obj_array_len",
    );
    check_result(
        unsafe { host::get_tx_nested_array_len(locator.as_ptr(), locator.len()) },
        32,
        "get_tx_nested_array_len",
    );
    check_result(
        unsafe { host::get_current_ledger_obj_nested_array_len(locator.as_ptr(), locator.len()) },
        32,
        "get_current_ledger_obj_nested_array_len",
    );
    check_result(
        unsafe { host::get_ledger_obj_nested_array_len(1, locator.as_ptr(), locator.len()) },
        32,
        "get_ledger_obj_nested_array_len",
    );
    check_result(
        unsafe { host::update_data(account.0.as_ptr(), account.0.len()) },
        20,
        "update_data",
    );
    with_buffer::<32, _, _>(|ptr, len| {
        check_result(
            unsafe { host::compute_sha512_half(locator.as_ptr(), locator.len(), ptr, len) },
            32,
            "compute_sha512_half",
        );
    });
    let message: &[u8] = b"test message";
    let pubkey: &[u8] = b"test pubkey"; //tx.get_public_key().unwrap_or_panic();
    let signature: &[u8] = b"test signature";
    check_result(
        unsafe {
            host::check_sig(
                message.as_ptr(),
                message.len(),
                pubkey.as_ptr(),
                pubkey.len(),
                signature.as_ptr(),
                signature.len(),
            )
        },
        1,
        "check_sig",
    );

    let nft_id: [u8; 32] = amendment_id;
    with_buffer::<18, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::get_nft(
                    account.0.as_ptr(),
                    account.0.len(),
                    nft_id.as_ptr(),
                    nft_id.len(),
                    ptr,
                    len,
                )
            },
            18,
            "get_nft",
        )
    });
    with_buffer::<20, _, _>(|ptr, len| {
        check_result(
            unsafe { host::get_nft_issuer(nft_id.as_ptr(), nft_id.len(), ptr, len) },
            20,
            "get_nft_issuer",
        )
    });
    with_buffer::<4, _, _>(|ptr, len| {
        check_result(
            unsafe { host::get_nft_taxon(nft_id.as_ptr(), nft_id.len(), ptr, len) },
            4,
            "get_nft_taxon",
        )
    });
    check_result(
        unsafe { host::get_nft_flags(nft_id.as_ptr(), nft_id.len()) },
        8,
        "get_nft_flags",
    );
    check_result(
        unsafe { host::get_nft_transfer_fee(nft_id.as_ptr(), nft_id.len()) },
        10,
        "get_nft_transfer_fee",
    );
    with_buffer::<4, _, _>(|ptr, len| {
        check_result(
            unsafe { host::get_nft_serial(nft_id.as_ptr(), nft_id.len(), ptr, len) },
            4,
            "get_nft_serial",
        )
    });
    let message = "testing trace";
    check_result(
        unsafe {
            host::trace_account(
                message.as_ptr(),
                message.len(),
                account.0.as_ptr(),
                account.0.len(),
            )
        },
        47,
        "trace_account",
    );
    let amount = &[0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5F]; // 95 drops of XRP
    check_result(
        unsafe {
            host::trace_amount(
                message.as_ptr(),
                message.len(),
                amount.as_ptr(),
                amount.len(),
            )
        },
        19,
        "trace_amount",
    );
    let amount = &[0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]; // 0 drops of XRP
    check_result(
        unsafe {
            host::trace_amount(
                message.as_ptr(),
                message.len(),
                amount.as_ptr(),
                amount.len(),
            )
        },
        18,
        "trace_amount_zero",
    );

    // ########################################
    // Step #2: Test set_data edge cases
    // ########################################
    check_result(
        unsafe { host_bindings_loose::get_parent_ledger_hash(-1, 4) },
        error_codes::INVALID_PARAMS,
        "get_parent_ledger_hash_neg_ptr",
    );
    with_buffer::<4, _, _>(|ptr, _len| {
        check_result(
            unsafe { host_bindings_loose::get_parent_ledger_hash(ptr as i32, -1) },
            error_codes::INVALID_PARAMS,
            "get_parent_ledger_hash_neg_len",
        )
    });
    with_buffer::<3, _, _>(|ptr, len| {
        check_result(
            unsafe { host_bindings_loose::get_parent_ledger_hash(ptr as i32, len as i32) },
            error_codes::BUFFER_TOO_SMALL,
            "get_parent_ledger_hash_buf_too_small",
        )
    });
    with_buffer::<4, _, _>(|ptr, _len| {
        check_result(
            unsafe { host_bindings_loose::get_parent_ledger_hash(ptr as i32, 1_000_000_000) },
            error_codes::POINTER_OUT_OF_BOUNDS,
            "get_parent_ledger_hash_len_too_long",
        )
    });
    let message = "testing trace";
    check_result(
        unsafe {
            host::trace_account(
                message.as_ptr(),
                message.len(),
                account.0.as_ptr(),
                account.0.len(),
            )
        },
        47,
        "trace_account",
    );
    let amount = &[0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5F]; // 95 drops of XRP
    check_result(
        unsafe {
            host::trace_amount(
                message.as_ptr(),
                message.len(),
                amount.as_ptr(),
                amount.len(),
            )
        },
        19,
        "trace_amount",
    );

    // ########################################
    // Step #3: Test getData[Type] edge cases
    // ########################################

    // uint64
    with_buffer::<32, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::float_from_uint(
                    locator.as_ptr().wrapping_add(1_000_000_000),
                    8,
                    ptr,
                    len,
                    FLOAT_ROUNDING_MODES_TO_NEAREST,
                )
            },
            error_codes::POINTER_OUT_OF_BOUNDS,
            "float_from_uint_len_oob",
        )
    });
    with_buffer::<32, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::float_from_uint(
                    locator.as_ptr(),
                    locator.len(),
                    ptr,
                    len,
                    FLOAT_ROUNDING_MODES_TO_NEAREST,
                )
            },
            error_codes::INVALID_PARAMS,
            "float_from_uint_wrong_len",
        )
    });

    // SField
    check_result(
        unsafe { host::get_tx_array_len(2) }, // not a valid SField value
        error_codes::INVALID_FIELD,
        "get_tx_array_len_invalid_sfield",
    );

    // Slice
    check_result(
        unsafe { host_bindings_loose::get_tx_nested_array_len(-1, locator.len() as i32) },
        error_codes::INVALID_PARAMS,
        "get_tx_nested_array_len_neg_ptr",
    );
    check_result(
        unsafe { host_bindings_loose::get_tx_nested_array_len(locator.as_ptr() as i32, -1) },
        error_codes::INVALID_PARAMS,
        "get_tx_nested_array_len_neg_len",
    );
    let long_len = 4 * 1024 + 1;
    check_result(
        unsafe {
            host_bindings_loose::get_tx_nested_array_len(locator.as_ptr() as i32, long_len as i32)
        },
        error_codes::DATA_FIELD_TOO_LARGE,
        "get_tx_nested_array_len_too_long",
    );
    check_result(
        unsafe {
            host_bindings_loose::get_tx_nested_array_len(
                locator.as_ptr() as i32 + 1_000_000_000,
                locator.len() as i32,
            )
        },
        error_codes::POINTER_OUT_OF_BOUNDS,
        "get_tx_nested_array_len_ptr_oob",
    );

    // uint256
    check_result(
        unsafe {
            host_bindings_loose::cache_ledger_obj(
                locator.as_ptr() as i32 + 1_000_000_000,
                locator.len() as i32,
                1,
            )
        },
        error_codes::POINTER_OUT_OF_BOUNDS,
        "cache_ledger_obj_ptr_oob",
    );
    check_result(
        unsafe {
            host_bindings_loose::cache_ledger_obj(locator.as_ptr() as i32, locator.len() as i32, 1)
        },
        error_codes::INVALID_PARAMS,
        "cache_ledger_obj_wrong_len",
    );

    // AccountID
    with_buffer::<32, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host_bindings_loose::account_keylet(
                    locator.as_ptr() as i32 + 1_000_000_000,
                    locator.len() as i32,
                    ptr,
                    len,
                )
            },
            error_codes::POINTER_OUT_OF_BOUNDS,
            "account_keylet_len_oob",
        )
    });
    with_buffer::<32, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host_bindings_loose::account_keylet(
                    locator.as_ptr() as i32,
                    locator.len() as i32,
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "account_keylet_wrong_len",
        )
    });

    // Currency
    with_buffer::<32, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host_bindings_loose::line_keylet(
                    account.0.as_ptr(),
                    account.0.len(),
                    account.0.as_ptr(),
                    account.0.len(),
                    locator.as_ptr() as i32 + 1_000_000_000,
                    locator.len() as i32,
                    ptr,
                    len,
                )
            },
            error_codes::POINTER_OUT_OF_BOUNDS,
            "line_keylet_len_oob_currency",
        )
    });
    with_buffer::<32, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host_bindings_loose::line_keylet(
                    account.0.as_ptr(),
                    account.0.len(),
                    account.0.as_ptr(),
                    account.0.len(),
                    locator.as_ptr() as i32,
                    locator.len() as i32,
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "line_keylet_wrong_len_currency",
        )
    });

    // Issue
    let asset1_bytes = Issue::XRP(XrpIssue {}).as_bytes();
    with_buffer::<32, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::amm_keylet(
                    asset1_bytes.as_ptr(),
                    asset1_bytes.len(),
                    locator.as_ptr().wrapping_add(1_000_000_000),
                    locator.len(),
                    ptr,
                    len,
                )
            },
            error_codes::POINTER_OUT_OF_BOUNDS,
            "amm_keylet_len_oob_asset2",
        )
    });
    with_buffer::<32, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::amm_keylet(
                    asset1_bytes.as_ptr(),
                    asset1_bytes.len(),
                    locator.as_ptr(),
                    locator.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "amm_keylet_len_wrong_len_asset2",
        )
    });
    let currency: &[u8] = b"USD00000000000000000"; // 20 bytes
    with_buffer::<32, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::amm_keylet(
                    asset1_bytes.as_ptr(),
                    asset1_bytes.len(),
                    currency.as_ptr(),
                    currency.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "amm_keylet_len_wrong_non_xrp_currency_len",
        )
    });
    let xrpissue: &[u8] = &[0; 40]; // 40 bytes
    with_buffer::<32, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::amm_keylet(
                    xrpissue.as_ptr(),
                    xrpissue.len(),
                    asset1_bytes.as_ptr(),
                    asset1_bytes.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "amm_keylet_len_wrong_xrp_currency_len",
        )
    });
    let mptid = MptId::new(1, account);
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::amm_keylet(
                    mptid.as_ptr(),
                    mptid.len(),
                    asset1_bytes.as_ptr(),
                    asset1_bytes.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "amm_keylet_mpt",
        )
    });

    // string
    check_result(
        unsafe {
            host_bindings_loose::trace_num(
                locator.as_ptr() as i32 + 1_000_000_000,
                locator.len() as i32,
                42,
            )
        },
        error_codes::POINTER_OUT_OF_BOUNDS,
        "trace_num_oob_str",
    );

    // ########################################
    // Step #4: Test other host function edge cases
    // ########################################

    // invalid SFields

    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe { host::get_tx_field(2, ptr, len) },
            error_codes::INVALID_FIELD,
            "get_tx_field_invalid_sfield",
        );
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe { host::get_current_ledger_obj_field(2, ptr, len) },
            error_codes::INVALID_FIELD,
            "get_current_ledger_obj_field_invalid_sfield",
        );
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe { host::get_ledger_obj_field(1, 2, ptr, len) },
            error_codes::INVALID_FIELD,
            "get_ledger_obj_field_invalid_sfield",
        );
    });
    check_result(
        unsafe { host::get_tx_array_len(2) },
        error_codes::INVALID_FIELD,
        "get_tx_array_len_invalid_sfield",
    );
    check_result(
        unsafe { host::get_current_ledger_obj_array_len(2) },
        error_codes::INVALID_FIELD,
        "get_current_ledger_obj_array_len_invalid_sfield",
    );
    check_result(
        unsafe { host::get_ledger_obj_array_len(1, 2) },
        error_codes::INVALID_FIELD,
        "get_ledger_obj_array_len_invalid_sfield",
    );

    // invalid Slice

    check_result(
        unsafe { host::amendment_enabled(amendment_name.as_ptr(), long_len) },
        error_codes::DATA_FIELD_TOO_LARGE,
        "amendment_enabled_too_big_slice",
    );
    check_result(
        unsafe { host::amendment_enabled(amendment_name.as_ptr(), 65) },
        error_codes::DATA_FIELD_TOO_LARGE,
        "amendment_enabled_too_long",
    );
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe { host::get_tx_nested_field(locator.as_ptr(), long_len, ptr, len) },
            error_codes::DATA_FIELD_TOO_LARGE,
            "get_tx_nested_field_too_big_slice",
        );
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::get_current_ledger_obj_nested_field(locator.as_ptr(), long_len, ptr, len)
            },
            error_codes::DATA_FIELD_TOO_LARGE,
            "get_current_ledger_obj_nested_field_too_big_slice",
        );
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe { host::get_ledger_obj_nested_field(1, locator.as_ptr(), long_len, ptr, len) },
            error_codes::DATA_FIELD_TOO_LARGE,
            "get_ledger_obj_nested_field_too_big_slice",
        );
    });
    check_result(
        unsafe { host::get_tx_nested_array_len(locator.as_ptr(), long_len) },
        error_codes::DATA_FIELD_TOO_LARGE,
        "get_tx_nested_array_len_too_big_slice",
    );
    check_result(
        unsafe { host::get_current_ledger_obj_nested_array_len(locator.as_ptr(), long_len) },
        error_codes::DATA_FIELD_TOO_LARGE,
        "get_current_ledger_obj_nested_array_len_too_big_slice",
    );
    check_result(
        unsafe { host::get_ledger_obj_nested_array_len(1, locator.as_ptr(), long_len) },
        error_codes::DATA_FIELD_TOO_LARGE,
        "get_ledger_obj_nested_array_len_too_big_slice",
    );
    check_result(
        unsafe { host::update_data(locator.as_ptr(), long_len) },
        error_codes::DATA_FIELD_TOO_LARGE,
        "update_data_too_big_slice",
    );
    check_result(
        unsafe {
            host::check_sig(
                message.as_ptr(),
                long_len,
                pubkey.as_ptr(),
                pubkey.len(),
                signature.as_ptr(),
                signature.len(),
            )
        },
        error_codes::DATA_FIELD_TOO_LARGE,
        "check_sig",
    );
    check_result(
        unsafe {
            host::check_sig(
                message.as_ptr(),
                message.len(),
                pubkey.as_ptr(),
                long_len,
                signature.as_ptr(),
                signature.len(),
            )
        },
        error_codes::DATA_FIELD_TOO_LARGE,
        "check_sig",
    );
    check_result(
        unsafe {
            host::check_sig(
                message.as_ptr(),
                message.len(),
                pubkey.as_ptr(),
                pubkey.len(),
                signature.as_ptr(),
                long_len,
            )
        },
        error_codes::DATA_FIELD_TOO_LARGE,
        "check_sig",
    );
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe { host::compute_sha512_half(locator.as_ptr(), long_len, ptr, len) },
            error_codes::DATA_FIELD_TOO_LARGE,
            "compute_sha512_half_too_big_slice",
        );
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::amm_keylet(
                    asset1_bytes.as_ptr(),
                    long_len,
                    asset1_bytes.as_ptr(),
                    asset1_bytes.len(),
                    ptr,
                    len,
                )
            },
            error_codes::DATA_FIELD_TOO_LARGE,
            "amm_keylet_too_big_slice",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::credential_keylet(
                    account.0.as_ptr(),
                    account.0.len(),
                    account.0.as_ptr(),
                    account.0.len(),
                    locator.as_ptr(),
                    long_len,
                    ptr,
                    len,
                )
            },
            error_codes::DATA_FIELD_TOO_LARGE,
            "credential_keylet_too_big_slice",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::mptoken_keylet(
                    mptid.as_ptr(),
                    long_len,
                    account.0.as_ptr(),
                    account.0.len(),
                    ptr,
                    len,
                )
            },
            error_codes::DATA_FIELD_TOO_LARGE,
            "mptoken_keylet_too_big_slice_mptid",
        )
    });
    check_result(
        unsafe {
            host::trace(
                message.as_ptr(),
                message.len(),
                locator.as_ptr().wrapping_add(1_000_000_000),
                locator.len(),
                0,
            )
        },
        error_codes::POINTER_OUT_OF_BOUNDS,
        "trace_oob_slice",
    );
    let float: [u8; 8] = [0xD4, 0x83, 0x8D, 0x7E, 0xA4, 0xC6, 0x80, 0x00];
    check_result(
        unsafe {
            host::trace_opaque_float(
                message.as_ptr(),
                message.len(),
                float.as_ptr().wrapping_add(1_000_000_000),
                float.len(),
            )
        },
        error_codes::POINTER_OUT_OF_BOUNDS,
        "trace_opaque_float_oob_slice",
    );
    check_result(
        unsafe {
            host::trace_amount(
                message.as_ptr(),
                message.len(),
                locator.as_ptr().wrapping_add(1_000_000_000),
                locator.len(),
            )
        },
        error_codes::POINTER_OUT_OF_BOUNDS,
        "trace_amount_oob_slice",
    );
    check_result(
        unsafe {
            host::float_compare(
                float.as_ptr().wrapping_add(1_000_000_000),
                float.len(),
                float.as_ptr(),
                float.len(),
            )
        },
        error_codes::POINTER_OUT_OF_BOUNDS,
        "float_compare_oob_slice1",
    );
    check_result(
        unsafe {
            host::float_compare(
                float.as_ptr(),
                float.len(),
                float.as_ptr().wrapping_add(1_000_000_000),
                float.len(),
            )
        },
        error_codes::POINTER_OUT_OF_BOUNDS,
        "float_compare_oob_slice2",
    );
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::float_add(
                    float.as_ptr().wrapping_add(1_000_000_000),
                    float.len(),
                    float.as_ptr(),
                    float.len(),
                    ptr,
                    len,
                    FLOAT_ROUNDING_MODES_TO_NEAREST,
                )
            },
            error_codes::POINTER_OUT_OF_BOUNDS,
            "float_add_oob_slice1",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::float_add(
                    float.as_ptr(),
                    float.len(),
                    float.as_ptr().wrapping_add(1_000_000_000),
                    float.len(),
                    ptr,
                    len,
                    FLOAT_ROUNDING_MODES_TO_NEAREST,
                )
            },
            error_codes::POINTER_OUT_OF_BOUNDS,
            "float_add_oob_slice2",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::float_subtract(
                    float.as_ptr().wrapping_add(1_000_000_000),
                    float.len(),
                    float.as_ptr(),
                    float.len(),
                    ptr,
                    len,
                    FLOAT_ROUNDING_MODES_TO_NEAREST,
                )
            },
            error_codes::POINTER_OUT_OF_BOUNDS,
            "float_subtract_oob_slice1",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::float_subtract(
                    float.as_ptr(),
                    float.len(),
                    float.as_ptr().wrapping_add(1_000_000_000),
                    float.len(),
                    ptr,
                    len,
                    FLOAT_ROUNDING_MODES_TO_NEAREST,
                )
            },
            error_codes::POINTER_OUT_OF_BOUNDS,
            "float_subtract_oob_slice2",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::float_multiply(
                    float.as_ptr().wrapping_add(1_000_000_000),
                    float.len(),
                    float.as_ptr(),
                    float.len(),
                    ptr,
                    len,
                    FLOAT_ROUNDING_MODES_TO_NEAREST,
                )
            },
            error_codes::POINTER_OUT_OF_BOUNDS,
            "float_multiply_oob_slice1",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::float_multiply(
                    float.as_ptr(),
                    float.len(),
                    float.as_ptr().wrapping_add(1_000_000_000),
                    float.len(),
                    ptr,
                    len,
                    FLOAT_ROUNDING_MODES_TO_NEAREST,
                )
            },
            error_codes::POINTER_OUT_OF_BOUNDS,
            "float_multiply_oob_slice2",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::float_divide(
                    float.as_ptr().wrapping_add(1_000_000_000),
                    float.len(),
                    float.as_ptr(),
                    float.len(),
                    ptr,
                    len,
                    FLOAT_ROUNDING_MODES_TO_NEAREST,
                )
            },
            error_codes::POINTER_OUT_OF_BOUNDS,
            "float_divide_oob_slice1",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::float_divide(
                    float.as_ptr(),
                    float.len(),
                    float.as_ptr().wrapping_add(1_000_000_000),
                    float.len(),
                    ptr,
                    len,
                    FLOAT_ROUNDING_MODES_TO_NEAREST,
                )
            },
            error_codes::POINTER_OUT_OF_BOUNDS,
            "float_divide_oob_slice2",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::float_root(
                    float.as_ptr().wrapping_add(1_000_000_000),
                    float.len(),
                    3,
                    ptr,
                    len,
                    FLOAT_ROUNDING_MODES_TO_NEAREST,
                )
            },
            error_codes::POINTER_OUT_OF_BOUNDS,
            "float_root_oob_slice",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::float_pow(
                    float.as_ptr().wrapping_add(1_000_000_000),
                    float.len(),
                    3,
                    ptr,
                    len,
                    FLOAT_ROUNDING_MODES_TO_NEAREST,
                )
            },
            error_codes::POINTER_OUT_OF_BOUNDS,
            "float_pow_oob_slice",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::float_log(
                    float.as_ptr().wrapping_add(1_000_000_000),
                    float.len(),
                    ptr,
                    len,
                    FLOAT_ROUNDING_MODES_TO_NEAREST,
                )
            },
            error_codes::POINTER_OUT_OF_BOUNDS,
            "float_log_oob_slice",
        )
    });

    // invalid UInt256

    check_result(
        unsafe { host::cache_ledger_obj(locator.as_ptr(), locator.len(), 0) },
        error_codes::INVALID_PARAMS,
        "cache_ledger_obj_wrong_size_uint256",
    );
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::get_nft(
                    account.0.as_ptr(),
                    account.0.len(),
                    locator.as_ptr(),
                    locator.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "get_nft_wrong_size_uint256",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe { host::get_nft_issuer(locator.as_ptr(), locator.len(), ptr, len) },
            error_codes::INVALID_PARAMS,
            "get_nft_issuer_wrong_size_uint256",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe { host::get_nft_taxon(locator.as_ptr(), locator.len(), ptr, len) },
            error_codes::INVALID_PARAMS,
            "get_nft_taxon_wrong_size_uint256",
        )
    });
    check_result(
        unsafe { host::get_nft_flags(locator.as_ptr(), locator.len()) },
        error_codes::INVALID_PARAMS,
        "get_nft_flags_wrong_size_uint256",
    );
    check_result(
        unsafe { host::get_nft_transfer_fee(locator.as_ptr(), locator.len()) },
        error_codes::INVALID_PARAMS,
        "get_nft_transfer_fee_wrong_size_uint256",
    );
    with_buffer::<4, _, _>(|ptr, len| {
        check_result(
            unsafe { host::get_nft_serial(locator.as_ptr(), locator.len(), ptr, len) },
            error_codes::INVALID_PARAMS,
            "get_nft_serial_wrong_size_uint256",
        )
    });

    // invalid AccountID

    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe { host::account_keylet(locator.as_ptr(), locator.len(), ptr, len) },
            error_codes::INVALID_PARAMS,
            "account_keylet_wrong_size_accountid",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe { host::check_keylet(locator.as_ptr(), locator.len(), 1, ptr, len) },
            error_codes::INVALID_PARAMS,
            "check_keylet_wrong_size_accountid",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::credential_keylet(
                    locator.as_ptr(), // invalid AccountID size
                    locator.len(),
                    account.0.as_ptr(),
                    account.0.len(),
                    locator.as_ptr(), // valid slice size
                    locator.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "credential_keylet_wrong_size_accountid1",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::credential_keylet(
                    account.0.as_ptr(),
                    account.0.len(),
                    locator.as_ptr(), // invalid AccountID size
                    locator.len(),
                    locator.as_ptr(), // valid slice size
                    locator.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "credential_keylet_wrong_size_accountid2",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::delegate_keylet(
                    locator.as_ptr(), // invalid AccountID size
                    locator.len(),
                    account.0.as_ptr(),
                    account.0.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "delegate_keylet_wrong_size_accountid1",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::delegate_keylet(
                    account.0.as_ptr(),
                    account.0.len(),
                    locator.as_ptr(), // invalid AccountID size
                    locator.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "delegate_keylet_wrong_size_accountid2",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::deposit_preauth_keylet(
                    locator.as_ptr(), // invalid AccountID size
                    locator.len(),
                    account.0.as_ptr(),
                    account.0.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "deposit_preauth_keylet_wrong_size_accountid1",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::deposit_preauth_keylet(
                    account.0.as_ptr(),
                    account.0.len(),
                    locator.as_ptr(), // invalid AccountID size
                    locator.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "deposit_preauth_keylet_wrong_size_accountid2",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe { host::did_keylet(locator.as_ptr(), locator.len(), ptr, len) },
            error_codes::INVALID_PARAMS,
            "did_keylet_wrong_size_accountid",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe { host::escrow_keylet(locator.as_ptr(), locator.len(), 1, ptr, len) },
            error_codes::INVALID_PARAMS,
            "escrow_keylet_wrong_size_accountid",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::line_keylet(
                    locator.as_ptr(), // invalid AccountID size
                    locator.len(),
                    account.0.as_ptr(),
                    account.0.len(),
                    currency.as_ptr(),
                    currency.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "line_keylet_wrong_size_accountid1",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::line_keylet(
                    account.0.as_ptr(),
                    account.0.len(),
                    locator.as_ptr(), // invalid AccountID size
                    locator.len(),
                    currency.as_ptr(),
                    currency.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "line_keylet_wrong_size_accountid2",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe { host::mpt_issuance_keylet(locator.as_ptr(), locator.len(), 1, ptr, len) },
            error_codes::INVALID_PARAMS,
            "mpt_issuance_keylet_wrong_size_accountid",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::mptoken_keylet(
                    mptid.as_ptr(),
                    mptid.len(),
                    locator.as_ptr(),
                    locator.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "mptoken_keylet_wrong_size_accountid",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe { host::nft_offer_keylet(locator.as_ptr(), locator.len(), 1, ptr, len) },
            error_codes::INVALID_PARAMS,
            "nft_offer_keylet_wrong_size_accountid",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe { host::offer_keylet(locator.as_ptr(), locator.len(), 1, ptr, len) },
            error_codes::INVALID_PARAMS,
            "offer_keylet_wrong_size_accountid",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe { host::oracle_keylet(locator.as_ptr(), locator.len(), 1, ptr, len) },
            error_codes::INVALID_PARAMS,
            "oracle_keylet_wrong_size_accountid",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::paychan_keylet(
                    locator.as_ptr(), // invalid AccountID size
                    locator.len(),
                    account.0.as_ptr(),
                    account.0.len(),
                    1,
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "paychan_keylet_wrong_size_accountid1",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::paychan_keylet(
                    account.0.as_ptr(),
                    account.0.len(),
                    locator.as_ptr(), // invalid AccountID size
                    locator.len(),
                    1,
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "paychan_keylet_wrong_size_accountid2",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::permissioned_domain_keylet(locator.as_ptr(), locator.len(), 1, ptr, len)
            },
            error_codes::INVALID_PARAMS,
            "permissioned_domain_keylet_wrong_size_accountid",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe { host::signers_keylet(locator.as_ptr(), locator.len(), ptr, len) },
            error_codes::INVALID_PARAMS,
            "signers_keylet_wrong_size_accountid",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe { host::ticket_keylet(locator.as_ptr(), locator.len(), 1, ptr, len) },
            error_codes::INVALID_PARAMS,
            "ticket_keylet_wrong_size_accountid",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe { host::vault_keylet(locator.as_ptr(), locator.len(), 1, ptr, len) },
            error_codes::INVALID_PARAMS,
            "vault_keylet_wrong_size_accountid",
        )
    });
    let uint256: &[u8] = b"00000000000000000000000000000001";
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::get_nft(
                    locator.as_ptr(),
                    locator.len(),
                    uint256.as_ptr(),
                    uint256.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "get_nft_wrong_size_accountid",
        )
    });
    check_result(
        unsafe {
            host::trace_account(
                message.as_ptr(),
                message.len(),
                locator.as_ptr(),
                locator.len(),
            )
        },
        error_codes::INVALID_PARAMS,
        "trace_account_wrong_size_accountid",
    );

    // invalid Currency was already tested above
    // invalid string

    check_result(
        unsafe {
            host::trace(
                message.as_ptr().wrapping_add(1_000_000_000),
                message.len(),
                uint256.as_ptr(),
                uint256.len(),
                0,
            )
        },
        error_codes::POINTER_OUT_OF_BOUNDS,
        "trace_oob_string",
    );
    check_result(
        unsafe {
            host::trace_opaque_float(
                message.as_ptr().wrapping_add(1_000_000_000),
                message.len(),
                float.as_ptr(),
                float.len(),
            )
        },
        error_codes::POINTER_OUT_OF_BOUNDS,
        "trace_opaque_float_oob_string",
    );
    check_result(
        unsafe {
            host::trace_account(
                message.as_ptr().wrapping_add(1_000_000_000),
                message.len(),
                account.0.as_ptr(),
                account.0.len(),
            )
        },
        error_codes::POINTER_OUT_OF_BOUNDS,
        "trace_account_oob_string",
    );
    check_result(
        unsafe {
            host::trace_amount(
                message.as_ptr().wrapping_add(1_000_000_000),
                message.len(),
                amount.as_ptr(),
                amount.len(),
            )
        },
        error_codes::POINTER_OUT_OF_BOUNDS,
        "trace_amount_oob_string",
    );

    // trace too large

    check_result(
        unsafe {
            host::trace(
                locator.as_ptr(),
                locator.len(),
                locator.as_ptr(),
                long_len,
                0,
            )
        },
        error_codes::DATA_FIELD_TOO_LARGE,
        "trace_too_long",
    );
    check_result(
        unsafe { host::trace_num(locator.as_ptr(), long_len, 1) },
        error_codes::DATA_FIELD_TOO_LARGE,
        "trace_num_too_long",
    );
    check_result(
        unsafe {
            host::trace_opaque_float(message.as_ptr(), long_len, float.as_ptr(), float.len())
        },
        error_codes::DATA_FIELD_TOO_LARGE,
        "trace_opaque_float_too_long",
    );
    check_result(
        unsafe {
            host::trace_account(
                message.as_ptr(),
                long_len,
                account.0.as_ptr(),
                account.0.len(),
            )
        },
        error_codes::DATA_FIELD_TOO_LARGE,
        "trace_account_too_long",
    );
    check_result(
        unsafe { host::trace_amount(message.as_ptr(), long_len, amount.as_ptr(), amount.len()) },
        error_codes::DATA_FIELD_TOO_LARGE,
        "trace_amount_too_long",
    );

    // trace amount errors

    check_result(
        unsafe {
            host::trace_amount(
                message.as_ptr(),
                message.len(),
                locator.as_ptr(),
                locator.len(),
            )
        },
        error_codes::INVALID_PARAMS,
        "trace_amount_wrong_length",
    );

    // other misc errors

    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::mptoken_keylet(
                    locator.as_ptr(),
                    locator.len(),
                    account.0.as_ptr(),
                    account.0.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "mptoken_keylet_mptid_wrong_length",
        )
    });

    1 // <-- If we get here, finish the escrow.
}
