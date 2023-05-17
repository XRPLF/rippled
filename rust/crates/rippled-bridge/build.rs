use std::env;
use std::path::{Path, PathBuf};
use conan::{BuildPolicy, InstallCommandBuilder};

fn main() {
    let manifest_dir = env!("CARGO_MANIFEST_DIR");
    let target_dir = Path::new(&manifest_dir)
        .join("target")
        .join("rippled");
    std::fs::create_dir_all(&target_dir).unwrap();

    remove_and_copy_file(None, "libxrpl_core.a", &target_dir);
    remove_and_copy_file(Some("src/secp256k1"), "libsecp256k1.a", &target_dir);
    remove_and_copy_file(Some("src/ed25519-donna"), "libed25519.a", &target_dir);

    println!("cargo:rustc-link-search=native={}/target/rippled/", manifest_dir);
    println!("cargo:rustc-link-lib=xrpl_core");
    println!("cargo:rustc-link-lib=secp256k1");
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

fn remove_and_copy_file(extra_build_path: Option<&str>, lib_name: &str, target_dir: &PathBuf) {
    let mut lib_file = Path::new(env!("CARGO_MANIFEST_DIR"))
        // .join("../../../../rippled-scaffold/.build/");
        .join("../../../.build/");
    if let Some(extra_path) = extra_build_path {
        lib_file = lib_file.join(extra_path);
    }

    lib_file = lib_file.join(lib_name);

    if target_dir.join(lib_name).exists() {
        std::fs::remove_file(target_dir.join(lib_name)).unwrap();
    }

    let res = std::fs::copy(lib_file.clone(), target_dir.join(lib_name).clone());
    if res.is_err() {
        println!(
            "cargo:warning=Error copying file: {} {} {:?}",
            lib_file.display(),
            target_dir.display(),
            res
        );
    }

    res.unwrap();
}