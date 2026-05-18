# `include/xrpl/protocol/MultiApiJson.h`

## Purpose

`MultiApiJson` solves a specific distribution problem: the XRPL server maintains long-lived subscriptions from clients that speak different API versions, and a single ledger event (e.g., a confirmed transaction) must be delivered in slightly different JSON shapes to each subscriber. Rather than re-serializing on every delivery or branching inside the send path, `MultiApiJson` holds one pre-built `Json::Value` per supported API version, populated once at event-creation time.

The public alias at the bottom of the file binds the template to the live version range:

```cpp
using MultiApiJson =
    detail::MultiApiJson<RPC::apiMinimumSupportedVersion, RPC::apiMaximumValidVersion>;
```

`apiMinimumSupportedVersion` is 1 and `apiMaximumValidVersion` is the beta version 3, so the concrete type stores exactly three `Json::Value` objects in a `std::array<Json::Value, 3>`.

## Storage and Indexing

The array has `MaxVer + 1 - MinVer` elements. The mapping from version number to array slot is a simple offset — `index(v)` returns `v - MinVer`. Out-of-range values (versions below `MinVer`) clamp to index 0 at the `index()` level, though the real guards are in `valid()` and the assertions inside `visitor_t`.

The constructor accepts an optional initializer. When supplied, every slot in the array is copy-initialized to the same `Json::Value`. This is the common pattern in `NetworkOPs.cpp`:

```cpp
MultiApiJson multiObj{jvObj};  // clone the base JSON into all three slots
```

Subsequent version-specific mutations are then applied selectively.

## Broadcast vs. Selective Operations

`set(key, value)` writes a key to **all** slots at once, covering fields that are identical across every API version — the majority of transaction fields. This is cheaper than calling `visit` per version for shared data.

`isMember(key)` returns a tri-state enum — `none`, `some`, or `all` — indicating how many slots contain a given key. The enum lives on the struct rather than a class enum deliberately, as the comment notes: `MultiApiJson` is narrow enough to serve as its own scope.

Both methods iterate the fixed-size array, so their cost is O(number-of-versions), currently O(3).

## The Visitor Pattern

The interesting design is in `visitor_t`, a `constexpr`-constructed stateless function object that routes calls to the right array slot. It provides four `operator()` overloads, split along two axes:

1. **Compile-time vs. runtime version** — A version passed as `std::integral_constant<unsigned int, V>` triggers a `static_assert` that V is in range; a plain `unsigned` triggers a runtime `XRPL_ASSERT`. The `some_integral_constant` concept is used in the `requires` clauses to prevent the runtime overloads from being selected when an `integral_constant` is passed, disambiguating what would otherwise be an ambiguous partial ordering.

2. **With or without extra arguments forwarded to the callable** — When extra args are present they're forwarded to `fn` after the selected `Json::Value` (and possibly the version value). This matches the calling convention of `forAllApiVersions` / `forApiVersions`, which pass each version as an `integral_constant` along with any extra args bound at the call site.

## The `visit()` Interface

`visit()` has two distinct call forms. Called with arguments it directly invokes `visitor_t`:

```cpp
jvObj.visit(RPC::apiVersion<1>, [](Json::Value& jv) { /* mutate v1 slot */ });
```

Called with no arguments, it returns a **closure** capturing `this`:

```cpp
forAllApiVersions(multiObj.visit(), [&]<unsigned Version>(Json::Value& jv, auto versionConst) {
    RPC::insertDeliverMax(jv[jss::transaction], txType, Version);
});
```

This two-form design is what makes `MultiApiJson` composable with the `forAllApiVersions`/`forApiVersions` utilities in `ApiVersion.h`. Those utilities expand the version range at compile time using `std::index_sequence`, passing each version as an `integral_constant` to the callable. The closure returned by `visit()` satisfies exactly that calling convention, so `forAllApiVersions(multiObj.visit(), lambda)` iterates all versions with a single consistent lambda.

Both `visit()` and `visit(args...)` have `const` and non-`const` overloads propagating through to the underlying `Json::Value` reference.

## Real-World Usage

In `NetworkOPs.cpp`, `transJson()` constructs a `MultiApiJson` from a common base, then uses `visit(apiVersion<1>, ...)` to apply backwards-compatibility fixups (e.g., converting `ledger_index` from a number to a string for old API consumers). Delivery happens in `BookListeners::publish()`:

```cpp
jvObj.visit(
    p->getApiVersion(),   // runtime unsigned from subscriber
    [&](Json::Value const& jv) { p->send(jv, true); });
```

Each subscriber queries its stored API version, `visit` picks the matching pre-built slot, and the value is sent without any re-serialization or conditional logic at the point of delivery.

## Design Tradeoffs

Keeping one copy per version trades memory for CPU. With three versions and typical transaction JSON, the extra allocations are modest. The alternative — computing the version-specific delta on every send — would add latency proportional to subscriber count for every transaction event. The current design amortizes the transformation cost to once per event regardless of subscriber count.

Placing `MultiApiJson` in `detail::` and exposing only the aliased concrete type (`xrpl::MultiApiJson`) ensures callers outside the test suite use only the version range actually enforced by the running server, preventing accidental construction of ranges that don't match the live constants.