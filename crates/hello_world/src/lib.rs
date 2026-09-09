#[cxx::bridge(namespace = "rs::hello_world")]
mod ffi {
    extern "Rust" {
        fn hello_world() -> String;
    }
}

pub fn hello_world() -> String {
    "hello_world".to_string()
}
