(module
  (func $test_int_overflow (result i32)
    ;; 1. Push INT_MIN (-2147483648)
    ;; In Hex: 0x80000000
    i32.const -2147483648

    ;; 2. Push -1
    i32.const -1

    ;; 3. Signed Division
    ;; This specific case is the ONLY integer arithmetic operation
    ;; (besides divide by zero) that traps in the spec.
    ;; Result would be +2147483648, which is too big for signed i32.
    i32.div_s
  )

  (export "finish" (func $test_int_overflow))
)
