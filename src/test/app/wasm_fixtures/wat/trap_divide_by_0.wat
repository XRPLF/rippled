(module
  (func $finish (export "finish") (result i32)
    ;; Setup for Requirement 2: Divide an i32 by 0
    i32.const 42   ;; Push numerator
    i32.const 0    ;; Push denominator (0)
    i32.div_s      ;; Perform signed division (42 / 0)

    ;; --- NOTE: Execution usually traps (crashes) at the line above ---

    ;; Logic to satisfy Requirement 1: Return i32 = 1
    ;; If execution continued, we would drop the division result and return 1
    drop           ;; Clear the stack
    i32.const 1    ;; Push the return value
  )
)
