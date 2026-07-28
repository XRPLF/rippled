(module
  ;; Define a table with exactly 0 entries
  (table 0 funcref)

  ;; Standard finish function
  (func $escrow_finish (result i32)
    i32.const 1
  )
  (export "escrow_finish" (func $escrow_finish))
)
