#![allow(unused_imports)]
#![cfg_attr(target_arch = "wasm32", no_std)]

#[cfg(not(target_arch = "wasm32"))]
extern crate std;

use xrpl_wasm_stdlib::host::trace::{DataRepr, trace, trace_data, trace_num, trace_float};
use xrpl_wasm_stdlib::host::{instance_param};
use xrpl_wasm_stdlib::core::params::instance::{get_instance_param};
use xrpl_wasm_stdlib::core::type_codes::{
    STI_AMOUNT, STI_VL, STI_ACCOUNT, STI_OBJECT, STI_ARRAY, STI_CURRENCY, STI_NUMBER
};
use xrpl_wasm_stdlib::core::types::opaque_float::OpaqueFloat;
use xrpl_wasm_stdlib::core::types::number::Number;
use xrpl_wasm_stdlib::core::types::account_id::AccountID;
use xrpl_wasm_stdlib::core::types::amount::Amount;
use xrpl_wasm_stdlib::host::{FLOAT_ROUNDING_MODES_TO_NEAREST, float_add, float_set};
use xrpl_wasm_stdlib::core::types::opaque_float::{FLOAT_NEGATIVE_ONE, FLOAT_ONE};

#[unsafe(no_mangle)]
pub extern "C" fn instance_params_other() -> i32 {
    // // VL
    // let mut buf = [0x00; 4];
    // let output_len = unsafe { instance_param(0, STI_VL.into(), buf.as_mut_ptr(), buf.len()) };
    // let _ = trace_num("VL Value Len:", output_len as i64);
    // // as hex
    // let _ = trace_data("VL Hex:", &buf[0..4], DataRepr::AsHex);

    // ACCOUNT
    let account_id = match get_instance_param::<AccountID>(1) {
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
    let xrp_token = match get_instance_param::<Amount>(2) {
        Ok(a) => a,
        Err(err) => {
            let _ = trace_num("AMOUNT XRP Parameter Error Code:", err as i64);
            return -1;
        }
    };
    match xrp_token {
        Amount::XRP { num_drops } => {
            let _ = trace_num("AMOUNT Value (XRP):", num_drops);
        }
        _ => {
            let _ = trace_num("AMOUNT Value (XRP):", -1);
        }
    }
    let buf = match xrp_token {
        Amount::XRP { num_drops } => num_drops.to_le_bytes(),
        _ => [0u8; 8],
    };
    let _ = trace_data("AMOUNT Hex:", &buf, DataRepr::AsHex);

    // TODO: replace with require
    if let Amount::XRP { num_drops } = xrp_token {
        if num_drops != 1000000 {
            let _ = trace("AMOUNT.XRP Parameter Error: Invalid Value");
            return -1;
        }
    } else {
        let _ = trace("AMOUNT.XRP Parameter Error: Invalid Type");
        return -1;
    }

    // AMOUNT IOU
    let iou_token = match get_instance_param::<Amount>(3) {
        Ok(a) => a,
        Err(err) => {
            let _ = trace_num("AMOUNT IOU Parameter Error Code:", err as i64);
            return -1;
        }
    };
    let (iou_amount, iou_issuer, iou_currency) = match &iou_token {
        Amount::IOU { amount, issuer, currency } => {
            // trace amount hex
            let _ = trace_data("AMOUNT Value (IOU):", &amount.0, DataRepr::AsHex);
            let _ = trace_float("AMOUNT Value (IOU) - Original:", &amount.0);
            let _ = trace_data("IOU Issuer:", &issuer.0, DataRepr::AsHex);
            let _ = trace_data("IOU Currency:", &currency.0, DataRepr::AsHex);

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

                // Create a new Amount with the updated amount
                let updated_token = Amount::IOU {
                    amount: new_amount.into(),
                    issuer: *issuer,
                    currency: *currency,
                };

                // You now have the updated token amount in `updated_token`
                // and the raw float bytes in `new_amount`

            } else {
                let _ = trace_num("Error adding FLOAT_ONE to IOU amount, result:", result as i64);
            }

            (Some(*amount), Some(*issuer), Some(*currency))
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
    let number = match get_instance_param::<Number>(4) {
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
        0x10, 0xA7, 0x41, 0xA4, 0x62, 0x78, 0x00, 0x00,
        0xFF, 0xFF, 0xFF, 0xEE
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
