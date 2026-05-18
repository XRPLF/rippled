# `HookImpl.h` — Abstract Base for Poll-Driven Metric Hooks

## Role in the System

`HookImpl` is the abstract base class at the bottom of the `beast::insight` metrics framework's hook mechanism. It exists to define the interface contract and carry the `HandlerType` alias that binds together the `Collector`, `Hook`, and their concrete backend implementations. On its own the class is intentionally minimal — it does nothing but establish the shared pointer ownership model and the function signature used when a collection interval fires.

The `beast::insight` subsystem is the XRPL node's pluggable metrics layer. Client code obtains a `Collector` (either the live `StatsDCollector` or the no-op `NullCollector`) and creates metric objects from it: counters, gauges, events, meters, and hooks. A hook is special among these: rather than the client *pushing* a value to a metric object, the framework *calls back* into the client at each collection interval. This is the polling model, useful when computing a metric value is cheap but you only want to do it on demand (e.g., reading an atomic counter, or computing a queue depth).

## Class Structure

```cpp
class HookImpl : public std::enable_shared_from_this<HookImpl>
{
public:
    using HandlerType = std::function<void(void)>;
    virtual ~HookImpl() = 0;
};
```

Inheriting `std::enable_shared_from_this<HookImpl>` is necessary because the live implementation (`StatsDHookImpl`) registers itself into the collector's intrusive list at construction time and must produce a valid `shared_ptr` to itself from within its own constructor chain — a pattern seen across all `*Impl` types in this subsystem.

The `HandlerType` alias is placed on `HookImpl` rather than on the public-facing `Hook` handle class because `Collector.h` must reference it in the virtual `make_hook(HookImpl::HandlerType const&)` signature. Since `Collector.h` already includes `Hook.h`, which in turn includes `HookImpl.h`, anchoring the type on `HookImpl` avoids any circular dependency.

The pure virtual destructor (`= 0`) makes the class abstract while still requiring an out-of-line definition. That definition lives in `Hook.cpp` as a single `= default` line. This is a deliberate C++ idiom: without the out-of-line body, derived class destructors that implicitly call `~HookImpl()` would cause a linker error, yet the pure specifier prevents anyone from instantiating `HookImpl` directly.

## Handle/Impl Split

The rest of the design follows the same handle/impl pattern used for every metric type in this subsystem. `Hook` (in `Hook.h`) is a lightweight value type that wraps a `shared_ptr<HookImpl>`. Users copy and store `Hook` objects freely; lifetime is managed automatically. The actual behavior lives in a heap-allocated `HookImpl` subclass. This split lets `Collector` return metric objects by value with no raw pointer exposure, while still allowing polymorphic backend implementations.

## Concrete Implementations

Two implementations exist:

**`NullHookImpl`** (in `NullCollector.cpp`) is a trivially empty subclass that adds no state and overrides nothing beyond the destructor. It is returned by `NullCollector::make_hook()` when metrics collection is disabled, ensuring the rest of the code never needs to guard against null hooks.

**`StatsDHookImpl`** (in `StatsDCollector.cpp`) stores the `HandlerType` callback and a `shared_ptr` back to the owning `StatsDCollectorImp`. On construction it registers itself into the collector's metric list; on destruction it deregisters. When the collector's background thread fires a collection interval it calls `do_process()` on every registered metric, and `StatsDHookImpl::do_process()` simply invokes `m_handler()`. The handler is whatever lambda or callable the client passed to `Collector::make_hook()`.

## Design Tradeoffs

The absence of any virtual `process()` or `invoke()` method on `HookImpl` itself is intentional. The only protocol between `HookImpl` and the backend collector is *lifetime* — the collector holds the hook alive via `enable_shared_from_this` and dispatches through the concrete type's own interface (`StatsDMetricBase::do_process()`). This means the abstract base stays clean and independent of any particular backend's scheduling mechanism. A future backend could store the handler differently or invoke it on a different thread without any change to `HookImpl.h`.