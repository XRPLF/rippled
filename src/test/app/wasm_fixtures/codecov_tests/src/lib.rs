#![cfg_attr(target_arch = "wasm32", no_std)]

#[cfg(not(target_arch = "wasm32"))]
extern crate std;

use core::panic;
use xrpl_std::core::current_tx::escrow_finish::{get_current_escrow_finish, EscrowFinish};
use xrpl_std::core::current_tx::traits::TransactionCommonFields;
use xrpl_std::core::keylets;
use xrpl_std::core::locator::Locator;
use xrpl_std::core::types::blob::DEFAULT_BLOB_SIZE;
use xrpl_std::core::types::issue::Issue;
use xrpl_std::core::types::issue::XrpIssue;
use xrpl_std::core::types::mpt_id::MptId;
use xrpl_std::host;
use xrpl_std::host::error_codes;
use xrpl_std::host::trace::{trace, trace_num as trace_number};
use xrpl_std::sfield;
use xrpl_std::types::XRPL_CONTRACT_DATA_SIZE;

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
    with_buffer::<4, _, _>(|ptr, len| {
        check_result(unsafe { host::ldgr_index(ptr, len) }, 4, "ldgr_index");
    });
    with_buffer::<4, _, _>(|ptr, len| {
        check_result(
            unsafe { host::parent_ldgr_time(ptr, len) },
            4,
            "parent_ldgr_time",
        );
    });
    with_buffer::<32, _, _>(|ptr, len| {
        check_result(
            unsafe { host::parent_ldgr_hash(ptr, len) },
            32,
            "parent_ldgr_hash",
        );
    });
    with_buffer::<4, _, _>(|ptr, len| {
        check_result(unsafe { host::base_fee(ptr, len) }, 4, "base_fee");
    });
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
    let keylet = keylets::accountroot_id(&account).unwrap_or_panic(); // accountroot_id under the hood
    check_result(
        unsafe { host::cache_le(keylet.as_ptr(), keylet.len(), 0) },
        1,
        "cache_le",
    );
    with_buffer::<20, _, _>(|ptr, len| {
        check_result(
            unsafe { host::home_le_field(sfield::Account.into(), ptr, len) },
            20,
            "home_le_field",
        );
    });
    with_buffer::<20, _, _>(|ptr, len| {
        check_result(
            unsafe { host::le_field(1, sfield::Account.into(), ptr, len) },
            20,
            "le_field",
        );
    });
    let mut locator = Locator::new();
    locator.pack(sfield::Account);
    with_buffer::<20, _, _>(|ptr, len| {
        check_result(
            unsafe { host::tx_inner(locator.as_ptr(), locator.len(), ptr, len) },
            20,
            "tx_inner",
        );
    });
    with_buffer::<20, _, _>(|ptr, len| {
        check_result(
            unsafe { host::home_le_inner(locator.as_ptr(), locator.len(), ptr, len) },
            20,
            "home_le_inner",
        );
    });
    with_buffer::<20, _, _>(|ptr, len| {
        check_result(
            unsafe { host::le_inner(1, locator.as_ptr(), locator.len(), ptr, len) },
            20,
            "le_inner",
        );
    });
    check_result(
        unsafe { host::tx_arr_len(sfield::Memos.into()) },
        32,
        "tx_arr_len",
    );
    check_result(
        unsafe { host::home_le_arr_len(sfield::Memos.into()) },
        32,
        "home_le_arr_len",
    );
    check_result(
        unsafe { host::le_arr_len(1, sfield::Memos.into()) },
        32,
        "le_arr_len",
    );
    check_result(
        unsafe { host::tx_inner_arr_len(locator.as_ptr(), locator.len()) },
        32,
        "tx_inner_arr_len",
    );
    check_result(
        unsafe { host::home_le_inner_arr_len(locator.as_ptr(), locator.len()) },
        32,
        "home_le_inner_arr_len",
    );
    check_result(
        unsafe { host::le_inner_arr_len(1, locator.as_ptr(), locator.len()) },
        32,
        "le_inner_arr_len",
    );
    check_result(
        unsafe { host::set_data(account.0.as_ptr(), account.0.len()) },
        20,
        "set_data",
    );
    with_buffer::<32, _, _>(|ptr, len| {
        check_result(
            unsafe { host::sha512_half(locator.as_ptr(), locator.len(), ptr, len) },
            32,
            "sha512_half",
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
                host::nft_uri(
                    account.0.as_ptr(),
                    account.0.len(),
                    nft_id.as_ptr(),
                    nft_id.len(),
                    ptr,
                    len,
                )
            },
            18,
            "nft_uri",
        )
    });
    with_buffer::<20, _, _>(|ptr, len| {
        check_result(
            unsafe { host::nft_issuer(nft_id.as_ptr(), nft_id.len(), ptr, len) },
            20,
            "nft_issuer",
        )
    });
    with_buffer::<4, _, _>(|ptr, len| {
        check_result(
            unsafe { host::nft_taxon(nft_id.as_ptr(), nft_id.len(), ptr, len) },
            4,
            "nft_taxon",
        )
    });
    check_result(
        unsafe { host::nft_flags(nft_id.as_ptr(), nft_id.len()) },
        8,
        "nft_flags",
    );
    check_result(
        unsafe { host::nft_xfer_fee(nft_id.as_ptr(), nft_id.len()) },
        10,
        "nft_xfer_fee",
    );
    with_buffer::<4, _, _>(|ptr, len| {
        check_result(
            unsafe { host::nft_serial(nft_id.as_ptr(), nft_id.len(), ptr, len) },
            4,
            "nft_serial",
        )
    });
    let message = "testing trace";
    check_result(
        unsafe {
            host::trace_acct(
                message.as_ptr(),
                message.len(),
                account.0.as_ptr(),
                account.0.len(),
            )
        },
        0,
        "trace_acct",
    );
    let amount = &[0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x5F]; // 95 drops of XRP
    check_result(
        unsafe {
            host::trace_amt(
                message.as_ptr(),
                message.len(),
                amount.as_ptr(),
                amount.len(),
            )
        },
        0,
        "trace_amt",
    );
    let amount = &[0x40, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00]; // 0 drops of XRP
    check_result(
        unsafe {
            host::trace_amt(
                message.as_ptr(),
                message.len(),
                amount.as_ptr(),
                amount.len(),
            )
        },
        0,
        "trace_amt_zero",
    );

    // ########################################
    // Step #2: Test set_data edge cases
    // ########################################
    check_result(
        unsafe { host_bindings_loose::parent_ldgr_hash(-1, 4) },
        error_codes::INVALID_PARAMS,
        "parent_ldgr_hash_neg_ptr",
    );
    with_buffer::<4, _, _>(|ptr, _len| {
        check_result(
            unsafe { host_bindings_loose::parent_ldgr_hash(ptr as i32, -1) },
            error_codes::INVALID_PARAMS,
            "parent_ldgr_hash_neg_len",
        )
    });
    with_buffer::<3, _, _>(|ptr, len| {
        check_result(
            unsafe { host_bindings_loose::parent_ldgr_hash(ptr as i32, len as i32) },
            error_codes::BUFFER_TOO_SMALL,
            "parent_ldgr_hash_buf_too_small",
        )
    });
    with_buffer::<4, _, _>(|ptr, _len| {
        check_result(
            unsafe { host_bindings_loose::parent_ldgr_hash(ptr as i32, 1_000_000_000) },
            error_codes::POINTER_OUT_OF_BOUNDS,
            "parent_ldgr_hash_len_too_long",
        )
    });

    // ########################################
    // Step #3: Test getData[Type] edge cases
    // ########################################

    // SField
    check_result(
        unsafe { host::tx_arr_len(2) }, // not a valid SField value
        error_codes::INVALID_FIELD,
        "tx_arr_len_invalid_sfield",
    );

    // Slice
    check_result(
        unsafe { host_bindings_loose::tx_inner_arr_len(-1, locator.len() as i32) },
        error_codes::INVALID_PARAMS,
        "tx_inner_arr_len_neg_ptr",
    );
    check_result(
        unsafe { host_bindings_loose::tx_inner_arr_len(locator.as_ptr() as i32, -1) },
        error_codes::INVALID_PARAMS,
        "tx_inner_arr_len_neg_len",
    );
    let long_len = DEFAULT_BLOB_SIZE + 1;
    check_result(
        unsafe { host_bindings_loose::tx_inner_arr_len(locator.as_ptr() as i32, long_len as i32) },
        error_codes::DATA_FIELD_TOO_LARGE,
        "tx_inner_arr_len_too_long",
    );
    check_result(
        unsafe {
            host_bindings_loose::tx_inner_arr_len(
                locator.as_ptr() as i32 + 1_000_000_000,
                locator.len() as i32,
            )
        },
        error_codes::POINTER_OUT_OF_BOUNDS,
        "tx_inner_arr_len_ptr_oob",
    );

    // uint32
    with_buffer::<32, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::check_id(
                    account.0.as_ptr(),
                    account.0.len(),
                    locator.as_ptr().wrapping_add(1_000_000_000),
                    8,
                    ptr,
                    len,
                )
            },
            error_codes::POINTER_OUT_OF_BOUNDS,
            "check_id_oob_len_u32",
        )
    });
    with_buffer::<32, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::check_id(
                    account.0.as_ptr(),
                    account.0.len(),
                    account.0.as_ptr(),
                    account.0.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "check_id_wrong_len_u32",
        )
    });

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
            "float_from_uint_wrong_len_uint64",
        )
    });

    // uint256
    check_result(
        unsafe {
            host_bindings_loose::cache_le(
                locator.as_ptr() as i32 + 1_000_000_000,
                locator.len() as i32,
                1,
            )
        },
        error_codes::POINTER_OUT_OF_BOUNDS,
        "cache_le_ptr_oob",
    );
    check_result(
        unsafe { host_bindings_loose::cache_le(locator.as_ptr() as i32, locator.len() as i32, 1) },
        error_codes::INVALID_PARAMS,
        "cache_le_wrong_len",
    );

    // AccountID
    with_buffer::<32, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host_bindings_loose::accountroot_id(
                    locator.as_ptr() as i32 + 1_000_000_000,
                    locator.len() as i32,
                    ptr,
                    len,
                )
            },
            error_codes::POINTER_OUT_OF_BOUNDS,
            "accountroot_id_len_oob",
        )
    });
    with_buffer::<32, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host_bindings_loose::accountroot_id(
                    locator.as_ptr() as i32,
                    locator.len() as i32,
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "accountroot_id_wrong_len",
        )
    });

    // Currency
    with_buffer::<32, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host_bindings_loose::trustline_id(
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
            "trustline_id_len_oob_currency",
        )
    });
    with_buffer::<32, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host_bindings_loose::trustline_id(
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
            "trustline_id_wrong_len_currency",
        )
    });

    // Issue
    let asset1_bytes = Issue::XRP(XrpIssue {}).as_bytes();
    with_buffer::<32, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::amm_id(
                    asset1_bytes.as_ptr(),
                    asset1_bytes.len(),
                    locator.as_ptr().wrapping_add(1_000_000_000),
                    locator.len(),
                    ptr,
                    len,
                )
            },
            error_codes::POINTER_OUT_OF_BOUNDS,
            "amm_id_len_oob_asset2",
        )
    });
    with_buffer::<32, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::amm_id(
                    asset1_bytes.as_ptr(),
                    asset1_bytes.len(),
                    locator.as_ptr(),
                    locator.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "amm_id_len_wrong_len_asset2",
        )
    });
    let currency: &[u8] = b"USD00000000000000000"; // 20 bytes
    with_buffer::<32, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::amm_id(
                    asset1_bytes.as_ptr(),
                    asset1_bytes.len(),
                    currency.as_ptr(),
                    currency.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "amm_id_len_wrong_non_xrp_currency_len",
        )
    });
    let xrp_issue: &[u8] = &[0; 40]; // 40 bytes
    with_buffer::<32, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::amm_id(
                    xrp_issue.as_ptr(),
                    xrp_issue.len(),
                    asset1_bytes.as_ptr(),
                    asset1_bytes.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "amm_id_len_wrong_xrp_currency_len",
        )
    });
    let mptid = MptId::new(1, account);
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::amm_id(
                    mptid.as_ptr(),
                    mptid.len(),
                    asset1_bytes.as_ptr(),
                    asset1_bytes.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "amm_id_mpt",
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
            unsafe { host::tx_field(2, ptr, len) },
            error_codes::INVALID_FIELD,
            "tx_field_invalid_sfield",
        );
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe { host::home_le_field(2, ptr, len) },
            error_codes::INVALID_FIELD,
            "home_le_field_invalid_sfield",
        );
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe { host::le_field(1, 2, ptr, len) },
            error_codes::INVALID_FIELD,
            "le_field_invalid_sfield",
        );
    });
    check_result(
        unsafe { host::tx_arr_len(2) },
        error_codes::INVALID_FIELD,
        "tx_arr_len_invalid_sfield",
    );
    check_result(
        unsafe { host::home_le_arr_len(2) },
        error_codes::INVALID_FIELD,
        "home_le_arr_len_invalid_sfield",
    );
    check_result(
        unsafe { host::le_arr_len(1, 2) },
        error_codes::INVALID_FIELD,
        "le_arr_len_invalid_sfield",
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
            unsafe { host::tx_inner(locator.as_ptr(), long_len, ptr, len) },
            error_codes::DATA_FIELD_TOO_LARGE,
            "tx_inner_too_big_slice",
        );
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe { host::home_le_inner(locator.as_ptr(), long_len, ptr, len) },
            error_codes::DATA_FIELD_TOO_LARGE,
            "home_le_inner_too_big_slice",
        );
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe { host::le_inner(1, locator.as_ptr(), long_len, ptr, len) },
            error_codes::DATA_FIELD_TOO_LARGE,
            "le_inner_too_big_slice",
        );
    });
    check_result(
        unsafe { host::tx_inner_arr_len(locator.as_ptr(), long_len) },
        error_codes::DATA_FIELD_TOO_LARGE,
        "tx_inner_arr_len_too_big_slice",
    );
    check_result(
        unsafe { host::home_le_inner_arr_len(locator.as_ptr(), long_len) },
        error_codes::DATA_FIELD_TOO_LARGE,
        "home_le_inner_arr_len_too_big_slice",
    );
    check_result(
        unsafe { host::le_inner_arr_len(1, locator.as_ptr(), long_len) },
        error_codes::DATA_FIELD_TOO_LARGE,
        "le_inner_arr_len_too_big_slice",
    );
    let too_big_data_len = XRPL_CONTRACT_DATA_SIZE + 1;
    check_result(
        unsafe { host::set_data(locator.as_ptr(), too_big_data_len) },
        error_codes::DATA_FIELD_TOO_LARGE,
        "set_data_too_big_slice",
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
            unsafe { host::sha512_half(locator.as_ptr(), long_len, ptr, len) },
            error_codes::DATA_FIELD_TOO_LARGE,
            "sha512_half_too_big_slice",
        );
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::amm_id(
                    asset1_bytes.as_ptr(),
                    long_len,
                    asset1_bytes.as_ptr(),
                    asset1_bytes.len(),
                    ptr,
                    len,
                )
            },
            error_codes::DATA_FIELD_TOO_LARGE,
            "amm_id_too_big_slice",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::credential_id(
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
            "credential_id_too_big_slice",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::mptoken_id(
                    mptid.as_ptr(),
                    long_len,
                    account.0.as_ptr(),
                    account.0.len(),
                    ptr,
                    len,
                )
            },
            error_codes::DATA_FIELD_TOO_LARGE,
            "mptoken_id_too_big_slice_mptid",
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
            host::trace_xfloat(
                message.as_ptr(),
                message.len(),
                float.as_ptr().wrapping_add(1_000_000_000),
                float.len(),
            )
        },
        error_codes::POINTER_OUT_OF_BOUNDS,
        "trace_xfloat_oob_slice",
    );
    check_result(
        unsafe {
            host::trace_amt(
                message.as_ptr(),
                message.len(),
                locator.as_ptr().wrapping_add(1_000_000_000),
                locator.len(),
            )
        },
        error_codes::POINTER_OUT_OF_BOUNDS,
        "trace_amt_oob_slice",
    );
    check_result(
        unsafe {
            host::float_cmp(
                float.as_ptr().wrapping_add(1_000_000_000),
                float.len(),
                float.as_ptr(),
                float.len(),
            )
        },
        error_codes::POINTER_OUT_OF_BOUNDS,
        "float_cmp_oob_slice1",
    );
    check_result(
        unsafe {
            host::float_cmp(
                float.as_ptr(),
                float.len(),
                float.as_ptr().wrapping_add(1_000_000_000),
                float.len(),
            )
        },
        error_codes::POINTER_OUT_OF_BOUNDS,
        "float_cmp_oob_slice2",
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
                host::float_sub(
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
            "float_sub_oob_slice1",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::float_sub(
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
            "float_sub_oob_slice2",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::float_mult(
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
            "float_mult_oob_slice1",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::float_mult(
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
            "float_mult_oob_slice2",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::float_div(
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
            "float_div_oob_slice1",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::float_div(
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
            "float_div_oob_slice2",
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

    // invalid UInt32

    with_buffer::<32, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::escrow_id(
                    account.0.as_ptr(),
                    account.0.len(),
                    account.0.as_ptr(),
                    account.0.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "escrow_id_wrong_size_uint32",
        )
    });
    with_buffer::<32, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::mpt_issuance_id(
                    account.0.as_ptr(),
                    account.0.len(),
                    account.0.as_ptr(),
                    account.0.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "mpt_issuance_id_wrong_size_uint32",
        )
    });
    with_buffer::<32, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::nft_offer_id(
                    account.0.as_ptr(),
                    account.0.len(),
                    account.0.as_ptr(),
                    account.0.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "nft_offer_id_wrong_size_uint32",
        )
    });
    with_buffer::<32, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::offer_id(
                    account.0.as_ptr(),
                    account.0.len(),
                    account.0.as_ptr(),
                    account.0.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "offer_id_wrong_size_uint32",
        )
    });
    with_buffer::<32, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::oracle_id(
                    account.0.as_ptr(),
                    account.0.len(),
                    account.0.as_ptr(),
                    account.0.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "oracle_id_wrong_size_uint32",
        )
    });
    with_buffer::<32, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::paychan_id(
                    account.0.as_ptr(),
                    account.0.len(),
                    account.0.as_ptr(),
                    account.0.len(),
                    account.0.as_ptr(),
                    account.0.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "paychan_id_wrong_size_uint32",
        )
    });
    with_buffer::<32, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::permissioned_domain_id(
                    account.0.as_ptr(),
                    account.0.len(),
                    account.0.as_ptr(),
                    account.0.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "permissioned_domain_id_wrong_size_uint32",
        )
    });
    with_buffer::<32, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::ticket_id(
                    account.0.as_ptr(),
                    account.0.len(),
                    account.0.as_ptr(),
                    account.0.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "ticket_id_wrong_size_uint32",
        )
    });
    with_buffer::<32, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::vault_id(
                    account.0.as_ptr(),
                    account.0.len(),
                    account.0.as_ptr(),
                    account.0.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "vault_id_wrong_size_uint32",
        )
    });

    // invalid UInt256

    check_result(
        unsafe { host::cache_le(locator.as_ptr(), locator.len(), 0) },
        error_codes::INVALID_PARAMS,
        "cache_le_wrong_size_uint256",
    );
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::nft_uri(
                    account.0.as_ptr(),
                    account.0.len(),
                    locator.as_ptr(),
                    locator.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "nft_uri_wrong_size_uint256",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe { host::nft_issuer(locator.as_ptr(), locator.len(), ptr, len) },
            error_codes::INVALID_PARAMS,
            "nft_issuer_wrong_size_uint256",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe { host::nft_taxon(locator.as_ptr(), locator.len(), ptr, len) },
            error_codes::INVALID_PARAMS,
            "nft_taxon_wrong_size_uint256",
        )
    });
    check_result(
        unsafe { host::nft_flags(locator.as_ptr(), locator.len()) },
        error_codes::INVALID_PARAMS,
        "nft_flags_wrong_size_uint256",
    );
    check_result(
        unsafe { host::nft_xfer_fee(locator.as_ptr(), locator.len()) },
        error_codes::INVALID_PARAMS,
        "nft_xfer_fee_wrong_size_uint256",
    );
    with_buffer::<4, _, _>(|ptr, len| {
        check_result(
            unsafe { host::nft_serial(locator.as_ptr(), locator.len(), ptr, len) },
            error_codes::INVALID_PARAMS,
            "nft_serial_wrong_size_uint256",
        )
    });

    // invalid AccountID

    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe { host::accountroot_id(locator.as_ptr(), locator.len(), ptr, len) },
            error_codes::INVALID_PARAMS,
            "accountroot_id_wrong_size_account_id",
        )
    });
    let seq: i32 = 1;
    let seq_bytes = seq.to_be_bytes();
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::check_id(
                    locator.as_ptr(),
                    locator.len(),
                    seq_bytes.as_ptr(),
                    seq_bytes.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "check_id_wrong_size_account_id",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::credential_id(
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
            "credential_id_wrong_size_account_id1",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::credential_id(
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
            "credential_id_wrong_size_account_id2",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::delegate_id(
                    locator.as_ptr(), // invalid AccountID size
                    locator.len(),
                    account.0.as_ptr(),
                    account.0.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "delegate_id_wrong_size_account_id1",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::delegate_id(
                    account.0.as_ptr(),
                    account.0.len(),
                    locator.as_ptr(), // invalid AccountID size
                    locator.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "delegate_id_wrong_size_account_id2",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::deposit_preauth_id(
                    locator.as_ptr(), // invalid AccountID size
                    locator.len(),
                    account.0.as_ptr(),
                    account.0.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "deposit_preauth_id_wrong_size_account_id1",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::deposit_preauth_id(
                    account.0.as_ptr(),
                    account.0.len(),
                    locator.as_ptr(), // invalid AccountID size
                    locator.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "deposit_preauth_id_wrong_size_account_id2",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe { host::did_id(locator.as_ptr(), locator.len(), ptr, len) },
            error_codes::INVALID_PARAMS,
            "did_id_wrong_size_account_id",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::escrow_id(
                    locator.as_ptr(),
                    locator.len(),
                    seq_bytes.as_ptr(),
                    seq_bytes.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "escrow_id_wrong_size_account_id",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::trustline_id(
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
            "trustline_id_wrong_size_account_id1",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::trustline_id(
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
            "trustline_id_wrong_size_account_id2",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::mpt_issuance_id(
                    locator.as_ptr(),
                    locator.len(),
                    seq_bytes.as_ptr(),
                    seq_bytes.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "mpt_issuance_id_wrong_size_account_id",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::mptoken_id(
                    mptid.as_ptr(),
                    mptid.len(),
                    locator.as_ptr(),
                    locator.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "mptoken_id_wrong_size_account_id",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::nft_offer_id(
                    locator.as_ptr(),
                    locator.len(),
                    seq_bytes.as_ptr(),
                    seq_bytes.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "nft_offer_id_wrong_size_account_id",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::offer_id(
                    locator.as_ptr(),
                    locator.len(),
                    seq_bytes.as_ptr(),
                    seq_bytes.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "offer_id_wrong_size_account_id",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::oracle_id(
                    locator.as_ptr(),
                    locator.len(),
                    seq_bytes.as_ptr(),
                    seq_bytes.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "oracle_id_wrong_size_account_id",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::paychan_id(
                    locator.as_ptr(), // invalid AccountID size
                    locator.len(),
                    account.0.as_ptr(),
                    account.0.len(),
                    seq_bytes.as_ptr(),
                    seq_bytes.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "paychan_id_wrong_size_account_id1",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::paychan_id(
                    account.0.as_ptr(),
                    account.0.len(),
                    locator.as_ptr(), // invalid AccountID size
                    locator.len(),
                    seq_bytes.as_ptr(),
                    seq_bytes.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "paychan_id_wrong_size_account_id2",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::permissioned_domain_id(
                    locator.as_ptr(),
                    locator.len(),
                    seq_bytes.as_ptr(),
                    seq_bytes.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "permissioned_domain_id_wrong_size_account_id",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe { host::signers_id(locator.as_ptr(), locator.len(), ptr, len) },
            error_codes::INVALID_PARAMS,
            "signers_id_wrong_size_account_id",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::ticket_id(
                    locator.as_ptr(),
                    locator.len(),
                    seq_bytes.as_ptr(),
                    seq_bytes.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "ticket_id_wrong_size_account_id",
        )
    });
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::vault_id(
                    locator.as_ptr(),
                    locator.len(),
                    seq_bytes.as_ptr(),
                    seq_bytes.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "vault_id_wrong_size_account_id",
        )
    });
    let uint256: &[u8] = b"00000000000000000000000000000001";
    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::nft_uri(
                    locator.as_ptr(),
                    locator.len(),
                    uint256.as_ptr(),
                    uint256.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "nft_uri_wrong_size_account_id",
        )
    });
    check_result(
        unsafe {
            host::trace_acct(
                message.as_ptr(),
                message.len(),
                locator.as_ptr(),
                locator.len(),
            )
        },
        error_codes::INVALID_PARAMS,
        "trace_acct_wrong_size_account_id",
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
            host::trace_xfloat(
                message.as_ptr().wrapping_add(1_000_000_000),
                message.len(),
                float.as_ptr(),
                float.len(),
            )
        },
        error_codes::POINTER_OUT_OF_BOUNDS,
        "trace_xfloat_oob_string",
    );
    check_result(
        unsafe {
            host::trace_acct(
                message.as_ptr().wrapping_add(1_000_000_000),
                message.len(),
                account.0.as_ptr(),
                account.0.len(),
            )
        },
        error_codes::POINTER_OUT_OF_BOUNDS,
        "trace_acct_oob_string",
    );
    check_result(
        unsafe {
            host::trace_amt(
                message.as_ptr().wrapping_add(1_000_000_000),
                message.len(),
                amount.as_ptr(),
                amount.len(),
            )
        },
        error_codes::POINTER_OUT_OF_BOUNDS,
        "trace_amt_oob_string",
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
        unsafe { host::trace_xfloat(message.as_ptr(), long_len, float.as_ptr(), float.len()) },
        error_codes::DATA_FIELD_TOO_LARGE,
        "trace_xfloat_too_long",
    );
    check_result(
        unsafe {
            host::trace_acct(
                message.as_ptr(),
                long_len,
                account.0.as_ptr(),
                account.0.len(),
            )
        },
        error_codes::DATA_FIELD_TOO_LARGE,
        "trace_acct_too_long",
    );
    check_result(
        unsafe { host::trace_amt(message.as_ptr(), long_len, amount.as_ptr(), amount.len()) },
        error_codes::DATA_FIELD_TOO_LARGE,
        "trace_amt_too_long",
    );

    // trace amount errors

    check_result(
        unsafe {
            host::trace_amt(
                message.as_ptr(),
                message.len(),
                locator.as_ptr(),
                locator.len(),
            )
        },
        error_codes::INVALID_PARAMS,
        "trace_amt_wrong_length",
    );

    // other misc errors

    with_buffer::<2, _, _>(|ptr, len| {
        check_result(
            unsafe {
                host::mptoken_id(
                    locator.as_ptr(),
                    locator.len(),
                    account.0.as_ptr(),
                    account.0.len(),
                    ptr,
                    len,
                )
            },
            error_codes::INVALID_PARAMS,
            "mptoken_id_mptid_wrong_length",
        )
    });
    check_result(
        unsafe {
            host::trace(
                message.as_ptr(),
                message.len(),
                locator.as_ptr(),
                locator.len(),
                2,
            )
        },
        error_codes::INVALID_PARAMS,
        "trace_invalid_as_hex",
    );

    // ensure that the Slice index desync issue is fixed
    let empty: &[u8] = b"";
    check_result(
        unsafe {
            host::trace_acct(
                empty.as_ptr(),
                empty.len(),
                account.0.as_ptr(),
                account.0.len(),
            )
        },
        0,
        "trace_acct_check_desync",
    );

    1 // <-- If we get here, finish the escrow.
}
