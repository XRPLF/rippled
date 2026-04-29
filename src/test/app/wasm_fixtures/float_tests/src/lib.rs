#![allow(unused_imports)]
#![allow(unused_variables)]
#![cfg_attr(target_arch = "wasm32", no_std)]

#[cfg(not(target_arch = "wasm32"))]
extern crate std;

use xrpl_std::core::locator::Locator;
use xrpl_std::decode_hex_32;
use xrpl_std::host::trace::DataRepr::AsHex;
use xrpl_std::host::trace::{trace, trace_data, trace_num, DataRepr};
use xrpl_std::host::{
    cache_ledger_obj, float_add, float_compare, float_divide, float_from_int, float_from_uint,
    float_multiply, float_pow, float_root, float_subtract,
    get_ledger_obj_array_len, get_ledger_obj_field, get_ledger_obj_nested_field,
    FLOAT_ROUNDING_MODES_TO_NEAREST,
};
use xrpl_std::sfield;
use xrpl_std::sfield::{
    Account, AccountTxnID, Balance, Domain, EmailHash, Flags, LedgerEntryType, MessageKey,
    OwnerCount, PreviousTxnID, PreviousTxnLgrSeq, RegularKey, Sequence, TicketCount, TransferRate,
};

// External host functions not yet in xrpl_std
unsafe extern "C" {
    #[link_name = "float_from_stamount"]
    fn float_from_stamount(
        amount_ptr: *const u8,
        amount_len: i32,
        out_ptr: *mut u8,
        out_len: i32,
        rounding: i32,
    ) -> i32;

    #[link_name = "float_from_stnumber"]
    fn float_from_stnumber(
        number_ptr: *const u8,
        number_len: i32,
        out_ptr: *mut u8,
        out_len: i32,
        rounding: i32,
    ) -> i32;

    #[link_name = "float_to_int"]
    fn float_to_int(
        float_ptr: *const u8,
        float_len: i32,
        out_ptr: *mut u8,
        out_len: i32,
        rounding: i32,
    ) -> i32;

    #[link_name = "float_to_mantissa_and_exponent"]
    fn float_to_mantissa_and_exponent(
        float_ptr: *const u8,
        float_len: i32,
        mantissa_ptr: *mut u8,
        mantissa_len: i32,
        exponent_ptr: *mut u8,
        exponent_len: i32,
    ) -> i32;

   #[link_name = "float_from_mantissa_and_exponent"]
    fn float_from_mantissa_and_exponent(
        exponent: i32,
        mantissa: i64,
        out_ptr: *mut u8,
        out_len: i32,
        rounding: i32,
    ) -> i32;
}

// Float size constant (8 bytes mantissa + 4 bytes exponent)
const FLOAT_SIZE: usize = 12;

// Float constants (8 bytes mantissa + 4 bytes exponent, big-endian)
// FLOAT_ONE: mantissa=0x0DE0B6B3A7640000 (10^18), exponent=0xFFFFFFEE (-18)
const FLOAT_ONE: [u8; FLOAT_SIZE] = [0x0D, 0xE0, 0xB6, 0xB3, 0xA7, 0x64, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xEE];
// FLOAT_NEGATIVE_ONE: mantissa=0xF21F494C589C0000 (-10^18), exponent=0xFFFFFFEE (-18)
const FLOAT_NEGATIVE_ONE: [u8; FLOAT_SIZE] = [0xF2, 0x1F, 0x49, 0x4C, 0x58, 0x9C, 0x00, 0x00, 0xFF, 0xFF, 0xFF, 0xEE];

// Helper function to trace floats
fn trace_float(msg: &str, f: &[u8; FLOAT_SIZE]) {
    let _ = trace(msg);
    let _ = trace_data("  ", f, AsHex);
}

fn test_float_from_wasm() -> bool {
    let _ = trace("\n$$$ test_float_from_wasm $$$");
    let mut all_pass = true;

    let mut f: [u8; FLOAT_SIZE] = [0u8; FLOAT_SIZE];
    if FLOAT_SIZE as i32 == unsafe { float_from_int(12300, f.as_mut_ptr(), FLOAT_SIZE, FLOAT_ROUNDING_MODES_TO_NEAREST) } {
        let _ = trace_float("  float from i64 12300:", &f);
        let _ = trace_data("  float from i64 12300 as HEX:", &f, AsHex);
    } else {
        let _ = trace("  float from i64 12300: failed");
        all_pass = false;
    }

    let u64_value: u64 = 12300;
    if FLOAT_SIZE as i32 == unsafe {
        float_from_uint(
            &u64_value as *const u64 as *const u8,
            8,
            f.as_mut_ptr(),
            FLOAT_SIZE,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    } {
        let _ = trace_float("  float from u64 12300:", &f);
    } else {
        let _ = trace("  float from u64 12300: failed");
        all_pass = false;
    }

    if FLOAT_SIZE as i32 == unsafe { float_from_mantissa_and_exponent(2, 123, f.as_mut_ptr(), FLOAT_SIZE as i32, FLOAT_ROUNDING_MODES_TO_NEAREST) } {
        let _ = trace_float("  float from exp 2, mantissa 123:", &f);
    } else {
        let _ = trace("  float from exp 2, mantissa 3: failed");
        all_pass = false;
    }

    let _ = trace_float("  float from const 1:", &FLOAT_ONE);
    let _ = trace_float("  float from const -1:", &FLOAT_NEGATIVE_ONE);

    all_pass
}

fn test_float_compare() -> bool {
    let _ = trace("\n$$$ test_float_compare $$$");
    let mut all_pass = true;

    let mut f1: [u8; FLOAT_SIZE] = [0u8; FLOAT_SIZE];
    if FLOAT_SIZE as i32 != unsafe { float_from_int(1, f1.as_mut_ptr(), FLOAT_SIZE, FLOAT_ROUNDING_MODES_TO_NEAREST) } {
        let _ = trace("  float from 1: failed");
        all_pass = false;
    } else {
        let _ = trace_float("  float from 1:", &f1);
    }

    if 0 == unsafe { float_compare(f1.as_ptr(), FLOAT_SIZE, FLOAT_ONE.as_ptr(), FLOAT_SIZE) } {
        let _ = trace("  float from 1 == FLOAT_ONE");
    } else {
        let _ = trace("  float from 1 != FLOAT_ONE, failed");
        all_pass = false;
    }

    if 1 == unsafe { float_compare(f1.as_ptr(), FLOAT_SIZE, FLOAT_NEGATIVE_ONE.as_ptr(), FLOAT_SIZE) } {
        let _ = trace("  float from 1 > FLOAT_NEGATIVE_ONE");
    } else {
        let _ = trace("  float from 1 !> FLOAT_NEGATIVE_ONE, failed");
        all_pass = false;
    }

    if 2 == unsafe { float_compare(FLOAT_NEGATIVE_ONE.as_ptr(), FLOAT_SIZE, f1.as_ptr(), FLOAT_SIZE) } {
        let _ = trace("  FLOAT_NEGATIVE_ONE < float from 1");
    } else {
        let _ = trace("  FLOAT_NEGATIVE_ONE !< float from 1, failed");
        all_pass = false;
    }

    all_pass
}

fn test_float_add_subtract() -> bool {
    let _ = trace("\n$$$ test_float_add_subtract $$$");
    let mut all_pass = true;

    let mut f_compute: [u8; FLOAT_SIZE] = FLOAT_ONE;
    for i in 0..9 {
        unsafe {
            float_add(
                f_compute.as_ptr(),
                FLOAT_SIZE,
                FLOAT_ONE.as_ptr(),
                FLOAT_SIZE,
                f_compute.as_mut_ptr(),
                FLOAT_SIZE,
                FLOAT_ROUNDING_MODES_TO_NEAREST,
            )
        };
        // let _ = trace_float("  float:", &f_compute);
    }
    let mut f10: [u8; FLOAT_SIZE] = [0u8; FLOAT_SIZE];
    if FLOAT_SIZE as i32 != unsafe { float_from_int(10, f10.as_mut_ptr(), FLOAT_SIZE, FLOAT_ROUNDING_MODES_TO_NEAREST) } {
        let _ = trace("  float from 10: failed");
        all_pass = false;
    }

    if 0 == unsafe { float_compare(f10.as_ptr(), FLOAT_SIZE, f_compute.as_ptr(), FLOAT_SIZE) } {
        let _ = trace("  repeated add: good");
    } else {
        let _ = trace("  repeated add: failed");
        all_pass = false;
    }

    for i in 0..11 {
        unsafe {
            float_subtract(
                f_compute.as_ptr(),
                FLOAT_SIZE,
                FLOAT_ONE.as_ptr(),
                FLOAT_SIZE,
                f_compute.as_mut_ptr(),
                FLOAT_SIZE,
                FLOAT_ROUNDING_MODES_TO_NEAREST,
            )
        };
    }
    if 0 == unsafe { float_compare(f_compute.as_ptr(), FLOAT_SIZE, FLOAT_NEGATIVE_ONE.as_ptr(), FLOAT_SIZE) } {
        let _ = trace("  repeated subtract: good");
    } else {
        let _ = trace("  repeated subtract: failed");
        all_pass = false;
    }

    all_pass
}

fn test_float_multiply_divide() -> bool {
    let _ = trace("\n$$$ test_float_multiply_divide $$$");
    let mut all_pass = true;

    let mut f10: [u8; FLOAT_SIZE] = [0u8; FLOAT_SIZE];
    unsafe { float_from_int(10, f10.as_mut_ptr(), FLOAT_SIZE, FLOAT_ROUNDING_MODES_TO_NEAREST) };
    let mut f_compute: [u8; FLOAT_SIZE] = FLOAT_ONE;
    for i in 0..6 {
        unsafe {
            float_multiply(
                f_compute.as_ptr(),
                FLOAT_SIZE,
                f10.as_ptr(),
                FLOAT_SIZE,
                f_compute.as_mut_ptr(),
                FLOAT_SIZE,
                FLOAT_ROUNDING_MODES_TO_NEAREST,
            )
        };
        // let _ = trace_float("  float:", &f_compute);
    }
    let mut f1000000: [u8; FLOAT_SIZE] = [0u8; FLOAT_SIZE];
    unsafe {
        float_from_int(
            1000000,
            f1000000.as_mut_ptr(),
            FLOAT_SIZE,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    };

    if 0 == unsafe { float_compare(f1000000.as_ptr(), FLOAT_SIZE, f_compute.as_ptr(), FLOAT_SIZE) } {
        let _ = trace("  repeated multiply: good");
    } else {
        let _ = trace("  repeated multiply: failed");
        all_pass = false;
    }

    for i in 0..7 {
        unsafe {
            float_divide(
                f_compute.as_ptr(),
                FLOAT_SIZE,
                f10.as_ptr(),
                FLOAT_SIZE,
                f_compute.as_mut_ptr(),
                FLOAT_SIZE,
                FLOAT_ROUNDING_MODES_TO_NEAREST,
            )
        };
    }
    let mut f01: [u8; FLOAT_SIZE] = [0u8; FLOAT_SIZE];
    unsafe { float_from_mantissa_and_exponent(-1, 1, f01.as_mut_ptr(), FLOAT_SIZE as i32, FLOAT_ROUNDING_MODES_TO_NEAREST) };

    if 0 == unsafe { float_compare(f_compute.as_ptr(), FLOAT_SIZE, f01.as_ptr(), FLOAT_SIZE) } {
        let _ = trace("  repeated divide: good");
    } else {
        let _ = trace("  repeated divide: failed");
        all_pass = false;
    }

    all_pass
}

fn test_float_pow() -> bool {
    let _ = trace("\n$$$ test_float_pow $$$");
    let mut all_pass = true;

    let mut f_compute: [u8; FLOAT_SIZE] = [0u8; FLOAT_SIZE];
    unsafe {
        float_pow(
            FLOAT_ONE.as_ptr(),
            FLOAT_SIZE,
            3,
            f_compute.as_mut_ptr(),
            FLOAT_SIZE,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    };
    let _ = trace_float("  float cube of 1:", &f_compute);

    unsafe {
        float_pow(
            FLOAT_NEGATIVE_ONE.as_ptr(),
            FLOAT_SIZE,
            6,
            f_compute.as_mut_ptr(),
            FLOAT_SIZE,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    };
    let _ = trace_float("  float 6th power of -1:", &f_compute);

    let mut f9: [u8; FLOAT_SIZE] = [0u8; FLOAT_SIZE];
    unsafe { float_from_int(9, f9.as_mut_ptr(), FLOAT_SIZE, FLOAT_ROUNDING_MODES_TO_NEAREST) };
    unsafe {
        float_pow(
            f9.as_ptr(),
            FLOAT_SIZE,
            2,
            f_compute.as_mut_ptr(),
            FLOAT_SIZE,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    };
    let _ = trace_float("  float square of 9:", &f_compute);

    unsafe {
        float_pow(
            f9.as_ptr(),
            FLOAT_SIZE,
            0,
            f_compute.as_mut_ptr(),
            FLOAT_SIZE,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    };
    let _ = trace_float("  float 0th power of 9:", &f_compute);

    let mut f0: [u8; FLOAT_SIZE] = [0u8; FLOAT_SIZE];
    unsafe { float_from_int(0, f0.as_mut_ptr(), FLOAT_SIZE, FLOAT_ROUNDING_MODES_TO_NEAREST) };
    unsafe {
        float_pow(
            f0.as_ptr(),
            FLOAT_SIZE,
            2,
            f_compute.as_mut_ptr(),
            FLOAT_SIZE,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    };
    let _ = trace_float("  float square of 0:", &f_compute);

    let r = unsafe {
        float_pow(
            f0.as_ptr(),
            FLOAT_SIZE,
            0,
            f_compute.as_mut_ptr(),
            FLOAT_SIZE,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    };
    let _ = trace_num(
        "  float 0th power of 0 (expecting INVALID_PARAMS error):",
        r as i64,
    );

    all_pass
}

fn test_float_root() -> bool {
    let _ = trace("\n$$$ test_float_root $$$");
    let mut all_pass = true;

    let mut f9: [u8; FLOAT_SIZE] = [0u8; FLOAT_SIZE];
    unsafe { float_from_int(9, f9.as_mut_ptr(), FLOAT_SIZE, FLOAT_ROUNDING_MODES_TO_NEAREST) };
    let mut f_compute: [u8; FLOAT_SIZE] = [0u8; FLOAT_SIZE];
    unsafe {
        float_root(
            f9.as_ptr(),
            FLOAT_SIZE,
            2,
            f_compute.as_mut_ptr(),
            FLOAT_SIZE,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    };
    let _ = trace_float("  float sqrt of 9:", &f_compute);
    unsafe {
        float_root(
            f9.as_ptr(),
            FLOAT_SIZE,
            3,
            f_compute.as_mut_ptr(),
            FLOAT_SIZE,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    };
    let _ = trace_float("  float cbrt of 9:", &f_compute);

    let mut f1000000: [u8; FLOAT_SIZE] = [0u8; FLOAT_SIZE];
    unsafe {
        float_from_int(
            1000000,
            f1000000.as_mut_ptr(),
            FLOAT_SIZE,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    };
    unsafe {
        float_root(
            f1000000.as_ptr(),
            FLOAT_SIZE,
            3,
            f_compute.as_mut_ptr(),
            FLOAT_SIZE,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    };
    let _ = trace_float("  float cbrt of 1000000:", &f_compute);
    unsafe {
        float_root(
            f1000000.as_ptr(),
            FLOAT_SIZE,
            6,
            f_compute.as_mut_ptr(),
            FLOAT_SIZE,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    };
    let _ = trace_float("  float 6th root of 1000000:", &f_compute);

    all_pass
}

fn test_float_invert() -> bool {
    let _ = trace("\n$$$ test_float_invert $$$");
    let mut all_pass = true;

    let mut f_compute: [u8; FLOAT_SIZE] = [0u8; FLOAT_SIZE];
    let mut f10: [u8; FLOAT_SIZE] = [0u8; FLOAT_SIZE];
    unsafe { float_from_int(10, f10.as_mut_ptr(), FLOAT_SIZE, FLOAT_ROUNDING_MODES_TO_NEAREST) };
    unsafe {
        float_divide(
            FLOAT_ONE.as_ptr(),
            FLOAT_SIZE,
            f10.as_ptr(),
            FLOAT_SIZE,
            f_compute.as_mut_ptr(),
            FLOAT_SIZE,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    };
    let _ = trace_float("  invert a float from 10:", &f_compute);
    unsafe {
        float_divide(
            FLOAT_ONE.as_ptr(),
            FLOAT_SIZE,
            f_compute.as_ptr(),
            FLOAT_SIZE,
            f_compute.as_mut_ptr(),
            FLOAT_SIZE,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    };
    let _ = trace_float("  invert again:", &f_compute);

    // if f10's value is 7, then invert twice won't match the original value
    if 0 == unsafe { float_compare(f10.as_ptr(), FLOAT_SIZE, f_compute.as_ptr(), FLOAT_SIZE) } {
        let _ = trace("  invert twice: good");
    } else {
        let _ = trace("  invert twice: failed");
        all_pass = false;
    }

    all_pass
}

fn test_float_to_int() -> bool {
    let _ = trace("\n$$$ test_float_to_int $$$");
    let mut all_pass = true;
    let mut result: [u8; 8] = [0u8; 8];

    // Test converting FLOAT_ONE (value 1) to int
    let ret = unsafe {
        float_to_int(
            FLOAT_ONE.as_ptr(),
            FLOAT_SIZE as i32,
            result.as_mut_ptr(),
            8,
            FLOAT_ROUNDING_MODES_TO_NEAREST
        )
    };
    if ret == 8 {
        let number = i64::from_le_bytes(result);
        if number == 1 {
            let _ = trace("  float_to_int(1): good");
        } else {
            let _ = trace("  float_to_int(1): failed");
            let _ = trace_num("    got:", number);
            all_pass = false;
        }
    } else {
        let _ = trace("  float_to_int(1): failed with error");
        let _ = trace_num("    error code:", ret as i64);
        all_pass = false;
    }

    // Test converting FLOAT_NEGATIVE_ONE (value -1) to int
    let ret = unsafe {
        float_to_int(
            FLOAT_NEGATIVE_ONE.as_ptr(),
            FLOAT_SIZE as i32,
            result.as_mut_ptr(),
            8,
            FLOAT_ROUNDING_MODES_TO_NEAREST
        )
    };
    if ret == 8 {
        let number = i64::from_le_bytes(result);
        if number == -1 {
            let _ = trace("  float_to_int(-1): good");
        } else {
            let _ = trace("  float_to_int(-1): failed");
            let _ = trace_num("    got:", number);
            all_pass = false;
        }
    } else {
        let _ = trace("  float_to_int(-1): failed with error");
        let _ = trace_num("    error code:", ret as i64);
        all_pass = false;
    }

    // Test converting a larger number (i64::MAX)
    let test_val: i64 = i64::MAX;
    let mut f_max: [u8; FLOAT_SIZE] = [0u8; FLOAT_SIZE];
    unsafe { float_from_int(test_val, f_max.as_mut_ptr(), FLOAT_SIZE, FLOAT_ROUNDING_MODES_TO_NEAREST) };
    let ret = unsafe {
        float_to_int(
            f_max.as_ptr(),
            FLOAT_SIZE as i32,
            result.as_mut_ptr(),
            8,
            FLOAT_ROUNDING_MODES_TO_NEAREST
        )
    };
    if ret == 8 {
        let number = i64::from_le_bytes(result);
        if number == test_val {
            let _ = trace("  float_to_int(i64::MAX): good");
        } else {
            let _ = trace("  float_to_int(i64::MAX): failed");
            let _ = trace_num("    expected:", test_val);
            let _ = trace_num("    got:", number);
            all_pass = false;
        }
    } else {
        let _ = trace("  float_to_int(i64::MAX): failed with error");
        let _ = trace_num("    error code:", ret as i64);
        all_pass = false;
    }

    // Test converting zero
    let mut f0: [u8; FLOAT_SIZE] = [0u8; FLOAT_SIZE];
    unsafe { float_from_int(0, f0.as_mut_ptr(), FLOAT_SIZE, FLOAT_ROUNDING_MODES_TO_NEAREST) };
    let ret = unsafe {
        float_to_int(
            f0.as_ptr(),
            FLOAT_SIZE as i32,
            result.as_mut_ptr(),
            8,
            FLOAT_ROUNDING_MODES_TO_NEAREST
        )
    };
    if ret == 8 {
        let number = i64::from_le_bytes(result);
        if number == 0 {
            let _ = trace("  float_to_int(0): good");
        } else {
            let _ = trace("  float_to_int(0): failed");
            let _ = trace_num("    got:", number);
            all_pass = false;
        }
    } else {
        let _ = trace("  float_to_int(0): failed with error");
        let _ = trace_num("    error code:", ret as i64);
        all_pass = false;
    }

    // Test rounding with fractional value (0.1)
    let mut f01: [u8; FLOAT_SIZE] = [0u8; FLOAT_SIZE];
    unsafe { float_from_mantissa_and_exponent(-1, 1, f01.as_mut_ptr(), FLOAT_SIZE as i32, FLOAT_ROUNDING_MODES_TO_NEAREST) };
    let ret = unsafe {
        float_to_int(
            f01.as_ptr(),
            FLOAT_SIZE as i32,
            result.as_mut_ptr(),
            8 as i32,
            FLOAT_ROUNDING_MODES_TO_NEAREST
        )
    };
    if ret == 8 as i32 {
        let number = i64::from_le_bytes(result);
        if number == 0 {
            let _ = trace("  float_to_int(0.1, to_nearest): good");
        } else {
            let _ = trace("  float_to_int(0.1, to_nearest): failed");
            let _ = trace_num("    got:", number);
            all_pass = false;
        }
    } else {
        let _ = trace("  float_to_int(0.1, to_nearest): failed with error");
        let _ = trace_num("    error code:", ret as i64);
        all_pass = false;
    }

    // Test rounding mode 1 (towards_zero)
    let ret = unsafe {
        float_to_int(
            f01.as_ptr(),
            FLOAT_SIZE as i32,
            result.as_mut_ptr(),
            8 as i32,
            1
        )
    };
    if ret == 8 as i32 {
        let number = i64::from_le_bytes(result);
        if number == 0 {
            let _ = trace("  float_to_int(0.1, towards_zero): good");
        } else {
            let _ = trace("  float_to_int(0.1, towards_zero): failed");
            let _ = trace_num("    got:", number);
            all_pass = false;
        }
    } else {
        let _ = trace("  float_to_int(0.1, towards_zero): failed with error");
        let _ = trace_num("    error code:", ret as i64);
        all_pass = false;
    }

    all_pass
}

fn test_float_to_mantissa_and_exponent() -> bool {
    let _ = trace("\n$$$ test_float_to_mantissa_and_exponent $$$");
    let mut all_pass = true;

    // Test with FLOAT_ONE (value 1)
    let mut mantissa_bytes: [u8; 8] = [0u8; 8];
    let mut exponent_bytes: [u8; 4] = [0u8; 4];
    let result = unsafe {
        float_to_mantissa_and_exponent(
            FLOAT_ONE.as_ptr(),
            FLOAT_SIZE as i32,
            mantissa_bytes.as_mut_ptr(),
            8,
            exponent_bytes.as_mut_ptr(),
            4,
        )
    };

    if result == FLOAT_SIZE as i32 {
        let mantissa = i64::from_le_bytes(mantissa_bytes);
        let exponent = i32::from_le_bytes(exponent_bytes);
        if mantissa == 1000000000000000000 && exponent == -18 {
            let _ = trace("  float_to_mantissa_and_exponent(1): good");
        } else {
            let _ = trace("  float_to_mantissa_and_exponent(1): failed");
            let _ = trace_num("    expected mantissa 1000000000000000000, got:", mantissa);
            let _ = trace_num("    expected exponent -18, got:", exponent as i64);
            all_pass = false;
        }
    } else {
        let _ = trace("  float_to_mantissa_and_exponent(1): failed with error");
        let _ = trace_num("    error code:", result as i64);
        all_pass = false;
    }

    // Test with FLOAT_NEGATIVE_ONE (value -1)
    let mut mantissa_bytes: [u8; 8] = [0u8; 8];
    let mut exponent_bytes: [u8; 4] = [0u8; 4];
    let result = unsafe {
        float_to_mantissa_and_exponent(
            FLOAT_NEGATIVE_ONE.as_ptr(),
            FLOAT_SIZE as i32,
            mantissa_bytes.as_mut_ptr(),
            8,
            exponent_bytes.as_mut_ptr(),
            4,
        )
    };

    if result == FLOAT_SIZE as i32 {
        let mantissa = i64::from_le_bytes(mantissa_bytes);
        let exponent = i32::from_le_bytes(exponent_bytes);
        if mantissa == -1000000000000000000 && exponent == -18 {
            let _ = trace("  float_to_mantissa_and_exponent(-1): good");
        } else {
            let _ = trace("  float_to_mantissa_and_exponent(-1): failed");
            let _ = trace_num("    expected mantissa -1000000000000000000, got:", mantissa);
            let _ = trace_num("    expected exponent -18, got:", exponent as i64);
            all_pass = false;
        }
    } else {
        let _ = trace("  float_to_mantissa_and_exponent(-1): failed with error");
        let _ = trace_num("    error code:", result as i64);
        all_pass = false;
    }

    // Test with a float created from int (10)
    let mut f10: [u8; FLOAT_SIZE] = [0u8; FLOAT_SIZE];
    unsafe { float_from_int(10, f10.as_mut_ptr(), FLOAT_SIZE, FLOAT_ROUNDING_MODES_TO_NEAREST) };

    let mut mantissa_bytes: [u8; 8] = [0u8; 8];
    let mut exponent_bytes: [u8; 4] = [0u8; 4];
    let result = unsafe {
        float_to_mantissa_and_exponent(
            f10.as_ptr(),
            FLOAT_SIZE as i32,
            mantissa_bytes.as_mut_ptr(),
            8,
            exponent_bytes.as_mut_ptr(),
            4,
        )
    };

    if result == FLOAT_SIZE as i32 {
        let mantissa = i64::from_le_bytes(mantissa_bytes);
        let exponent = i32::from_le_bytes(exponent_bytes);
        if mantissa == 1000000000000000000 && exponent == -17 {
            let _ = trace("  float_to_mantissa_and_exponent(10): good");
        } else {
            let _ = trace("  float_to_mantissa_and_exponent(10): failed");
            let _ = trace_num("    expected mantissa 1000000000000000000, got:", mantissa);
            let _ = trace_num("    expected exponent -17, got:", exponent as i64);
            all_pass = false;
        }
    } else {
        let _ = trace("  float_to_mantissa_and_exponent(10): failed with error");
        let _ = trace_num("    error code:", result as i64);
        all_pass = false;
    }

    // Test with zero
    let mut f0: [u8; FLOAT_SIZE] = [0u8; FLOAT_SIZE];
    unsafe { float_from_int(0, f0.as_mut_ptr(), FLOAT_SIZE, FLOAT_ROUNDING_MODES_TO_NEAREST) };

    let mut mantissa_bytes: [u8; 8] = [0u8; 8];
    let mut exponent_bytes: [u8; 4] = [0u8; 4];
    let result = unsafe {
        float_to_mantissa_and_exponent(
            f0.as_ptr(),
            FLOAT_SIZE as i32,
            mantissa_bytes.as_mut_ptr(),
            8,
            exponent_bytes.as_mut_ptr(),
            4,
        )
    };

    if result == FLOAT_SIZE as i32 {
        let mantissa = i64::from_le_bytes(mantissa_bytes);
        let exponent = i32::from_le_bytes(exponent_bytes);
        if mantissa == 0 && exponent == -2147483648 {
            let _ = trace("  float_to_mantissa_and_exponent(0): good");
        } else {
            let _ = trace("  float_to_mantissa_and_exponent(0): failed");
            let _ = trace_num("    expected mantissa 0, got:", mantissa);
            let _ = trace_num("    expected exponent -2147483648, got:", exponent as i64);
            all_pass = false;
        }
    } else {
        let _ = trace("  float_to_mantissa_and_exponent(0): failed with error");
        let _ = trace_num("    error code:", result as i64);
        all_pass = false;
    }

    all_pass
}

fn test_float_from_stamount() -> bool {
    let _ = trace("\n$$$ test_float_from_stamount $$$");
    let mut all_pass = true;

    // STAmount is serialized as:
    // - 1 byte: type/flags
    // - 8 bytes: amount (for XRP) or mantissa (for IOU)
    // - For IOU: additional currency and issuer fields

    // Create an XRP amount: 100 XRP = 100,000,000 drops
    // XRP format: bit 62 clear (not IOU), bit 63 clear (not negative)
    // Amount in drops: 100,000,000 = 0x05F5E100
    let xrp_amount: [u8; 8] = [
        0x40, 0x00, 0x00, 0x00, 0x05, 0xF5, 0xE1, 0x00
    ];

    let mut f_result: [u8; FLOAT_SIZE] = [0u8; FLOAT_SIZE];
    let result_size = unsafe {
        float_from_stamount(
            xrp_amount.as_ptr(),
            8,
            f_result.as_mut_ptr(),
            FLOAT_SIZE as i32,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    };

    if result_size == FLOAT_SIZE as i32 {
        let _ = trace_float("  float from XRP amount (100 XRP):", &f_result);

        // Convert back to int to verify
        let mut int_bytes: [u8; 8] = [0u8; 8];
        let ret = unsafe {
            float_to_int(
                f_result.as_ptr(),
                FLOAT_SIZE as i32,
                int_bytes.as_mut_ptr(),
                8,
                FLOAT_ROUNDING_MODES_TO_NEAREST
            )
        };
        if ret == 8 {
            let int_val = i64::from_le_bytes(int_bytes);
            if int_val == 100000000 {
                let _ = trace("  XRP amount conversion: good");
            } else {
                let _ = trace("  XRP amount conversion: failed");
                let _ = trace_num("    expected 100000000, got:", int_val);
                all_pass = false;
            }
        } else {
            let _ = trace("  XRP amount conversion: failed - float_to_int error");
            let _ = trace_num("    error code:", ret as i64);
            all_pass = false;
        }
    } else {
        let _ = trace("  float from XRP amount: failed");
        let _ = trace_num("    result_size:", result_size as i64);
        all_pass = false;
    }

    all_pass
}

fn test_float_from_stnumber() -> bool {
    let _ = trace("\n$$$ test_float_from_stnumber $$$");
    let mut all_pass = true;

    // STNumber is serialized as:
    // - 8 bytes: mantissa (big-endian signed int64)
    // - 4 bytes: exponent (big-endian signed int32)

    // Create STNumber for value 123 (mantissa=123*10^18, exponent=-18)
    // mantissa = 123000000000000000000 = 0x6ADF37F675EF6B28000
    // But we need to fit in int64, so use mantissa=123*10^15, exponent=-15
    // 123*10^15 = 123000000000000000 = 0x01B69B4BA630F34000
    let stnumber_123: [u8; 12] = [
        0x01, 0xB6, 0x9B, 0x4B, 0xA6, 0x30, 0xF3, 0x40,  // mantissa
        0xFF, 0xFF, 0xFF, 0xF1,  // exponent = -15
    ];

    let mut f_result: [u8; FLOAT_SIZE] = [0u8; FLOAT_SIZE];
    let result_size = unsafe {
        float_from_stnumber(
            stnumber_123.as_ptr(),
            12,
            f_result.as_mut_ptr(),
            FLOAT_SIZE as i32,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    };

    if result_size == FLOAT_SIZE as i32 {
        let _ = trace_float("  float from STNumber (123):", &f_result);

        // Convert back to int to verify
        let mut int_bytes: [u8; 8] = [0u8; 8];
        let ret = unsafe {
            float_to_int(
                f_result.as_ptr(),
                FLOAT_SIZE as i32,
                int_bytes.as_mut_ptr(),
                8,
                FLOAT_ROUNDING_MODES_TO_NEAREST
            )
        };
        if ret == 8 {
            let int_val = i64::from_le_bytes(int_bytes);
            if int_val == 123 {
                let _ = trace("  STNumber conversion: good");
            } else {
                let _ = trace("  STNumber conversion: failed");
                let _ = trace_num("    expected 123, got:", int_val);
                all_pass = false;
            }
        } else {
            let _ = trace("  STNumber conversion: failed - float_to_int error");
            let _ = trace_num("    error code:", ret as i64);
            all_pass = false;
        }
    } else {
        let _ = trace("  float from STNumber: failed");
        let _ = trace_num("    result_size:", result_size as i64);
        all_pass = false;
    }

    // Test with FLOAT_ONE constant (which is already in STNumber format)
    let result_size = unsafe {
        float_from_stnumber(
            FLOAT_ONE.as_ptr(),
            FLOAT_SIZE as i32,
            f_result.as_mut_ptr(),
            FLOAT_SIZE as i32,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    };

    if result_size == FLOAT_SIZE as i32 {
        let _ = trace_float("  float from STNumber (1):", &f_result);

        // Should match FLOAT_ONE
        if 0 == unsafe { float_compare(f_result.as_ptr(), FLOAT_SIZE, FLOAT_ONE.as_ptr(), FLOAT_SIZE) } {
            let _ = trace("  STNumber(1) == FLOAT_ONE: good");
        } else {
            let _ = trace("  STNumber(1) == FLOAT_ONE: failed");
            all_pass = false;
        }
    } else {
        let _ = trace("  float from STNumber(1): failed");
        all_pass = false;
    }

    all_pass
}

#[unsafe(no_mangle)]
pub extern "C" fn finish() -> i32 {
    let mut all_pass = true;
    all_pass &= test_float_from_wasm();
    all_pass &= test_float_compare();
    all_pass &= test_float_add_subtract();
    all_pass &= test_float_multiply_divide();
    all_pass &= test_float_pow();
    all_pass &= test_float_root();
    all_pass &= test_float_invert();
    all_pass &= test_float_to_int();
    all_pass &= test_float_to_mantissa_and_exponent();
    all_pass &= test_float_from_stamount();
    all_pass &= test_float_from_stnumber();

    if all_pass { 1 } else { 0 }
}
