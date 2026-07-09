use std::hint::black_box;

// Rust analogue of the C++ UBSan check: with overflow checks enabled the
// compiler inserts a runtime check that panics on signed integer overflow.
// `black_box` keeps the operands opaque so the addition is evaluated at
// runtime rather than being rejected by the compile-time overflow lint.
fn main() {
    let max = black_box(i32::MAX);
    let one = black_box(1);
    println!("Current max: {max}");
    let overflowed = max + one;
    println!("Overflowed result: {overflowed}");
}
