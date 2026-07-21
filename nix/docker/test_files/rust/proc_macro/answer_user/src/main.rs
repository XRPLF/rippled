use echo_macro::define_echo;

// Expands to `fn echo() -> u32 { 42 }`. Reaching this point at all means the
// proc-macro dylib was loaded and expanded successfully during compilation.
define_echo!(42);

fn main() {
    let a = echo();
    println!("proc-macro answer = {a}");
    assert_eq!(a, 42, "proc-macro expansion produced the wrong value");
}
