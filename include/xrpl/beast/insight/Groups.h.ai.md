# `include/xrpl/beast/insight/Groups.h`

## Role in the System

`Groups.h` defines the `Groups` abstract interface, a registry that creates and caches named metric namespaces within the `beast::insight` telemetry subsystem. Its purpose is to let different XRPLD subsystems each claim a named prefix under a shared `Collector` without interfering with one another's metric names. Every module that wants to emit counters, gauges, events, or meters obtains a `Group` object through this registry, and all metrics it creates are automatically scoped under that group's name.

## The `Groups` Interface

`Groups` is a pure abstract class with a single virtual method, `get(std::string const& name)`, plus a convenience `operator[]` that forwards to it. Both return `Group::ptr const&` — a reference into the internal map — rather than a copy, intentionally avoiding shared-pointer reference-count overhead on every lookup. This is safe because the `Groups` instance in `CollectorManager` is held for the entire lifetime of the application, so the map entry (and the referenced `shared_ptr`) is guaranteed to outlive all callers.

The factory function `make_Groups(Collector::ptr const& collector)` is the only way to obtain a concrete instance. It returns `std::unique_ptr<Groups>`, signaling that ownership is exclusive and the resulting object is not meant to be shared.

## Concrete Implementation (`Groups.cpp`)

The implementation lives in `detail::GroupsImp`, which stores a `Collector::ptr` and an `unordered_map<string, shared_ptr<Group>, uhash<>>`. The `get()` method uses `emplace` to perform a single-lookup upsert: `std::pair<Items::iterator, bool>` captures whether the key was newly inserted, and only in that case is a new `GroupImp` constructed. This avoids the find-then-insert double-lookup that a naïve `count` + `operator[]` pattern would incur.

`detail::GroupImp` implements the `Group` interface (which itself extends `Collector`). It stores the group name and the underlying `Collector`, and overrides every metric-creation method to prepend `name + "."` to each metric name via `make_name()`. For example, a call to `group->make_counter("tx_count")` on a group named `"ledger"` results in the metric `"ledger.tx_count"` being registered with the real collector. This dot-separated hierarchy maps directly to StatsD's naming convention, enabling hierarchical dashboards without callers needing to manage prefixes manually. Hooks are an exception: they are forwarded to the underlying collector unchanged, because hooks represent polling callbacks rather than named metrics.

Copy assignment on `GroupImp` is explicitly deleted, preventing accidental shallow copies of a type that holds shared ownership over live metric state.

## Relationship to `Group` and `Collector`

`Group` inherits from `Collector`, meaning a `Group::ptr` is substitutable wherever a `Collector::ptr` is expected. This allows subsystems to receive their metric interface as a `Collector::ptr` while the `Groups` registry internally tracks them as `Group::ptr` values, preserving the named identity needed for diagnostics without leaking the registry abstraction to consumers.

## Usage in `CollectorManager`

The sole production consumer of `Groups` is `CollectorManager` (`src/xrpld/app/main/CollectorManager.cpp`). On startup, it constructs either a `StatsDCollector` or a `NullCollector` (based on config), then wraps it with `make_Groups`. All subsystems call `CollectorManager::group(name)` to obtain their scoped `Group`, which is a thin call to `m_groups->get(name)`. This pattern means subsystems never see the raw `StatsDCollector` or `NullCollector` directly — they only interact with the uniform `Collector` interface, and switching between real and null telemetry requires no change to any subsystem code.