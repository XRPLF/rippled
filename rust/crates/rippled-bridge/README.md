To build this project:

1. Build rippled
```shell
cd ./external/rippled/.build
conan install .. --output-folder . --build missing --settings build_type=Release
cmake -DCMAKE_TOOLCHAIN_FILE:FILEPATH=build/generators/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
```

Then build the Rust project via Cargo:
```shell
cd ./rust
cargo build
```

Then execute the rust runtime:

```shell
cd ./rust
cargo run
```