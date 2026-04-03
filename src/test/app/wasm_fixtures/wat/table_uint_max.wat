(module
  ;; Definition: (table <min> <optional_max> <type>)
  ;; We use 0xFFFFFFFF (4,294,967,295), which is the unsigned equivalent of -1.
  ;; This tests if the runtime handles the maximum possible u32 value
  ;; without integer overflows or attempting a massive allocation.
  ;;
  ;; Note that using -1 as the table size cannot be parsed by wasm-tools or wat2wasm
  (table 0xFFFFFFFF funcref)

  (func $finish (result i32)
    ;; If the module loads despite the massive table, return 1.
    i32.const 1
  )
  (export "finish" (func $finish))
)
