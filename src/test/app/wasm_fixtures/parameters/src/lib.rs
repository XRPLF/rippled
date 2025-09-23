
#![allow(unused_imports)]
#![cfg_attr(target_arch = "wasm32", no_std)]

#[cfg(not(target_arch = "wasm32"))]
extern crate std;

use xrpl_wasm_std::host::trace::{DataRepr, trace_data, trace_num, trace_float};
use xrpl_wasm_std::host::{instance_param, function_param};
use xrpl_wasm_std::core::type_codes::{
    STI_UINT8, STI_UINT16, STI_UINT32, STI_UINT64, STI_UINT128,
    STI_UINT160, STI_UINT192, STI_UINT256, STI_AMOUNT, STI_VL, STI_ACCOUNT,
    STI_OBJECT, STI_ARRAY, STI_CURRENCY, STI_NUMBER
};
use xrpl_wasm_std::core::types::amount::opaque_float::OpaqueFloat;
use xrpl_wasm_std::core::types::amount::st_number::STNumber;
use xrpl_wasm_std::core::types::account_id::AccountID;
use xrpl_wasm_std::core::types::amount::token_amount::TokenAmount;
use xrpl_wasm_std::host::{FLOAT_ROUNDING_MODES_TO_NEAREST, float_add, float_set};
use xrpl_wasm_std::core::types::amount::opaque_float::{FLOAT_NEGATIVE_ONE, FLOAT_ONE};

#[unsafe(no_mangle)]
pub extern "C" fn function_params() -> i32 {
    // UINT8
    let mut buf = [0x00; 1];
    let output_len = unsafe { function_param(0, STI_UINT8.into(), buf.as_mut_ptr(), buf.len()) };
    let _ = trace_num("UINT8 Value Len:", output_len as i64);
    let value = buf[0] as u8;
    // trace the value
    let _ = trace_num("UINT8 Value:", value as i64);
    // as hex
    let _ = trace_data("UINT8 Hex:", &buf[0..1], DataRepr::AsHex);

    // // UINT16
    let mut buf = [0x00; 2];
    let output_len = unsafe { function_param(1, STI_UINT16.into(), buf.as_mut_ptr(), buf.len()) };
    let _ = trace_num("UINT16 Value Len:", output_len as i64);
    let value = u16::from_le_bytes(buf);
    // trace the value
    let _ = trace_num("UINT16 Value:", value as i64);
    // as hex
    let _ = trace_data("UINT16 Hex:", &buf[0..2], DataRepr::AsHex);

    // // UINT32
    let mut buf = [0x00; 4];
    let output_len = unsafe { function_param(2, STI_UINT32.into(), buf.as_mut_ptr(), buf.len()) };
    let _ = trace_num("UINT32 Value Len:", output_len as i64);
    let value = u32::from_le_bytes(buf);
    // trace the value
    let _ = trace_num("UINT32 Value:", value as i64);
    // as hex
    let _ = trace_data("UINT32 Hex:", &buf[0..4], DataRepr::AsHex);

    // UINT64
    let mut buf = [0x00; 8];
    let output_len = unsafe { function_param(3, STI_UINT64.into(), buf.as_mut_ptr(), buf.len()) };
    let _ = trace_num("UINT64 Value Len:", output_len as i64);
    let value = i64::from_le_bytes(buf);
    // trace the value
    let _ = trace_num("UINT64 Value:", value as i64);
    // as hex
    let _ = trace_data("UINT64 Hex:", &buf[0..8], DataRepr::AsHex);

    // // UINT128
    let mut buf = [0x00; 16];
    let output_len = unsafe { function_param(4, STI_UINT128.into(), buf.as_mut_ptr(), buf.len()) };
    let _ = trace_num("UINT128 Value Len:", output_len as i64);
    let value = u128::from_le_bytes(buf);
    // trace the value
    let _ = trace_num("UINT128 Value:", value as i64);
    // as hex
    let _ = trace_data("UINT128 Hex:", &buf[0..16], DataRepr::AsHex);

    // UINT160
    let mut buf = [0x00; 20];
    let output_len = unsafe { function_param(5, STI_UINT160.into(), buf.as_mut_ptr(), buf.len()) };
    let _ = trace_num("UINT160 Value Len:", output_len as i64);
    let value = u128::from_le_bytes(buf[0..16].try_into().unwrap());
    // trace the value
    let _ = trace_num("UINT160 Value:", value as i64);
    // as hex
    let _ = trace_data("UINT160 Hex:", &buf[0..20], DataRepr::AsHex);

    // UINT192
    let mut buf = [0x00; 24];
    let output_len = unsafe { function_param(6, STI_UINT192.into(), buf.as_mut_ptr(), buf.len()) };
    let _ = trace_num("UINT192 Value Len:", output_len as i64);
    let value = u128::from_le_bytes(buf[0..16].try_into().unwrap());
    // trace the value
    let _ = trace_num("UINT192 Value:", value as i64);
    // as hex
    let _ = trace_data("UINT192 Hex:", &buf[0..24], DataRepr::AsHex);

    // UINT256
    let mut buf = [0x00; 32];
    let output_len = unsafe { function_param(7, STI_UINT256.into(), buf.as_mut_ptr(), buf.len()) };
    let _ = trace_num("UINT256 Value Len:", output_len as i64);
    // as hex
    let _ = trace_data("UINT256 Hex:", &buf[0..32], DataRepr::AsHex);

    // VL
    let mut buf = [0x00; 4];
    let output_len = unsafe { function_param(8, STI_VL.into(), buf.as_mut_ptr(), buf.len()) };
    let _ = trace_num("VL Value Len:", output_len as i64);
    // as hex
    let _ = trace_data("VL Hex:", &buf[0..4], DataRepr::AsHex);

    // ACCOUNT
    let mut buf = [0x00; 20];
    let output_len = unsafe { function_param(9, STI_ACCOUNT.into(), buf.as_mut_ptr(), buf.len()) };
    let _ = trace_num("ACCOUNT Value Len:", output_len as i64);
    // Convert to AccountID
    let account_id = AccountID::from(buf);
    // trace the value
    let _ = trace_data("ACCOUNT Value:", &account_id.0, DataRepr::AsHex);

    // AMOUNT XRP
    let mut buf = [0x00; 8];
    let output_len = unsafe { function_param(10, STI_AMOUNT.into(), buf.as_mut_ptr(), buf.len()) };
    let _ = trace_num("AMOUNT Value Len:", output_len as i64);

    let xrp_token = TokenAmount::from_bytes(&buf);
    match xrp_token {
        Ok(TokenAmount::XRP { num_drops }) => {
            let _ = trace_num("AMOUNT Value (XRP):", num_drops);
        }
        _ => {
            let _ = trace_num("AMOUNT Value (XRP):", -1);
        }
    }
    let _ = trace_data("AMOUNT Hex:", &buf[0..output_len as usize], DataRepr::AsHex);

    // AMOUNT IOU
    let mut buf = [0x00; 48];
    let output_len = unsafe { function_param(11, STI_AMOUNT.into(), buf.as_mut_ptr(), buf.len()) };
    let _ = trace_num("AMOUNT Value Len:", output_len as i64);

    let iou_token = TokenAmount::from_bytes(&buf[0..output_len as usize]);
    let (iou_amount, iou_issuer, iou_currency) = match &iou_token {
        Ok(TokenAmount::IOU { amount, issuer, currency_code }) => {
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
    let _ = trace_data("AMOUNT Hex:", &buf[0..output_len as usize], DataRepr::AsHex);
    // trace new iou_amount as hex
    if let Some(amount) = iou_amount {
        let _ = trace_data("IOU Amount:", &amount.0, DataRepr::AsHex);
    } else {
        let _ = trace_data("IOU Amount:", &[0u8; 8], DataRepr::AsHex);
    }

    // NUMBER
    let mut buf = [0x00; 12];
    let output_len = unsafe { function_param(12, STI_NUMBER.into(), buf.as_mut_ptr(), buf.len()) };
    let _ = trace_num("NUMBER Value Len:", output_len as i64);

    // Parse STNumber to get mantissa and exponent
    let stnumber = STNumber::from_bytes(&buf).unwrap();
    let _ = trace_num("NUMBER Mantissa:", stnumber.mantissa);
    let _ = trace_num("NUMBER Exponent:", stnumber.exponent as i64);

    // Use float_set to create the proper OpaqueFloat
    // This function handles the mantissa × 10^exponent conversion internally
    let mut opaque_float_buf = [0x00; 8];
    let result = unsafe {
        float_set(
            stnumber.exponent,
            stnumber.mantissa,
            opaque_float_buf.as_mut_ptr(),
            8,
            FLOAT_ROUNDING_MODES_TO_NEAREST
        )
    };

    let opaque = OpaqueFloat::from(opaque_float_buf);
    let _ = trace_float("NUMBER as OpaqueFloat:", &opaque.0);
    let _ = trace_data("NUMBER OpaqueFloat Hex:", &opaque_float_buf, DataRepr::AsHex);

    // ISSUE (XRP)
    // ISSUE (IOU)
    // ISSUE (MPT)
    // CURRENCY

    return 0; // Return success code
}

#[unsafe(no_mangle)]
pub extern "C" fn instance_params() -> i32 {
    // UINT8
    let mut buf = [0x00; 1];
    let output_len = unsafe { instance_param(1, STI_UINT8.into(), buf.as_mut_ptr(), buf.len()) };
    let _ = trace_num("UINT8 Value Len:", output_len as i64);
    let value = buf[0] as u8;
    // trace the value
    let _ = trace_num("UINT8 Value:", value as i64);
    // as hex
    let _ = trace_data("UINT8 Hex:", &buf[0..1], DataRepr::AsHex);

    // // UINT16
    let mut buf = [0x00; 2];
    let output_len = unsafe { instance_param(2, STI_UINT16.into(), buf.as_mut_ptr(), buf.len()) };
    let _ = trace_num("UINT16 Value Len:", output_len as i64);
    let value = u16::from_le_bytes(buf);
    // trace the value
    let _ = trace_num("UINT16 Value:", value as i64);
    // as hex
    let _ = trace_data("UINT16 Hex:", &buf[0..2], DataRepr::AsHex);

    // // UINT32
    let mut buf = [0x00; 4];
    let output_len = unsafe { instance_param(3, STI_UINT32.into(), buf.as_mut_ptr(), buf.len()) };
    let _ = trace_num("UINT32 Value Len:", output_len as i64);
    let value = u32::from_le_bytes(buf);
    // trace the value
    let _ = trace_num("UINT32 Value:", value as i64);
    // as hex
    let _ = trace_data("UINT32 Hex:", &buf[0..4], DataRepr::AsHex);

    // UINT64
    let mut buf = [0x00; 8];
    let output_len = unsafe { instance_param(4, STI_UINT64.into(), buf.as_mut_ptr(), buf.len()) };
    let _ = trace_num("UINT64 Value Len:", output_len as i64);
    let value = i64::from_le_bytes(buf);
    // trace the value
    let _ = trace_num("UINT64 Value:", value as i64);
    // as hex
    let _ = trace_data("UINT64 Hex:", &buf[0..8], DataRepr::AsHex);

    // // UINT128
    let mut buf = [0x00; 16];
    let output_len = unsafe { instance_param(5, STI_UINT128.into(), buf.as_mut_ptr(), buf.len()) };
    let _ = trace_num("UINT128 Value Len:", output_len as i64);
    let value = u128::from_le_bytes(buf);
    // trace the value
    let _ = trace_num("UINT128 Value:", value as i64);
    // as hex
    let _ = trace_data("UINT128 Hex:", &buf[0..16], DataRepr::AsHex);

    // UINT160
    let mut buf = [0x00; 20];
    let output_len = unsafe { instance_param(6, STI_UINT160.into(), buf.as_mut_ptr(), buf.len()) };
    let _ = trace_num("UINT160 Value Len:", output_len as i64);
    let value = u128::from_le_bytes(buf[0..16].try_into().unwrap());
    // trace the value
    let _ = trace_num("UINT160 Value:", value as i64);
    // as hex
    let _ = trace_data("UINT160 Hex:", &buf[0..20], DataRepr::AsHex);

    // UINT192
    let mut buf = [0x00; 24];
    let output_len = unsafe { instance_param(7, STI_UINT192.into(), buf.as_mut_ptr(), buf.len()) };
    let _ = trace_num("UINT192 Value Len:", output_len as i64);
    let value = u128::from_le_bytes(buf[0..16].try_into().unwrap());
    // trace the value
    let _ = trace_num("UINT192 Value:", value as i64);
    // as hex
    let _ = trace_data("UINT192 Hex:", &buf[0..24], DataRepr::AsHex);

    // UINT256
    let mut buf = [0x00; 32];
    let output_len = unsafe { instance_param(8, STI_UINT256.into(), buf.as_mut_ptr(), buf.len()) };
    let _ = trace_num("UINT256 Value Len:", output_len as i64);
    // as hex
    let _ = trace_data("UINT256 Hex:", &buf[0..32], DataRepr::AsHex);

    // VL
    let mut buf = [0x00; 4];
    let output_len = unsafe { instance_param(9, STI_VL.into(), buf.as_mut_ptr(), buf.len()) };
    let _ = trace_num("VL Value Len:", output_len as i64);
    // as hex
    let _ = trace_data("VL Hex:", &buf[0..4], DataRepr::AsHex);

    // ACCOUNT
    let mut buf = [0x00; 20];
    let output_len = unsafe { instance_param(10, STI_ACCOUNT.into(), buf.as_mut_ptr(), buf.len()) };
    let _ = trace_num("ACCOUNT Value Len:", output_len as i64);
    // Convert to AccountID
    let account_id = AccountID::from(buf);
    // trace the value
    let _ = trace_data("ACCOUNT Value:", &account_id.0, DataRepr::AsHex);

    // AMOUNT XRP
    let mut buf = [0x00; 8];
    let output_len = unsafe { instance_param(11, STI_AMOUNT.into(), buf.as_mut_ptr(), buf.len()) };
    let _ = trace_num("AMOUNT Value Len:", output_len as i64);

    let xrp_token = TokenAmount::from_bytes(&buf);
    match xrp_token {
        Ok(TokenAmount::XRP { num_drops }) => {
            let _ = trace_num("AMOUNT Value (XRP):", num_drops);
        }
        _ => {
            let _ = trace_num("AMOUNT Value (XRP):", -1);
        }
    }
    let _ = trace_data("AMOUNT Hex:", &buf[0..output_len as usize], DataRepr::AsHex);

    // AMOUNT IOU
    let mut buf = [0x00; 48];
    let output_len = unsafe { instance_param(12, STI_AMOUNT.into(), buf.as_mut_ptr(), buf.len()) };
    let _ = trace_num("AMOUNT Value Len:", output_len as i64);

    let iou_token = TokenAmount::from_bytes(&buf[0..output_len as usize]);
    let (iou_amount, iou_issuer, iou_currency) = match &iou_token {
        Ok(TokenAmount::IOU { amount, issuer, currency_code }) => {
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
    let _ = trace_data("AMOUNT Hex:", &buf[0..output_len as usize], DataRepr::AsHex);
    // trace new iou_amount as hex
    if let Some(amount) = iou_amount {
        let _ = trace_data("IOU Amount:", &amount.0, DataRepr::AsHex);
    } else {
        let _ = trace_data("IOU Amount:", &[0u8; 8], DataRepr::AsHex);
    }

    // NUMBER
    let mut buf = [0x00; 12];
    let output_len = unsafe { function_param(13, STI_NUMBER.into(), buf.as_mut_ptr(), buf.len()) };
    let _ = trace_num("NUMBER Value Len:", output_len as i64);

    // Parse STNumber to get mantissa and exponent
    let stnumber = STNumber::from_bytes(&buf).unwrap();
    let _ = trace_num("NUMBER Mantissa:", stnumber.mantissa);
    let _ = trace_num("NUMBER Exponent:", stnumber.exponent as i64);

    // Use float_set to create the proper OpaqueFloat
    // This function handles the mantissa × 10^exponent conversion internally
    let mut opaque_float_buf = [0x00; 8];
    let result = unsafe {
        float_set(
            stnumber.exponent,
            stnumber.mantissa,
            opaque_float_buf.as_mut_ptr(),
            8,
            FLOAT_ROUNDING_MODES_TO_NEAREST
        )
    };

    let opaque = OpaqueFloat::from(opaque_float_buf);
    let _ = trace_float("NUMBER as OpaqueFloat:", &opaque.0);
    let _ = trace_data("NUMBER OpaqueFloat Hex:", &opaque_float_buf, DataRepr::AsHex);

    // ISSUE (XRP)
    // ISSUE (IOU)
    // ISSUE (MPT)
    // CURRENCY

    return 0; // Return success code
}
