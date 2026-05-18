# `EventImpl.h` — Abstract Interface for Event Timing Metrics

## Role in the System

`EventImpl` is the pure abstract base class at the heart of the `beast::insight` event metric type. Within the `insight` subsystem, "events" represent operations that have an associated duration — things like transaction processing time or validation latency. This file defines the minimal contract every concrete event backend must satisfy: accept a millisecond-precision timing value whenever such an event is fired.

The file is intentionally minimal, containing only the abstract interface so that the front-facing handle (`Event`) and the concrete back-end implementations can be compiled and evolved independently. The same split-interface pattern is applied uniformly across the insight subsystem — compare `CounterImpl` (uses `int64_t` for incrementable counts) and `GaugeImpl` (tracks a current level), each following the same two-file structure.

## Design of `EventImpl`

`EventImpl` inherits `std::enable_shared_from_this<EventImpl>`. This is not incidental: the only meaningful concrete implementation, `StatsDEventImpl` in `StatsDCollector.cpp`, dispatches `notify()` calls asynchronously onto a `boost::asio` I/O context. Inside the dispatch, it captures `shared_from_this()` to extend the object's lifetime across the async hop. Were `EventImpl` not `enable_shared_from_this`, the derived class could not safely call `std::static_pointer_cast<StatsDEventImpl>(shared_from_this())` in `notify()` — the implementation would either be forced into an awkward workaround or risk a use-after-free if the last `Event` handle dropped just before the async callback ran.

The pure-virtual destructor `virtual ~EventImpl() = 0` ensures the class is abstract while still permitting correct destruction of derived objects through a base pointer. The single pure-virtual method is:

```cpp
virtual void notify(value_type const& value) = 0;
```

where `value_type` is `std::chrono::milliseconds`. Using a concrete `chrono` duration type rather than a raw integer makes the unit explicit and eliminates a class of silent scaling bugs. The user-facing `Event` wrapper reinforces this by accepting any `std::chrono::duration` and converting to milliseconds using `std::chrono::ceil` before forwarding to `notify()` — rounding up so that sub-millisecond events register as at least 1 ms rather than silently disappearing as zero.

## Relationship to `Event` and `StatsDEventImpl`

`Event` is a lightweight, copyable reference handle that owns a `shared_ptr<EventImpl>`. It is the type callers hold and call `notify()` on. When the last `Event` copy is destroyed, the `shared_ptr` refcount drops to zero and the `EventImpl` is cleaned up. This design deliberately ties metric collection lifetime to the objects that own the metric handle, avoiding the need for explicit registration or deregistration.

`StatsDCollector::make_event()` constructs a `StatsDEventImpl` — the only non-null concrete implementation — wraps it in a `shared_ptr`, and returns it via an `Event` handle. `StatsDEventImpl::notify()` posts work to the collector's I/O context via `boost::asio::dispatch`, then `do_notify()` formats the value in StatsD wire format (`prefix.name:count|ms`) and hands it to the collector's send buffer. The `|ms` type tag is the StatsD convention for timing samples.

A `NullCollector` also exists and produces no-op `Event` objects — the `Event` default constructor sets its `m_impl` to `nullptr`, and `Event::notify()` silently skips the call when `m_impl` is null. This lets production code accept a collector reference without needing `#ifdef`-style guards.

## Summary

`EventImpl.h` is a deliberately small file — a focused contract between the public `Event` handle and whatever backend is collecting metrics. Its three design decisions that carry real weight are: inheriting `enable_shared_from_this` to support async lifetime extension, adopting `std::chrono::milliseconds` as `value_type` for type-safe duration reporting, and declaring a pure-virtual destructor to enforce abstractness while preserving correct polymorphic deletion.