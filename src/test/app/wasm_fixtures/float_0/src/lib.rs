#![cfg_attr(target_arch = "wasm32", no_std)]

use xrpl_std::host::trace::trace;
use xrpl_std::host::{float_compare, float_from_int, float_subtract, FLOAT_ROUNDING_MODES_TO_NEAREST};

// Float size constant (8 bytes mantissa + 4 bytes exponent)
const FLOAT_SIZE: usize = 12;

// FLOAT_ZERO constant
const FLOAT_ZERO: [u8; FLOAT_SIZE] = [0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00, 0x00, 0x00];

#[unsafe(no_mangle)]
pub extern "C" fn finish() -> i32 {
    let _ = trace("\n$$$ test_float_0 $$$");

    // Test: 10 - 10 should equal 0
    let mut f10: [u8; FLOAT_SIZE] = [0u8; FLOAT_SIZE];
    let mut f_result: [u8; FLOAT_SIZE] = [0u8; FLOAT_SIZE];

    // Create float from 10
    if FLOAT_SIZE as i32 != unsafe { float_from_int(10, f10.as_mut_ptr(), FLOAT_SIZE, FLOAT_ROUNDING_MODES_TO_NEAREST) } {
        let _ = trace("  float 10-10: failed");
        return 1;
    }

    // Subtract: 10 - 10 = 0
    if FLOAT_SIZE as i32 != unsafe {
        float_subtract(
            f10.as_ptr(),
            FLOAT_SIZE,
            f10.as_ptr(),
            FLOAT_SIZE,
            f_result.as_mut_ptr(),
            FLOAT_SIZE,
            FLOAT_ROUNDING_MODES_TO_NEAREST,
        )
    } {
        let _ = trace("  float 10-10: failed");
        return 1;
    }

    // Compare result with zero
    if 0 == unsafe { float_compare(f_result.as_ptr(), FLOAT_SIZE, f_result.as_ptr(), FLOAT_SIZE) } {
        let _ = trace("  float 0 compare: good");
    } else {
        let _ = trace("  float 0 compare: bad");
    }

    // Compare result with FLOAT_ZERO constant
    if 0 == unsafe { float_compare(f_result.as_ptr(), FLOAT_SIZE, FLOAT_ZERO.as_ptr(), FLOAT_SIZE) } {
        let _ = trace("  FLOAT_ZERO compare: good");
    } else {
        let _ = trace("  FLOAT_ZERO compare: bad");
    }

    1
}
