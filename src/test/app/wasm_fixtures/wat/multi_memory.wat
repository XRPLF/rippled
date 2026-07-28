(module
  ;; Memory 0: Index 0 (Empty)
  (memory 0)

  ;; Memory 1: Index 1 (Size 1 page)
  ;; If multi-memory is disabled, this line causes a validation error (max 1 memory).
  (memory 1)

  (func $escrow_finish (result i32)
    ;; Query size of Memory Index 1.
    ;; Should return 1 (success).
    memory.size 1
  )

  (export "escrow_finish" (func $escrow_finish))
)
