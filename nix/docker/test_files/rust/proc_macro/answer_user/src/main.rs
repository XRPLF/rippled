use answer_macro::answer_echo;

// Expands to `fn answer() -> u32 { 42 }`. Reaching this point at all means the
// proc-macro dylib was loaded and expanded successfully during compilation.
answer_echo!(42);

fn main() {
    let a = answer();
    println!("proc-macro answer = {a}");
    assert_eq!(a, 42, "proc-macro expansion produced the wrong value");
}
