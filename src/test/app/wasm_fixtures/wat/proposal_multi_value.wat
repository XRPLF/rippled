(module
  ;; 1. Function returning TWO values (Multi-Value feature)
  (func $get_numbers (result i32 i32)
    i32.const 10
    i32.const 20
  )

  (func $finish (result i32)
    ;; Call pushes [10, 20] onto the stack
    call $get_numbers

    ;; 2. Block taking TWO parameters (Multi-Value feature)
    ;;    It consumes the [10, 20] from the stack.
    block (param i32 i32) (result i32)
      i32.add       ;; 10 + 20 = 30
      i32.const 30  ;; Expected result
      i32.eq        ;; Compare: returns 1 if equal
    end
  )

  (export "finish" (func $finish))
)
