# External Conan recipes

The subdirectories in this directory contain either copies or Conan recipes
of external libraries used by rippled.
The Conan recipes include patches we have not yet pushed upstream.

| Folder          | Upstream                                     | Description |
|:----------------|:---------------------------------------------|:------------|
| `antithesis-sdk`| [Project](https://github.com/antithesishq/antithesis-sdk-cpp/) | [Antithesis](https://antithesis.com/docs/using_antithesis/sdk/cpp/overview.html) SDK for C++ |
| `ed25519-donna` | [Project](https://github.com/floodyberry/ed25519-donna) | [Ed25519](http://ed25519.cr.yp.to/) digital signatures |
| `rocksdb`       | [Recipe](https://github.com/conan-io/conan-center-index/tree/master/recipes/rocksdb) | Fast key/value database. (Supports rotational disks better than NuDB.) |
| `secp256k1`     | [Project](https://github.com/bitcoin-core/secp256k1)    | ECDSA digital signatures using the **secp256k1** curve |
| `snappy`        | [Recipe](https://github.com/conan-io/conan-center-index/tree/master/recipes/snappy)  | "Snappy" lossless compression algorithm. |
| `soci`          | [Recipe](https://github.com/conan-io/conan-center-index/tree/master/recipes/soci)    | Abstraction layer for database access. |
