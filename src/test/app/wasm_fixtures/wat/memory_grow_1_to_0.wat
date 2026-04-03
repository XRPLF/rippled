(module
  ;; 1. Define Memory: Start with 1 page (64KB)
  (memory 1)

  ;; Export memory to host
  (export "memory" (memory 0))

  (func $grow_negative (result i32)
    ;; The user pushed -1. In Wasm, this is interpreted as unsigned MAX_UINT32.
    ;; This is requesting to add 4,294,967,295 pages (approx 256 TB).
    ;; A secure runtime MUST fail this request (return -1) without crashing.
    i32.const -1

    ;; Grow the memory.
    ;; Returns: old_size if success, -1 if failure.
    memory.grow

    ;; Check if result == -1 (Failure)
    i32.const -1
    i32.eq
    if
        ;; If memory.grow returned -1, we return -1 to signal "Correctly failed".
        i32.const -1
        return
    end

    ;; If we are here, memory.grow somehow SUCCEEDED (Vulnerability).
    ;; We return 1 to signal "Unexpected Success".
    i32.const 1
  )

  (export "finish" (func $grow_negative))
)
