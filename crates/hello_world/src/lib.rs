#![cfg_attr(coverage_nightly, feature(coverage_attribute))]

#[cxx::bridge(namespace = "rs::hello_world")]
mod ffi {
    extern "Rust" {
        fn hello_world() -> String;
    }
}

pub fn hello_world() -> String {
    "hello_world".to_string()
}

#[cfg(test)]
#[cfg_attr(coverage_nightly, coverage(off))]
mod tests {
    use super::*;

    #[test]
    fn hello_world_returns_hello_world() {
        assert_eq!(hello_world(), "hello_world")
    }
}
