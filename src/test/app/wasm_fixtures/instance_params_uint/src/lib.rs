#![allow(unused_imports)]
#![cfg_attr(target_arch = "wasm32", no_std)]

#[cfg(not(target_arch = "wasm32"))]
extern crate std;

use xrpl_wasm_stdlib::host::trace::{DataRepr, trace, trace_data, trace_num};
use xrpl_wasm_stdlib::host::{instance_param};
use xrpl_wasm_stdlib::core::params::instance::{get_instance_param};
use xrpl_wasm_stdlib::core::type_codes::{
    STI_UINT8, STI_UINT16, STI_UINT32, STI_UINT64, STI_UINT128,
    STI_UINT160, STI_UINT192, STI_UINT256
};
use xrpl_wasm_stdlib::core::types::uint::Hash160;
use xrpl_wasm_stdlib::core::types::uint::Hash192;
use xrpl_wasm_stdlib::core::types::uint::Hash256;

#[unsafe(no_mangle)]
pub extern "C" fn instance_params_uint() -> i32 {
    // UINT8
    let value = match get_instance_param::<u8>(0) {
        Ok(a) => a,
        Err(err) => {
            let _ = trace_num("UINT8 Parameter Error Code:", err as i64);
            return -1;
        }
    };
    let _ = trace_num("UINT8 Value:", value as i64);
    // as hex
    let _ = trace_data("UINT8 Hex:", &[value], DataRepr::AsHex);

    // TODO: replace with require
    if value != 255 {
        let _ = trace("UINT8 Parameter Error: Invalid Value");
        return -1;
    }

    // UINT16
    let value = match get_instance_param::<u16>(1) {
        Ok(a) => a,
        Err(err) => {
            let _ = trace_num("UINT16 Parameter Error Code:", err as i64);
            return -1;
        }
    };
    let _ = trace_num("UINT16 Value:", value as i64);
    // as hex
    let buf = value.to_le_bytes();
    let _ = trace_data("UINT16 Hex:", &buf, DataRepr::AsHex);

    // TODO: replace with require
    if value != 65535 {
        let _ = trace("UINT16 Parameter Error: Invalid Value");
        return -1;
    }

    // UINT32
    let value = match get_instance_param::<u32>(2) {
        Ok(a) => a,
        Err(err) => {
            let _ = trace_num("UINT32 Parameter Error Code:", err as i64);
            return -1;
        }
    };
    // as hex
    let buf = value.to_le_bytes();
    let _ = trace_data("UINT32 Hex:", &buf, DataRepr::AsHex);

    // TODO: replace with require
    if value != 4294967295 {
        let _ = trace("UINT32 Parameter Error: Invalid Value");
        return -1;
    }

    // UINT64
    let value = match get_instance_param::<u64>(3) {
        Ok(a) => a,
        Err(err) => {
            let _ = trace_num("UINT64 Parameter Error Code:", err as i64);
            return -1;
        }
    };
    let _ = trace_num("UINT64 Value:", value as i64);
    // as hex
    let buf = value.to_le_bytes();
    let _ = trace_data("UINT64 Hex:", &buf, DataRepr::AsHex);

    // TODO: replace with require
    if buf != [0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF] {
        let _ = trace("UINT64 Parameter Error: Invalid Value");
        return -1;
    }

    // UINT128
    let value = match get_instance_param::<u128>(4) {
        Ok(a) => a,
        Err(err) => {
            let _ = trace_num("UINT128 Parameter Error Code:", err as i64);
            return -1;
        }
    };
    // as hex
    let buf = value.to_le_bytes();
    let _ = trace_data("UINT128 Hex:", &buf, DataRepr::AsHex);

    // TODO: replace with require
    if buf != [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01] {
        let _ = trace("UINT128 Parameter Error: Invalid Value");
        return -1;
    }

    // UINT160
    let value = match get_instance_param::<Hash160>(5) {
        Ok(a) => a,
        Err(err) => {
            let _ = trace_num("UINT160 Parameter Error Code:", err as i64);
            return -1;
        }
    };
    // as hex
    let buf = value.as_bytes();
    let _ = trace_data("UINT160 Hex:", buf, DataRepr::AsHex);

    // TODO: replace with require
    let expected190: [u8; 20] = [
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01
    ];
    if *buf != expected190 {
        let _ = trace("UINT160 Parameter Error: Invalid Value");
        return -1;
    }

    // UINT192
    let value = match get_instance_param::<Hash192>(6) {
        Ok(a) => a,
        Err(err) => {
            let _ = trace_num("UINT192 Parameter Error Code:", err as i64);
            return -1;
        }
    };
    // as hex
    let buf = value.as_bytes();
    let _ = trace_data("UINT192 Hex:", buf, DataRepr::AsHex);

    // TODO: replace with require
    let expected192: [u8; 24] = [
        0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x01
    ];
    if *buf != expected192 {
        let _ = trace("UINT192 Parameter Error: Invalid Value");
        return -1;
    }

    // UINT256
    let value = match get_instance_param::<Hash256>(7) {
        Ok(a) => a,
        Err(err) => {
            let _ = trace_num("UINT256 Parameter Error Code:", err as i64);
            return -1;
        }
    };
    // as hex
    let buf = value.as_bytes();
    let _ = trace_data("UINT256 Hex:", buf, DataRepr::AsHex);

    // TODO: replace with require
    let expected256: [u8; 32] = [
        0xD9, 0x55, 0xDA, 0xC2, 0xE7, 0x75, 0x19, 0xF0,
        0x5A, 0xD1, 0x51, 0xA5, 0xD3, 0xC9, 0x9F, 0xC8,
        0x12, 0x5F, 0xB3, 0x9D, 0x58, 0xFF, 0x9F, 0x10,
        0x6F, 0x1A, 0xCA, 0x44, 0x91, 0x90, 0x2C, 0x25
    ];
    if *buf != expected256 {
        let _ = trace("UINT256 Parameter Error: Invalid Value");
        return -1;
    }

    return 0; // Return success code
}
