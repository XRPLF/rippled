(module
  ;; 1. Define Memory: 1 Page = 64KB
  (memory 1)

  (export "memory" (memory 0))

  (func $test_offset_overflow (result i32)
    ;; 1. Push the base address onto the stack.
    ;; We use '0', which is the safest, most valid address possible.
    i32.const 0

    ;; 2. Attempt to load using a static offset.
    ;; syntax: i32.load offset=N align=N
    ;; We set the offset to 65536 (the size of the memory).
    ;; The effective address becomes 0 + 65536 = 65536.
    i32.load offset=65536

    ;; Clean up the stack.
    ;; The load pushed a value, but we don't care what it is.
    drop

    ;; Return 1 to signal "I survived the memory access"
    i32.const 1
  )

  (export "finish" (func $test_offset_overflow))
)
