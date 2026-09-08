(module

  ;; Type for call_indirect
  (type (func (result i32)))

  ;; Memory and table declarations
  (memory 1)
  (table 1 funcref)
  (data (i32.const 0) "test")
  (elem (i32.const 0) $test_func)

  ;; Global declarations
  (global $g0 (mut i32) (i32.const 0))
  (global $g1 (mut i64) (i64.const 0))

  ;; Test function for call/call_indirect
  (func $test_func (result i32)
    i32.const 42
  )


  ;; Main function with all instructions in hex order
  (func $all_instructions (export "all_instructions") (result i32)
    (local $l0 i32)
    (local $l1 i64)

    ;; 0x01: nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    nop
    i32.const 11
  )
)
