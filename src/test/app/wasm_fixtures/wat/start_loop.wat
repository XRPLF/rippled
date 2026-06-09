(module
  ;; Function 1: The Infinite Loop
  (func $run_forever
    (loop $infinite
      br $infinite
    )
  )

  ;; Function 2: Finish
  (func $finish (result i32)
    i32.const 1
  )

  ;; 1. EXPORT the functions (optional, if you want to call them later)
  (export "start" (func $run_forever))
  (export "finish" (func $finish))

  ;; 2. The special start section
  ;; This tells the VM: "Run function $run_forever immediately
  ;; when this module is instantiated."
  (start $run_forever)
)
