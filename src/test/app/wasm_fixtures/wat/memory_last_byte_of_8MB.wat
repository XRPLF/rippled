(module
  ;; Define memory: 128 pages (8MB) min, 128 pages max
  (memory 128 128)

  ;; Export memory so host can verify size
  (export "memory" (memory 0))

  (func $access_last_byte (result i32)
    ;; Math: 128 pages * 64,536 bytes/page = 8,388,608 bytes
    ;; Valid indices: 0 to 8,388,607

    ;; Push the address of the LAST valid byte
    i32.const 8388607

    ;; Load byte from that address
    i32.load8_u

    ;; Drop the value (we don't care what it is, just that we could read it)
    drop

    ;; Return 1 to indicate success
    i32.const 1
  )

  (export "finish" (func $access_last_byte))
)
