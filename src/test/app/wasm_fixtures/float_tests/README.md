# Float Operations Test Module

This WebAssembly module tests floating-point operations in wasm-host.

## Building

Build using:

```bash
# Navigate to the project directory
cd projects/float_tests

# Build the WASM file
cargo build --target wasm32-unknown-unknown --release
```

The resulting WASM file will be located at:

```
./target/wasm32-unknown-unknown/release/float_tests.wasm
```

## Running with wasm-host

Run the contract using the wasm-host application:

```bash
cd ../../wasm-host
cargo run -- --wasm-file ../projects/float_tests/target/wasm32-unknown-unknown/release/float_tests.wasm --function finish
```

## Current Limitations

### Rounding Modes Not Implemented

The current implementation accepts but **does not honor** the `rounding_mode` parameter in float operations. All operations use BigDecimal's default behavior, unlike XRPL/rippled.

**Affected functions:**
- `float_from_int`
- `float_from_uint`
- `float_set`
- `float_add`
- `float_subtract`
- `float_multiply`
- `float_divide`
- `float_root`
- `float_log`

### Precision Differences

The current implementation uses Rust's BigDecimal library, which may produce slightly different results than rippled's Number class for edge cases involving:
- Very large or very small numbers
- Operations near the precision limits
- Rounding of intermediate results

### Performance Considerations

BigDecimal operations are significantly slower than native floating-point operations. This is acceptable for testing but may impact performance in production scenarios.

## Expected Behavior After Migration

Once migrated to rippled's Number class:

1. **Rounding modes will be fully supported**:
   - `0`: Round to nearest (ties to even)
   - `1`: Round towards zero
   - `2`: Round down (floor)
   - `3`: Round up (ceiling)

2. **Bit-for-bit compatibility** with rippled validators

3. **Improved performance** using optimized C++ implementation

4. **Consistent behavior** across all XRPL nodes, per the XRPL protocol

## Test Coverage

The test module covers:
- ✅ Float creation from integers
- ✅ Float comparison operations
- ✅ Basic arithmetic (add, subtract, multiply, divide)
- ✅ Mathematical functions (root, log)
- ✅ Special values (positive/negative)
- ❌ Rounding mode variations (not yet implemented)
- ❌ Edge cases requiring exact rippled compatibility

## Notes

- The wasm-host uses mock implementations and may not reflect production behavior
- For production testing, use devnet or a standalone rippled node
- Float values are represented in XRPL's custom 64-bit format, not IEEE 754
