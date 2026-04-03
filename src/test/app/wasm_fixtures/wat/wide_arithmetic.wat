(module
  (func $finish (result i32)
    ;; 1. Push operands
    i64.const 1
    i64.const 2

    ;; 2. Execute Wide Multiplication
    ;;    If the feature is DISABLED, the parser/validator will trap here
    ;;    with "unknown instruction" or "invalid opcode".
    ;;    Input: [i64, i64] -> Output: [i64, i64]
    i64.mul_wide_u

    ;; 3. Clean up the stack (drop the two i64 results)
    drop
    drop

    ;; 4. Return 1 to signal that validation passed
    i32.const 1
  )

  (export "finish" (func $finish))
)
