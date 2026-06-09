(module
  ;; 1. Define a global using an EXTENDED constant expression.
  ;;    MVP only allows (i32.const X).
  ;;    This proposal allows (i32.add (i32.const X) (i32.const Y)).
  (global $g i32 (i32.add (i32.const 10) (i32.const 32)))

  (func $finish (result i32)
    ;; 2. verify the global equals 42
    global.get $g
    i32.const 42
    i32.eq
  )

  (export "finish" (func $finish))
)
