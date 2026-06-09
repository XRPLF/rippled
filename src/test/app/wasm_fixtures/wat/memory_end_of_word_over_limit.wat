(module
  ;; 1. Define Memory: 1 Page = 64KB = 65,536 bytes
  (memory 1)

  ;; Export memory so the host can inspect it if needed
  (export "memory" (memory 0))

  (func $test_straddle (result i32)
    ;; Push the address onto the stack.
    ;; 65534 is valid, but it is only 2 bytes away from the end.
    i32.const 65534

    ;; Attempt to load an i32 (4 bytes) from that address.
    ;; This requires bytes 65534, 65535, 65536, and 65537.
    ;; Since 65536 is the first invalid byte, this MUST trap.
    i32.load

    ;; Clean up the stack.
    ;; The load pushed a value, but we don't care what it is.
    drop

    ;; Return 1 to signal "I survived the memory access"
    i32.const 1
  )

  ;; Export the function so you can call it from your host (JS, Python, etc.)
  (export "finish" (func $test_straddle))
)
