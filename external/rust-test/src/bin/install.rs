use std::path::PathBuf;
use std::process::{Command};
use std::{env, fs};

fn main() {
    let args: Vec<String> = env::args().collect();
    // Pass 'build' for the first argument and pass all other
    // parameters to the cargo command
    Command::new("cargo")
        .arg("build")
        .args(args.iter().skip(1))
        .status()
        .expect("failed to run cargo");
    
    // Determine the build config
    let build_config = args.iter()
        .map(|arg| arg.to_lowercase())
        .filter(|arg| arg == "--release" || arg == "--debug")
        .next()
        .unwrap_or("--debug".to_string());

    let config = if build_config == "--debug" { "debug" } else { "release" };

    let crate_name = env::var("CARGO_PKG_NAME").unwrap();
    let lib_name = format!("{}.a", crate_name);
    let header_name = format!("{}.h", crate_name);

    let crate_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
    let header_out_path = out_dir.join(&header_name);
    let binary_dir = crate_dir.join(format!("target/{config}"));
    let lib_path = fs::read_dir(binary_dir)
        .expect("Failed to find lib files")
        .filter_map(Result::ok)
        .map(|entry| entry.path())
        .find(|path| path.extension().and_then(|s| s.to_str()) == Some("a"))
        .expect("Can't find a lib file");

    let install_base_dir = crate_dir.join("target").join("install").join(config);
    let lib_install_dir = install_base_dir.join("lib");
    let include_install_dir = install_base_dir.join("include");

    let include_install_path = include_install_dir.join(&header_name);
    let lib_install_path = lib_install_dir.join(&lib_name);

    let _ = std::fs::create_dir_all(&lib_install_dir);
    let _ = std::fs::create_dir_all(&include_install_dir);

    fs::copy(header_out_path, &include_install_path).expect("Failed to install the header file");
    fs::copy(lib_path, &lib_install_path)
        .expect("Failed to install the lib file");

    println!("Installed to {}", install_base_dir.display());
}
