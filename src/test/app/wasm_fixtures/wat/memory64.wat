(module
  ;; Define a 64-bit memory (index type i64)
  ;; Start with 1 page.
  (memory i64 1)

  (func $finish (result i32)
    ;; 1. Perform a store using a 64-bit address.
    ;;    Even if the value is small (0), the type MUST be i64.
    i64.const 0     ;; Address (64-bit)
    i32.const 42    ;; Value (32-bit)
    i32.store8      ;; Opcode doesn't change, but validation rules do.

    ;; 2. check memory size
    ;;    memory.size now returns an i64.
    memory.size
    i64.const 1
    i64.eq          ;; Returns i32 (1 if true)
  )

  (export "finish" (func $finish))
)
