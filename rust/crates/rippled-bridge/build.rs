use std::env;
use std::path::{Path, PathBuf};
use conan::{BuildPolicy, InstallCommandBuilder};

fn main() {
    let manifest_dir = env!("CARGO_MANIFEST_DIR");

    println!("cargo:rustc-link-search=native={}/../../../.build/", manifest_dir);
    println!("cargo:rustc-link-lib=xrpl_core");
    println!("cargo:rustc-link-search=native={}/../../../.build/src/secp256k1", manifest_dir);
    println!("cargo:rustc-link-lib=secp256k1");
    println!("cargo:rustc-link-search=native={}/../../../.build/src/ed25519-donna", manifest_dir);
    println!("cargo:rustc-link-lib=ed25519");
    println!("cargo:rerun-if-changed=src/lib.rs");
    println!("cargo:rerun-if-changed=src/rippled_api.cpp");
    println!("cargo:rerun-if-changed=include/rippled_api.h");


    let command = InstallCommandBuilder::new()
        .build_policy(BuildPolicy::Missing)
        .recipe_path(Path::new("conanfile.txt"))
        .build();

    println!("cargo:warning=CONAN: {:?}", env::var("PATH"));
    let build_info = command.generate().expect("Error executing conan command.");
    build_info.cargo_emit();

    cxx_build::bridge("src/lib.rs")
        .file("src/rippled_api.cpp")
        .flag_if_supported("-std=c++20")
        .flag("-DBOOST_ASIO_HAS_STD_INVOKE_RESULT")
        .includes([
            Path::new(manifest_dir).join("../../../src/"),
            Path::new(build_info.get_dependency("boost").unwrap().get_include_dir().unwrap()).to_path_buf(),
            Path::new(build_info.get_dependency("date").unwrap().get_include_dir().unwrap()).to_path_buf()
        ])
        .compile("rippled_bridge");
}