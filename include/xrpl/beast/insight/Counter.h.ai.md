# `beast/insight/Counter.h` — Lightweight Metric Handle for Integral Counting

`Counter.h` defines the `beast::insight::Counter` class, a thin reference-counted handle used to track and report integer-valued metrics through the XRPL node's telemetry pipeline. It lives in the `beast::insight` subsystem alongside `Gauge`, `Meter`, `Event`, and `Hook` — a family of metric types that a `Collector` (most concretely `StatsDCollector`) exports to an external monitoring backend such as StatsD.

## Role in the Insight System

The `beast::insight` module follows a handle-body pattern throughout. Every user-facing metric type (`Counter`, `Gauge`, `Meter`, etc.) is a copyable, value-semantics wrapper around a `shared_ptr` to an abstract `*Impl` base class. `Counter` holds a `std::shared_ptr<CounterImpl>`, where `CounterImpl` is an abstract interface with a single pure-virtual method, `increment(value_type amount)`. The concrete implementation — wiring updates over UDP to a StatsD server — lives inside `StatsDCollector` and is invisible to callers.

This design is intentional: application code interacts only with `Counter`, never with the underlying transport, so swapping the collector (e.g., to `NullCollector` for testing) requires no changes to the code emitting metrics.

## Handle Semantics and Lifetime

`Counter` is explicitly designed as a "lightweight reference wrapper which is cheap to copy and assign." Copying a `Counter` merely copies the `shared_ptr`, incrementing the reference count — both copies report to the same underlying metric. When the last `Counter` referencing a particular `CounterImpl` is destroyed, the `shared_ptr` reference count drops to zero and the metric is de-registered; the collector stops collecting it. This lifetime coupling means a subsystem can stop being monitored simply by letting its `Counter` member go out of scope, without any explicit unregistration call.

The null-object idiom is also embedded directly. The default constructor leaves `m_impl` empty, and every mutating operation guards with `if (m_impl)` before dispatching. A default-constructed `Counter` silently accepts all arithmetic operations without crashing or emitting anything — useful in contexts where metric collection is optional or the collector is absent.

## Operator Design — `const` on a Mutable Handle

A subtle but important design choice: all arithmetic operators (`operator+=`, `operator-=`, `operator++`, `operator--`) and `increment()` are declared `const`. This might seem contradictory since these operations modify external state. However, `const` here applies to the handle itself (the `shared_ptr`), not the resource it points to. The pointer is not reseated; only the metric value behind it changes. This allows a `Counter` stored as a `const` member or accessed through a `const` reference to still emit updates — the right behavior for a metric that must be updated from within a logically `const` operation.

## Bidirectional Counting vs. Gauges

Despite the name "counter" suggesting a monotonically increasing value, `Counter` supports both increment and decrement via `operator-=` and `operator--`, which forward to `increment(-amount)`. The class comment describes it as "a gauge calculated at the server" — the distinction from `Gauge` (defined in `Gauge.h`) is conceptual: a `Gauge` is an absolute, instantaneous value set directly by the caller, while a `Counter` is adjusted relative to its current value and the server (StatsD backend) tracks the running total. In StatsD terminology, this maps to the "gauge with delta" or "counter" metric types.

## `CounterImpl` Interface

`CounterImpl` (in `CounterImpl.h`) inherits `std::enable_shared_from_this<CounterImpl>` and exposes only `virtual void increment(value_type amount) = 0` with a pure-virtual destructor. The `value_type` is `std::int64_t`, giving the full signed 64-bit range for both positive and negative adjustments. The `enable_shared_from_this` base is present so concrete implementations can safely pass `shared_ptr`s to themselves when registering with the collector's internal bookkeeping.

## Usage Pattern

Callers never construct `Counter` directly from a `CounterImpl`. Instead, they obtain one from a `Collector` via `make_counter(name)` or `make_counter(prefix, name)`, which builds the dotted metric path and returns a fully initialized `Counter`. The `Counter` is then stored as a member and incremented at the appropriate call sites:

```cpp
// In some subsystem's constructor:
m_requestCounter = collector->make_counter("app", "requests");

// At a call site:
++m_requestCounter;          // or
m_requestCounter += 5;       // or
m_requestCounter.increment(n);
```

The entire metric emission path — from `Counter::increment()` through `CounterImpl::increment()` to the StatsD UDP packet — is thus hidden behind this two-file abstraction, keeping instrumented code clean and the telemetry backend fully substitutable.