# rippled Source

Some of these directories come from entire outside repositories brought in
using [git-subtree][]. This means that the source files are inserted directly
into the `rippled` repository. They can be edited and committed just as if they
were normal files.
[git-subtree]: https://github.com/apenwarr/git-subtree

If you create a commit that contains files both from a subtree, and from the
`rippled` source tree, please use care when designing the commit message, since
it will appear in the subtree's individual repository when the changes are
pushed back to the upstream. Better yet, do not mix files from subtrees and
`ripple` in the same commit at all.

Source folders:

| Folder          | Upstream Repo                                | Description |
|:----------------|:---------------------------------------------|:------------|
| `beast`         | N/A                                          | legacy utility code that was formerly associated with boost::beast
| `ed25519-donna` | https://github.com/floodyberry/ed25519-donna | [Ed25519](http://ed25519.cr.yp.to/) digital signatures |
| `ripple`        | N/A                                          | **Core source code for `rippled`** |
| `secp256k1`     | https://github.com/bitcoin-core/secp256k1    | ECDSA digital signatures using the **secp256k1** curve |
| `test`          | N/A                                          | **Unit tests for `rippled`** |

The following dependencies are downloaded and built using ExternalProject
(or FetchContent, where possible). Refer to CMakeLists.txt file for
details about how these sources are built :

| Name            | Upstream Repo                                | Description |
|:----------------|:---------------------------------------------|:------------|
| `lz4`           | https://github.com/lz4/lz4                   | LZ4 lossless compression algorithm |
| `nudb`          | https://github.com/vinniefalco/NuDB          | Constant-time insert-only key/value database for SSD drives (Less memory usage than RocksDB.) |
| `snappy`        | https://github.com/google/snappy             | "Snappy" lossless compression algorithm. |
| `soci`          | https://github.com/SOCI/soci                 | Abstraction layer for database access. |
| `sqlite`        | https://www.sqlite.org/src                   | An embedded database engine that writes to simple files. |
| `rocksdb`       | https://github.com/facebook/rocksdb          | Fast key/value database. (Supports rotational disks better than NuDB.) |
| `protobuf`      | https://github.com/google/protobuf           | Protocol buffer data interchange format. Only downloaded/built if a suitable version is not found by find_package, or if the local_protobuf option is explicitly set |

