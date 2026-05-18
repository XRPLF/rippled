# `src/libxrpl/basics/Archive.cpp`

## Purpose

This file implements a single utility function, `extractTarLz4`, which decompresses and extracts a `.tar.lz4` archive from a source path to a destination directory. Its narrow scope reflects a specific operational need: XRPL nodes sometimes bootstrap their ledger databases by downloading pre-built compressed snapshots rather than replaying the full transaction history from genesis. This function provides the extraction primitive for that bootstrap path.

## Design: Two-Handle libarchive Pattern

The implementation follows the standard libarchive "copy" idiom, which requires two separate context objects: a read handle (`ar`, created with `archive_read_new()`) and a disk-write handle (`aw`, created with `archive_write_disk_new()`). This is a deliberate API design choice by libarchive — the reader is responsible for decompressing and parsing the archive format, while the disk writer handles the host filesystem semantics (permissions, timestamps, ACLs). Neither object can substitute for the other.

Both handles are wrapped in `std::unique_ptr<struct archive, void(*)(struct archive*)>` with custom deleters (`archive_read_free` and `archive_write_free` respectively). This RAII ownership model is essential: if any subsequent libarchive call throws, C++ stack unwinding will invoke the deleters automatically, preventing handle leaks without requiring explicit cleanup code in error paths.

## Extraction Pipeline

Setup happens in three phases before the extraction loop begins:

1. The reader is configured by calling `archive_read_support_format_tar()` and `archive_read_support_filter_lz4()` — explicitly narrowing the reader to the expected format rather than using auto-detect. This makes the function's purpose unambiguous and avoids accidentally accepting other archive types. The file is then opened with a 10 240-byte block size, which matches the value used in libarchive's own example code.

2. The writer is configured with four extraction flags: `ARCHIVE_EXTRACT_TIME`, `ARCHIVE_EXTRACT_PERM`, `ARCHIVE_EXTRACT_ACL`, and `ARCHIVE_EXTRACT_FFLAGS`. Together these instruct the disk writer to faithfully restore timestamps, file permissions, access-control lists, and file flags from the archive metadata — a faithful extraction rather than just content.

3. `archive_write_disk_set_standard_lookup()` enables the standard user/group name-to-UID/GID resolution, so ownership stored in the archive is remapped to the local system's user database.

The extraction loop iterates over each entry in the archive via `archive_read_next_header()`. Before writing, it rewrites the entry's stored pathname by prepending `dst` with `archive_entry_set_pathname(entry, (dst / archive_entry_pathname(entry)).string().c_str())`. This ensures every extracted path lands under the caller-specified destination directory rather than at whatever absolute or relative path the archive records. The loop then writes the header and, for entries with non-zero size, streams data with `archive_read_data_block()` / `archive_write_data_block()`. The inner data-block loop is skipped for zero-size entries (directories, symlinks, device nodes) — calling `archive_read_data_block()` on such entries would be meaningless and could produce erroneous behavior.

## Error Handling

Every libarchive call returns an integer status. The code checks each return value against `ARCHIVE_OK` (0): anything strictly less than `ARCHIVE_OK` is treated as an error; `ARCHIVE_WARN` (positive) is silently tolerated as a non-fatal advisory. `ARCHIVE_EOF` is the expected termination signal for both the entry loop and the inner data-block loop.

All errors are reported through `Throw<std::runtime_error>()`, the XRPL contract mechanism defined in `contract.h`. Unlike a plain `throw`, `Throw<>` first calls `LogThrow()` to record a stack trace, giving operators a full call chain to diagnose failures. The error message is sourced from `archive_error_string()`, which returns libarchive's own human-readable description tied to the specific handle that failed — distinguishing reader-side failures (corrupt or wrong-format archive) from writer-side failures (permission denied, disk full).

The pre-flight check `is_regular_file(src)` guards against accidentally passing a directory path or a non-existent path as the source, throwing immediately with a clear "Invalid source file" message rather than letting libarchive emit a cryptic error on open.

## Invariants and Limitations

The function throws on any failure; it has no partial-success state. If an error occurs mid-extraction, already-written files are not rolled back — the destination directory is left in a partial state. Callers are responsible for validating or cleaning up the destination if they need atomicity.

The pathname rewriting prevents obvious path injection (`dst` is always prepended), but the code does not strip or reject `..` components within the stored entry names. An adversarially crafted archive could use a path like `../../etc/cron.d/evil` to escape the destination. Given that the archives in this context are expected to be trusted first-party snapshots, this is an acceptable trade-off, but it is worth noting for any future use against untrusted sources.