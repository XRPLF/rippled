fn main() {
    // Verify the panic runtime works: a panic must print its message to stderr
    // and exit with a non-zero status (Rust's default panic exit code is 101).
    panic!("explicit panic from test");
}
