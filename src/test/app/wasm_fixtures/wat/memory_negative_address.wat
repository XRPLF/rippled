(module
  ;; Define memory: 128 pages (8MB) min, 128 pages max
  (memory 128 128)

  ;; Export memory so host can verify size
  (export "memory" (memory 0))

  (func $access_last_byte (result i32)
    ;; Push a negative address
    i32.const -1

    ;; Load byte from that address
    i32.load8_u

    ;; Drop the value
    drop

    ;; Return 1 to indicate success
    i32.const 1
  )

  (export "finish" (func $access_last_byte))
)
