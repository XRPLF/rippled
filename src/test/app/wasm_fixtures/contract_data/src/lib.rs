#![cfg_attr(target_arch = "wasm32", no_std)]

#[cfg(not(target_arch = "wasm32"))]
extern crate std;

use xrpl_wasm_std::host::trace::{trace, trace_num};
use xrpl_wasm_std::core::types::account_id::AccountID;
use xrpl_wasm_std::core::data::codec::{
    get_data,
    set_data,
    get_nested_data,
    set_nested_data,
    get_array_element,
    set_array_element,
    get_nested_array_element,
    set_nested_array_element
};

// Different accounts for different test patterns
const ACCOUNT: [u8; 20] = [
    0xAE, 0x12, 0x3A, 0x85, 0x56, 0xF3, 0xCF, 0x91, 0x15, 0x47,
    0x11, 0x37, 0x6A, 0xFB, 0x0F, 0x89, 0x4F, 0x83, 0x2B, 0x3D
];

// ============================================================================
// TEST 1: Simple Object - Only top-level key-value pairs
// Creates: { "value_u8": 42, "value_u16": 1234, "count": 3, ... }
// ============================================================================

#[unsafe(no_mangle)]
pub extern "C" fn object_simple_create() -> i32 {
    let _ = trace("=== TEST 1: Simple Object Create ===");
    let account = AccountID(ACCOUNT);

    // Test u8
    let _ = trace("Testing u8...");
    if let Err(e) = set_data::<u8>(&account, "value_u8", 42) {
        return e;
    }
    if let Some(val) = get_data::<u8>(&account, "value_u8") {
        let _ = trace_num("Read back u8:", val.into());
    } else {
        let _ = trace("Failed to read back u8");
        return -1;
    }

    // Test u16
    let _ = trace("Testing u16...");
    if let Err(e) = set_data::<u16>(&account, "value_u16", 1234) {
        return e;
    }
    if let Some(val) = get_data::<u16>(&account, "value_u16") {
        let _ = trace_num("Read back u16:", val.into());
    } else {
        let _ = trace("Failed to read back u16");
        return -1;
    }

    // Test u32
    let _ = trace("Testing u32...");
    if let Err(e) = set_data::<u32>(&account, "count", 3) {
        return e;
    }
    if let Err(e) = set_data::<u32>(&account, "total", 12) {
        return e;
    }
    if let Some(count_val) = get_data::<u32>(&account, "count") {
        let _ = trace_num("Read back count:", count_val.into());
    } else {
        let _ = trace("Failed to read back count");
        return -1;
    }

    // Test u64
    let _ = trace("Testing u64...");
    if let Err(e) = set_data::<u64>(&account, "value_u64", 9876543210) {
        return e;
    }
    if let Some(val) = get_data::<u64>(&account, "value_u64") {
        let _ = trace_num("Read back u64:", val as i64);
    } else {
        let _ = trace("Failed to read back u64");
        return -1;
    }

    // Test AccountID
    let _ = trace("Testing AccountID...");
    const DESTINATION: [u8; 20] = [
        0x05, 0x96, 0x91, 0x5C, 0xFD, 0xEE, 0xE3, 0xA6, 0x95, 0xB3,
        0xEF, 0xD6, 0xBD, 0xA9, 0xAC, 0x78, 0x8A, 0x36, 0x8B, 0x7B
    ];
    let destination = AccountID(DESTINATION);
    if let Err(e) = set_data(&account, "destination", destination) {
        return e;
    }
    if let Some(_dest) = get_data::<AccountID>(&account, "destination") {
        let _ = trace("Read back AccountID successfully");
    } else {
        let _ = trace("Failed to read back AccountID");
        return -1;
    }

    // Test reading non-existent key
    let _ = trace("Testing non-existent key...");
    if let Some(_) = get_data::<u32>(&account, "nonexistent") {
        let _ = trace("ERROR: Should not have found nonexistent key");
        return -1;
    } else {
        let _ = trace("Correctly returned None for nonexistent key");
    }

    let _ = trace("Simple object create tests passed!");
    0
}

#[unsafe(no_mangle)]
pub extern "C" fn object_simple_update() -> i32 {
    let _ = trace("=== TEST 1: Simple Object Update ===");
    let account = AccountID(ACCOUNT);

    // Update u8
    let _ = trace("Updating u8 to 99...");
    if let Err(e) = set_data::<u8>(&account, "value_u8", 99) {
        return e;
    }
    if let Some(val) = get_data::<u8>(&account, "value_u8") {
        let _ = trace_num("Read back updated u8:", val.into());
    } else {
        let _ = trace("Failed to read back u8");
        return -1;
    }

    // Update u32
    let _ = trace("Updating count to 4...");
    if let Err(e) = set_data::<u32>(&account, "count", 4) {
        return e;
    }
    if let Some(count_val) = get_data::<u32>(&account, "count") {
        let _ = trace_num("Read back updated count:", count_val.into());
    } else {
        let _ = trace("Failed to read back count");
        return -1;
    }

    // Add new field
    let _ = trace("Adding new field 'status'...");
    if let Err(e) = set_data::<u32>(&account, "status", 100) {
        return e;
    }
    if let Some(val) = get_data::<u32>(&account, "status") {
        let _ = trace_num("Read back new status:", val.into());
    } else {
        let _ = trace("Failed to read back status");
        return -1;
    }

    let _ = trace("Simple object update tests passed!");
    0
}

// ============================================================================
// TEST 2: Nested Object - Objects containing objects (depth 1)
// Creates: { "stats": {"score": 9999, "level": 5}, "key": {"subkey": 12} }
// ============================================================================

#[unsafe(no_mangle)]
pub extern "C" fn object_nested_create() -> i32 {
    let _ = trace("=== TEST 2: Nested Object Create ===");
    let account = AccountID(ACCOUNT);

    // Test nested u8
    let _ = trace("Testing nested u8...");
    if let Err(e) = set_nested_data::<u8>(&account, "key", "subkey", 12) {
        return e;
    }
    if let Some(nested_val) = get_nested_data::<u8>(&account, "key", "subkey") {
        let _ = trace_num("Read back nested value:", nested_val.into());
    } else {
        let _ = trace("Failed to read back nested value");
        return -1;
    }

    // Test nested u32
    let _ = trace("Testing nested u32...");
    if let Err(e) = set_nested_data::<u32>(&account, "stats", "score", 9999) {
        return e;
    }
    if let Some(val) = get_nested_data::<u32>(&account, "stats", "score") {
        let _ = trace_num("Read back nested u32:", val.into());
    } else {
        let _ = trace("Failed to read back nested u32");
        return -1;
    }

    // Test multiple fields in same nested object
    let _ = trace("Adding multiple fields to nested object...");
    if let Err(e) = set_nested_data::<u32>(&account, "stats", "level", 5) {
        return e;
    }
    if let Err(e) = set_nested_data::<u32>(&account, "stats", "coins", 1000) {
        return e;
    }
    if let Some(val) = get_nested_data::<u32>(&account, "stats", "level") {
        let _ = trace_num("Read back stats.level:", val.into());
    } else {
        let _ = trace("Failed to read back stats.level");
        return -1;
    }

    // Test nested u64
    let _ = trace("Testing nested u64...");
    if let Err(e) = set_nested_data::<u64>(&account, "data", "timestamp", 1234567890) {
        return e;
    }
    if let Some(val) = get_nested_data::<u64>(&account, "data", "timestamp") {
        let _ = trace_num("Read back nested u64:", val as i64);
    } else {
        let _ = trace("Failed to read back nested u64");
        return -1;
    }

    let _ = trace("Nested object create tests passed!");
    0
}

#[unsafe(no_mangle)]
pub extern "C" fn object_nested_update() -> i32 {
    let _ = trace("=== TEST 2: Nested Object Update ===");
    let account = AccountID(ACCOUNT);

    // Update nested value
    let _ = trace("Updating nested score to 12345...");
    if let Err(e) = set_nested_data::<u32>(&account, "stats", "score", 12345) {
        return e;
    }
    if let Some(val) = get_nested_data::<u32>(&account, "stats", "score") {
        let _ = trace_num("Read back updated nested score:", val.into());
    } else {
        let _ = trace("Failed to read back nested score");
        return -1;
    }

    // Update another nested field
    let _ = trace("Updating nested level to 10...");
    if let Err(e) = set_nested_data::<u32>(&account, "stats", "level", 10) {
        return e;
    }
    if let Some(val) = get_nested_data::<u32>(&account, "stats", "level") {
        let _ = trace_num("Read back updated level:", val.into());
    } else {
        let _ = trace("Failed to read back level");
        return -1;
    }

    // Add new nested field
    let _ = trace("Adding new nested field...");
    if let Err(e) = set_nested_data::<u32>(&account, "config", "timeout", 30) {
        return e;
    }
    if let Some(val) = get_nested_data::<u32>(&account, "config", "timeout") {
        let _ = trace_num("Read back new config.timeout:", val.into());
    } else {
        let _ = trace("Failed to read back config.timeout");
        return -1;
    }

    let _ = trace("Nested object update tests passed!");
    0
}

// ============================================================================
// TEST 3: Object with Arrays - Objects containing arrays of simple values
// Creates: { "items": [10, 20, 30], "values": [100, 200] }
// Note: This uses set_array_element which creates an object with array fields
// ============================================================================

#[unsafe(no_mangle)]
pub extern "C" fn object_with_arrays_create() -> i32 {
    let _ = trace("=== TEST 3: Object with Arrays Create ===");
    let account = AccountID(ACCOUNT);

    // Test u8 array
    let _ = trace("Testing u8 array...");
    if let Err(e) = set_array_element::<u8>(&account, "array_u8", 0, 10) {
        return e;
    }
    if let Err(e) = set_array_element::<u8>(&account, "array_u8", 1, 20) {
        return e;
    }
    if let Err(e) = set_array_element::<u8>(&account, "array_u8", 2, 30) {
        return e;
    }
    if let Some(val) = get_array_element::<u8>(&account, "array_u8", 0) {
        let _ = trace_num("Read array_u8[0]:", val.into());
    } else {
        let _ = trace("Failed to read array_u8[0]");
        return -1;
    }
    if let Some(val) = get_array_element::<u8>(&account, "array_u8", 1) {
        let _ = trace_num("Read array_u8[1]:", val.into());
    } else {
        let _ = trace("Failed to read array_u8[1]");
        return -1;
    }

    // Test u16 array
    let _ = trace("Testing u16 array...");
    if let Err(e) = set_array_element::<u16>(&account, "array_u16", 0, 100) {
        return e;
    }
    if let Err(e) = set_array_element::<u16>(&account, "array_u16", 1, 200) {
        return e;
    }
    if let Some(val) = get_array_element::<u16>(&account, "array_u16", 0) {
        let _ = trace_num("Read array_u16[0]:", val.into());
    } else {
        let _ = trace("Failed to read array_u16[0]");
        return -1;
    }

    // Test u32 array
    let _ = trace("Testing u32 array...");
    if let Err(e) = set_array_element::<u32>(&account, "array_u32", 0, 1000) {
        return e;
    }
    if let Err(e) = set_array_element::<u32>(&account, "array_u32", 1, 2000) {
        return e;
    }
    if let Some(val) = get_array_element::<u32>(&account, "array_u32", 0) {
        let _ = trace_num("Read array_u32[0]:", val.into());
    } else {
        let _ = trace("Failed to read array_u32[0]");
        return -1;
    }

    // Test u64 array
    let _ = trace("Testing u64 array...");
    if let Err(e) = set_array_element::<u64>(&account, "array_u64", 0, 10000) {
        return e;
    }
    if let Err(e) = set_array_element::<u64>(&account, "array_u64", 1, 20000) {
        return e;
    }
    if let Some(val) = get_array_element::<u64>(&account, "array_u64", 0) {
        let _ = trace_num("Read array_u64[0]:", val as i64);
    } else {
        let _ = trace("Failed to read array_u64[0]");
        return -1;
    }

    let _ = trace("Object with arrays create tests passed!");
    0
}

#[unsafe(no_mangle)]
pub extern "C" fn object_with_arrays_update() -> i32 {
    let _ = trace("=== TEST 3: Object with Arrays Update ===");
    let account = AccountID(ACCOUNT);

    // Update array element
    let _ = trace("Updating array_u32[0] to 7777...");
    if let Err(e) = set_array_element::<u32>(&account, "array_u32", 0, 7777) {
        return e;
    }
    if let Some(val) = get_array_element::<u32>(&account, "array_u32", 0) {
        let _ = trace_num("Read back updated array_u32[0]:", val.into());
    } else {
        let _ = trace("Failed to read back array_u32[0]");
        return -1;
    }

    // Add new array element
    let _ = trace("Adding new array element array_u16[2]...");
    if let Err(e) = set_array_element::<u16>(&account, "array_u16", 2, 300) {
        return e;
    }
    if let Some(val) = get_array_element::<u16>(&account, "array_u16", 2) {
        let _ = trace_num("Read back new array_u16[2]:", val.into());
    } else {
        let _ = trace("Failed to read back array_u16[2]");
        return -1;
    }

    // Add element with gap (should auto-fill with nulls)
    let _ = trace("Adding array_u8[5] (skipping indices 3-4)...");
    if let Err(e) = set_array_element::<u8>(&account, "array_u8", 5, 50) {
        return e;
    }
    if let Some(val) = get_array_element::<u8>(&account, "array_u8", 5) {
        let _ = trace_num("Read back array_u8[5]:", val.into());
    } else {
        let _ = trace("Failed to read back array_u8[5]");
        return -1;
    }

    let _ = trace("Object with arrays update tests passed!");
    0
}

// ============================================================================
// TEST 4: Object with Nested Arrays - Objects containing arrays of objects
// Creates: { "nested_array": [{"field1": 55, "field2": 66}, {"field1": 77}] }
// This is the most complex structure allowed (depth 1)
// DA: I wouldnt use this. If you are doing this, consider redesigning your data model
// ============================================================================

#[unsafe(no_mangle)]
pub extern "C" fn object_with_nested_arrays_create() -> i32 {
    let _ = trace("=== TEST 4: Object with Nested Arrays Create ===");
    let account = AccountID(ACCOUNT);

    // Test nested u8 array with multiple fields
    let _ = trace("Testing nested u8 array...");
    if let Err(e) = set_nested_array_element::<u8>(&account, "nested_array", 0, "field1", 55) {
        return e;
    }
    if let Err(e) = set_nested_array_element::<u8>(&account, "nested_array", 0, "field2", 66) {
        return e;
    }
    if let Err(e) = set_nested_array_element::<u8>(&account, "nested_array", 1, "field1", 77) {
        return e;
    }

    if let Some(val) = get_nested_array_element::<u8>(&account, "nested_array", 0, "field1") {
        let _ = trace_num("Read nested_array[0].field1:", val.into());
    } else {
        let _ = trace("Failed to read nested_array[0].field1");
        return -1;
    }
    if let Some(val) = get_nested_array_element::<u8>(&account, "nested_array", 0, "field2") {
        let _ = trace_num("Read nested_array[0].field2:", val.into());
    } else {
        let _ = trace("Failed to read nested_array[0].field2");
        return -1;
    }
    if let Some(val) = get_nested_array_element::<u8>(&account, "nested_array", 1, "field1") {
        let _ = trace_num("Read nested_array[1].field1:", val.into());
    } else {
        let _ = trace("Failed to read nested_array[1].field1");
        return -1;
    }

    // Test nested u32 array
    let _ = trace("Testing nested u32 array...");
    if let Err(e) = set_nested_array_element::<u32>(&account, "nested_array_u32", 0, "value", 5555) {
        return e;
    }
    if let Err(e) = set_nested_array_element::<u32>(&account, "nested_array_u32", 1, "value", 6666) {
        return e;
    }
    if let Some(val) = get_nested_array_element::<u32>(&account, "nested_array_u32", 0, "value") {
        let _ = trace_num("Read nested_array_u32[0].value:", val.into());
    } else {
        let _ = trace("Failed to read nested_array_u32[0].value");
        return -1;
    }

    // Test nested u64 array
    let _ = trace("Testing nested u64 array...");
    if let Err(e) = set_nested_array_element::<u64>(&account, "items", 0, "id", 99999) {
        return e;
    }
    if let Err(e) = set_nested_array_element::<u64>(&account, "items", 0, "price", 123456) {
        return e;
    }
    if let Some(val) = get_nested_array_element::<u64>(&account, "items", 0, "id") {
        let _ = trace_num("Read items[0].id:", val as i64);
    } else {
        let _ = trace("Failed to read items[0].id");
        return -1;
    }

    let _ = trace("Object with nested arrays create tests passed!");
    0
}

#[unsafe(no_mangle)]
pub extern "C" fn object_with_nested_arrays_update() -> i32 {
    let _ = trace("=== TEST 4: Object with Nested Arrays Update ===");
    let account = AccountID(ACCOUNT);

    // Update nested array element
    let _ = trace("Updating nested_array[0].field1 to 88...");
    if let Err(e) = set_nested_array_element::<u8>(&account, "nested_array", 0, "field1", 88) {
        return e;
    }
    if let Some(val) = get_nested_array_element::<u8>(&account, "nested_array", 0, "field1") {
        let _ = trace_num("Read back updated nested_array[0].field1:", val.into());
    } else {
        let _ = trace("Failed to read back nested_array[0].field1");
        return -1;
    }

    // Add new field to existing array element
    let _ = trace("Adding field3 to nested_array[0]...");
    if let Err(e) = set_nested_array_element::<u8>(&account, "nested_array", 0, "field3", 111) {
        return e;
    }
    if let Some(val) = get_nested_array_element::<u8>(&account, "nested_array", 0, "field3") {
        let _ = trace_num("Read back nested_array[0].field3:", val.into());
    } else {
        let _ = trace("Failed to read back nested_array[0].field3");
        return -1;
    }

    // Add new array element
    let _ = trace("Adding nested_array[2]...");
    if let Err(e) = set_nested_array_element::<u8>(&account, "nested_array", 2, "field1", 99) {
        return e;
    }
    if let Some(val) = get_nested_array_element::<u8>(&account, "nested_array", 2, "field1") {
        let _ = trace_num("Read back nested_array[2].field1:", val.into());
    } else {
        let _ = trace("Failed to read back nested_array[2].field1");
        return -1;
    }

    // Update u32 nested array
    let _ = trace("Updating nested_array_u32[1].value to 8888...");
    if let Err(e) = set_nested_array_element::<u32>(&account, "nested_array_u32", 1, "value", 8888) {
        return e;
    }
    if let Some(val) = get_nested_array_element::<u32>(&account, "nested_array_u32", 1, "value") {
        let _ = trace_num("Read back updated nested_array_u32[1].value:", val.into());
    } else {
        let _ = trace("Failed to read back nested_array_u32[1].value");
        return -1;
    }

    let _ = trace("Object with nested arrays update tests passed!");
    0
}
