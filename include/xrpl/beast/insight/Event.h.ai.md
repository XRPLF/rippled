# `beast/insight/Event.h` — Push-Style Timing Metric

## Role in the System

`Event` lives within the `beast::insight` metrics framework, which provides a uniform abstraction for exporting runtime telemetry from XRPL node subsystems. The framework defines five metric kinds — `Counter`, `Event`, `Gauge`, `Hook`, and `Meter` — all obtained through the `Collector` interface and forwarded to a backend such as `StatsDCollector`. `Event` specifically models a discrete occurrence that has an associated elapsed duration: for example, the time taken to process a transaction or validate a ledger. It complements `Counter` (a cumulative integer tally) and `Gauge` (an instantaneous snapshot) by capturing the *timing* of individual operations rather than their count or current magnitude.

## Design: Reference Wrapper over a Polymorphic Implementation

`Event` follows the same split-implementation pattern used throughout the insight module: a thin, value-semantic handle class pairs with an abstract `EventImpl` that backends override. `EventImpl` inherits `std::enable_shared_from_this<EventImpl>` and exposes a single pure-virtual method:

```cpp
virtual void notify(std::chrono::milliseconds const& value) = 0;
```

`Event` itself holds a `std::shared_ptr<EventImpl>` as its only data member. This means `Event` is cheap to copy, safe to store by value anywhere in the codebase, and naturally ref-counted: when the last copy of an `Event` handle is destroyed, the `shared_ptr` refcount drops to zero and the implementation is torn down, stopping collection automatically without any explicit unregister call.

## Null Metric Pattern

A default-constructed `Event` has a null `m_impl`. Every method that touches the implementation guards with `if (m_impl)`, so a null `Event` silently no-ops. This is deliberate: call sites don't need to check whether a collector was actually configured. Code paths that run without a live collector — unit tests, minimal deployments — construct a null `Event` (or receive one from `NullCollector`) and incur no overhead beyond the branch.

## `notify()` and Duration Coercion

The only mutation method is `notify()`, a template accepting any `std::chrono::duration`:

```cpp
template <class Rep, class Period>
void notify(std::chrono::duration<Rep, Period> const& value) const {
    if (m_impl)
        m_impl->notify(ceil<value_type>(value));
}
```

The internal `value_type` is `std::chrono::milliseconds` (defined in `EventImpl`). `notify()` uses `std::chrono::ceil` — not `duration_cast` — to convert the caller's duration to milliseconds. The choice of `ceil` over truncation is defensive: it ensures a sub-millisecond event is reported as 1 ms rather than silently disappearing as 0 ms, which would skew histograms in a StatsD backend. Callers can pass nanoseconds, microseconds, or any other duration unit; the coercion is automatic and lossless in the rounding-up direction.

## Obtaining an `Event`

`Event` objects are not constructed directly in application code. Instead, the `Collector` interface provides factory overloads:

```cpp
virtual Event make_event(std::string const& name) = 0;
Event make_event(std::string const& prefix, std::string const& name);
```

The two-argument overload concatenates `prefix + "." + name` before delegating to the single-argument virtual, which is a convenience for subsystems that namespace their metrics hierarchically. The returned `Event` wraps whichever concrete `EventImpl` the backend supplies.

## Push vs. Pull Semantics

Unlike `Gauge` or `Hook`-based metrics — which support a polling model where the collector calls back into application state on each reporting interval — `Event` is strictly push-only. The owner calls `notify()` at the moment the timed operation completes, and the value is forwarded immediately to the backend. This makes sense architecturally: an event represents a point in time, so there is nothing to poll; waiting for a collection interval would conflate multiple independent events or lose the individual timing data entirely.

## Relationship to Sibling Types

All metric handle types in `beast/insight/` share the same structural idiom: a `final` wrapper class holding a `shared_ptr` to an abstract `*Impl`, with a null-safe default constructor. `Event`'s uniqueness is its push-only, time-valued interface. `Counter` supports increment/decrement operators for cumulative tracking; `Gauge` allows setting an arbitrary integer snapshot; `Meter` measures a rate. `Event` is the right choice when what matters is how long a specific operation took, reported exactly when it completes.