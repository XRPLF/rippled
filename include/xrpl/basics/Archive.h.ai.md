# `Archive.h` — Tar/LZ4 Archive Extraction

This header declares a single utility function within the `xrpl` namespace: `extractTarLz4`. Its purpose is narrowly scoped — providing the XRPL node software with the ability to unpack `.tar.lz4` archives to a target directory at runtime. The most natural use case is ledger database bootstrapping, where a node downloads a pre-built snapshot of the ledger state rather than replaying the entire transaction history from genesis.

## The Interface

```cpp
void extractTarLz4(
    boost::filesystem::path const& src,
    boost::filesystem::path const& dst);
```

Both parameters are `boost::filesystem::path` rather than `std::string` or `std::filesystem::path`. This is consistent with the broader `xrpl/basics` module (see `FileUtilities.h`), which predates C++17's standard filesystem library and relies on Boost.Filesystem throughout. The function throws `std::runtime_error` on any failure — there is no return value to check or error code to inspect.

## Implementation Design

The implementation in `Archive.cpp` delegates all archive I/O to **libarchive**, a portable C library (`<archive.h>`, `<archive_entry.h>`). This is a deliberate choice over rolling a custom tar/lz4 parser: libarchive handles format detection, streaming decompression, and sparse file support in a well-tested, security-audited way.

Resource management for the two libarchive handles — a reader (`ar`) and a disk writer (`aw`) — is handled via `std::unique_ptr` with custom deleters that call `archive_read_free` and `archive_write_free` respectively. This is the only safe pattern here: libarchive resources must be released even when intermediate steps throw, and wrapping them in `unique_ptr` ensures cleanup happens automatically as the stack unwinds.

The reader is configured explicitly for the tar format and the lz4 filter (rather than using libarchive's auto-detection). This prevents the function from silently accepting other archive formats, keeping the interface contract tight. The file is opened with a 10240-byte block size, which matches the canonical recommendation in libarchive documentation.

The disk writer is configured with `ARCHIVE_EXTRACT_TIME | ARCHIVE_EXTRACT_PERM | ARCHIVE_EXTRACT_ACL | ARCHIVE_EXTRACT_FFLAGS`, meaning extracted files faithfully preserve timestamps, permissions, access control lists, and BSD file flags from the archive. For a snapshot intended to be a drop-in replacement for a live ledger database directory, this fidelity matters: the consuming software may rely on mtime or permission bits being intact.

A non-obvious detail is the pathname rewriting on line 65: before writing each entry to disk, the function prepends `dst` to the entry's stored path using Boost.Filesystem's `/` operator. This is what places all extracted content under `dst` rather than at absolute paths embedded in the archive, and it prevents path traversal issues where a maliciously constructed archive might attempt to write files outside the intended directory tree.

## Error Handling

All errors are surfaced through `xrpl::Throw<std::runtime_error>`, defined in `contract.h`. Unlike a raw `throw`, `Throw` first calls `LogThrow` to capture a stack trace before the exception propagates. This means extraction failures produce actionable diagnostics in the node's log — important for diagnosing corrupted snapshots or filesystem problems during a bootstrap operation that might otherwise appear as a silent crash.

The function validates `src` is a regular file (not a directory or symlink) before opening it, providing a clear early error rather than letting libarchive fail with a less informative message.