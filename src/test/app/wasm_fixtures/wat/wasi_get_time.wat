(module
  ;; Import clock_time_get from WASI
  ;; Signature: (param clock_id precision return_ptr) (result errno)
  (import "wasi_snapshot_preview1" "clock_time_get"
    (func $clock_time_get (param i32 i64 i32) (result i32))
  )

  (memory 1)
  (export "memory" (memory 0))

  (func $finish (result i32)
    ;; We will store the timestamp (a 64-bit integer) at address 0.
    ;; No setup required in memory beforehand!

    ;; Call the function
    (call $clock_time_get
      (i32.const 0)       ;; clock_id: 0 = Realtime (Wallclock)
      (i64.const 1000)    ;; precision: 1000ns (hint to OS)
      (i32.const 0)       ;; result_ptr: Write the time to address 0
    )

    ;; The function returns an 'errno' (error code).
    ;; 0 = Success. Anything else = Error.

    ;; Check if errno (top of stack) is 0
    i32.eqz
    if (result i32)
      ;; Success! The time is now stored in heap[0..8].
      ;; We return 1 as requested.
      i32.const 1
    else
      ;; Failed (maybe WASI is disabled or clock is missing)
      i32.const -1
    end
  )

  (export "finish" (func $finish))
)
