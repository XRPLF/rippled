(module
  ;; Define a memory with 1 initial page.
  ;; CRITICAL: We explicitly set the page size to 1 byte.
  ;; Standard Wasm implies (pagesize 65536).
  (memory 1 (pagesize 1))

  (func $escrow_finish (result i32)
    ;; If this module instantiates, the runtime accepted the custom page size.
    i32.const 1
  )

  (export "escrow_finish" (func $escrow_finish))
)
