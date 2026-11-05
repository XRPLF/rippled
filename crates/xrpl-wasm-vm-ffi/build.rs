//! Generates the C++ header for the ABI's wire enums and constants, as `cbindgen.toml`
//! configures it.

use std::env;
use std::path::PathBuf;

const ABI_SOURCE: &str = "../xrpl-host-functions/src/lib.rs";
const CONFIG: &str = "cbindgen.toml";
const HEADER: &str = "xrpl_host_functions.h";
/// Set by CMake to the directory on C++'s include path. Unset — plain `cargo`,
/// rust-analyzer — nothing reads the header, so none is generated.
const HEADER_OUT_DIR: &str = "HOST_FUNCTIONS_BINDGEN_OUT_DIR";

fn main() {
    println!("cargo::rerun-if-changed={ABI_SOURCE}");
    println!("cargo::rerun-if-changed={CONFIG}");
    println!("cargo::rerun-if-env-changed={HEADER_OUT_DIR}");

    let Some(out_dir) = env::var_os(HEADER_OUT_DIR) else {
        return;
    };

    let config = cbindgen::Config::from_file(CONFIG).unwrap_or_else(|error| panic!("{error}"));
    cbindgen::Builder::new()
        .with_config(config)
        .with_src(ABI_SOURCE)
        .generate()
        .unwrap_or_else(|error| panic!("cbindgen could not read {ABI_SOURCE}: {error}"))
        .write_to_file(PathBuf::from(out_dir).join(HEADER));
}
