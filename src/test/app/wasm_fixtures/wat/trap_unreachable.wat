(module
  (func $escrow_finish (result i32)
    ;; This instruction explicitly causes a trap.
    ;; It consumes no fuel (beyond the instruction itself) and stops execution.
    unreachable

    ;; This code is dead and never reached
    i32.const 1
  )

  (export "escrow_finish" (func $escrow_finish))
)
