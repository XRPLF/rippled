# `src/libxrpl/basics/FileUtilities.cpp`

This file implements two thin utility functions — `getFileContents` and `writeFileContents` — that provide the XRPL codebase with a consistent, error-code-based interface for whole-file I/O. Both functions live in the `xrpl` namespace and are declared in `include/xrpl/basics/FileUtilities.h`.

## Why This Exists

XRPL's server startup and runtime both need to load files from disk: the node configuration (`Config.cpp`), the trusted validator list (`ValidatorList.cpp`), and file-based validator site responses (`WorkFile.h`). Rather than scattering ad-hoc `std::ifstream` boilerplate throughout those callers — each with slightly different error handling — this module centralises the pattern. It also gives callers a uniform `boost::system::error_code` output rather than requiring them to handle both C++ exceptions and POSIX errno.

## `getFileContents`

The read path is deliberately layered. Four distinct checks gate progress before a single byte is read into the result string:

1. **`boost::filesystem::canonical(sourcePath, ec)`** — resolves the path to an absolute, symlink-free canonical form and verifies that the path exists. Failures (non-existent file, broken symlink, permission denied during resolution) set `ec` and return an empty string immediately. This is the most defensive step: using the canonical path for all subsequent operations ensures there is no TOCTOU race where the path could be swapped between the existence check and the open.

2. **`file_size(fullPath, ec)` against `maxSize`** — when the caller supplies an `std::optional<std::size_t>` upper bound, the file size is checked before any read attempt. On excess size the function injects `boost::system::errc::file_too_large` into `ec` and returns. This is significant: `file_too_large` is not an errno that most operating systems would naturally produce for an `fread`, so injecting it explicitly makes callers' error-checking code unambiguous. `WorkFile::run()` enforces a hard 1 MB cap this way.

3. **`std::ifstream` construction and `!fileStream` check** — even if `canonical` succeeded and the file size is within bounds, the actual open can fail (e.g., due to permission changes between the two calls). On failure, `errno` is cast to `boost::system::errc_t` and wrapped into `ec`. This cast is sound because Boost defines its `errc` values to map directly to POSIX errno values.

4. **`fileStream.bad()` post-read check** — `std::istreambuf_iterator` reads the entire file in a single expression. `bad()` detects hardware errors or stream corruption that occurred during that read. EOF is not an error here; `bad()` only triggers on actual I/O failures.

## `writeFileContents`

The write path is simpler and intentionally asymmetric with the read path. Notably, it **does not call `canonical()`**. This is the correct choice: `canonical()` requires the path to exist, but the destination file is often being created for the first time (e.g., writing a cached validator list). Instead, `writeFileContents` opens directly with `std::ios::out | std::ios::trunc`, which creates or truncates as needed. The same two-stage error pattern applies: check `!fileStream` on open, then check `fileStream.bad()` after writing.

`std::ios::trunc` means the write is a full replacement — there is no atomic rename/replace-then-swap pattern here. Callers that need crash-safe writes must handle that at a higher level.

## Error Reporting Contract

Both functions communicate errors exclusively through the `boost::system::error_code& ec` output parameter. Neither throws. This matches the broader XRPL convention for low-level operations where the caller — not the callee — decides whether an error is fatal. Config loading converts a non-zero `ec` into a thrown `std::runtime_error`; `ValidatorList` also throws; `WorkFile` passes `ec` directly to its async callback. The same two functions serve all three idioms without any special-casing.

## Resource Management

Both `std::ifstream` and `std::ofstream` are stack-allocated local variables, so file handles are unconditionally released when the function returns, even on early-exit error paths. No explicit `close()` call is needed or present.

## Test Coverage

`FileUtilities_test.cpp` exercises the happy path (no size cap), a permissive size cap (1 KB on a 44-byte file), and the rejection case (`maxSize = 16`, expecting `errc::file_too_large`). `writeFileContents` is used as test setup but has no dedicated failure tests. Edge cases such as permission errors, broken symlinks, and partial-write failures are not covered by the unit suite.