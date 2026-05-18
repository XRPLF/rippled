# `include/xrpl/beast/utility/temp_dir.h`

## Role and Purpose

`temp_dir` is a small, focused RAII wrapper that creates a unique temporary directory on construction and unconditionally removes it — along with all of its contents — on destruction. It lives in the `beast` utility layer and serves exclusively as a test infrastructure primitive throughout the XRPL codebase: unit tests that need scratch space on disk (database backends, configuration files, ledger snapshots) instantiate a `temp_dir`, use it, and let the destructor clean up without any explicit teardown code.

## Design

The constructor delegates directory selection entirely to Boost.Filesystem. It queries `boost::filesystem::temp_directory_path()` for the OS-appropriate temp root (`/tmp` on Linux, `%TEMP%` on Windows), then generates a cryptographically random path component via `boost::filesystem::unique_path()`. The `do/while` loop guards against the astronomically unlikely case where a generated name already exists before calling `create_directory`. There is no user-supplied base name or prefix — the generated path is fully opaque, which prevents accidental collisions between parallel test runs.

Copy construction and copy assignment are explicitly deleted. This is the correct choice for an ownership-of-resource type: two instances pointing at the same directory path would result in a double `remove_all` on destruction, and it would be ambiguous which object "owned" the lifetime of the files inside. Move semantics are also absent, keeping the interface minimal — callers hold the object by value and access the directory through the two accessor methods.

The destructor calls `boost::filesystem::remove_all` with a `boost::system::error_code` out-parameter rather than letting exceptions propagate. Destructors that throw cause `std::terminate` in most C++ contexts, so swallowing the error here is the only safe option. The `TODO` comment acknowledging the silenced error is honest — a failed cleanup could leave orphaned temp directories, but that is an acceptable tradeoff for destructor safety in test code.

## Accessor Interface

`path()` returns the native string representation of the directory itself, suitable for passing directly to subsystems that accept `std::string` paths (e.g., NuDB and RocksDB backend configuration via `Section::set("path", ...)`).

`file(name)` computes the path of a named entry inside the directory by appending the given name with `operator/` before converting to string. The method comment explicitly notes the file does not need to exist — this is intentional, since tests often need a pre-determined path string before actually creating the file (as seen in `Config_test.cpp`, where the path is passed to `std::ofstream` for writing).

## Usage in Tests

Every consumer follows the same idiom: declare `beast::temp_dir const td;` as a local variable in the test body, then use `td.path()` or `td.file("name")` to wire up paths. Because it is `const`, the object is immutable after construction — there is no API to rename, move, or replace the directory mid-test, reinforcing that its only job is to own a stable scratch location for the duration of a test case. When the scope exits, the destructor fires and the filesystem is cleaned up, leaving no artifacts regardless of whether the test passed or failed.