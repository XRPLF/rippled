# `NullCollector.h` — No-Op Metrics Collector

`NullCollector` is the Null Object implementation of the `beast::insight::Collector` interface. Its purpose is to allow all production code that depends on metric collection to work unchanged when no external metrics backend is configured. Rather than sprinkling null checks throughout the codebase, callers always hold a valid `Collector::ptr` and call `make_counter()`, `make_gauge()`, etc. normally — with `NullCollector`, those calls simply do nothing.

## Role in the Insight Subsystem

The `beast::insight` module provides a thin abstraction over time-series metrics. The `Collector` base class declares virtual factory methods for five metric types: `Hook`, `Counter`, `Event`, `Gauge`, and `Meter`. There are exactly two concrete implementations: `StatsDCollector`, which ships metrics over UDP to a StatsD server, and `NullCollector`, which discards everything silently.

`NullCollector` is selected at startup by `CollectorManager` in `src/xrpld/app/main/CollectorManager.cpp`. When the `[insight]` configuration section either omits `server` or sets it to anything other than `"statsd"`, `NullCollector::New()` is called and the resulting pointer flows through the entire application as the active collector. This makes metrics optional at the configuration level with zero code-path divergence in the components being monitored.

## Header Design

The header is intentionally minimal: a class declaration that publicly inherits `Collector` and exposes only a default constructor and the static `New()` factory. The constructor is `explicit` and defaulted, providing no user-accessible state. The factory returns `std::shared_ptr<Collector>` — the base pointer type — rather than a pointer to `NullCollector` itself. This keeps callers decoupled from the concrete type, consistent with how `StatsDCollector::New()` also returns a typed pointer but consumers universally hold `Collector::ptr`.

## Implementation (NullCollector.cpp)

The actual work lives in the translation unit, where a private `detail::NullCollectorImp` class inherits `NullCollector` and overrides all five `Collector` factory methods. Each override creates and returns the corresponding null metric object:

- `NullHookImpl` — stores the handler but never calls it (no polling thread is started).
- `NullCounterImpl` — `increment()` is an empty function body.
- `NullEventImpl` — `notify()` is an empty function body.
- `NullGaugeImpl` — both `set()` and `increment()` are empty function bodies.
- `NullMeterImpl` — `increment()` is an empty function body.

All null `*Impl` classes explicitly delete the copy-assignment operator, preventing accidental sharing of impl state — a defensive pattern carried over from the live implementations where sharing would be a data race.

The assignment of `operator=` as private (rather than using `= delete`) is a legacy C++03-style guard that predates widespread `= delete` usage in the codebase, but its intent is the same.

`NullCollector::New()` constructs a `NullCollectorImp` wrapped in a `shared_ptr` and returns it cast to `shared_ptr<Collector>`, keeping the implementation type entirely invisible to callers.

## Usage Pattern

Any XRPL subsystem that wants to report metrics accepts a `Collector::ptr` in its constructor. In tests and default configurations, `NullCollector::New()` is the standard provider — for example, `TaggedCache`, `FullBelowCache`, and the CSF simulation framework in `src/test/csf/collectors.h` all use it to silence metrics without special-casing their constructors. This makes `NullCollector` the go-to stub whenever a `Collector` dependency needs to be satisfied in an environment where metric reporting is irrelevant.