(module
  ;; Define a Mutable Global Variable to act as our counter.
  ;; We initialize it to 1,000,000.
  (global $counter (mut i32) (i32.const 1000000))

  (func $finish (result i32)
    ;; 1. Check if counter == 0 (Base Case)
    global.get $counter
    i32.eqz
    if
      ;; If counter is 0, we are done. Return 1.
      i32.const 1
      return
    end

    ;; 2. Decrement the Global Counter
    global.get $counter
    i32.const 1
    i32.sub
    global.set $counter

    ;; 3. Recursive Step: Call SELF
    ;; This puts an i32 (1) on the stack when it returns.
    call $finish
  )

  ;; Export the only function we have
  (export "finish" (func $finish))
)
