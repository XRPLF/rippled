#![allow(unused_imports)]
#![allow(unused_variables)]
#![cfg_attr(target_arch = "wasm32", no_std)]

#[cfg(not(target_arch = "wasm32"))]
extern crate std;

use xrpl_std::core::locator::Locator;
use xrpl_std::core::types::opaque_float::{FLOAT_NEGATIVE_ONE, FLOAT_ONE};
use xrpl_std::decode_hex_32;
use xrpl_std::host::trace::DataRepr::AsHex;
use xrpl_std::host::trace::{trace, trace_data, trace_float, trace_num, DataRepr};
use xrpl_std::host::{
    cache_ledger_obj, float_add, float_compare, float_divide, float_from_int, float_from_uint,
    float_log, float_multiply, float_pow, float_root, float_set, float_subtract,
    get_ledger_obj_array_len, get_ledger_obj_field, get_ledger_obj_nested_field,
    trace_opaque_float, FLOAT_ROUNDING_MODES_TO_NEAREST,
};
use xrpl_std::sfield;
use xrpl_std::sfield::{
    Account, AccountTxnID, Balance, Domain, EmailHash, Flags, LedgerEntryType, MessageKey,
    OwnerCount, PreviousTxnID, PreviousTxnLgrSeq, RegularKey, Sequence, TicketCount, TransferRate,
};

fn test_float_from_wasm() {
    let _ = trace("\n$$$ test_float_from_wasm $$$");

    let mut f: [u8; 8] = [0u8; 8];
    if 8 == unsafe { float_from_int(12300, f.as_mut_ptr(), 8, FLOAT_ROUNDING_MODES_TO_NEAREST) } {
        let _ = trace_float("  float from i64 12300:", &f);
        let _ = trace_data("  float from i64 12300 as HEX:", &f, AsHex);
    } else {
        let _ = trace("  float from i64 12300: failed");
    }

    let u64_value: u64 = 12300;
    if 8 == unsafe {
        float_from_uint(
            &u64_value as *const u64 as *const u8,
            8,
            f.as_mut_ptr(),
            8,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    } {
        let _ = trace_float("  float from u64 12300:", &f);
    } else {
        let _ = trace("  float from u64 12300: failed");
    }

    if 8 == unsafe { float_set(2, 123, f.as_mut_ptr(), 8, FLOAT_ROUNDING_MODES_TO_NEAREST) } {
        let _ = trace_float("  float from exp 2, mantissa 123:", &f);
    } else {
        let _ = trace("  float from exp 2, mantissa 3: failed");
    }

    let _ = trace_float("  float from const 1:", &FLOAT_ONE);
    let _ = trace_float("  float from const -1:", &FLOAT_NEGATIVE_ONE);
}

fn test_float_compare() {
    let _ = trace("\n$$$ test_float_compare $$$");

    let mut f1: [u8; 8] = [0u8; 8];
    if 8 != unsafe { float_from_int(1, f1.as_mut_ptr(), 8, FLOAT_ROUNDING_MODES_TO_NEAREST) } {
        let _ = trace("  float from 1: failed");
    } else {
        let _ = trace_float("  float from 1:", &f1);
    }

    if 0 == unsafe { float_compare(f1.as_ptr(), 8, FLOAT_ONE.as_ptr(), 8) } {
        let _ = trace("  float from 1 == FLOAT_ONE");
    } else {
        let _ = trace("  float from 1 != FLOAT_ONE");
    }

    if 1 == unsafe { float_compare(f1.as_ptr(), 8, FLOAT_NEGATIVE_ONE.as_ptr(), 8) } {
        let _ = trace("  float from 1 > FLOAT_NEGATIVE_ONE");
    } else {
        let _ = trace("  float from 1 !> FLOAT_NEGATIVE_ONE");
    }

    if 2 == unsafe { float_compare(FLOAT_NEGATIVE_ONE.as_ptr(), 8, f1.as_ptr(), 8) } {
        let _ = trace("  FLOAT_NEGATIVE_ONE < float from 1");
    } else {
        let _ = trace("  FLOAT_NEGATIVE_ONE !< float from 1");
    }
}

fn test_float_add_subtract() {
    let _ = trace("\n$$$ test_float_add_subtract $$$");

    let mut f_compute: [u8; 8] = FLOAT_ONE;
    for i in 0..9 {
        unsafe {
            float_add(
                f_compute.as_ptr(),
                8,
                FLOAT_ONE.as_ptr(),
                8,
                f_compute.as_mut_ptr(),
                8,
                FLOAT_ROUNDING_MODES_TO_NEAREST,
            )
        };
        // let _ = trace_float("  float:", &f_compute);
    }
    let mut f10: [u8; 8] = [0u8; 8];
    if 8 != unsafe { float_from_int(10, f10.as_mut_ptr(), 8, FLOAT_ROUNDING_MODES_TO_NEAREST) } {
        // let _ = trace("  float from 10: failed");
    }
    if 0 == unsafe { float_compare(f10.as_ptr(), 8, f_compute.as_ptr(), 8) } {
        let _ = trace("  repeated add: good");
    } else {
        let _ = trace("  repeated add: bad");
    }

    for i in 0..11 {
        unsafe {
            float_subtract(
                f_compute.as_ptr(),
                8,
                FLOAT_ONE.as_ptr(),
                8,
                f_compute.as_mut_ptr(),
                8,
                FLOAT_ROUNDING_MODES_TO_NEAREST,
            )
        };
    }
    if 0 == unsafe { float_compare(f_compute.as_ptr(), 8, FLOAT_NEGATIVE_ONE.as_ptr(), 8) } {
        let _ = trace("  repeated subtract: good");
    } else {
        let _ = trace("  repeated subtract: bad");
    }
}

fn test_float_multiply_divide() {
    let _ = trace("\n$$$ test_float_multiply_divide $$$");

    let mut f10: [u8; 8] = [0u8; 8];
    unsafe { float_from_int(10, f10.as_mut_ptr(), 8, FLOAT_ROUNDING_MODES_TO_NEAREST) };
    let mut f_compute: [u8; 8] = FLOAT_ONE;
    for i in 0..6 {
        unsafe {
            float_multiply(
                f_compute.as_ptr(),
                8,
                f10.as_ptr(),
                8,
                f_compute.as_mut_ptr(),
                8,
                FLOAT_ROUNDING_MODES_TO_NEAREST,
            )
        };
        // let _ = trace_float("  float:", &f_compute);
    }
    let mut f1000000: [u8; 8] = [0u8; 8];
    unsafe {
        float_from_int(
            1000000,
            f1000000.as_mut_ptr(),
            8,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    };

    if 0 == unsafe { float_compare(f1000000.as_ptr(), 8, f_compute.as_ptr(), 8) } {
        let _ = trace("  repeated multiply: good");
    } else {
        let _ = trace("  repeated multiply: bad");
    }

    for i in 0..7 {
        unsafe {
            float_divide(
                f_compute.as_ptr(),
                8,
                f10.as_ptr(),
                8,
                f_compute.as_mut_ptr(),
                8,
                FLOAT_ROUNDING_MODES_TO_NEAREST,
            )
        };
    }
    let mut f01: [u8; 8] = [0u8; 8];
    unsafe { float_set(-1, 1, f01.as_mut_ptr(), 8, FLOAT_ROUNDING_MODES_TO_NEAREST) };

    if 0 == unsafe { float_compare(f_compute.as_ptr(), 8, f01.as_ptr(), 8) } {
        let _ = trace("  repeated divide: good");
    } else {
        let _ = trace("  repeated divide: bad");
    }
}

fn test_float_pow() {
    let _ = trace("\n$$$ test_float_pow $$$");

    let mut f_compute: [u8; 8] = [0u8; 8];
    unsafe {
        float_pow(
            FLOAT_ONE.as_ptr(),
            8,
            3,
            f_compute.as_mut_ptr(),
            8,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    };
    let _ = trace_float("  float cube of 1:", &f_compute);

    unsafe {
        float_pow(
            FLOAT_NEGATIVE_ONE.as_ptr(),
            8,
            6,
            f_compute.as_mut_ptr(),
            8,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    };
    let _ = trace_float("  float 6th power of -1:", &f_compute);

    let mut f9: [u8; 8] = [0u8; 8];
    unsafe { float_from_int(9, f9.as_mut_ptr(), 8, FLOAT_ROUNDING_MODES_TO_NEAREST) };
    unsafe {
        float_pow(
            f9.as_ptr(),
            8,
            2,
            f_compute.as_mut_ptr(),
            8,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    };
    let _ = trace_float("  float square of 9:", &f_compute);

    unsafe {
        float_pow(
            f9.as_ptr(),
            8,
            0,
            f_compute.as_mut_ptr(),
            8,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    };
    let _ = trace_float("  float 0th power of 9:", &f_compute);

    let mut f0: [u8; 8] = [0u8; 8];
    unsafe { float_from_int(0, f0.as_mut_ptr(), 8, FLOAT_ROUNDING_MODES_TO_NEAREST) };
    unsafe {
        float_pow(
            f0.as_ptr(),
            8,
            2,
            f_compute.as_mut_ptr(),
            8,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    };
    let _ = trace_float("  float square of 0:", &f_compute);

    let r = unsafe {
        float_pow(
            f0.as_ptr(),
            8,
            0,
            f_compute.as_mut_ptr(),
            8,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    };
    let _ = trace_num(
        "  float 0th power of 0 (expecting INVALID_PARAMS error):",
        r as i64,
    );
}

fn test_float_root() {
    let _ = trace("\n$$$ test_float_root $$$");

    let mut f9: [u8; 8] = [0u8; 8];
    unsafe { float_from_int(9, f9.as_mut_ptr(), 8, FLOAT_ROUNDING_MODES_TO_NEAREST) };
    let mut f_compute: [u8; 8] = [0u8; 8];
    unsafe {
        float_root(
            f9.as_ptr(),
            8,
            2,
            f_compute.as_mut_ptr(),
            8,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    };
    let _ = trace_float("  float sqrt of 9:", &f_compute);
    unsafe {
        float_root(
            f9.as_ptr(),
            8,
            3,
            f_compute.as_mut_ptr(),
            8,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    };
    let _ = trace_float("  float cbrt of 9:", &f_compute);

    let mut f1000000: [u8; 8] = [0u8; 8];
    unsafe {
        float_from_int(
            1000000,
            f1000000.as_mut_ptr(),
            8,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    };
    unsafe {
        float_root(
            f1000000.as_ptr(),
            8,
            3,
            f_compute.as_mut_ptr(),
            8,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    };
    let _ = trace_float("  float cbrt of 1000000:", &f_compute);
    unsafe {
        float_root(
            f1000000.as_ptr(),
            8,
            6,
            f_compute.as_mut_ptr(),
            8,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    };
    let _ = trace_float("  float 6th root of 1000000:", &f_compute);
}

fn test_float_log() {
    let _ = trace("\n$$$ test_float_log $$$");

    let mut f1000000: [u8; 8] = [0u8; 8];
    unsafe {
        float_from_int(
            1000000,
            f1000000.as_mut_ptr(),
            8,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    };
    let mut f_compute: [u8; 8] = [0u8; 8];
    unsafe {
        float_log(
            f1000000.as_ptr(),
            8,
            f_compute.as_mut_ptr(),
            8,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    };
    let _ = trace_float("  log_10 of 1000000:", &f_compute);
}

fn test_float_negate() {
    let _ = trace("\n$$$ test_float_negate $$$");

    let mut f_compute: [u8; 8] = [0u8; 8];
    unsafe {
        float_multiply(
            FLOAT_ONE.as_ptr(),
            8,
            FLOAT_NEGATIVE_ONE.as_ptr(),
            8,
            f_compute.as_mut_ptr(),
            8,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    };
    // let _ = trace_float("  float:", &f_compute);
    if 0 == unsafe { float_compare(FLOAT_NEGATIVE_ONE.as_ptr(), 8, f_compute.as_ptr(), 8) } {
        let _ = trace("  negate const 1: good");
    } else {
        let _ = trace("  negate const 1: bad");
    }

    unsafe {
        float_multiply(
            FLOAT_NEGATIVE_ONE.as_ptr(),
            8,
            FLOAT_NEGATIVE_ONE.as_ptr(),
            8,
            f_compute.as_mut_ptr(),
            8,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    };
    // let _ = trace_float("  float:", &f_compute);
    if 0 == unsafe { float_compare(FLOAT_ONE.as_ptr(), 8, f_compute.as_ptr(), 8) } {
        let _ = trace("  negate const -1: good");
    } else {
        let _ = trace("  negate const -1: bad");
    }
}

fn test_float_invert() {
    let _ = trace("\n$$$ test_float_invert $$$");

    let mut f_compute: [u8; 8] = [0u8; 8];
    let mut f10: [u8; 8] = [0u8; 8];
    unsafe { float_from_int(10, f10.as_mut_ptr(), 8, FLOAT_ROUNDING_MODES_TO_NEAREST) };
    unsafe {
        float_divide(
            FLOAT_ONE.as_ptr(),
            8,
            f10.as_ptr(),
            8,
            f_compute.as_mut_ptr(),
            8,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    };
    let _ = trace_float("  invert a float from 10:", &f_compute);
    unsafe {
        float_divide(
            FLOAT_ONE.as_ptr(),
            8,
            f_compute.as_ptr(),
            8,
            f_compute.as_mut_ptr(),
            8,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    };
    let _ = trace_float("  invert again:", &f_compute);

    // if f10's value is 7, then invert twice won't match the original value
    if 0 == unsafe { float_compare(f10.as_ptr(), 8, f_compute.as_ptr(), 8) } {
        let _ = trace("  invert twice: good");
    } else {
        let _ = trace("  invert twice: bad");
    }
}

#[unsafe(no_mangle)]
pub extern "C" fn finish() -> i32 {
    test_float_from_wasm();
    test_float_compare();
    test_float_add_subtract();
    test_float_multiply_divide();
    test_float_pow();
    test_float_root();
    test_float_log();
    test_float_negate();
    test_float_invert();

    1
}
