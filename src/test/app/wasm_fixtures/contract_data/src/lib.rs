#![cfg_attr(target_arch = "wasm32", no_std)]

#[cfg(not(target_arch = "wasm32"))]
extern crate std;

use xrpl_wasm_std::host::trace::{trace, trace_num};
use xrpl_wasm_std::core::types::account_id::AccountID;
use xrpl_wasm_std::core::data::codec_v2::{get_uint32, set_uint32, get_account, set_account, set_nested_uint8, get_nested_uint8};

#[unsafe(no_mangle)]
pub extern "C" fn create() -> i32 {
    const ACCOUNT: [u8; 20] = [
        0xAE, 0x12, 0x3A, 0x85, 0x56, 0xF3, 0xCF, 0x91, 0x15, 0x47,
        0x11, 0x37, 0x6A, 0xFB, 0x0F, 0x89, 0x4F, 0x83, 0x2B, 0x3D
    ];
    let account = AccountID(ACCOUNT);

    // Set values using the new lightweight API
    if let Err(e) = set_uint32(&account, "count", 3) {
        return e;
    }

    if let Err(e) = set_uint32(&account, "total", 12) {
        return e;
    }

    if let Err(e) = set_nested_uint8(&account, "key", "subkey", 12) {
        return e;
    }

    // Add account ID
    const DESTINATION: [u8; 20] = [
        0x05, 0x96, 0x91, 0x5C, 0xFD, 0xEE, 0xE3, 0xA6, 0x95, 0xB3,
        0xEF, 0xD6, 0xBD, 0xA9, 0xAC, 0x78, 0x8A, 0x36, 0x8B, 0x7B
    ];

    if let Err(e) = set_account(&account, "destination", DESTINATION) {
        return e;
    }

    // Read back to verify
    if let Some(count_val) = get_uint32(&account, "count") {
        let _ = trace_num("Read back count: {}", count_val.into());
    } else {
        let _ = trace("Failed to read back count");
        return -1;
    }

    // Read back the nested value
    if let Some(nested_val) = get_nested_uint8(&account, "key", "subkey") {
        let _ = trace_num("Read back nested value: {}", nested_val.into());
    } else {
        let _ = trace("Failed to read back nested value");
        return -1;
    }

    0
}

#[unsafe(no_mangle)]
pub extern "C" fn update() -> i32 {
    const ACCOUNT: [u8; 20] = [
        0xAE, 0x12, 0x3A, 0x85, 0x56, 0xF3, 0xCF, 0x91, 0x15, 0x47,
        0x11, 0x37, 0x6A, 0xFB, 0x0F, 0x89, 0x4F, 0x83, 0x2B, 0x3D
    ];
    let account = AccountID(ACCOUNT);

    // Update the count
    if let Err(e) = set_uint32(&account, "count", 4) {
        return e;
    }

    // Read back to verify
    if let Some(count_val) = get_uint32(&account, "count") {
        let _ = trace_num("Read back count: {}", count_val.into());
    } else {
        let _ = trace("Failed to read back count");
        return -1;
    }

    0
}
