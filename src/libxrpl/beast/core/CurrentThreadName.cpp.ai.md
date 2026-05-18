# `beast/core/CurrentThreadName.cpp` — Cross-Platform Thread Naming

## Purpose

This file provides the implementation of thread naming utilities for the XRPL codebase. Thread naming is a debugging and observability primitive: once a name is set, it appears in debuggers (Visual Studio, GDB/LLDB), profilers, and OS-level tools (`top`, `htop`, `ps`, Activity Monitor). Without it, all XRPL threads would appear as anonymous worker threads, making production diagnostics and crash investigation significantly harder.

The file lives in the Beast utility layer, a legacy dependency within rippled originally derived from the JUCE framework. It pairs tightly with `CurrentThreadName.h`, which declares the public API and the Linux-specific `maxThreadNameLength` constant.

## Architecture: One Interface, Three OS Backends

The design follows a classic platform-dispatch pattern: a single public API (`setCurrentThreadName` / `getCurrentThreadName`) delegates to a hidden `beast::detail::setCurrentThreadNameImpl` whose body is selected at compile time via `#if BOOST_OS_WINDOWS` / `#if BOOST_OS_MACOS` / `#if BOOST_OS_LINUX` guards using Boost.Predef macros. Because each platform block defines the same function name in `namespace beast::detail`, exactly one definition is compiled into any given build, keeping the linker happy without any vtable or function pointer overhead.

## Thread-Local Name Storage

Regardless of platform, the name is always stored in `thread_local std::string detail::threadName`. This matters because `getCurrentThreadName()` simply reads that variable — it never queries the OS. This design choice avoids the complexity and potential failure modes of reverse-querying the OS (e.g., `pthread_getname_np` on Linux, which requires a fixed-size buffer), and it means the name returned is always exactly what was passed to `setCurrentThreadName`, never a silently-truncated OS copy. The tradeoff is that names set by external tools or the OS itself (not through this API) are invisible to `getCurrentThreadName()`, a documented limitation in the header.

## Platform-Specific Implementations

**Windows** (debug + MSVC only): Uses the classic structured exception trick documented by Microsoft — raising exception `0x406d1388` with a `THREADNAME_INFO` payload. This works because the Visual Studio debugger intercepts that specific exception code and reads the thread name from the payload. The implementation is guarded by `#if DEBUG && BOOST_COMP_MSVC`, so it compiles away in release builds or under non-MSVC compilers. The `#pragma pack(push, 8)` ensures the struct layout matches what the debugger expects precisely.

**macOS**: Calls `pthread_setname_np(name.data())` — the one-argument Darwin variant, which names only the calling thread (unlike the POSIX two-argument form). A clang-tidy suppression comment (`NOLINT(bugprone-suspicious-stringview-data-usage)`) is present because the linter flags `std::string_view::data()` as potentially non-null-terminated; here the safety invariant is enforced by the caller who always passes a null-terminated string (either a string literal or a `std::string`).

**Linux**: The most defensive implementation. Linux enforces a hard kernel limit of 16 bytes (including null terminator) for thread names via `pthread_setname_np`. Names exceeding 15 characters cause `pthread_setname_np` to return `ERANGE` and silently fail to set the name at all. To prevent this, the implementation manually truncates using `std::snprintf` into a stack buffer of `maxThreadNameLength + 1` bytes before calling `pthread_setname_np(pthread_self(), boundedName)`. The two-argument form is needed on Linux (unlike macOS). If the optional `TRUNCATED_THREAD_NAME_LOGS` macro is defined at build time, a warning is emitted to `std::cerr` when truncation occurs, aiding developers who may not realize their thread names are being silently clipped.

The header complements this at compile time: a template overload of `setCurrentThreadName` for `char const (&)[N]` adds a `static_assert(N <= maxThreadNameLength + 1, ...)`, catching oversized string literals at compile time rather than silently truncating at runtime. The runtime truncation in the `.cpp` handles the `std::string_view` overload where the length is not known at compile time.

## Call Flow

```
setCurrentThreadName(name)
  ├─ detail::threadName = name          // thread-local store, always complete
  └─ detail::setCurrentThreadNameImpl(name)  // platform-specific OS notification
```

`getCurrentThreadName()` reads only from `detail::threadName` — it never calls the OS.

## Test Coverage

`beast_CurrentThreadName_test.cpp` verifies two key properties: (1) thread-local isolation — two concurrently-named threads retain their individual names without cross-contamination, and (2) Linux truncation correctness — `pthread_getname_np` is used to read back the kernel-visible name and confirm it matches the expected (possibly truncated) value. The test does not cover Windows at all, reflecting the difficulty of unit-testing the debugger-exception approach.