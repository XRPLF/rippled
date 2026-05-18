# `beast/utility/Journal.h` — Lightweight Runtime-Configurable Logging Facade

## Role in the System

`Journal.h` defines the core logging abstraction used throughout `rippled`. Every subsystem — consensus, the ledger, RPC, peerfinder, transaction processing — receives a `beast::Journal` to emit diagnostic output. The header is entirely self-contained except for `instrumentation.h` (which provides `XRPL_ASSERT`), and it purposefully exposes no dependency on any particular log backend or output format. The design goal stated in the source comment says it all: be lightweight enough to copy by value, leave logging calls in source code permanently, and control them entirely at runtime via a severity threshold.

## The `Sink` Contract

`Journal::Sink` is the abstract output destination. It stores two pieces of state — a severity threshold (`thresh_`) and a boolean console flag — in a protected base; subclasses inherit this state and override the two pure virtual methods `write()` and `writeAlways()`. The distinction between these two methods is intentional: `write()` is expected to silently discard messages below the threshold, while `writeAlways()` bypasses the filter but must still format the output identically. This is used in the XRPL codebase to emit certain critical startup or diagnostic messages unconditionally without coupling the call site to any particular threshold decision.

The `static_assert` block immediately following the `Sink` declaration enforces a strict value-semantics contract at compile time: `Sink` is neither default-constructible, copy-constructible, move-constructible, copy-assignable, nor move-assignable. This intentional immobility means every concrete sink must be created at a known lifetime scope (typically owned by the `Logs` manager) and shared only via raw reference or pointer — preventing accidental copies of a stateful I/O object.

The null sink returned by `Journal::getNullSink()` is a Meyer's-singleton `NullJournalSink`, defined in `beast_Journal.cpp`. Its `active()` always returns `false`, all write methods are no-ops, and it is initialized with `kDisabled` threshold. This singleton ensures that a `Journal::Stream` constructed with no explicit sink (its default constructor) always has a valid, safe sink reference — eliminating any possibility of null-pointer dereference in the hot logging path.

## `Stream`: The Severity-Tagged Logging Handle

`Journal::Stream` is a thin, copyable wrapper around a `(Sink&, Severity)` pair. Methods like `j.info()` or `j.warn()` return a `Stream` by value. The active check — `m_sink.active(m_level)` — is deliberately inlined in the header so the compiler can inline the single comparison `level >= thresh_`. When a log message is below threshold, the entire call resolves to a boolean branch that the optimizer can collapse.

`Stream` provides `operator bool()` which maps to `active()`. This powers the `JLOG` macro defined in `Log.h`:

```cpp
#define JLOG(x) if (!x) {} else x
```

When `x` is a `Stream`, the `if (!x)` guard prevents the entire right-hand side of `JLOG(j.debug()) << expensiveFormatCall()` from being evaluated when the stream is inactive. This is the primary performance mechanism: formatting can involve string concatenation, hex encoding, or JSON serialization, none of which should run for suppressed messages.

## `ScopedStream`: RAII Ostream Buffer

`Journal::ScopedStream` is a temporary RAII object created when `operator<<` is first invoked on a `Stream`. It holds a `std::ostringstream` that accumulates everything chained together on a single logging statement. In its destructor it calls `m_sink.write(m_level, s)` — flushing the fully assembled string to the sink in one call. The destructor also handles the degenerate case where the buffered content is exactly `"\n"` (a bare newline with no preceding text), writing an empty string instead to avoid log backends emitting a blank formatted line.

Both the `ScopedStream` constructor variants apply `std::boolalpha` and `std::showbase` to the underlying `ostringstream`. This means booleans always format as `true`/`false` (not `1`/`0`) and numeric bases are always annotated, which simplifies debugging without per-callsite formatting flags.

`ScopedStream` is copy-constructible (needed for template deduction paths) but not assignable — the destructor must fire exactly once and at the right point.

## The Three-Layer Architecture

In practice, three layers compose the logging system:

1. **`beast::Journal`** — the lightweight value-typed facade passed by value into subsystems. Holds a raw pointer to a `Sink` (non-owning). The invariant `m_sink always points to a valid Sink` is documented in the source and guaranteed by construction — there is no default constructor, and the null sink covers the "no output" case.

2. **`beast::Journal::Sink`** (concrete: `Logs::Sink` in `basics/Log.h`) — owns the threshold state and routes formatted messages into the `Logs` file and console subsystem. Each named partition (`"Ledger"`, `"RPC"`, `"PeerFinder"`, etc.) gets its own `Sink` instance with an independent severity threshold. The `Logs` class stores these as `std::unique_ptr<Sink>` in a case-insensitive `std::map`, and its `journal(name)` method creates a `Journal` pointing into that map.

3. **`JLOG` / `CLOG` macros** — the call-site guards that avoid evaluation of arguments when the stream is inactive.

## `basic_logstream`: Standard-Stream Bridge

`basic_logstream<CharT>` (aliased as `logstream` and `logwstream`) adapts a `Journal::Stream` into a `std::basic_ostream`. The underlying `detail::logstream_buf` overrides `sync()` to flush the buffer's accumulated string via the `Journal::Stream`. This is useful in contexts where a third-party interface accepts only a `std::ostream&` but the code wants all output routed through the journal system. The buffer's destructor also calls `sync()`, ensuring nothing is lost on scope exit.

## Design Decisions Worth Noting

Keeping `Journal` as a thin pointer wrapper (8 bytes on a 64-bit system) means it can be freely passed by value into lambdas, coroutines, and deeply nested call chains without any allocation or reference-counting overhead. The alternative — passing `Sink&` directly — would expose implementation details and prevent the convenient severity-accessor API (`j.warn()`, `j.error()`, etc.).

The dual `write`/`writeAlways` interface on `Sink` provides an escape hatch without forcing call sites to manipulate threshold state directly, which would introduce a thread-safety race in the common case where thresholds are only written by the configuration thread and read by worker threads. Concrete `Sink` implementations are responsible for their own thread safety when `write()` is called concurrently.