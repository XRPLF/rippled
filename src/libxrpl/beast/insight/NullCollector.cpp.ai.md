# `NullCollector.cpp` — No-Op Metrics Collector

## Role in the System

The `beast::insight` subsystem provides a thin abstraction layer for exporting runtime metrics — counters, gauges, events, meters, and polling hooks — to an external aggregator. In production, the concrete implementation is `StatsDCollector`, which serializes readings and ships them over UDP to a StatsD daemon. `NullCollector.cpp` fills the other side of that contract: a complete, correctly-typed implementation of every interface that does nothing at all.

This matters because XRPL subsystems accept a `Collector::ptr` (a `std::shared_ptr<Collector>`) at construction time and use it throughout their lifetime. Without a null object, every call site that doesn't want metrics would need its own `if (collector)` guard. Instead, code that doesn't need metrics simply receives a `NullCollector` and operates identically to instrumented code.

## Structure and Encapsulation

The file defines its implementation entirely inside the `beast::insight::detail` namespace, none of which leaks into headers. The public surface is exactly two things: the `NullCollector` header (which just declares the class and its `New()` factory) and the `New()` definition itself, which returns a `std::shared_ptr<Collector>` pointing at the private `NullCollectorImp`.

Callers never name `NullCollectorImp` or any `Null*Impl` type. The return type of `NullCollector::New()` is `std::shared_ptr<Collector>` — the concrete class is invisible at every call site. This is a deliberate application of the factory + interface idiom: the only way to obtain a `NullCollector` is through `New()`, and the returned pointer immediately upcasts to the abstract base.

## The Five Null Metric Types

Each metric type in the `insight` framework follows a two-layer design. The public handle class (`Counter`, `Hook`, etc.) is a value type that holds a `std::shared_ptr` to an abstract `*Impl` base (`CounterImpl`, `HookImpl`, etc.). All `*Impl` classes inherit from `std::enable_shared_from_this` — they are always heap-allocated and reference-counted, never stack-owned.

`NullCollectorImp` implements all five `make_*` virtual methods by constructing a shared pointer to the corresponding null impl and passing it to the public handle's constructor:

- `NullCounterImpl` — overrides `increment(value_type)` as a no-op.
- `NullEventImpl` — overrides `notify(value_type const&)` as a no-op.
- `NullGaugeImpl` — overrides both `set(value_type)` and `increment(difference_type)` as no-ops, since gauges support both absolute assignment and relative adjustment.
- `NullMeterImpl` — overrides `increment(value_type)` as a no-op.
- `NullHookImpl` — has no virtual methods to override beyond the destructor. Hooks work by registering a `std::function<void()>` handler that the real collector calls on its collection interval; the null impl simply discards that handler at construction time.

Every null impl also suppresses copy assignment (declared private with no definition). This is a common defensive pattern for `shared_from_this` types: the object must remain heap-allocated, so accidental value-copy semantics are blocked at compile time.

## Why the Null Pattern Is the Right Choice Here

An alternative design might use a nullable `Collector*` everywhere with a sentinel `nullptr` meaning "no metrics." The null object approach is strictly superior here: the consumer code has no branches, no pointer checks, no conditional metric updates. Subsystems that initialize their metrics in their constructor — allocating a `Counter` from the collector and storing it as a member — work identically whether the collector is null or real. The cost of a null collection call is just a virtual dispatch followed by an immediate return.

A shared no-op singleton could save the per-call `make_*` allocations, but the current design allocates a fresh `Null*Impl` per metric. This is a deliberate trade-off: callers hold the returned `Counter` or `Gauge` by value (which wraps the shared pointer), so the lifetime of the null impl is tied to the holder's lifetime, which is consistent with how the real `StatsDCollector` impls are managed.

## Relationship to `StatsDCollector`

`NullCollector` and `StatsDCollector` are the two concrete implementations of `Collector` in the library. The entire `beast::insight` framework is built on the assumption that these two are interchangeable — any code that accepts a `Collector::ptr` will work with either. `NullCollector.cpp` is therefore both a usable component (for production nodes that disable metrics reporting) and an implicit specification test: if `NullCollector` compiles and satisfies all pure virtuals, the interface contract is correctly defined.