# `suite_list.h` — Ordered Registry of Unit Test Suites

`suite_list` is the central registry that holds all test suites in the beast unit testing framework. It sits at the core of the static-registration pattern used throughout the XRPL codebase: suites declare themselves at program startup, and `suite_list` accumulates them into a sorted, deduplicated collection that test runners later iterate over.

## Inheritance and the Read-Only Exposure Pattern

`suite_list` inherits from `detail::const_container<std::set<suite_info>>`. This base class holds the actual `std::set<suite_info>` as a private member and exposes it to derived classes through a protected `cont()` accessor, while publishing only read-only operations (`begin`, `end`, `size`, `empty`) to external callers. The design enforces a clear invariant: consumers of the list can iterate and inspect suites, but only the controlled `insert()` path can add to it. This is why there is no `remove()`, `clear()`, or public `operator[]` — the registry is write-once during static initialization, then read-many at run time.

The underlying container is `std::set<suite_info>`, which keeps suites sorted according to `suite_info`'s `operator<`. That comparator orders by *negated* priority first (so higher-priority suites sort to the front), then lexicographically by library, module, and name. This means that simply iterating the `suite_list` in order already produces a deterministic execution sequence with high-priority suites running first — no sorting step needed at run time.

## The `insert()` Template Method

```cpp
template <class Suite>
void insert(char const* name, char const* module, char const* library, bool manual, int priority);
```

The template parameter `Suite` is the concrete test class rather than a `suite_info` object, for two reasons. First, the debug checks need `std::type_index(typeid(Suite))` to verify that no two C++ types collide. Second, `make_suite_info<Suite>` (from `suite_info.h`) captures the type in a lambda that instantiates `Suite{}` and calls it with a `runner&` — this deferred construction is what allows the test to be run on demand without storing a live object.

The call to `cont().emplace(...)` inserts directly into the underlying `std::set`, relying on `suite_info`'s move constructor and the set's ordering to place the entry correctly.

## Debug-Only Duplicate Guards

Two fields exist only in debug builds:

```cpp
#ifndef NDEBUG
std::unordered_set<std::string> names_;
std::unordered_set<std::type_index> classes_;
#endif
```

`names_` catches the case where the same fully-qualified string (`library.module.name`) is registered twice — perhaps through copy-paste of a registration macro. `classes_` catches the complementary case where the same C++ type is registered under two different names. A `std::set` insertion alone would silently ignore a second entry for an equivalent `suite_info` if its sort key matched, or accept a duplicate with a different name for the same type. The `BOOST_ASSERT` calls make both failure modes immediately visible during development without paying any cost in release builds.

Using `std::unordered_set` for these guards rather than querying `names_` or `classes_` from the `std::set` itself is intentional: the ordered set is keyed by `operator<` (priority + name tuple), while duplicate detection needs a flat string and type key respectively — different access patterns warrant separate structures.

## Integration with `global_suites.h`

`suite_list` is consumed by `global_suites.h`, which defines the process-wide singleton:

```cpp
inline suite_list& global_suites() {
    static suite_list s;
    return s;
}
```

The companion `insert_suite<Suite>` struct calls `global_suites().insert<Suite>(...)` in its constructor, so declaring a static `insert_suite<MySuite>` variable at namespace scope causes registration to happen before `main()`. Runners then call the public `beast::unit_test::global_suites()` (which returns a `const suite_list&`) and iterate over the sorted set to discover and execute suites. The constness of the public accessor enforces that no code outside the initialization path can mutate the global registry after startup.