use std::{env};
use std::{path::PathBuf};

fn main() {
    let crate_name = env::var("CARGO_PKG_NAME").unwrap();
    let header_name = format!("{}.h", crate_name);

    let crate_dir = PathBuf::from(env::var("CARGO_MANIFEST_DIR").unwrap());
    let out_dir = PathBuf::from(env::var("OUT_DIR").unwrap());
    let header_path = out_dir.join(header_name);

    cbindgen::generate(&crate_dir)
        .expect("Unable to generate bindings")
        .write_to_file(header_path);
}
