(module
  ;; Define a table with exactly 0 entries
  (table 0 funcref)

  ;; Standard finish function
  (func $finish (result i32)
    i32.const 1
  )
  (export "finish" (func $finish))

  (@custom "xrpl-escrow-stdlib" "4.5.6")
  (@custom "xrpl-common-stdlib" "1.2.3")
)
