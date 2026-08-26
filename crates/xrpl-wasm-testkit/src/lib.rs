//! Assembles WebAssembly text for the C++ test suite. **Test-only.**
//!
//! A crate of its own rather than an entry on `xrpl-wasm-vm-ffi`, and the separation is the
//! point. The engine pins `wasmi = { default-features = false }` precisely so a text
//! assembler cannot reach the consensus path — wasmi's `wat` feature is on by default and
//! makes `Module::new` accept text as readily as binary, which would make a transaction's
//! validity a build flag. Putting `compile_wat` on the production bridge would link `wat`
//! into xrpld even if nothing called it.
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

        /// The gas a host function is charged before it runs, by its guest import name.
        ///
        /// For the C++ gas benchmarks, which measure what a host call actually costs and
        /// report it against what the table says it costs. Reading the declaration through
        /// here rather than copying the numbers into C++ is the point: 61 transcribed
        /// constants would drift from `lib.rs` the first time a price changed, and drift
        /// silently, because a benchmark has nothing to fail.
        ///
        /// Throws `rust::Error` on an unknown name — a typo should fail loudly rather than
        /// quietly compare against zero.
        fn declared_gas(wasm_name: &str) -> Result<u64>;
    }
}

fn compile_wat(wat: &str) -> Result<Vec<u8>, wat::Error> {
    wat::parse_str(wat)
}

fn declared_gas(wasm_name: &str) -> Result<u64, UnknownHostFunction> {
    xrpl_host_functions::HostFunctionSpec::ALL
        .iter()
        .find(|op| op.wasm_name() == wasm_name)
        .map(|op| op.gas())
        .ok_or_else(|| UnknownHostFunction(wasm_name.to_owned()))
}

#[derive(Debug)]
struct UnknownHostFunction(String);

impl std::fmt::Display for UnknownHostFunction {
    fn fmt(&self, f: &mut std::fmt::Formatter<'_>) -> std::fmt::Result {
        write!(f, "no host function is imported as `{}`", self.0)
    }
}

impl std::error::Error for UnknownHostFunction {}

#[cfg(test)]
mod tests {
    use super::compile_wat;

    #[test]
    fn a_module_assembles_to_something_beginning_with_the_wasm_magic() {
        let wasm = compile_wat("(module)").expect("assembles");

        assert_eq!(&wasm[..4], b"\0asm");
    }

    #[test]
    fn a_host_function_reports_the_gas_its_declaration_gives_it() {
        // `trace` is the cheapest declaration in the table; the point is not the number but
        // that the lookup reaches the same constant the engine charges from.
        assert_eq!(
            super::declared_gas("trace").expect("trace is a host function"),
            xrpl_host_functions::HostFunctionSpec::Trace.gas()
        );
    }

    #[test]
    fn every_host_function_is_reachable_by_its_import_name() {
        for op in xrpl_host_functions::HostFunctionSpec::ALL {
            assert_eq!(
                super::declared_gas(op.wasm_name()).expect("declared"),
                op.gas(),
                "{} must be reachable by name",
                op.wasm_name()
            );
        }
    }

    #[test]
    fn an_unknown_name_is_an_error_rather_than_zero_gas() {
        super::declared_gas("not_a_host_function").expect_err("must not resolve");
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
