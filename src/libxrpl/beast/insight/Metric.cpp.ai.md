# `beast/insight/Metric.cpp`

This file exists to satisfy a subtle but mandatory C++ requirement: **pure virtual destructors must have an out-of-line definition**. Its entire body is four defaulted destructor definitions — one each for `CounterImpl`, `EventImpl`, `GaugeImpl`, and `MeterImpl`.

## Why This File Exists

Each of the four metric implementation abstract base classes declares its destructor as pure virtual (`virtual ~Foo() = 0`). In C++, declaring a destructor pure virtual is a common technique for making a class abstract while still keeping it as the designated polymorphic base — but unlike other pure virtual functions, the destructor is always called implicitly by every derived class destructor. This means the linker will fail to resolve the symbol unless a body is provided somewhere. `Metric.cpp` is that somewhere.

The alternative — defining each destructor inline in its own header — would technically work but would scatter the definitions across four files with no clear home. Consolidating them here makes the intent explicit: this single translation unit is the canonical anchor for all metric `Impl` destructor bodies.

## The `beast::insight` Metric Hierarchy

The four classes grounded here form the abstract interface layer for the `beast::insight` telemetry system:

- **`CounterImpl`** — represents an integer metric that can be incremented or decremented (`value_type = int64_t`). The public-facing `Counter` class holds a `shared_ptr<CounterImpl>` and delegates all operations through it.
- **`GaugeImpl`** — represents an absolute value metric that can be set to a specific `uint64_t` or adjusted by a signed delta. Corresponds to the StatsD gauge concept.
- **`EventImpl`** — represents a timing metric, where `value_type = std::chrono::milliseconds`. Used to record discrete durations.
- **`MeterImpl`** — represents a rate metric, incrementable by `uint64_t` amounts. Analogous to a StatsD counter intended for throughput measurement.

All four inherit from `std::enable_shared_from_this<T>`, enabling implementations to safely produce `shared_ptr` handles to themselves — necessary because the public metric wrappers (`Counter`, `Gauge`, etc.) are lightweight reference types that hold the `shared_ptr` directly. When the last `Counter` referencing a `CounterImpl` is destroyed, the impl is destroyed too, automatically de-registering the metric.

## Relationship to Collector and Implementations

The `Collector` interface (in `Collector.h`) is the factory: callers invoke `make_counter()`, `make_gauge()`, etc. to receive the public wrapper types. Concrete backends — `StatsDCollector` (for live reporting over UDP) and `NullCollector` (a no-op sink) — implement `Collector` and return their own subclasses of `CounterImpl`, `GaugeImpl`, `EventImpl`, and `MeterImpl`. The `= default` destructors defined here in `Metric.cpp` are the base-class destructor entries through which all those concrete impl destructors will chain, regardless of which backend is active.

The design enforces a clean separation: the public metric types (`Counter`, `Gauge`, etc.) are pure value handles with no knowledge of the backend; the `Impl` classes define the operations backends must support; and `Metric.cpp` provides the minimal glue that lets the compiler and linker close the loop on the virtual dispatch chain.