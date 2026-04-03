(module
  (func $test_saturation (result i32)
    ;; 1. Push a float that is too big for a 32-bit integer
    ;; 1e10 (10 billion) > 2.14 billion (Max i32)
    f32.const 1.0e10

    ;; 2. Attempt saturating conversion (Opcode 0xFC 0x00)
    ;; If supported: Clamps to MAX_I32.
    ;; If disabled: Validation error (unknown instruction).
    i32.trunc_sat_f32_s

    ;; 3. Check if result is MAX_I32 (2147483647)
    i32.const 2147483647
    i32.eq
  )

  (export "finish" (func $test_saturation))
)
