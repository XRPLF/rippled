# `include/xrpl/basics/MallocTrim.h`

## Role in the System

This header is a targeted memory-pressure relief valve for the XRPL node process. On long-running servers, glibc's ptmalloc allocator can accumulate free pages in its internal arenas without returning them to the OS, causing the process's Resident Set Size (RSS) to drift upward over time even when application-level memory usage has genuinely declined. `MallocTrim.h` exposes a controlled wrapper around `::malloc_trim(0)` that reclaims that slack, paired with a diagnostic report so callers can observe what the operation actually accomplished.

## `MallocTrimReport`

The `MallocTrimReport` struct is the observability surface for a single trim invocation. All fields default to sentinel values (`-1` or `false`) that signal "not populated" rather than zero, which is important because a real RSS reading or page-fault count of zero is valid and meaningful. The fields capture:

- **`supported`** — whether the current platform supports trimming at all. This lets callers distinguish "trim ran and did nothing" from "trim was never attempted."
- **`trimResult`** — the raw return value from `::malloc_trim`: `1` if the allocator actually released pages, `0` if there was nothing to release.
- **`rssBeforeKB` / `rssAfterKB`** — process RSS read from `/proc/self/statm` bracketing the trim call, in kilobytes.
- **`durationUs`** — wall-clock duration of the `::malloc_trim` call in microseconds, useful for detecting trim latency spikes.
- **`minfltDelta` / `majfltDelta`** — thread-level minor and major page fault deltas (via `RUSAGE_THREAD`) across the trim, making it possible to detect cases where trimming caused unexpected kernel re-faulting of pages.

The inline `deltaKB()` method returns the signed RSS change (negative means memory was freed, positive means RSS grew). It guards against uninitialized sentinel values by returning `0` if either RSS reading is negative.

## `mallocTrim()` Function

The function signature is:

```cpp
MallocTrimReport mallocTrim(std::string_view tag, beast::Journal journal);
```

The `tag` is a caller-supplied identifier that appears in log output, allowing multiple call sites to be distinguished in diagnostics. The actual implementation in `MallocTrim.cpp` is compiled conditionally: the entire trim path is gated on `#if defined(__GLIBC__) && BOOST_OS_LINUX`. On any other platform, the function is a no-op that returns a default-constructed report with `supported = false`.

When active, the implementation makes a deliberate performance tradeoff gated on the journal's debug severity. If debug logging is disabled, only `::malloc_trim(0)` is called and the report stays mostly at sentinel values — no `/proc` reads, no rusage calls, minimal overhead. If debug logging is enabled, the function reads `/proc/self/statm` before and after, calls `getrusage(RUSAGE_THREAD, ...)` before and after, and measures wall time. This layered instrumentation is only paid when someone is actively debugging memory behavior, which makes the function cheap enough for production call sites.

The trim padding constant is hardcoded to `TRIM_PAD = 0`. A comment in the implementation documents that this choice came from 12-hour empirical testing on Mainnet across four padding values (0, 256 KB, 1 MB, 16 MB): zero delivered the best balance of RSS reduction and trim-latency stability without adding a tuning surface. This is a non-obvious design choice that prevents the parameter from becoming a configuration footgun.

## Allocator Compatibility and Scope Limits

The header's comment block is worth reading carefully: `malloc_trim` only affects glibc's own `sbrk`-based arenas. Large allocations backed by `mmap` are returned to the OS when freed, regardless of trimming. More critically, if jemalloc or tcmalloc is linked or `LD_PRELOAD`ed, calling `::malloc_trim` from glibc's symbol is harmless but does not touch those allocators' arenas. The function has no way to detect this at runtime, so the `supported` flag is set based on compiler/OS detection only, not on whether the active allocator will actually respond.

## Call Site

The function is invoked in `Application.cpp`'s `doSweep()` method, immediately after cache sweeps and ledger cleanup:

```cpp
mallocTrim("doSweep", m_journal);
```

This is the canonical usage pattern: call at a known point after significant bulk-free operations, rather than on a timer. The header comment recommends rate-limiting calls to avoid churn, which `doSweep`'s existing sweep timer naturally provides.