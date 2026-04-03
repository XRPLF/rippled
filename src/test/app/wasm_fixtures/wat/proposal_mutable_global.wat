(module
  ;; Define a mutable global initialized to 0
  (global $counter (mut i32) (i32.const 0))

  ;; EXPORTING a mutable global is the key feature of this proposal.
  ;; In strict MVP, exported globals had to be immutable (const).
  (export "counter" (global $counter))

  (func $finish (result i32)
    ;; 1. Get current value
    global.get $counter

    ;; 2. Add 1
    i32.const 1
    i32.add

    ;; 3. Set new value (Mutation)
    global.set $counter

    ;; 4. Return 1 for success
    i32.const 1
  )

  (export "finish" (func $finish))
)
