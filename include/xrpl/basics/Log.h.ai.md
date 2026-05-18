# `include/xrpl/basics/Log.h` — Logging Infrastructure

## Role in the System

`Log.h` is the entry point for all structured logging in the XRPL C++ server. It defines the central `Logs` class, which acts as a factory and registry for named logging partitions, manages the output file, and controls severity thresholds. Every component in the server — consensus, ledger management, networking, transaction processing — acquires a `beast::Journal` handle from `Logs`, then uses that handle to write messages at the appropriate severity level without coupling to the output destination.

The header sits at the boundary between xrpl's own types and the `beast` logging primitives it builds on. `beast::Journal` (from `xrpl/beast/utility/Journal.h`) is the lightweight, copyable handle that individual subsystems carry; `Logs` is the stateful root that backs all those handles.

## The `beast::Journal` Layer

Before understanding `Logs`, it helps to understand what it produces. A `beast::Journal` is a non-owning, copyable pointer to a `beast::Journal::Sink`. The `Sink` is an abstract base providing two key operations: `write()` (filtered by threshold) and `writeAlways()` (bypasses the threshold check). The `Journal` itself offers named severity accessors — `trace()`, `debug()`, `info()`, `warn()`, `error()`, `fatal()` — each returning a `Journal::Stream` that implements `operator bool()` returning whether the level is currently active.

This architecture makes cheap caller-side checks possible: every `Stream` can be tested for activity before any string formatting happens.

## The `JLOG` Macro and Lazy Evaluation

The `JLOG` macro is the primary idiom for logging throughout the codebase:

```cpp
JLOG(j.warn()) << "Computed value: " << expensiveFunction();
```

Expanding to an `if (!x) {} else x` pattern, this ensures that if the `warn()` stream is below threshold, the `<<` chain is never evaluated. This is architecturally significant: without it, even a disabled `trace()` call would still serialize all arguments into a string, burning CPU in a hot path. The macro rather than an inline function avoids any overhead whatsoever at the disabled level — the compiler simply skips the branch. `CLOG` is a similar guard for optional pointer-style stream objects (used when the stream may be null).

## The `Logs` Class

`Logs` is constructed once per process with an initial severity threshold and acts as the single routing point for all log output. It maintains a `std::map<std::string, unique_ptr<Sink>>` keyed by partition name (subsystem label), with a `boost::beast::iless` comparator for case-insensitive lookup. This means code can request `"Ledger"` or `"ledger"` and get the same sink.

### Partition Sinks

The inner `Logs::Sink` class extends `beast::Journal::Sink`, carrying a partition name and a back-reference to its parent `Logs`. When `write()` is called on a sink, it delegates immediately to `Logs::write()`, which holds the shared `mutex_`, formats the message, writes to the log file, and — unless `silent_` is set — writes to `std::cerr`. The indirection through `Logs::write()` is what makes it possible to serialize all partition output through one lock and one file handle.

Callers acquire sinks via `Logs::get()` or `Logs::journal()`. The `get()` implementation uses `emplace()` on the map: if the partition already exists the existing sink is returned, otherwise a new one is created via the virtual `makeSink()` factory method. Because `makeSink()` is virtual, tests can subclass `Logs` to inject mock sinks without touching the rest of the system.

Changing the global threshold via `Logs::threshold(Severity)` updates `thresh_` and then iterates all existing sinks to propagate the change. This means a run-time config reload can silence verbose partitions or turn on trace output without restarting the server.

### File Output and Log Rotation

The nested `Logs::File` class wraps a `std::ofstream` opened in append mode. The design is intentionally minimal: `open()`, `close()`, `write()`, `writeln()`. The critical method is `closeAndReopen()`, which is the POSIX log rotation idiom: external tools like `logrotate(8)` rename the live log file and then send a signal to the server; the server responds by calling `Logs::rotate()`, which closes its file handle and reopens the path, creating a fresh file at the original location. The `File` class stores `m_path` specifically to enable this reopen without any external coordination.

### Message Formatting and Security Scrubbing

`Logs::format()` — a private static method called unconditionally before any write — prepends a timestamp and renders the severity as a three-letter tag (`TRC`, `DBG`, `NFO`, `WRN`, `ERR`, `FTL`), followed by the partition name. It then truncates messages exceeding 12 KB to prevent runaway log lines.

Critically, `format()` also performs **sensitive data redaction**. After composing the full output string, it scans for JSON keys `"seed"`, `"seed_hex"`, `"secret"`, `"master_key"`, `"master_seed"`, `"master_seed_hex"`, and `"passphrase"`, and replaces the associated quoted values with asterisks. This is a defensive measure to prevent operator errors from leaking wallet secrets into log files — even if a developer mistakenly logs a raw JSON RPC request containing signing keys.

## Deprecated `LogSeverity` Enum

The `LogSeverity` enum (`lsTRACE`, `lsDEBUG`, etc.) is a legacy mapping that predates the `beast::severities::Severity` enum. It is marked deprecated but kept for backwards compatibility, with `fromSeverity()` and `toSeverity()` providing the translation. The `fromString()` method accepts several aliases (`"warn"`, `"warning"`, `"warnings"`) using case-insensitive comparison, which serves the config file parser.

## Debug Logging

`setDebugLogSink()` and `debugLog()` provide a secondary, process-global logging channel backed by a thread-safe `DebugSink` Meyers singleton (defined in `Log.cpp`). This channel defaults to a null sink and is primarily for test code that wants to capture log output without constructing a full `Logs` instance. The documentation explicitly warns that output from `debugLog()` may never be seen — it is unsuitable for any information that matters operationally.

## Thread Safety

All mutable state in `Logs` — the sink map, the file handle, and the threshold — is protected by a single `mutable mutex_`. `beast::Journal` and `Stream` objects are copyable and safe to use across threads because they only dereference a `Sink*` for reads. The `DebugSink` in `Log.cpp` has its own separate mutex for swapping the debug sink atomically while concurrent `debugLog()` calls may be in flight.