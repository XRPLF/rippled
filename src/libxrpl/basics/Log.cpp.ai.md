# `src/libxrpl/basics/Log.cpp` — XRPL Logging System Implementation

## Role in the System

`Log.cpp` provides the concrete implementation of the XRPL node's logging infrastructure. It bridges the `beast::Journal` abstraction layer — a lightweight, copyable logging front-end — with the actual I/O facilities: an append-mode log file and `stderr`. Every subsystem in `xrpld` obtains a `beast::Journal` by calling `Logs::journal("PartitionName")`, which creates or retrieves a named channel and hands back an object cheap enough to store as a member variable and copy freely.

## Architecture: Three Collaborating Classes

### `Logs::Sink`

This private class is the glue between `beast::Journal::Sink` (the abstract write target) and the `Logs` coordinator. Each named partition owns exactly one `Sink`. When `beast::Journal::Stream` triggers a write, it calls `Sink::write()`, which applies the per-sink severity threshold gate (`level < threshold()`) before delegating to `logs_.write()`. The design keeps the threshold check close to the point of formatted string construction, avoiding unnecessary formatting work.

`writeAlways()` is the bypass path: it skips the threshold check but otherwise follows the same route. This exists to support administrative overrides where an operator needs a message emitted regardless of the current verbosity configuration.

The back-reference to `Logs&` is safe by design: `Sink` instances are owned by the `Logs` object itself via the `sinks_` map, so they cannot outlive it.

### `Logs::File`

A thin RAII wrapper around `std::ofstream`. Its most important interface is `closeAndReopen()`, which exists specifically to interoperate with Unix log-rotation tools like `logrotate(8)`. When the log daemon renames the active log file, a SIGHUP handler can call `rotate()` on the `Logs` object; the file descriptor is released and reopened at the original path, picking up the freshly created file.

`File::open()` validates the stream with `stream->good()` before committing the pointer, and `write()`/`writeln()` silently no-op if `m_stream` is null — so calling code never needs to check whether a file is configured before writing.

### `Logs` (the Coordinator)

`Logs` owns the sink registry (`sinks_`), the shared file, and the global threshold. The sink map is keyed by partition name using `boost::beast::iless`, making partition names case-insensitive at lookup time. `get()` is the lazily-creating accessor: it acquires the mutex and calls `sinks_.emplace()` — the standard library guarantees this is a no-op if the key already exists, returning the existing iterator, so there is no double-creation risk.

Setting a new threshold via `threshold(Severity)` holds the mutex and iterates all existing sinks to push the new threshold to each — a global dial. This is the mechanism behind the `logLevel` admin command on a running node.

`makeSink()` is `virtual`, which is the extension point for testing. A test harness can subclass `Logs` and override `makeSink()` to inject mock sinks without touching file I/O.

## The `format()` Function — Security Scrubbing

The most security-sensitive code in the file lives in `Logs::format()`. After assembling the timestamp, partition label, and severity abbreviation (e.g., `"NFO "`, `"WRN "`), it enforces a hard 12 KB cap on total message length and then runs a scrubber lambda over the formatted string.

The scrubber searches for specific JSON key names — `"seed"`, `"seed_hex"`, `"secret"`, `"master_key"`, `"master_seed"`, `"master_seed_hex"`, `"passphrase"` — and replaces the value between the next pair of double quotes with asterisks. This prevents sensitive wallet credentials from appearing in log files if an RPC request is accidentally logged verbatim. The key names are the exact field names used in XRPL's wallet and key-generation RPC calls (`wallet_propose`, `sign`, etc.), so the list is deliberately narrow and explicit rather than using a general-purpose PII scanner.

## Severity Translation

The file maintains a two-way mapping between the older `LogSeverity` enum (deprecated, prefixed `ls`) and `beast::severities::Severity` (prefixed `k`). The `fromSeverity()` and `toSeverity()` static methods exist purely as a compatibility bridge for code that hasn't migrated off the old enum. Both use `UNREACHABLE()` on the default branch — an assertion macro that fires in debug builds — ensuring that any future severity value added to one enum without updating the other fails loudly.

`fromString()` uses `boost::iequals` for case-insensitive parsing and accepts alias spellings (`"warn"`, `"warnings"`, `"information"`) to be forgiving of operator input from config files or admin commands.

## Thread Safety

All mutable state in `Logs` — the `sinks_` map, the threshold, and the file writes — is protected by a single `mutable mutex_`. This means `partition_severities()` (a const query) still takes the lock. The trade-off is simplicity over granularity: log writes from multiple threads serialize through one mutex, but since logging is never on the hot path for consensus or transaction processing, this is acceptable. The `Logs::File` methods are explicitly documented as *not* thread-safe; they are called only while the mutex is already held.

## The `DebugSink` / `debugLog()` Facility

The file-local `DebugSink` class wraps a `beast::Journal::Sink` reference behind a mutex, defaulting to the null sink. `setDebugLogSink()` atomically swaps in a new sink and returns the old one — a clean ownership-transfer idiom using `std::swap` on `unique_ptr`. This global injectable debug journal lets tests redirect debug output without any changes to production code paths. The header explicitly warns that `debugLog()` may write to a null sink, making it unsuitable for anything that must be observed.