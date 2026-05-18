# `GaugeImpl.h` — Abstract Interface for Gauge Metric Implementations

`GaugeImpl` is the abstract backend interface for gauge-type metrics within the `beast::insight` telemetry framework. It sits at the boundary between the user-facing `Gauge` handle class and any concrete metric-reporting backend (such as `StatsDCollector` or `NullCollector`), following a consistent bridge pattern used throughout the `insight` module.

## Role in the `beast::insight` Framework

The `insight` namespace implements a lightweight, pluggable metrics system. Every metric type — counter, event, gauge, meter — is split into two layers: a thin, copyable handle (`Gauge`) that application code holds and uses, and a polymorphic implementation object (`GaugeImpl`) that the concrete `Collector` backend creates and owns. `GaugeImpl.h` defines the contract that any backend must satisfy to support gauge metrics.

This separation means application code never depends directly on whether metrics go to StatsD, a log file, or nowhere at all. The `Gauge` handle just delegates to whatever `GaugeImpl` it was given — or silently does nothing if it holds a null `shared_ptr` (the "null metric" case, where `Gauge` was default-constructed).

## Interface Design

`GaugeImpl` declares exactly two operations:

- `set(value_type value)` — assigns an absolute value to the gauge (e.g. "current queue depth is 47").
- `increment(difference_type amount)` — adjusts the gauge by a signed delta (positive or negative).

The type aliases express a deliberate design choice: `value_type` is `std::uint64_t` (unsigned, because a gauge represents a non-negative quantity like a count or size) while `difference_type` is `std::int64_t` (signed, because an adjustment can go either direction). This mirrors the `std::vector::size_type` / `std::ptrdiff_t` convention and prevents callers from accidentally passing a negative absolute value while still allowing bidirectional relative adjustment.

The destructor is declared pure virtual (`= 0`) even though it must have a definition, which is C++'s standard idiom for forcing the class to be abstract while still permitting proper virtual destruction through a base pointer. Concrete subclasses must call `GaugeImpl::~GaugeImpl()` implicitly through their own destructors, which the linker provides as an out-of-line symbol.

## Ownership and Lifetime via `enable_shared_from_this`

`GaugeImpl` inherits from `std::enable_shared_from_this<GaugeImpl>`. This allows a `GaugeImpl` subclass to produce a `shared_ptr` to itself — a necessity for implementations that need to register themselves with a background collection loop or scheduler inside the `Collector`. When the last `Gauge` handle goes out of scope, the `shared_ptr` reference count drops to zero and the metric is destroyed; the collector stops reporting it. This is the "last reference = metric disappears" lifetime semantics documented in `Gauge.h`.

## Relationship to `Gauge`

`Gauge` is the class that application code actually uses. It wraps a `shared_ptr<GaugeImpl>` and exposes the same `set` / `increment` operations plus arithmetic operator overloads (`=`, `+=`, `-=`, `++`, `--`). Every operator first checks `if (m_impl)` before delegating, so a null-constructed `Gauge` is completely safe to use — all operations silently no-op. `Gauge` includes `GaugeImpl.h` directly and aliases its `value_type` and `difference_type`, so the numeric types are defined in exactly one place.

## Parallel Structure Across the Module

The same two-file pattern — a lightweight handle and a `*Impl` pure virtual base — is used for every metric kind in `insight`: `Counter`/`CounterImpl`, `Event`/`EventImpl`, `Gauge`/`GaugeImpl`, `Hook`/`HookImpl`, and `Meter`/`MeterImpl`. `CounterImpl`, for comparison, only exposes `increment` (no `set`), because counters are monotonically accumulating; gauges are the appropriate choice when a value can go up and down arbitrarily. The `Collector` interface (in `Collector.h`) is the factory that creates all these objects: `make_gauge(name)` returns a fully wired `Gauge` wrapping a backend-specific `GaugeImpl` subclass.