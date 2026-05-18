# `include/xrpl/beast/core/CurrentThreadName.h`

This header, adapted from the JUCE framework, provides a small but operationally important facility for tagging OS threads with human-readable names. In a server process like `rippled` that runs dozens of concurrent threads — job queue workers, network I/O loops, database backends, ledger cleaners, gRPC handlers — meaningful thread names are essential for attaching debuggers, reading `top`/`htop`, and interpreting profiler output.

## Design: Two-Layer Name Storage

The implementation (in `CurrentThreadName.cpp`) maintains two parallel representations of the thread name:

1. **An in-process `thread_local std::string`** (`detail::threadName`) that `getCurrentThreadName()` reads back.
2. **The OS-level thread name** set via platform-specific calls.

The `thread_local` store is the reason `getCurrentThreadName()` returns an empty string for threads that have never called `setCurrentThreadName()` — the function reads its own thread's storage rather than interrogating the OS. This is intentional: the comment in the header notes that names set "by an external force" are intentionally invisible. The approach avoids any syscall on the read path and keeps reads fast.

## The Linux 15-Character Constraint

Linux imposes a hard 16-byte limit (including the null terminator) on thread names via `pthread_setname_np`. This header makes that constraint a compile-time concern rather than a silent runtime truncation. On Linux builds (`BOOST_OS_LINUX`), a template overload of `setCurrentThreadName` accepts only string literals:

```cpp
template <std::size_t N>
void setCurrentThreadName(char const (&newThreadName)[N])
{
    static_assert(N <= maxThreadNameLength + 1, "Thread name cannot exceed 15 characters");
    setCurrentThreadName(std::string_view(newThreadName, N - 1));
}
```

The `static_assert` fires at compile time if the literal exceeds 15 characters, turning an easy-to-miss runtime defect into a build error. Callers passing a `std::string` or `std::string_view` at runtime fall through to the base overload, which truncates silently in the implementation (and optionally emits a `std::cerr` warning when `TRUNCATED_THREAD_NAME_LOGS` is defined). The `maxThreadNameLength` constant (15) is exposed publicly so tests and other code can query the limit without magic numbers.

## Platform Implementations

The `.cpp` file routes to three platform back-ends selected by `boost/predef.h` macros:

- **macOS**: `pthread_setname_np(name.data())` — macOS's variant takes only the name (not a thread ID), so it always applies to the calling thread.
- **Linux**: `pthread_setname_np(pthread_self(), boundedName)` — the two-argument POSIX extension form, applied after truncating to `maxThreadNameLength` characters with `snprintf`.
- **Windows (MSVC debug builds only)**: Raises the documented Microsoft debugger exception `0x406d1388` with a `THREADNAME_INFO` struct. This only fires when `DEBUG` is defined and the MSVC compiler is in use, so it is a pure developer ergonomics path with no production overhead.

## Usage in `rippled`

The facility is used throughout the application layer wherever a dedicated thread is spawned: `BasicApp`, `LoadManager`, `LedgerCleaner`, `GRPCServer`, the RocksDB backend, `ResourceManager`, and others all call `setCurrentThreadName()` at thread startup. This makes it straightforward to identify which subsystem owns a thread when investigating hangs or CPU spikes.

The header is intentionally minimal: two free functions and one compile-time constant, all in `namespace beast`. There are no classes, no virtual dispatch, and no dependencies beyond `<string>`, `<string_view>`, and `<boost/predef.h>`.