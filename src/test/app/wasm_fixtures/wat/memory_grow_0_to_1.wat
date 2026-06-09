(module
  ;; 1. Define Memory: Start with 0 pages
  (memory 0)

  ;; Export memory to host
  (export "memory" (memory 0))

  (func $grow_from_zero (result i32)
    ;; We have 0 pages. We want to add 1 page.
    ;; Push delta (1) onto stack.
    i32.const 1

    ;; Grow the memory.
    ;; If successful: memory becomes 64KB, returns old size (0).
    ;; If failed: memory stays 0, returns -1.
    memory.grow

    ;; Drop the return value of memory.grow
    drop

    ;; Return 1 (as requested)
    i32.const 1
  )

  (export "finish" (func $grow_from_zero))
)
