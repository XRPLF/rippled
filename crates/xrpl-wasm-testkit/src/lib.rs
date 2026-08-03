//! Assembles WebAssembly text for the C++ test suite. **Test-only.**
//!
//! A crate of its own rather than an entry on `xrpl-wasm-vm-ffi`, and the separation is the
//! point. The engine pins `wasmi = { default-features = false }` precisely so a text
//! assembler cannot reach the consensus path — wasmi's `wat` feature is on by default and
//! makes `Module::new` accept text as readily as binary, which would make a transaction's
//! validity a build flag (review finding A5). Putting `compile_wat` on the production bridge
//! would link `wat` into xrpld even if nothing called it.
//!
//! Linked only into `xrpl_tests`, never into `libxrpl` or `xrpld`, so "no assembler in the
//! shipped node" is a property of the link graph rather than a flag someone can flip.
#![deny(rustdoc::broken_intra_doc_links)]

#[cxx::bridge(namespace = "rs::wasm_testkit")]
mod ffi {
    extern "Rust" {
        /// Assemble `wat` to a wasm module.
        ///
        /// Throws `rust::Error` on invalid input, which is what a test wants: a typo in a
        /// fixture should fail the test that holds it, at the line that holds it.
        fn compile_wat(wat: &str) -> Result<Vec<u8>>;
    }
}

fn compile_wat(wat: &str) -> Result<Vec<u8>, wat::Error> {
    wat::parse_str(wat)
}

#[cfg(test)]
mod tests {
    use super::compile_wat;

    #[test]
    fn a_module_assembles_to_something_beginning_with_the_wasm_magic() {
        let wasm = compile_wat("(module)").expect("assembles");

        assert_eq!(&wasm[..4], b"\0asm");
    }

    #[test]
    fn a_typo_is_an_error_rather_than_a_module() {
        let error = compile_wat("(module (func (export").expect_err("must not assemble");

        assert!(
            !error.to_string().is_empty(),
            "the error has to say something"
        );
    }
}
