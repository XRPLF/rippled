# `include/xrpl/beast/insight/Gauge.h`

## Role in the System

`Gauge` is one of the core metric types in the `beast::insight` metrics subsystem — the instrumentation layer that feeds runtime telemetry from the rippled node to external monitoring systems (primarily StatsD). A gauge models an instantaneous snapshot of a numeric value: queue depth, connected peer count, memory pressure, or any other measurement that can go up or down freely. Unlike a `Counter` (which only accumulates increments), a gauge supports both absolute assignment and signed relative adjustment.

## Design Pattern: Handle-Body with Shared Ownership

The class is a thin handle over a `std::shared_ptr<GaugeImpl>`, following the same pimpl-via-shared_ptr pattern used by every metric type in this subsystem (`Counter`, `Event`, `Meter`). The handle is deliberately cheap: copying or moving a `Gauge` is just a reference-count bump on the shared impl. The metric remains registered with the collector only as long as at least one `Gauge` handle keeps the `shared_ptr` alive. When the last handle is destroyed, the impl's destructor fires and the metric is automatically unregistered — no explicit cleanup required. This lifetime semantics makes it safe to embed `Gauge` members directly in objects without lifecycle ceremony.

## Null State

Default construction yields a null gauge (`m_impl` is empty). Every mutating operation guards on `if (m_impl)` before delegating, so null gauges silently absorb all calls. This is the appropriate behavior for code paths where a collector may not be configured (e.g., unit tests using `NullCollector`). Callers never need to check whether a gauge is valid before using it.

## `const` on Mutating Methods

All mutation methods — `set()`, `increment()`, and every arithmetic operator — are marked `const`. This is intentional: `const` here applies to the *handle*, not to the underlying metric value stored inside the impl. A `const Gauge` cannot be reseated to a different impl, but it can still update the metric it points to. The design mirrors how `const std::shared_ptr<T>` prevents pointer reassignment while leaving the pointed-to object mutable.

## Value Types

`GaugeImpl` defines `value_type` as `std::uint64_t` and `difference_type` as `std::int64_t`. The unsigned value type means the gauge cannot represent negative quantities, which suits typical server metrics. The signed difference type allows natural expressions like `gauge -= 5` when reducing a count. The `StatsDGaugeImpl` implementation in `StatsDCollector.cpp` handles edge cases explicitly: overflow is clamped to `UINT64_MAX` and underflow is clamped to `0`, preventing wrap-around bugs when increments arrive with bad signs.

## `operator=` as Value Assignment

`operator=(value_type)` overloads the assignment operator to mean "set the gauge to this absolute value", enabling the expressive `gauge = 42;` syntax. This is the only overload — no `operator=(Gauge const&)` is suppressed, so the compiler-generated copy-assignment still copies the handle (sharing the same impl). This design lets gauge handles be stored in standard containers while still supporting natural numeric-assignment syntax.

## Relationship to `GaugeImpl` and Collector

`GaugeImpl` (in `GaugeImpl.h`) is an abstract base with `enable_shared_from_this`, declaring only `set()` and `increment()` as pure virtual. Two concrete implementations exist:

- **`NullGaugeImpl`** (in `NullCollector.cpp`): Both methods are empty no-ops. Used when the system runs without a metrics backend.
- **`StatsDGaugeImpl`** (in `StatsDCollector.cpp`): Delegates `set()` and `increment()` via `boost::asio::dispatch` to a dedicated I/O thread, where the actual value is maintained with dirty-flag tracking. Only changed values are flushed to the StatsD UDP stream at each collection interval, avoiding unnecessary wire traffic.

`Gauge` handles are always created through `Collector::make_gauge(name)` — the public factory — not by constructing them directly. The explicit `impl()`-taking constructor is marked `explicit` precisely to prevent accidental construction outside of collector implementations.