(module
  ;; Define a table with exactly 64 entries
  (table 64 funcref)

  ;; A dummy function to reference
  (func $dummy)

  ;; Initialize the table at offset 0 with 64 references to $dummy
  (elem (i32.const 0)
    $dummy $dummy $dummy $dummy $dummy $dummy $dummy $dummy ;; 8
    $dummy $dummy $dummy $dummy $dummy $dummy $dummy $dummy ;; 16
    $dummy $dummy $dummy $dummy $dummy $dummy $dummy $dummy ;; 24
    $dummy $dummy $dummy $dummy $dummy $dummy $dummy $dummy ;; 32
    $dummy $dummy $dummy $dummy $dummy $dummy $dummy $dummy ;; 40
    $dummy $dummy $dummy $dummy $dummy $dummy $dummy $dummy ;; 48
    $dummy $dummy $dummy $dummy $dummy $dummy $dummy $dummy ;; 56
    $dummy $dummy $dummy $dummy $dummy $dummy $dummy $dummy ;; 64
  )

  ;; Standard finish function
  (func $finish (result i32)
    i32.const 1
  )
  (export "finish" (func $finish))
)
