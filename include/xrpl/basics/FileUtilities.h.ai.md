# `include/xrpl/basics/FileUtilities.h`

This header declares two thin file I/O utilities — `getFileContents` and `writeFileContents` — that form the XRPL codebase's standard interface for synchronous file access. The design problem they solve is not the I/O itself, but the error-handling contract: the broader rippled codebase avoids exceptions in many subsystems and instead relies on `boost::system::error_code` for structured, non-throwing error propagation. These two functions provide a consistent, exception-free surface for the handful of places in the node that must read or write files.

## Interface Design

Both functions follow the same convention: an `error_code&` output parameter is the first argument, populated on failure while the function returns an empty result (or returns nothing for the write case). This is the classic Boost.Asio-style error-out-parameter pattern, chosen over exception-throwing I/O because the callers — configuration loading, validator list file reads, test scaffolding — operate in contexts where an error is a recoverable condition requiring a structured diagnostic path rather than a stack unwind.

`getFileContents` takes a `boost::filesystem::path` and an optional `std::size_t` upper bound. The `std::optional<std::size_t> maxSize` parameter is the key safety valve: it allows callers to cap memory usage before any bytes are read into a `std::string`. When absent, the file is read in full. When present, the function checks the on-disk file size before opening the stream and returns `file_too_large` immediately if the limit is exceeded. This pre-check is cheap and prevents unbounded allocation when reading untrusted or potentially large files.

`writeFileContents` takes a `boost::filesystem::path` and the string to write. It opens with `std::ios::out | std::ios::trunc`, guaranteeing the destination file is replaced atomically from the content's perspective — no partial appends. The function does not check or create intermediate directories; the caller is responsible for ensuring the destination path exists.

## Implementation Notes (from the `.cpp`)

`getFileContents` calls `boost::filesystem::canonical()` before doing anything else. This resolves symlinks and relative components into an absolute, normalized path, ensuring the subsequent `file_size()` check and stream open operate on the same physical file. Calling `canonical()` with the `ec` overload also intercepts path resolution errors (non-existent file, permission denied) through the same error-code channel rather than a filesystem exception.

After path resolution, the implementation reads the file via a `std::istreambuf_iterator` range construction directly into a `std::string`. This is idiomatic C++ for slurping a whole file but has a subtle implication: for text-mode streams on some platforms, newline translation may occur. The stream is opened in `std::ios::in` (text mode), consistent with the intended use cases — TOML/JSON configuration and validator list JSON — where the content is human-readable text rather than binary data.

Error checking is done at three points: path resolution failure, pre-open size check, and post-read `fileStream.bad()`. The `bad()` check (not `fail()`) specifically catches I/O errors during reading, not logical stream state issues, which is the correct guard for a hardware or OS-level read failure mid-stream.

## Callers in Context

The three primary call sites reveal the intended use scope:

- `src/xrpld/core/detail/Config.cpp` uses `getFileContents` twice: once to load the main configuration file and once to load the validators file specified within that config. These are startup-time reads on the main thread, where a missing file is a fatal misconfiguration.
- `src/xrpld/app/misc/detail/WorkFile.h` uses `getFileContents` with a hard cap of `megabytes(1)` to read validator list files fetched from the network. The 1 MB cap is a deliberate denial-of-service defense against a maliciously large or corrupted file consuming unbounded memory.
- `src/xrpld/app/misc/detail/ValidatorList.cpp` uses `writeFileContents` to persist the current validator list as styled JSON after an update.

The `maxSize` parameter's real motivation is visible in the `WorkFile` usage: without it, a 4 GB file at a validator list URL would allocate 4 GB of heap before the caller could inspect the error. The pre-check using `file_size()` is a TOCTOU (time-of-check/time-of-use) race in theory, but in practice the files involved are either local config files or freshly downloaded files in a controlled temp location, making the race window negligible.

## Relationship to the `basics` Module

Within `include/xrpl/basics/`, this header occupies the narrowest role: it is a leaf utility with no dependencies on other XRPL types. It depends only on Boost.Filesystem and `<optional>`, making it safe to include anywhere in the stack without pulling in heavier XRPL headers. The `ByteUtilities.h` header (which provides `kilobytes()` and `megabytes()`) is the natural companion when callers need to express size limits in readable units, as the test suite and `WorkFile` both demonstrate.