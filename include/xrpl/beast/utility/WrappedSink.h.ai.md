# `WrappedSink.h` — Prefixing Decorator for Journal Sinks

`WrappedSink` is a small but widely-used decorator in the XRPL logging infrastructure. Its sole purpose is to prepend a fixed string to every log message before forwarding the message to an underlying `Journal::Sink`, letting call sites tag log output with contextual identifiers (peer IP addresses, transaction hashes, slot identifiers, etc.) without changing any of the sink's behavioral policies.

## The Decorator Pattern and Its Dual Inheritance

The class comment captures the design concisely:

> A WrappedSink both *is* a Sink and *has* a Sink.

`WrappedSink` inherits from `beast::Journal::Sink` so it satisfies the `Sink` interface and can be passed anywhere a `Sink&` or used to construct a `Journal`. Simultaneously, it holds a *reference* to another `Sink` — the real, concrete destination — and delegates every virtual method to that reference.

The consequence is deliberate and notable: the data members inherited from `Sink` (namely `thresh_` and `m_console`, the threshold severity and the console-output flag) are **never used**. They exist in the base class subobject because the `Sink(Sink const&)` copy constructor is called during `WrappedSink`'s initialization (`Sink(sink)` in the member-initializer list), but every query for threshold, active status, or console state immediately forwards to `sink_`. This means `WrappedSink` always reflects the live state of the underlying sink: if a system-level config change raises the threshold on the real sink after a `WrappedSink` has been constructed, the `WrappedSink` picks up the change automatically — something a value-copying approach would break.

## Write Path

Only two methods deviate from pure delegation: `write()` and `writeAlways()`. Both prepend `prefix_` to the text argument before passing it down:

```cpp
void write(beast::severities::Severity level, std::string const& text) override {
    sink_.write(level, prefix_ + text);
}
```

The distinction between `write()` and `writeAlways()` is preserved faithfully: `write()` is subject to the underlying sink's active threshold (the sink will silently drop messages below the threshold), while `writeAlways()` bypasses that check. `WrappedSink` is careful to call the *matching* method on `sink_` rather than routing both through `write()`, which would have incorrectly suppressed forced-output messages.

## Constructors

Two constructors are provided. The primary one accepts a `beast::Journal::Sink&` and a prefix string. A convenience overload accepts a `beast::Journal const&` and immediately extracts the sink via `journal.sink()`, delegating to the first constructor — this saves callers from having to unwrap the journal themselves when they already have a `Journal` in scope.

The prefix defaults to an empty string, making a zero-prefix `WrappedSink` valid (though in practice the interesting use case is always a non-empty prefix). The prefix can also be changed at any time via the `prefix(const std::string&)` mutator.

## Ownership and Lifetime

`WrappedSink` holds a non-owning reference (`sink_`). Callers are responsible for ensuring the referenced sink outlives the `WrappedSink`. This is safe in practice because sinks are long-lived application objects owned by log managers or application-level infrastructure that outlive any individual peer or subsystem. The reference semantics avoid shared ownership overhead while keeping the interface clean.

## Usage Patterns in the Codebase

The dominant usage pattern is per-connection context tagging. In `PeerImp.h`, each network peer object owns two `WrappedSink` members (`sink_` and `p_sink_`) that wrap the shared overlay journal with a per-peer prefix, then vend those as `Journal` instances. In `PeerFinder::Logic`, temporary `WrappedSink` instances are stack-allocated at the top of each method to tag log messages with the relevant slot's prefix for the duration of a single operation. `Transactor.h` follows the same pattern to annotate transaction-processing log output with a transaction-specific identifier.

This pattern keeps the core `Journal`/`Sink` interface immutable and free of prefix concerns, while letting higher-level components inject contextual tagging without coordinating with the logging backend.