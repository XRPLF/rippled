(module
  ;; Define 1 page of memory
  (memory 1)
  (export "memory" (memory 0))

  (func $test_bulk_ops (result i32)
    ;; Setup: Write value 42 at index 0 so we have something to copy
    (i32.store8 (i32.const 0) (i32.const 42))

    ;; Test memory.copy (Opcode 0xFC 0x0A)
    ;; Copy 1 byte from offset 0 to offset 100
    (memory.copy
      (i32.const 100) ;; Destination Offset
      (i32.const 0)   ;; Source Offset
      (i32.const 1)   ;; Size (bytes)
    )

    ;; Verify: Read byte at offset 100. Should be 42.
    (i32.load8_u (i32.const 100))
    (i32.const 42)
    i32.eq
  )

  (export "finish" (func $test_bulk_ops))
)
