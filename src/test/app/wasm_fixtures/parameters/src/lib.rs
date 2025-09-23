#![allow(unused_imports)]
#![cfg_attr(target_arch = "wasm32", no_std)]

#[cfg(not(target_arch = "wasm32"))]
extern crate std;

use xrpl_wasm_std::host::trace::{DataRepr, trace, trace_data, trace_num, trace_float};
use xrpl_wasm_std::host::{instance_param, function_param};
use xrpl_wasm_std::core::params::function::{get_function_param};
use xrpl_wasm_std::core::params::instance::{get_instance_param};
use xrpl_wasm_std::core::type_codes::{
    STI_UINT8, STI_UINT16, STI_UINT32, STI_UINT64, STI_UINT128,
    STI_UINT160, STI_UINT192, STI_UINT256, STI_AMOUNT, STI_VL, STI_ACCOUNT,
    STI_OBJECT, STI_ARRAY, STI_CURRENCY, STI_NUMBER
};
use xrpl_wasm_std::core::types::amount::opaque_float::OpaqueFloat;
use xrpl_wasm_std::core::types::number::Number;
use xrpl_wasm_std::core::types::account_id::AccountID;
use xrpl_wasm_std::core::types::amount::token_amount::TokenAmount;
use xrpl_wasm_std::host::{FLOAT_ROUNDING_MODES_TO_NEAREST, float_add, float_set};
use xrpl_wasm_std::core::types::amount::opaque_float::{FLOAT_NEGATIVE_ONE, FLOAT_ONE};
use xrpl_wasm_std::core::types::uint_160::UInt160;
use xrpl_wasm_std::core::types::uint_192::UInt192;
use xrpl_wasm_std::core::types::hash_256::Hash256;


#[unsafe(no_mangle)]
pub extern "C" fn function_params() -> i32 {
    // UINT8
    let value = match get_function_param::<u8>(0) {
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
    let value = match get_function_param::<u16>(1) {
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
    let value = match get_function_param::<u32>(2) {
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
    let value = match get_function_param::<u64>(3) {
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
    let value = match get_function_param::<u128>(4) {
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
    let value = match get_function_param::<UInt160>(5) {
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
    let value = match get_function_param::<UInt192>(6) {
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
    let value = match get_function_param::<Hash256>(7) {
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

    // // VL
    // let mut buf = [0x00; 4];
    // let output_len = unsafe { function_param(8, STI_VL.into(), buf.as_mut_ptr(), buf.len()) };
    // let _ = trace_num("VL Value Len:", output_len as i64);
    // // as hex
    // let _ = trace_data("VL Hex:", &buf[0..4], DataRepr::AsHex);

    // ACCOUNT
    let account_id = match get_function_param::<AccountID>(9) {
        Ok(a) => a,
        Err(err) => {
            let _ = trace_num("ACCOUNT Parameter Error Code:", err as i64);
            return -1;
        }
    };
    // trace the value
    let _ = trace_data("ACCOUNT Value:", &account_id.0, DataRepr::AsHex);

    // TODO: replace with require
    let expectedAccount: [u8; 20] = [
        0xAE, 0x12, 0x3A, 0x85, 0x56, 0xF3, 0xCF, 0x91,
        0x15, 0x47, 0x11, 0x37, 0x6A, 0xFB, 0x0F, 0x89,
        0x4F, 0x83, 0x2B, 0x3D
    ];
    if account_id.0 != expectedAccount {
        let _ = trace("ACCOUNT Parameter Error: Invalid Value");
        return -1;
    }

    // AMOUNT XRP
    let xrp_token = match get_function_param::<TokenAmount>(10) {
        Ok(a) => a,
        Err(err) => {
            let _ = trace_num("AMOUNT XRP Parameter Error Code:", err as i64);
            return -1;
        }
    };
    match xrp_token {
        TokenAmount::XRP { num_drops } => {
            let _ = trace_num("AMOUNT Value (XRP):", num_drops);
        }
        _ => {
            let _ = trace_num("AMOUNT Value (XRP):", -1);
        }
    }
    let buf = match xrp_token {
        TokenAmount::XRP { num_drops } => num_drops.to_le_bytes(),
        _ => [0u8; 8],
    };
    let _ = trace_data("AMOUNT Hex:", &buf, DataRepr::AsHex);

    // TODO: replace with require
    if let TokenAmount::XRP { num_drops } = xrp_token {
        if num_drops != 1000000 {
            let _ = trace("AMOUNT.XRP Parameter Error: Invalid Value");
            return -1;
        }
    } else {
        let _ = trace("AMOUNT.XRP Parameter Error: Invalid Type");
        return -1;
    }

    // AMOUNT IOU
    let iou_token = match get_function_param::<TokenAmount>(11) {
        Ok(a) => a,
        Err(err) => {
            let _ = trace_num("AMOUNT IOU Parameter Error Code:", err as i64);
            return -1;
        }
    };
    let (iou_amount, iou_issuer, iou_currency) = match &iou_token {
        TokenAmount::IOU { amount, issuer, currency_code } => {
            // trace amount hex
            let _ = trace_data("AMOUNT Value (IOU):", &amount.0, DataRepr::AsHex);
            let _ = trace_float("AMOUNT Value (IOU) - Original:", &amount.0);
            let _ = trace_data("IOU Issuer:", &issuer.0, DataRepr::AsHex);
            let _ = trace_data("IOU Currency:", &currency_code.0, DataRepr::AsHex);

            // Add FLOAT_ONE to the IOU amount
            let mut new_amount: [u8; 8] = [0u8; 8];
            let result = unsafe {
                float_add(
                    amount.0.as_ptr(),
                    8,
                    FLOAT_ONE.as_ptr(),
                    8,
                    new_amount.as_mut_ptr(),
                    8,
                    FLOAT_ROUNDING_MODES_TO_NEAREST,
                )
            };

            if result == 8 {
                // trace hex of the new amount
                let _ = trace_data("AMOUNT Value (IOU) - After adding 1:", &new_amount, DataRepr::AsHex);
                let _ = trace_float("AMOUNT Value (IOU) - After adding 1:", &new_amount);

                // Create a new TokenAmount with the updated amount
                let updated_token = TokenAmount::IOU {
                    amount: new_amount.into(),
                    issuer: *issuer,
                    currency_code: *currency_code,
                };

                // You now have the updated token amount in `updated_token`
                // and the raw float bytes in `new_amount`

            } else {
                let _ = trace_num("Error adding FLOAT_ONE to IOU amount, result:", result as i64);
            }

            (Some(*amount), Some(*issuer), Some(*currency_code))
        }
        _ => {
            let _ = trace_data("AMOUNT Value (IOU):", &[0u8; 8], DataRepr::AsHex);
            (None, None, None)
        }
    };
    // trace new iou_amount as hex
    if let Some(amount) = iou_amount {
        let _ = trace_data("IOU Amount:", &amount.0, DataRepr::AsHex);
    } else {
        let _ = trace_data("IOU Amount:", &[0u8; 8], DataRepr::AsHex);
    }

    // TODO: replace with require
    if iou_amount.is_none() {
        let _ = trace("AMOUNT.IOU Parameter Error: Invalid Type");
        return -1;
    }

    // let mut buf = [0x00; 12];
    // let output_len = unsafe { function_param(12, STI_NUMBER.into(), buf.as_mut_ptr(), buf.len()) };
    // let _ = trace_num("NUMBER Value Len:", output_len as i64);

    // NUMBER
    let number = match get_function_param::<Number>(12) {
        Ok(a) => a,
        Err(err) => {
            let _ = trace_num("NUMBER Parameter Error Code:", err as i64);
            return -1;
        }
    };
    // trace the value
    let buf = number.as_bytes();
    let _ = trace_data("NUMBER Value:", &buf, DataRepr::AsHex);

    // TODO: replace with require
    let expectedNumber: [u8; 12] = [
        0x00, 0x04, 0x43, 0x64, 0xC5, 0xBB, 0x00, 0x00,
        0xFF, 0xFF, 0xFF, 0xF1
    ];
    if buf != expectedNumber {
        let _ = trace("NUMBER Parameter Error: Invalid Value");
        return -1;
    }

    // // Parse Number to get mantissa and exponent
    // let stnumber = Number::from(&buf).unwrap();
    let _ = trace_num("NUMBER Mantissa:", number.mantissa);
    let _ = trace_num("NUMBER Exponent:", number.exponent as i64);

    let mut opaque_float_buf = [0x00; 8];
    let result = unsafe {
        float_set(
            number.exponent,
            number.mantissa,
            opaque_float_buf.as_mut_ptr(),
            8,
            FLOAT_ROUNDING_MODES_TO_NEAREST
        )
    };

    let opaque = OpaqueFloat::from(opaque_float_buf);
    let _ = trace_float("NUMBER as OpaqueFloat:", &opaque.0);
    let _ = trace_data("NUMBER OpaqueFloat Hex:", &opaque_float_buf, DataRepr::AsHex);

    // AMOUNT (MPT)
    // ISSUE (XRP)
    // ISSUE (IOU)
    // ISSUE (MPT)
    // CURRENCY

    return 0; // Return success code
}

#[unsafe(no_mangle)]
pub extern "C" fn instance_params() -> i32 {
    // UINT8
    let value = match get_instance_param::<u8>(1) {
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
    let value = match get_instance_param::<u16>(2) {
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
    let value = match get_instance_param::<u32>(3) {
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
    let value = match get_instance_param::<u64>(4) {
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
    let value = match get_instance_param::<u128>(5) {
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
    let value = match get_instance_param::<UInt160>(6) {
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
    let value = match get_instance_param::<UInt192>(7) {
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
    let value = match get_instance_param::<Hash256>(8) {
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

    // // VL
    // let mut buf = [0x00; 4];
    // let output_len = unsafe { instance_param(8, STI_VL.into(), buf.as_mut_ptr(), buf.len()) };
    // let _ = trace_num("VL Value Len:", output_len as i64);
    // // as hex
    // let _ = trace_data("VL Hex:", &buf[0..4], DataRepr::AsHex);

    // ACCOUNT
    let account_id = match get_instance_param::<AccountID>(10) {
        Ok(a) => a,
        Err(err) => {
            let _ = trace_num("ACCOUNT Parameter Error Code:", err as i64);
            return -1;
        }
    };
    // trace the value
    let _ = trace_data("ACCOUNT Value:", &account_id.0, DataRepr::AsHex);

    // TODO: replace with require
    let expectedAccount: [u8; 20] = [
        0xAE, 0x12, 0x3A, 0x85, 0x56, 0xF3, 0xCF, 0x91,
        0x15, 0x47, 0x11, 0x37, 0x6A, 0xFB, 0x0F, 0x89,
        0x4F, 0x83, 0x2B, 0x3D
    ];
    if account_id.0 != expectedAccount {
        let _ = trace("ACCOUNT Parameter Error: Invalid Value");
        return -1;
    }

    // AMOUNT XRP
    let xrp_token = match get_instance_param::<TokenAmount>(11) {
        Ok(a) => a,
        Err(err) => {
            let _ = trace_num("AMOUNT XRP Parameter Error Code:", err as i64);
            return -1;
        }
    };
    match xrp_token {
        TokenAmount::XRP { num_drops } => {
            let _ = trace_num("AMOUNT Value (XRP):", num_drops);
        }
        _ => {
            let _ = trace_num("AMOUNT Value (XRP):", -1);
        }
    }
    let buf = match xrp_token {
        TokenAmount::XRP { num_drops } => num_drops.to_le_bytes(),
        _ => [0u8; 8],
    };
    let _ = trace_data("AMOUNT Hex:", &buf, DataRepr::AsHex);

    // TODO: replace with require
    if let TokenAmount::XRP { num_drops } = xrp_token {
        if num_drops != 1000000 {
            let _ = trace("AMOUNT.XRP Parameter Error: Invalid Value");
            return -1;
        }
    } else {
        let _ = trace("AMOUNT.XRP Parameter Error: Invalid Type");
        return -1;
    }

    // AMOUNT IOU
    let iou_token = match get_instance_param::<TokenAmount>(12) {
        Ok(a) => a,
        Err(err) => {
            let _ = trace_num("AMOUNT IOU Parameter Error Code:", err as i64);
            return -1;
        }
    };
    let (iou_amount, iou_issuer, iou_currency) = match &iou_token {
        TokenAmount::IOU { amount, issuer, currency_code } => {
            // trace amount hex
            let _ = trace_data("AMOUNT Value (IOU):", &amount.0, DataRepr::AsHex);
            let _ = trace_float("AMOUNT Value (IOU) - Original:", &amount.0);
            let _ = trace_data("IOU Issuer:", &issuer.0, DataRepr::AsHex);
            let _ = trace_data("IOU Currency:", &currency_code.0, DataRepr::AsHex);

            // Add FLOAT_ONE to the IOU amount
            let mut new_amount: [u8; 8] = [0u8; 8];
            let result = unsafe {
                float_add(
                    amount.0.as_ptr(),
                    8,
                    FLOAT_ONE.as_ptr(),
                    8,
                    new_amount.as_mut_ptr(),
                    8,
                    FLOAT_ROUNDING_MODES_TO_NEAREST,
                )
            };

            if result == 8 {
                // trace hex of the new amount
                let _ = trace_data("AMOUNT Value (IOU) - After adding 1:", &new_amount, DataRepr::AsHex);
                let _ = trace_float("AMOUNT Value (IOU) - After adding 1:", &new_amount);

                // Create a new TokenAmount with the updated amount
                let updated_token = TokenAmount::IOU {
                    amount: new_amount.into(),
                    issuer: *issuer,
                    currency_code: *currency_code,
                };

                // You now have the updated token amount in `updated_token`
                // and the raw float bytes in `new_amount`

            } else {
                let _ = trace_num("Error adding FLOAT_ONE to IOU amount, result:", result as i64);
            }

            (Some(*amount), Some(*issuer), Some(*currency_code))
        }
        _ => {
            let _ = trace_data("AMOUNT Value (IOU):", &[0u8; 8], DataRepr::AsHex);
            (None, None, None)
        }
    };
    // trace new iou_amount as hex
    if let Some(amount) = iou_amount {
        let _ = trace_data("IOU Amount:", &amount.0, DataRepr::AsHex);
    } else {
        let _ = trace_data("IOU Amount:", &[0u8; 8], DataRepr::AsHex);
    }

    // TODO: replace with require
    if iou_amount.is_none() {
        let _ = trace("AMOUNT.IOU Parameter Error: Invalid Type");
        return -1;
    }

    // let mut buf = [0x00; 12];
    // let output_len = unsafe { instance_param(12, STI_NUMBER.into(), buf.as_mut_ptr(), buf.len()) };
    // let _ = trace_num("NUMBER Value Len:", output_len as i64);

    // NUMBER
    let number = match get_instance_param::<Number>(13) {
        Ok(a) => a,
        Err(err) => {
            let _ = trace_num("NUMBER Parameter Error Code:", err as i64);
            return -1;
        }
    };
    // trace the value
    let buf = number.as_bytes();
    let _ = trace_data("NUMBER Value:", &buf, DataRepr::AsHex);

    // TODO: replace with require
    let expectedNumber: [u8; 12] = [
        0x00, 0x04, 0x43, 0x64, 0xC5, 0xBB, 0x00, 0x00,
        0xFF, 0xFF, 0xFF, 0xF1
    ];
    if buf != expectedNumber {
        let _ = trace("NUMBER Parameter Error: Invalid Value");
        return -1;
    }

    // // Parse Number to get mantissa and exponent
    // let stnumber = Number::from(&buf).unwrap();
    let _ = trace_num("NUMBER Mantissa:", number.mantissa);
    let _ = trace_num("NUMBER Exponent:", number.exponent as i64);

    let mut opaque_float_buf = [0x00; 8];
    let result = unsafe {
        float_set(
            number.exponent,
            number.mantissa,
            opaque_float_buf.as_mut_ptr(),
            8,
            FLOAT_ROUNDING_MODES_TO_NEAREST
        )
    };

    let opaque = OpaqueFloat::from(opaque_float_buf);
    let _ = trace_float("NUMBER as OpaqueFloat:", &opaque.0);
    let _ = trace_data("NUMBER OpaqueFloat Hex:", &opaque_float_buf, DataRepr::AsHex);

    // AMOUNT (MPT)
    // ISSUE (XRP)
    // ISSUE (IOU)
    // ISSUE (MPT)
    // CURRENCY

    return 0; // Return success code
}
