use echo_macro::define_echo;

define_echo!(42);

fn main() {
    let a = echo();
    println!("proc-macro answer = {a}");
    assert_eq!(a, 42, "proc-macro expansion produced the wrong value");
}
