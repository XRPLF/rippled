# `beast/insight/Group.h` — Namespaced Metric Collector Front-End

`Group` is a minimal abstract interface within the `beast::insight` metrics subsystem that extends `Collector` with a single piece of identity: a name. Its purpose is to enable namespace isolation for metrics emitted by different XRPL subsystems without requiring each subsystem to manage its own metric prefixing.

## Role in the Insight Subsystem

`Collector` is the base factory interface through which code creates metric objects — `Counter`, `Gauge`, `Event`, `Meter`, and `Hook`. Any class that wants to export metrics accepts a `Collector::ptr` in its constructor and calls `make_counter()`, `make_gauge()`, and so on.

`Group` IS-A `Collector` by inheritance, making it a transparent drop-in wherever a `Collector::ptr` is accepted. The additional `name()` method simply records which logical subsystem the group belongs to. The real work happens in the concrete implementation `detail::GroupImp` (defined in `Groups.cpp`), which intercepts every `make_*` call and prepends the group name as a dot-separated prefix:

```cpp
std::string make_name(std::string const& name) {
    return m_name + "." + name;
}
Counter make_counter(std::string const& name) override {
    return m_collector->make_counter(make_name(name));
}
```

So if a `Group` named `"consensus"` creates a counter called `"proposals"`, the metric arrives at the underlying `Collector` (e.g., `StatsDCollector`) as `"consensus.proposals"`. The subsystem calling `make_counter("proposals")` never needs to know its own prefix.

## The `Groups` Factory and Lifecycle

`Group` instances are not created directly; they are produced by `Groups`, a companion interface (`Groups.h`) that acts as a named registry. `Groups::get(name)` returns a `Group::ptr`, creating a new `GroupImp` if one does not already exist for that name, or returning the cached instance. The `operator[]` convenience overload delegates to `get`. The free function `make_Groups(Collector::ptr)` constructs a `GroupsImp` backed by a specific underlying collector.

At the application layer, `CollectorManager` (in `app/main/CollectorManager.h`) surfaces both the raw `collector()` and a `group(name)` accessor, giving XRPL subsystems a uniform entry point to either a flat metric namespace or a prefixed group namespace.

## Design Rationale

The key design decision is that `Group` inherits from `Collector` rather than wrapping it with a separate interface. This means every existing subsystem API that accepts a `Collector::ptr` automatically supports grouped metrics: pass a `Group::ptr` and all metrics created through it will be namespaced without any code change at the call site. There is no need for a separate "grouped collector" concept or adapter pattern.

The `name()` method is explicitly scoped for diagnostics rather than metric construction. The name is used in `GroupImp::make_name()` for prefix assembly, but the public API documents it as a diagnostic aid, keeping the interface contract focused. The `shared_ptr` alias `Group::ptr` follows the same convention as `Collector::ptr` throughout the subsystem, making ownership semantics uniform and cacheable inside `GroupsImp::m_items`.

The combination of a short abstract header (`Group.h`) and a single implementation file (`Groups.cpp`) that hosts both `GroupImp` and `GroupsImp` keeps the concrete details hidden behind the ABI boundary. Callers only ever hold a `Group::ptr` and see the `Collector` interface; the prefix logic is an implementation detail invisible to metric-emitting subsystems.