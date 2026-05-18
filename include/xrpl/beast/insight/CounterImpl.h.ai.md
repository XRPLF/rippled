# `CounterImpl.h` — Abstract Backend Interface for Counter Metrics

## Role in the System

`CounterImpl.h` defines the pure-virtual base class that backs the `Counter` metric handle in the `beast::insight` telemetry framework. The `beast::insight` layer provides a thin abstraction over external metric backends (primarily StatsD), letting XRPL subsystems emit measurements without coupling to any specific transport. `CounterImpl` is the seam between the public-facing `Counter` handle and whichever concrete backend is in use.

The file is intentionally minimal — 23 lines — because it exists purely to express the contract a backend must satisfy. All business logic lives either in `Counter.h` (the handle) or in the concrete implementations such as `StatsDCounterImpl` in `StatsDCollector.cpp`.

## Design of `CounterImpl`

The class inherits `std::enable_shared_from_this<CounterImpl>`, which is the central lifetime-management mechanism for the entire insight metric system. `Counter` (the public handle) holds a `std::shared_ptr<CounterImpl>` as its only data member. When the last `Counter` copy referencing a given impl is destroyed, the `shared_ptr` refcount drops to zero, triggering the impl's destructor and automatically unregistering the metric from the backend. In `StatsDCounterImpl`, the destructor calls `m_impl->remove(*this)`, cleanly deregistering itself from the collector's tracking list. The `shared_from_this()` pattern is exploited further inside `increment()` implementations to safely capture a strong reference when posting asynchronous work items (see below).

The pure virtual destructor (`virtual ~CounterImpl() = 0`) makes the class abstract while still permitting destruction through a base pointer — the standard C++ idiom when no other method alone is sufficient to force abstract status. Here it is the right choice: `increment` could have been the sole pure virtual, but marking the destructor pure as well clearly communicates that `CounterImpl` must not be instantiated directly.

## The Single Operation: `increment`

`increment(value_type amount)` is the entire mutation surface. `value_type` is `std::int64_t`, which means a single method covers both incrementing (positive `amount`) and decrementing (negative `amount`). This is a deliberate simplification relative to `GaugeImpl`, which exposes separate `set()` and `increment()` methods because a gauge can be assigned an absolute value. A counter, by contrast, is always adjusted relatively — it accumulates a running delta. Exposing only `increment` prevents misuse and keeps implementations simpler.

`Counter.h` translates the full operator interface (`+=`, `-=`, `++`, `--`) entirely in terms of `increment`, so backend implementations never need to handle those cases. The mapping is straightforward: `operator--` calls `increment(-1)`, `operator-=` calls `increment(-amount)`.

## Null-Safety Pattern

`Counter`'s `increment` method guards the call with `if (m_impl)` before dispatching. This means a default-constructed `Counter` (holding a null `shared_ptr`) silently drops all operations. This null-object pattern avoids the need for callers to check whether a metric was actually created, which matters in contexts where a `NullCollector` is installed and no real backend exists.

## Asynchronous Dispatch in the Concrete Implementation

Although not visible in this header, it is worth noting what `increment` must accommodate in real backends. `StatsDCounterImpl::increment` immediately dispatches onto the collector's Boost.Asio I/O context via `boost::asio::dispatch`, capturing a strong `shared_ptr` to itself via `shared_from_this()`. The actual mutation (`m_value += amount; m_dirty = true;`) runs on the I/O thread, making the operation thread-safe without explicit locking. This asynchronous design is why `enable_shared_from_this` is part of the base class rather than just the concrete class: the dispatch lambda must extend the object's lifetime until the posted handler executes, and only a `shared_ptr` can guarantee that.

## Relationship to Sibling Interfaces

`CounterImpl` is structurally parallel to `GaugeImpl`, `EventImpl`, `MeterImpl`, and `HookImpl`, all of which follow the same `enable_shared_from_this` + pure-virtual-interface pattern. The `Collector` interface produces concrete impls via factory methods (`make_counter`, `make_gauge`, etc.), wrapping them in the corresponding handle types. `CounterImpl` specifically sits at the narrowest point in this hierarchy: one method, one type alias, one abstract class.