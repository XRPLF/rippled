# `src/libxrpl/basics/MallocTrim.cpp`

## Purpose and Context

Long-running server processes like rippled accumulate fragmented free memory inside glibc's ptmalloc arena — pages that have been freed by the application but not yet returned to the kernel. The OS still charges this memory against the process's Resident Set Size (RSS), making the process appear to consume far more memory than it actually needs. `MallocTrim.cpp` implements a targeted call to `::malloc_trim(0)` that explicitly requests glibc to consolidate its arenas and return any eligible free pages back to the OS. The public entry point `mallocTrim()` is called from `Application::doSweep()`, the periodic ledger-maintenance sweep that runs after cache evictions and other operations that free significant amounts of memory.

## Platform Gating

The entire implementation is guarded by `#if defined(__GLIBC__) && BOOST_OS_LINUX`. On any other platform — macOS, Windows, or Linux with an alternative allocator preloaded — the function is a documented no-op that returns a `MallocTrimReport` with `supported = false`. The compile-time `#error` directive for `RUSAGE_THREAD` enforces an additional invariant: Linux/glibc builds that somehow lack per-thread resource accounting are rejected outright rather than silently degrading. This is a deliberately hard boundary; thread-scoped page-fault measurement is non-negotiable to the diagnostic design.

## The `TRIM_PAD = 0` Decision

`malloc_trim(pad)` accepts a padding argument that leaves `pad` bytes of free space unreleased in the arena, potentially reducing future syscall overhead on re-allocation. The constant `TRIM_PAD` is hardcoded to `0` with an inline comment noting that 12-hour Mainnet testing across four candidate values (0, 256 KB, 1 MB, 16 MB) showed no consistent RSS benefit from non-zero padding. Setting it to zero avoids introducing an opaque tuning surface while achieving the best observed balance of reclamation and latency stability. The comment is an intentional data trail for future maintainers who might revisit the decision.

## Two-Mode Execution

`mallocTrim()` branches on whether `journal.debug()` logging is active:

**Without debug logging**: The code executes a bare `detail::mallocTrimWithPad(0)` — a single-line wrapper around `::malloc_trim`. No `/proc` I/O, no clock calls, no `getrusage` overhead. This is the production hot path.

**With debug logging**: The function becomes a full instrumentation harness. Before calling `malloc_trim`, it reads `/proc/self/statm` as a raw string and calls `detail::parseStatmRSSkB()` to extract the resident-page count (the second whitespace-delimited field), converting pages to kilobytes via `sysconf(_SC_PAGESIZE)`. It also captures a `RUSAGE_THREAD` snapshot via `getrusage()`. After the trim call, it reads `/proc/self/statm` again and takes a second `getrusage` snapshot, then logs the before/after RSS, the RSS delta in KB, the trim duration in microseconds (via `std::chrono::steady_clock`), and the minor/major page-fault deltas. All of this is gated at runtime on debug logging being enabled, so operators can enable diagnostics without recompilation.

## Defensive Data Handling

Each piece of external data is treated skeptically:

- `readFile()` checks `ifs.is_open()` before reading, returning an empty string on failure rather than throwing.
- `parseStatmRSSkB()` uses `std::istringstream` extraction that sets `failbit` on malformed input, returning `-1` on parse failure. It also validates that `sysconf(_SC_PAGESIZE)` returns a positive value before performing the page-to-KB multiplication.
- `getRusageThread()` checks the `getrusage` return value. The boolean `have_ru0` / `have_ru1` flags gate the page-fault delta computation, so a failed `getrusage` call simply leaves those fields at their default `-1` sentinel values rather than producing garbage deltas.

The `MallocTrimReport` struct uses `-1` as the sentinel for "not available" across all numeric fields, and `deltaKB()` returns `0` rather than a misleading negative number when either RSS reading failed.

## Relationship to the Header

`MallocTrimReport` is defined entirely in `MallocTrim.h`. Notably, the struct's `deltaKB()` convenience method is `[[nodiscard]]` and `noexcept`, making it safe to call in performance-sensitive paths and impossible to silently discard the return value. The header also carries an explicit allocator-interaction note warning that alternative allocators (jemalloc, tcmalloc) make this call a no-op on the active heap, and that mmap-backed large allocations are already returned to the OS on `free()` regardless — scoping the promise the facility actually makes.

## LCOV Exclusions

The entire `mallocTrim()` body and the `getRusageThread()` helper are excluded from code-coverage analysis via `LCOV_EXCL_START` / `LCOV_EXCL_STOP` and `LCOV_EXCL_LINE`. The tests in `src/tests/libxrpl/basics/MallocTrim.cpp` exercise the public interface but the underlying platform syscalls are inherently environment-dependent and produce unreliable coverage numbers in CI, so they are intentionally excluded rather than obscuring the overall coverage metric.