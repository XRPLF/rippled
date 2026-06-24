# validator-keys-tool

Xrpld validator key generation tool

## Build

Use the same build process as xrpld from the repository root, enabling the
optional `validator_keys` target and building `validator-keys`:

```
mkdir .build
cd .build
conan install .. --output-folder . --build=missing --settings=build_type=Release
cmake -DCMAKE_POLICY_DEFAULT_CMP0091=NEW \
    -DCMAKE_TOOLCHAIN_FILE:FILEPATH=conan_toolchain.cmake \
    -DCMAKE_BUILD_TYPE=Release \
    -Dvalidator_keys=ON \
    ..
cmake --build . --target validator-keys
./validator-keys --unittest
```

The Conan test package in `tests/conan` builds the same target against the
exported `xrpl` package.

## Guide

[Validator Keys Tool Guide](doc/validator-keys-tool-guide.md)
