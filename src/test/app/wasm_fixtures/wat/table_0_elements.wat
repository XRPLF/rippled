(module
  ;; Define a table with exactly 0 entries
  (table 0 funcref)

  ;; Standard finish function
  (func $finish (result i32)
    i32.const 1
  )
  (export "finish" (func $finish))
)
