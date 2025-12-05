(module
  ;; Table size is 1, so Index 0 is VALID bounds.
  ;; However, we do NOT initialize it, so it contains 'ref.null'.
  (table 1 funcref)

  (type $t (func (result i32)))

  (func $finish (result i32)
    ;; Call Index 0.
    ;; Bounds check passes (0 < 1).
    ;; Null check fails.
    ;; TRAP: "uninitialized element" or "undefined element"

    ;; 1. Push the index (0) onto the stack first
    i32.const 0

    ;; 2. Perform the call. This pops the index.
    call_indirect (type $t)
  )

  (export "finish" (func $finish))
)
