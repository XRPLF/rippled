(module
  ;; Import WASI fd_write
  ;; Signature: (fd, iovs_ptr, iovs_len, nwritten_ptr) -> errno
  (import "wasi_snapshot_preview1" "fd_write"
    (func $fd_write (param i32 i32 i32 i32) (result i32))
  )

  (memory 1)
  (export "memory" (memory 0))

  ;; --- DATA SEGMENTS ---

  ;; 1. The String Data "Hello\n" placed at offset 16
  ;;    We assume offset 0-16 is reserved for the IOVec struct
  (data (i32.const 16) "Hello\n")

  ;; 2. The IO Vector (struct iovec) placed at offset 0
  ;;    Structure: { buf_ptr: u32, buf_len: u32 }

  ;;    Field 1: buf_ptr = 16 (Location of "Hello\n")
  ;;    Encoded in little-endian: 10 00 00 00
  (data (i32.const 0) "\10\00\00\00")

  ;;    Field 2: buf_len = 6 (Length of "Hello\n")
  ;;    Encoded in little-endian: 06 00 00 00
  (data (i32.const 4) "\06\00\00\00")

  (func $finish (result i32)
    (local $nwritten_ptr i32)

    ;; We will ask WASI to write the "number of bytes written" to address 24
    ;; (safely after our string data)
    i32.const 24
    local.set $nwritten_ptr

    ;; Call fd_write
    (call $fd_write
      (i32.const 1)       ;; fd: 1 = STDOUT
      (i32.const 0)       ;; iovs_ptr: Address 0 (where we defined the struct)
      (i32.const 1)       ;; iovs_len: We are passing 1 vector
      (local.get $nwritten_ptr) ;; nwritten_ptr: Address 24
    )

    ;; The function returns an 'errno' (i32).
    ;; 0 means Success.

    ;; Check if errno == 0
    i32.eqz
    if (result i32)
      ;; Success: Return 1
      i32.const 1
    else
      ;; Failure: Return -1
      i32.const -1
    end
  )

  (export "finish" (func $finish))
)
