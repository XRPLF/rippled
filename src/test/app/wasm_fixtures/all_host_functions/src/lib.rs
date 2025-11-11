#![cfg_attr(target_arch = "wasm32", no_std)]

#[cfg(not(target_arch = "wasm32"))]
extern crate std;

//
// Host Functions Test
// Tests 26 host functions (across 7 categories)
//
// With craft you can run this test with:
//   craft test --project host_functions_test --test-case host_functions_test
//
// Amount Format Update:
// - XRP amounts now return as 8-byte serialized rippled objects
// - IOU and MPT amounts return in variable-length serialized format
// - Format details: https://xrpl.org/docs/references/protocol/binary-format#amount-fields
//
// Error Code Ranges:
// -100 to -199: Ledger Header Functions (3 functions)
// -200 to -299: Transaction Data Functions (5 functions)
// -300 to -399: Current Ledger Object Functions (4 functions)
// -400 to -499: Any Ledger Object Functions (5 functions)
// -500 to -599: Keylet Generation Functions (4 functions)
// -600 to -699: Utility Functions (4 functions)
// -700 to -799: Data Update Functions (1 function)
//

use xrpl_std::core::current_tx::escrow_finish::EscrowFinish;
use xrpl_std::core::current_tx::traits::TransactionCommonFields;
use xrpl_std::host;
use xrpl_std::host::trace::{trace, trace_account_buf, trace_data, trace_num, DataRepr};
use xrpl_std::sfield;

#[unsafe(no_mangle)]
pub extern "C" fn finish() -> i32 {
    let _ = trace("=== HOST FUNCTIONS TEST ===");
    let _ = trace("Testing 26 host functions");

    // Category 1: Ledger Header Data Functions (3 functions)
    // Error range: -100 to -199
    match test_ledger_header_functions() {
        0 => (),
        err => return err,
    }

    // Category 2: Transaction Data Functions (5 functions)
    // Error range: -200 to -299
    match test_transaction_data_functions() {
        0 => (),
        err => return err,
    }

    // Category 3: Current Ledger Object Functions (4 functions)
    // Error range: -300 to -399
    match test_current_ledger_object_functions() {
        0 => (),
        err => return err,
    }

    // Category 4: Any Ledger Object Functions (5 functions)
    // Error range: -400 to -499
    match test_any_ledger_object_functions() {
        0 => (),
        err => return err,
    }

    // Category 5: Keylet Generation Functions (4 functions)
    // Error range: -500 to -599
    match test_keylet_generation_functions() {
        0 => (),
        err => return err,
    }

    // Category 6: Utility Functions (4 functions)
    // Error range: -600 to -699
    match test_utility_functions() {
        0 => (),
        err => return err,
    }

    // Category 7: Data Update Functions (1 function)
    // Error range: -700 to -799
    match test_data_update_functions() {
        0 => (),
        err => return err,
    }

    let _ = trace("SUCCESS: All host function tests passed!");
    1 // Success return code for WASM finish function
}

/// Test Category 1: Ledger Header Data Functions (3 functions)
/// - get_ledger_sqn() - Get ledger sequence number
/// - get_parent_ledger_time() - Get parent ledger timestamp
/// - get_parent_ledger_hash() - Get parent ledger hash
fn test_ledger_header_functions() -> i32 {
    let _ = trace("--- Category 1: Ledger Header Functions ---");

    // Test 1.1: get_ledger_sqn() - should return current ledger sequence number
    let sqn_result = unsafe { host::get_ledger_sqn() };

    if sqn_result <= 0 {
        let _ = trace_num("ERROR: get_ledger_sqn failed:", sqn_result as i64);
        return -101; // Ledger sequence number test failed
    }
    let _ = trace_num("Ledger sequence number:", sqn_result as i64);

    // Test 1.2: get_parent_ledger_time() - should return parent ledger timestamp
    let time_result = unsafe { host::get_parent_ledger_time() };

    if time_result <= 0 {
        let _ = trace_num("ERROR: get_parent_ledger_time failed:", time_result as i64);
        return -102; // Parent ledger time test failed
    }
    let _ = trace_num("Parent ledger time:", time_result as i64);

    // Test 1.3: get_parent_ledger_hash() - should return parent ledger hash (32 bytes)
    let mut hash_buffer = [0u8; 32];
    let hash_result =
        unsafe { host::get_parent_ledger_hash(hash_buffer.as_mut_ptr(), hash_buffer.len()) };

    if hash_result != 32 {
        let _ = trace_num(
            "ERROR: get_parent_ledger_hash wrong length:",
            hash_result as i64,
        );
        return -103; // Parent ledger hash test failed - should be exactly 32 bytes
    }
    let _ = trace_data("Parent ledger hash:", &hash_buffer, DataRepr::AsHex);

    let _ = trace("SUCCESS: Ledger header functions");
    0
}

/// Test Category 2: Transaction Data Functions (5 functions)
/// Tests all functions for accessing current transaction data
fn test_transaction_data_functions() -> i32 {
    let _ = trace("--- Category 2: Transaction Data Functions ---");

    // Test 2.1: get_tx_field() - Basic transaction field access
    // Test with Account field (required, 20 bytes)
    let mut account_buffer = [0u8; 20];
    let account_len = unsafe {
        host::get_tx_field(
            sfield::Account,
            account_buffer.as_mut_ptr(),
            account_buffer.len(),
        )
    };

    if account_len != 20 {
        let _ = trace_num(
            "ERROR: get_tx_field(Account) wrong length:",
            account_len as i64,
        );
        return -201; // Basic transaction field test failed
    }
    let _ = trace_account_buf("Transaction Account:", &account_buffer);

    // Test with Fee field (XRP amount - 8 bytes in new serialized format)
    // New format: XRP amounts are always 8 bytes (positive: value | cPositive flag, negative: just value)
    let mut fee_buffer = [0u8; 8];
    let fee_len =
        unsafe { host::get_tx_field(sfield::Fee, fee_buffer.as_mut_ptr(), fee_buffer.len()) };

    if fee_len != 8 {
        let _ = trace_num(
            "ERROR: get_tx_field(Fee) wrong length (expected 8 bytes for XRP):",
            fee_len as i64,
        );
        return -202; // Fee field test failed - XRP amounts should be exactly 8 bytes
    }
    let _ = trace_num("Transaction Fee length:", fee_len as i64);
    let _ = trace_data(
        "Transaction Fee (serialized XRP amount):",
        &fee_buffer,
        DataRepr::AsHex,
    );

    // Test with Sequence field (required, 4 bytes uint32)
    let mut seq_buffer = [0u8; 4];
    let seq_len =
        unsafe { host::get_tx_field(sfield::Sequence, seq_buffer.as_mut_ptr(), seq_buffer.len()) };

    if seq_len != 4 {
        let _ = trace_num(
            "ERROR: get_tx_field(Sequence) wrong length:",
            seq_len as i64,
        );
        return -203; // Sequence field test failed
    }
    let _ = trace_data("Transaction Sequence:", &seq_buffer, DataRepr::AsHex);

    // NOTE: get_tx_field2() through get_tx_field6() have been deprecated.
    // Use get_tx_field() with appropriate parameters for all transaction field access.

    // Test 2.2: get_tx_nested_field() - Nested field access with locator
    let locator = [0x01, 0x00]; // Simple locator for first element
    let mut nested_buffer = [0u8; 32];
    let nested_result = unsafe {
        host::get_tx_nested_field(
            locator.as_ptr(),
            locator.len(),
            nested_buffer.as_mut_ptr(),
            nested_buffer.len(),
        )
    };

    if nested_result < 0 {
        let _ = trace_num(
            "INFO: get_tx_nested_field not applicable:",
            nested_result as i64,
        );
        // Expected - locator may not match transaction structure
    } else {
        let _ = trace_num("Nested field length:", nested_result as i64);
        let _ = trace_data(
            "Nested field:",
            &nested_buffer[..nested_result as usize],
            DataRepr::AsHex,
        );
    }

    // Test 2.3: get_tx_array_len() - Get array length
    let signers_len = unsafe { host::get_tx_array_len(sfield::Signers) };
    let _ = trace_num("Signers array length:", signers_len as i64);

    let memos_len = unsafe { host::get_tx_array_len(sfield::Memos) };
    let _ = trace_num("Memos array length:", memos_len as i64);

    // Test 2.4: get_tx_nested_array_len() - Get nested array length with locator
    let nested_array_len =
        unsafe { host::get_tx_nested_array_len(locator.as_ptr(), locator.len()) };

    if nested_array_len < 0 {
        let _ = trace_num(
            "INFO: get_tx_nested_array_len not applicable:",
            nested_array_len as i64,
        );
    } else {
        let _ = trace_num("Nested array length:", nested_array_len as i64);
    }

    let _ = trace("SUCCESS: Transaction data functions");
    0
}

/// Test Category 3: Current Ledger Object Functions (4 functions)
/// Tests functions that access the current ledger object being processed
fn test_current_ledger_object_functions() -> i32 {
    let _ = trace("--- Category 3: Current Ledger Object Functions ---");

    // Test 3.1: get_current_ledger_obj_field() - Access field from current ledger object
    // Test with Balance field (XRP amount - 8 bytes in new serialized format)
    let mut balance_buffer = [0u8; 8];
    let balance_result = unsafe {
        host::get_current_ledger_obj_field(
            sfield::Balance,
            balance_buffer.as_mut_ptr(),
            balance_buffer.len(),
        )
    };

    if balance_result <= 0 {
        let _ = trace_num(
            "INFO: get_current_ledger_obj_field(Balance) failed (may be expected):",
            balance_result as i64,
        );
        // This might fail if current ledger object doesn't have balance field
    } else if balance_result == 8 {
        let _ = trace_num(
            "Current object balance length (XRP amount):",
            balance_result as i64,
        );
        let _ = trace_data(
            "Current object balance (serialized XRP amount):",
            &balance_buffer,
            DataRepr::AsHex,
        );
    } else {
        let _ = trace_num(
            "Current object balance length (non-XRP amount):",
            balance_result as i64,
        );
        let _ = trace_data(
            "Current object balance:",
            &balance_buffer[..balance_result as usize],
            DataRepr::AsHex,
        );
    }

    // Test with Account field
    let mut current_account_buffer = [0u8; 20];
    let current_account_result = unsafe {
        host::get_current_ledger_obj_field(
            sfield::Account,
            current_account_buffer.as_mut_ptr(),
            current_account_buffer.len(),
        )
    };

    if current_account_result <= 0 {
        let _ = trace_num(
            "INFO: get_current_ledger_obj_field(Account) failed:",
            current_account_result as i64,
        );
    } else {
        let _ = trace_account_buf("Current ledger object account:", &current_account_buffer);
    }

    // Test 3.2: get_current_ledger_obj_nested_field() - Nested field access
    let locator = [0x01, 0x00]; // Simple locator
    let mut current_nested_buffer = [0u8; 32];
    let current_nested_result = unsafe {
        host::get_current_ledger_obj_nested_field(
            locator.as_ptr(),
            locator.len(),
            current_nested_buffer.as_mut_ptr(),
            current_nested_buffer.len(),
        )
    };

    if current_nested_result < 0 {
        let _ = trace_num(
            "INFO: get_current_ledger_obj_nested_field not applicable:",
            current_nested_result as i64,
        );
    } else {
        let _ = trace_num("Current nested field length:", current_nested_result as i64);
        let _ = trace_data(
            "Current nested field:",
            &current_nested_buffer[..current_nested_result as usize],
            DataRepr::AsHex,
        );
    }

    // Test 3.3: get_current_ledger_obj_array_len() - Array length in current object
    let current_array_len = unsafe { host::get_current_ledger_obj_array_len(sfield::Signers) };
    let _ = trace_num(
        "Current object Signers array length:",
        current_array_len as i64,
    );

    // Test 3.4: get_current_ledger_obj_nested_array_len() - Nested array length
    let current_nested_array_len =
        unsafe { host::get_current_ledger_obj_nested_array_len(locator.as_ptr(), locator.len()) };

    if current_nested_array_len < 0 {
        let _ = trace_num(
            "INFO: get_current_ledger_obj_nested_array_len not applicable:",
            current_nested_array_len as i64,
        );
    } else {
        let _ = trace_num(
            "Current nested array length:",
            current_nested_array_len as i64,
        );
    }

    let _ = trace("SUCCESS: Current ledger object functions");
    0
}

/// Test Category 4: Any Ledger Object Functions (5 functions)
/// Tests functions that work with cached ledger objects
fn test_any_ledger_object_functions() -> i32 {
    let _ = trace("--- Category 4: Any Ledger Object Functions ---");

    // First we need to cache a ledger object to test the other functions
    // Get the account from transaction and generate its keylet
    let escrow_finish = EscrowFinish;
    let account_id = escrow_finish.get_account().unwrap();

    // Test 4.1: cache_ledger_obj() - Cache a ledger object
    let mut keylet_buffer = [0u8; 32];
    let keylet_result = unsafe {
        host::account_keylet(
            account_id.0.as_ptr(),
            account_id.0.len(),
            keylet_buffer.as_mut_ptr(),
            keylet_buffer.len(),
        )
    };

    if keylet_result != 32 {
        let _ = trace_num(
            "ERROR: account_keylet failed for caching test:",
            keylet_result as i64,
        );
        return -401; // Keylet generation failed for caching test
    }

    let cache_result =
        unsafe { host::cache_ledger_obj(keylet_buffer.as_ptr(), keylet_result as usize, 0) };

    if cache_result <= 0 {
        let _ = trace_num(
            "INFO: cache_ledger_obj failed (expected with test fixtures):",
            cache_result as i64,
        );
        // Test fixtures may not contain the account object - this is expected
        // We'll test the interface but expect failures

        // Test 4.2-4.5 with invalid slot (should fail gracefully)
        let mut test_buffer = [0u8; 32];

        // Test get_ledger_obj_field with invalid slot
        let field_result = unsafe {
            host::get_ledger_obj_field(
                1,
                sfield::Balance,
                test_buffer.as_mut_ptr(),
                test_buffer.len(),
            )
        };
        if field_result < 0 {
            let _ = trace_num(
                "INFO: get_ledger_obj_field failed as expected (no cached object):",
                field_result as i64,
            );
        }

        // Test get_ledger_obj_nested_field with invalid slot
        let locator = [0x01, 0x00];
        let nested_result = unsafe {
            host::get_ledger_obj_nested_field(
                1,
                locator.as_ptr(),
                locator.len(),
                test_buffer.as_mut_ptr(),
                test_buffer.len(),
            )
        };
        if nested_result < 0 {
            let _ = trace_num(
                "INFO: get_ledger_obj_nested_field failed as expected:",
                nested_result as i64,
            );
        }

        // Test get_ledger_obj_array_len with invalid slot
        let array_result = unsafe { host::get_ledger_obj_array_len(1, sfield::Signers) };
        if array_result < 0 {
            let _ = trace_num(
                "INFO: get_ledger_obj_array_len failed as expected:",
                array_result as i64,
            );
        }

        // Test get_ledger_obj_nested_array_len with invalid slot
        let nested_array_result =
            unsafe { host::get_ledger_obj_nested_array_len(1, locator.as_ptr(), locator.len()) };
        if nested_array_result < 0 {
            let _ = trace_num(
                "INFO: get_ledger_obj_nested_array_len failed as expected:",
                nested_array_result as i64,
            );
        }

        let _ = trace("SUCCESS: Any ledger object functions (interface tested)");
        return 0;
    }

    // If we successfully cached an object, test the access functions
    let slot = cache_result;
    let _ = trace_num("Successfully cached object in slot:", slot as i64);

    // Test 4.2: get_ledger_obj_field() - Access field from cached object
    let mut cached_balance_buffer = [0u8; 8];
    let cached_balance_result = unsafe {
        host::get_ledger_obj_field(
            slot,
            sfield::Balance,
            cached_balance_buffer.as_mut_ptr(),
            cached_balance_buffer.len(),
        )
    };

    if cached_balance_result <= 0 {
        let _ = trace_num(
            "INFO: get_ledger_obj_field(Balance) failed:",
            cached_balance_result as i64,
        );
    } else if cached_balance_result == 8 {
        let _ = trace_num(
            "Cached object balance length (XRP amount):",
            cached_balance_result as i64,
        );
        let _ = trace_data(
            "Cached object balance (serialized XRP amount):",
            &cached_balance_buffer,
            DataRepr::AsHex,
        );
    } else {
        let _ = trace_num(
            "Cached object balance length (non-XRP amount):",
            cached_balance_result as i64,
        );
        let _ = trace_data(
            "Cached object balance:",
            &cached_balance_buffer[..cached_balance_result as usize],
            DataRepr::AsHex,
        );
    }

    // Test 4.3: get_ledger_obj_nested_field() - Nested field from cached object
    let locator = [0x01, 0x00];
    let mut cached_nested_buffer = [0u8; 32];
    let cached_nested_result = unsafe {
        host::get_ledger_obj_nested_field(
            slot,
            locator.as_ptr(),
            locator.len(),
            cached_nested_buffer.as_mut_ptr(),
            cached_nested_buffer.len(),
        )
    };

    if cached_nested_result < 0 {
        let _ = trace_num(
            "INFO: get_ledger_obj_nested_field not applicable:",
            cached_nested_result as i64,
        );
    } else {
        let _ = trace_num("Cached nested field length:", cached_nested_result as i64);
        let _ = trace_data(
            "Cached nested field:",
            &cached_nested_buffer[..cached_nested_result as usize],
            DataRepr::AsHex,
        );
    }

    // Test 4.4: get_ledger_obj_array_len() - Array length from cached object
    let cached_array_len = unsafe { host::get_ledger_obj_array_len(slot, sfield::Signers) };
    let _ = trace_num(
        "Cached object Signers array length:",
        cached_array_len as i64,
    );

    // Test 4.5: get_ledger_obj_nested_array_len() - Nested array length from cached object
    let cached_nested_array_len =
        unsafe { host::get_ledger_obj_nested_array_len(slot, locator.as_ptr(), locator.len()) };

    if cached_nested_array_len < 0 {
        let _ = trace_num(
            "INFO: get_ledger_obj_nested_array_len not applicable:",
            cached_nested_array_len as i64,
        );
    } else {
        let _ = trace_num(
            "Cached nested array length:",
            cached_nested_array_len as i64,
        );
    }

    let _ = trace("SUCCESS: Any ledger object functions");
    0
}

/// Test Category 5: Keylet Generation Functions (4 functions)
/// Tests keylet generation functions for different ledger entry types
fn test_keylet_generation_functions() -> i32 {
    let _ = trace("--- Category 5: Keylet Generation Functions ---");

    let escrow_finish = EscrowFinish;
    let account_id = escrow_finish.get_account().unwrap();

    // Test 5.1: account_keylet() - Generate keylet for account
    let mut account_keylet_buffer = [0u8; 32];
    let account_keylet_result = unsafe {
        host::account_keylet(
            account_id.0.as_ptr(),
            account_id.0.len(),
            account_keylet_buffer.as_mut_ptr(),
            account_keylet_buffer.len(),
        )
    };

    if account_keylet_result != 32 {
        let _ = trace_num(
            "ERROR: account_keylet failed:",
            account_keylet_result as i64,
        );
        return -501; // Account keylet generation failed
    }
    let _ = trace_data("Account keylet:", &account_keylet_buffer, DataRepr::AsHex);

    // Test 5.2: credential_keylet() - Generate keylet for credential
    let mut credential_keylet_buffer = [0u8; 32];
    let credential_keylet_result = unsafe {
        host::credential_keylet(
            account_id.0.as_ptr(), // Subject
            account_id.0.len(),
            account_id.0.as_ptr(), // Issuer - same account for test
            account_id.0.len(),
            b"TestType".as_ptr(), // Credential type
            9usize,               // Length of "TestType"
            credential_keylet_buffer.as_mut_ptr(),
            credential_keylet_buffer.len(),
        )
    };

    if credential_keylet_result <= 0 {
        let _ = trace_num(
            "INFO: credential_keylet failed (expected - interface issue):",
            credential_keylet_result as i64,
        );
        // This is expected to fail due to unusual parameter types
    } else {
        let _ = trace_data(
            "Credential keylet:",
            &credential_keylet_buffer[..credential_keylet_result as usize],
            DataRepr::AsHex,
        );
    }

    // Test 5.3: escrow_keylet() - Generate keylet for escrow
    let mut escrow_keylet_buffer = [0u8; 32];
    let escrow_keylet_result = unsafe {
        host::escrow_keylet(
            account_id.0.as_ptr(),
            account_id.0.len(),
            1000, // Sequence number
            escrow_keylet_buffer.as_mut_ptr(),
            escrow_keylet_buffer.len(),
        )
    };

    if escrow_keylet_result != 32 {
        let _ = trace_num("ERROR: escrow_keylet failed:", escrow_keylet_result as i64);
        return -503; // Escrow keylet generation failed
    }
    let _ = trace_data("Escrow keylet:", &escrow_keylet_buffer, DataRepr::AsHex);

    // Test 5.4: oracle_keylet() - Generate keylet for oracle
    let mut oracle_keylet_buffer = [0u8; 32];
    let oracle_keylet_result = unsafe {
        host::oracle_keylet(
            account_id.0.as_ptr(),
            account_id.0.len(),
            42, // Document ID
            oracle_keylet_buffer.as_mut_ptr(),
            oracle_keylet_buffer.len(),
        )
    };

    if oracle_keylet_result != 32 {
        let _ = trace_num("ERROR: oracle_keylet failed:", oracle_keylet_result as i64);
        return -504; // Oracle keylet generation failed
    }
    let _ = trace_data("Oracle keylet:", &oracle_keylet_buffer, DataRepr::AsHex);

    let _ = trace("SUCCESS: Keylet generation functions");
    0
}

/// Test Category 6: Utility Functions (4 functions)
/// Tests utility functions for hashing, NFT access, and tracing
fn test_utility_functions() -> i32 {
    let _ = trace("--- Category 6: Utility Functions ---");

    // Test 6.1: compute_sha512_half() - SHA512 hash computation (first 32 bytes)
    let test_data = b"Hello, XRPL WASM world!";
    let mut hash_output = [0u8; 32];
    let hash_result = unsafe {
        host::compute_sha512_half(
            test_data.as_ptr(),
            test_data.len(),
            hash_output.as_mut_ptr(),
            hash_output.len(),
        )
    };

    if hash_result != 32 {
        let _ = trace_num("ERROR: compute_sha512_half failed:", hash_result as i64);
        return -601; // SHA512 half computation failed
    }
    let _ = trace_data("Input data:", test_data, DataRepr::AsHex);
    let _ = trace_data("SHA512 half hash:", &hash_output, DataRepr::AsHex);

    // Test 6.2: get_nft() - NFT data retrieval
    let escrow_finish = EscrowFinish;
    let account_id = escrow_finish.get_account().unwrap();
    let nft_id = [0u8; 32]; // Dummy NFT ID for testing
    let mut nft_buffer = [0u8; 256];
    let nft_result = unsafe {
        host::get_nft(
            account_id.0.as_ptr(),
            account_id.0.len(),
            nft_id.as_ptr(),
            nft_id.len(),
            nft_buffer.as_mut_ptr(),
            nft_buffer.len(),
        )
    };

    if nft_result <= 0 {
        let _ = trace_num(
            "INFO: get_nft failed (expected - no such NFT):",
            nft_result as i64,
        );
        // This is expected - test account likely doesn't own the dummy NFT
    } else {
        let _ = trace_num("NFT data length:", nft_result as i64);
        let _ = trace_data(
            "NFT data:",
            &nft_buffer[..nft_result as usize],
            DataRepr::AsHex,
        );
    }

    // Test 6.3: trace() - Debug logging with data
    let trace_message = b"Test trace message";
    let trace_data_payload = b"payload";
    let trace_result = unsafe {
        host::trace(
            trace_message.as_ptr(),
            trace_message.len(),
            trace_data_payload.as_ptr(),
            trace_data_payload.len(),
            1, // as_hex = true
        )
    };

    if trace_result < 0 {
        let _ = trace_num("ERROR: trace() failed:", trace_result as i64);
        return -603; // Trace function failed
    }
    let _ = trace_num("Trace function bytes written:", trace_result as i64);

    // Test 6.4: trace_num() - Debug logging with number
    let test_number = 42i64;
    let trace_num_result = trace_num("Test number trace", test_number);

    use xrpl_std::host::Result;
    match trace_num_result {
        Result::Ok(_) => {
            let _ = trace_num("Trace_num function succeeded", 0);
        }
        Result::Err(_) => {
            let _ = trace_num("ERROR: trace_num() failed:", -604);
            return -604; // Trace number function failed
        }
    }

    let _ = trace("SUCCESS: Utility functions");
    0
}

/// Test Category 7: Data Update Functions (1 function)
/// Tests the function for modifying the current ledger entry
fn test_data_update_functions() -> i32 {
    let _ = trace("--- Category 7: Data Update Functions ---");

    // Test 7.1: update_data() - Update current ledger entry data
    let update_payload = b"Updated ledger entry data from WASM test";

    let update_result = unsafe { host::update_data(update_payload.as_ptr(), update_payload.len()) };

    if update_result != update_payload.len() as i32 {
        let _ = trace_num("ERROR: update_data failed:", update_result as i64);
        return -701; // Data update failed
    }

    let _ = trace_data(
        "Successfully updated ledger entry with:",
        update_payload,
        DataRepr::AsHex,
    );
    let _ = trace("SUCCESS: Data update functions");
    0
}
