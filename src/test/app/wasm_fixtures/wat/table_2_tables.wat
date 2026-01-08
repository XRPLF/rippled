(module
  ;; Define a dummy function to put in the tables
  (func $dummy)

  ;; TABLE 0: The default table (allowed in MVP)
  ;; Size: 1 initial, 1 max
  (table $t0 1 1 funcref)

  ;; Initialize Table 0 at index 0
  (elem (table $t0) (i32.const 0) $dummy)

  ;; TABLE 1: The second table (Requires Reference Types proposal)
  ;; If strict MVP is enforced, the parser should error here.
  (table $t1 1 1 funcref)

  ;; Initialize Table 1 at index 0
  (elem (table $t1) (i32.const 0) $dummy)

  (func $finish (result i32)
    ;; If we successfully loaded a module with 2 tables, return 1.
    i32.const 1
  )
  (export "finish" (func $finish))
)
