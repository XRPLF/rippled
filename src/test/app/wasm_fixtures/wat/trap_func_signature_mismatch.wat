(module
  ;; Define a table with 1 slot
  (table 1 funcref)

  ;; Define Type A: Takes nothing, returns nothing
  (type $type_void (func))

  ;; Define Type B: Takes nothing, returns i32
  (type $type_i32 (func (result i32)))

  ;; Define a function of Type A
  (func $void_func (type $type_void)
    nop
  )

  ;; Put Type A function into Table[0]
  (elem (i32.const 0) $void_func)

  (func $finish (result i32)
    ;; Attempt to call Index 0, but CLAIM we expect Type B (result i32).
    ;; The function at Index 0 matches Type A.
    ;; TRAP: "indirect call type mismatch"

    ;; 1. Push the table index (0) onto the stack
    i32.const 0

    ;; 2. Call indirect using Type B signature.
    ;;    This pops the index (0) from the stack.
    call_indirect (type $type_i32)
  )

  (export "finish" (func $finish))
)
