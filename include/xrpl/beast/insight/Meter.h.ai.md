# `Meter.h` — Increment-Only Metric Handle

## Role in the System

`Meter.h` defines the `Meter` class within the `beast::insight` metrics collection framework. The `beast::insight` subsystem provides a thin abstraction layer over external telemetry backends (primarily StatsD), allowing XRPL node code to emit operational metrics without coupling to any specific reporting infrastructure. `Meter` represents one of five metric primitives in this framework alongside `Counter`, `Gauge`, `Event`, and `Hook`.

A `Meter` is an **increment-only** integral counter. Where `Counter` models a bidirectional gauge that can both increase and decrease, `Meter` enforces the constraint that values only ever go up — appropriate for cumulative event counts such as transactions processed, packets received, or bytes written.

## Handle/Body Design

The class is a lightweight reference-counted handle over a `MeterImpl` backend. The actual metric state and any reporting logic live behind `MeterImpl`, which is a pure abstract base class with a single virtual method:

```cpp
virtual void increment(value_type amount) = 0;
```

`MeterImpl` inherits `std::enable_shared_from_this<MeterImpl>`, and `Meter` holds the implementation via `std::shared_ptr<MeterImpl>`. This gives the handle value semantics — `Meter` objects are cheap to copy and assign, and the underlying metric remains live precisely as long as at least one `Meter` handle refers to it. When the last handle is destroyed, the `shared_ptr` refcount drops to zero, the `MeterImpl` is destroyed, and the metric silently stops reporting. This lifetime-as-collection-scope design means callers don't need an explicit deregister step.

## Null Object Pattern

The default constructor produces a **null meter** — one where `m_impl` is empty. Every mutation method guards with `if (m_impl)` before delegating, so a null `Meter` silently absorbs all increments with no effect:

```cpp
void increment(value_type amount) const {
    if (m_impl)
        m_impl->increment(amount);
}
```

This is intentional. Code that receives a `Meter` from a `Collector` doesn't need to check whether telemetry is enabled. When the node is configured with `NullCollector`, `make_meter()` still returns a valid (but no-op) `Meter` object, and the calling code is identical in both cases.

## `const`-Qualified Mutation Operators

A notable design choice: `increment()`, `operator+=`, `operator++` (both prefix and postfix) are all declared `const`. This allows a component to store its metrics as `const Meter` member fields or within a `const`-qualified context and still mutate the underlying count. The `const` guarantee applies to the *handle* (the `shared_ptr` itself is not reseated), not to the referenced counter state, which is mutable through the pointer indirection. This is a deliberate ergonomic choice that mirrors how `const` member functions can still call methods on pointer or reference members when the pointed-to object's identity, not its value, is what the `const` is protecting.

## Relationship to `Collector` and `MeterImpl`

`Meter` objects are obtained exclusively through `Collector::make_meter()`, not constructed directly. The `Collector` interface serves as the factory; concrete implementations (`NullCollector`, `StatsDCollector`) produce the corresponding `MeterImpl` subclass and wrap it in a `Meter` handle. `NullCollector` yields a `NullMeterImpl` whose `increment()` is a no-op, while `StatsDCollector` produces an implementation that buffers and flushes increment events to the StatsD daemon.

The `impl()` accessor exposes the `shared_ptr<MeterImpl>` to allow frameworks like `Groups` to introspect or aggregate metrics, without exposing the `Meter`'s internal mutation interface directly.

## Comparison with `Counter`

The only structural difference between `Meter` and `Counter` in this codebase is that `Counter` also exposes `operator-=`, `operator--` (prefix and postfix). The semantic difference is intentional: meters model monotonically increasing totals (cumulative counts), while counters model values that can go up or down (like queue depths). In StatsD terminology, these map to different metric types — a meter maps to a `c` (count) or `m` (meter) type, while a counter maps to a gauge. Enforcing this at the type level rather than by convention prevents callers from accidentally decrementing a metric that semantically should only grow.