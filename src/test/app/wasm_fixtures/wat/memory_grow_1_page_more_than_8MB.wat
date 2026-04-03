(module
  ;; Start at your limit: 128 pages (8MB)
  (memory 128)
  (export "memory" (memory 0))

  (func $try_grow_beyond_limit (result i32)
    ;; Attempt to grow by 1 page
    i32.const 1
    memory.grow

    ;; memory.grow returns:
    ;;   -1  if the growth failed (Correct behavior for your limit)
    ;;   128 (old size) if growth succeeded (Means limit was bypassed)

    ;; Check if result == -1
    i32.const -1
    i32.eq
    if
      ;; Growth FAILED (Host blocked it). Return -1.
      i32.const -1
      return
    end

    ;; Growth SUCCEEDED (Host allowed it). Return 1.
    i32.const 1
  )

  (export "finish" (func $try_grow_beyond_limit))
)
