(module
  ;; Import a table from the host that holds externrefs
  (import "env" "table" (table 1 externref))

  (func $test_ref_types (result i32)
    ;; Store a null externref into the table at index 0
    ;; If reference_types is disabled, 'externref' and 'ref.null' will fail parsing.
    (table.set
      (i32.const 0)       ;; Index
      (ref.null extern)   ;; Value (Null External Reference)
    )

    ;; Return 1 (Success)
    i32.const 1
  )

  (export "finish" (func $test_ref_types))
)
