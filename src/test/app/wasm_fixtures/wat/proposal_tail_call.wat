(module
  ;; Define a simple function we can tail-call
  (func $target (result i32)
    i32.const 1
  )

  (func $finish (result i32)
    ;; Try to use the 'return_call' instruction (Opcode 0x12)
    ;; If Tail Call proposal is disabled, this fails to Compile/Validate.
    ;; If enabled, it jumps to $target, which returns 1.
    return_call $target
  )

  (export "finish" (func $finish))
)
