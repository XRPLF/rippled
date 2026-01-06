(module
  ;; Define 1 page of memory (64KB = 65,536 bytes)
  (memory 1)

  (func $read_edge (result i32)
    ;; Push the index of the LAST valid byte
    i32.const 65535

    ;; Load 1 byte (unsigned)
    i32.load8_u

    ;; Clean up the stack.
    ;; The load pushed a value, but we don't care what it is.
    drop

    ;; Return 1 to signal "I survived the memory access"
    i32.const 1
  )

  ;; Export as "finish" as requested
  (export "finish" (func $read_edge))
)
