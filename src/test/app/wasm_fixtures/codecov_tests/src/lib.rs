#![cfg_attr(target_arch = "wasm32", no_std)]

#[cfg(not(target_arch = "wasm32"))]
extern crate std;

use core::panic;
use xrpl_std::core::current_tx::escrow_finish::{EscrowFinish, get_current_escrow_finish};
use xrpl_std::core::current_tx::traits::TransactionCommonFields;
use xrpl_std::core::error_codes;
use xrpl_std::core::locator::Locator;
use xrpl_std::core::types::keylets;
use xrpl_std::host;
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
    // that's in a separate test file.
    // ########################################
    with_buffer::<4, _, _>(|ptr, len| {
        check_result(
            unsafe { host::get_ledger_sqn(ptr, len) },
            4,
            "get_ledger_sqn",
        )
    });
    with_buffer::<4, _, _>(|ptr, len| {
        check_result(
            unsafe { host::get_parent_ledger_time(ptr, len) },
            4,
            "get_parent_ledger_time",
        );
    });
    with_buffer::<32, _, _>(|ptr, len| {
        check_result(
            unsafe { host::get_parent_ledger_hash(ptr, len) },
            32,
            "get_parent_ledger_hash",
        );
    });
    with_buffer::<32, _, _>(|ptr, len| {
        check_result(
            unsafe { host::get_ledger_account_hash(ptr, len) },
            32,
            "get_ledger_account_hash",
        );
    });
    with_buffer::<32, _, _>(|ptr, len| {
        check_result(
            unsafe { host::get_ledger_tx_hash(ptr, len) },
            32,
            "get_ledger_tx_hash",
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
        0,
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

    // ########################################
    // Step #2: Test set_data edge cases
    // ########################################
    check_result(
        unsafe { host_bindings_loose::get_ledger_sqn(-1, 4) },
        error_codes::INVALID_PARAMS,
        "get_ledger_sqn_neg_ptr",
    );
    with_buffer::<4, _, _>(|ptr, _len| {
        check_result(
            unsafe { host_bindings_loose::get_ledger_sqn(ptr as i32, -1) },
            error_codes::INVALID_PARAMS,
            "get_ledger_sqn_neg_len",
        )
    });
    with_buffer::<3, _, _>(|ptr, len| {
        check_result(
            unsafe { host_bindings_loose::get_ledger_sqn(ptr as i32, len as i32) },
            error_codes::BUFFER_TOO_SMALL,
            "get_ledger_sqn_buf_too_small",
        )
    });
    with_buffer::<4, _, _>(|ptr, _len| {
        check_result(
            unsafe { host_bindings_loose::get_ledger_sqn(ptr as i32, 1_000_000_000) },
            error_codes::POINTER_OUT_OF_BOUNDS,
            "get_ledger_sqn_len_too_long",
        )
    });

    // ########################################
    // Step #3: Test getData[Type] edge cases
    // ########################################

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
            "account_keylet_len_too_long",
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
            "line_keylet_len_too_long_currency",
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
        "trace_num_wrong_len_str",
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
        "amendment_enabled",
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
    check_result(
        unsafe {
            host::trace(
                locator.as_ptr(),
                locator.len(),
                locator.as_ptr().wrapping_add(1_000_000_000),
                locator.len(),
                0,
            )
        },
        error_codes::POINTER_OUT_OF_BOUNDS,
        "trace_oob_slice",
    );

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
    let currency: &[u8] = b"USD00000000000000000"; // 20 bytes
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

    // invalid Currency was already tested above
    // invalid string

    check_result(
        unsafe {
            host::trace(
                locator.as_ptr().wrapping_add(1_000_000_000),
                locator.len(),
                uint256.as_ptr(),
                uint256.len(),
                0,
            )
        },
        error_codes::POINTER_OUT_OF_BOUNDS,
        "get_nft_wrong_size_string",
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

    1 // <-- If we get here, finish the escrow.
}
