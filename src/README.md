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
| `beast`         | https://github.com/boostorg/beast            | Cross-platform library for WebSocket and HTTP built on [Boost.Asio](https://think-async.com/Asio) |
| `ed25519-donna` | https://github.com/floodyberry/ed25519-donna | [Ed25519](http://ed25519.cr.yp.to/) digital signatures |
| `lz4`           | https://github.com/lz4/lz4                   | LZ4 lossless compression algorithm |
| `nudb`          | https://github.com/vinniefalco/NuDB          | Constant-time insert-only key/value database for SSD drives (Less memory usage than RocksDB.) |
| `protobuf`      | https://github.com/google/protobuf           | Protocol buffer data interchange format. Ripple has changed some names in order to support the unity-style of build (a single .cpp added to the project, instead of linking to a separately built static library). |
| `ripple`        | N/A                                          | **Core source code for `rippled`** |
| `rocksdb2`      | https://github.com/facebook/rocksdb          | Fast key/value database. (Supports rotational disks better than NuDB.) |
| `secp256k1`     | https://github.com/bitcoin-core/secp256k1    | ECDSA digital signatures using the **secp256k1** curve |
| `snappy`        | https://github.com/google/snappy             | "Snappy" lossless compression algorithm. (Technically, the source is in `snappy/snappy`, while `snappy/` also has config options that aren't part of the upstream repository.) |
| `soci`          | https://github.com/SOCI/soci                 | Abstraction layer for database access. |
| `sqlite`        | https://www.sqlite.org/src                   | An embedded database engine that writes to simple files. (Technically not a subtree, just a direct copy of the [SQLite source distribution](http://sqlite.org/download.html).) |
| `test`          | N/A                                          | **Unit tests for `rippled`** |
