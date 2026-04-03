(module
  (func $test_sign_ext (result i32)
    ;; Push 255 (0x000000FF) onto the stack
    i32.const 255

    ;; Sign-extend from 8-bit to 32-bit
    ;; If 255 is treated as an i8, it is -1.
    ;; Result should be -1 (0xFFFFFFFF).
    ;; Without this proposal, this opcode (0xC0) causes a validation error.
    i32.extend8_s

    ;; Check if result is -1
    i32.const -1
    i32.eq
  )

  (export "finish" (func $test_sign_ext))
)
