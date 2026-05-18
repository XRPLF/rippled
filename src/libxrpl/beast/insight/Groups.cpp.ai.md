# `beast/insight/Groups.cpp` — Metric Group Registry and Namespaced Collector Facade

## Role in the System

The beast `insight` subsystem provides an abstraction layer for exporting runtime telemetry — counters, gauges, events, meters, and polling hooks — to backends such as StatsD or a null sink. `Groups.cpp` implements the grouping layer on top of this: a registry that maps logical names to scoped `Collector` facades, so that different subsystems of the node can each register metrics under their own dot-separated namespace prefix without knowing about or conflicting with one another.

## Key Design: A Group IS a Collector

The central design decision is that `Group` inherits from `Collector` rather than merely containing one. Defined in `Group.h`:

```cpp
class Group : public Collector {
public:
    using ptr = std::shared_ptr<Group>;
    virtual std::string const& name() const = 0;
};
```

This means a `Group::ptr` satisfies any interface that accepts a `Collector::ptr`. Code that wants metrics can be handed a group directly — it never needs to know whether it is talking to a root collector or a namespaced sub-collector. The grouping is completely transparent to metric consumers.

## `GroupImp` — The Prefix-Injecting Facade

`GroupImp` is the concrete `Group` implementation. It holds two members: the group's name string (`m_name`) and a `shared_ptr` to the underlying real `Collector`. Every `make_*` call intercepts the metric name, prepends `m_name + "."`, then delegates to the underlying collector:

```cpp
std::string make_name(std::string const& name) {
    return m_name + "." + name;
}

Counter make_counter(std::string const& name) override {
    return m_collector->make_counter(make_name(name));
}
```

The effect is automatic namespacing. If a group is named `"ledger"` and a component asks for a counter called `"validations"`, the underlying collector sees `"ledger.validations"`. The caller never constructs this path manually. This matches StatsD's conventional dot-separated hierarchy, and it means metric names are guaranteed to be consistent: there is no way for a consumer to accidentally omit the prefix.

`make_hook()` is the notable exception — it does not apply `make_name()`. Hooks are polling callbacks rather than named time-series, so they carry no name to prefix. The hook is forwarded verbatim to the underlying collector.

`GroupImp` inherits `std::enable_shared_from_this<GroupImp>` but does not call `shared_from_this()` directly in this file — the pattern is present for potential future use or for derived classes.

## `GroupsImp` — Lazy-Creating Registry

`GroupsImp` owns the group registry as an `unordered_map<std::string, std::shared_ptr<Group>, uhash<>>`. The `uhash<>` type is beast's universal hash adapter, which here resolves to a standard string hasher.

The `get()` method implements a find-or-create pattern using `emplace()`:

```cpp
Group::ptr const& get(std::string const& name) override {
    std::pair<Items::iterator, bool> const result(
        m_items.emplace(name, Group::ptr()));
    Group::ptr& group(result.first->second);
    if (result.second)
        group = std::make_shared<GroupImp>(name, m_collector);
    return group;
}
```

`emplace()` inserts a null `Group::ptr` as a placeholder only if the key is new (`result.second == true`), then immediately replaces it with a real `GroupImp`. If the key already existed, the existing group is returned untouched. The method returns a `const&` to the stored `shared_ptr`, so the caller receives a stable reference into the map — valid as long as `GroupsImp` is alive and the map is not rehashed. Because `unordered_map` does not invalidate references on insertion (only on rehashing), this reference stability holds unless the map rehashes under a concurrent insertion — which points to the thread-safety concern below.

## Lifetime and Ownership

Both `GroupsImp` and each `GroupImp` independently hold a `Collector::ptr` (`shared_ptr<Collector>`). This means the underlying collector's lifetime is bounded by the last reference among the groups registry and all individual groups. If a caller retains a `Group::ptr` after destroying the `Groups` container, it will continue to work correctly because both hold their own reference to the collector. There is no dangling pointer risk.

The `make_Groups()` factory returns `std::unique_ptr<Groups>`, giving the caller exclusive ownership of the registry itself.

## Thread Safety

Neither `GroupsImp` nor `GroupImp` provides any locking. Concurrent calls to `get()` with new group names would race on the `unordered_map`, and concurrent metric-creation calls on distinct `GroupImp` instances are safe only if the underlying `Collector` implementation is thread-safe. In practice, callers are expected to create groups during initialization on a single thread, not during hot-path concurrent operation.

## Summary

`Groups.cpp` is a small but structurally important file. It provides the metric namespace registry for the insight subsystem via two concrete types hidden in `namespace detail`: `GroupImp`, which wraps a `Collector` and transparently prefixes all metric names, and `GroupsImp`, which caches those wrappers by name. The `make_Groups()` factory is the sole public entry point. The design prioritizes simplicity and composability — because `Group` is a `Collector`, the grouping mechanism is invisible to any code that simply holds a `Collector::ptr`, making it easy to add or change grouping structure without touching metric consumers.