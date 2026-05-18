# `KeyCache.h` — Key-Only Cache Type Alias

`KeyCache.h` provides a single-line type alias that expresses a common, narrow caching pattern throughout the XRPL codebase: track the *presence* of `uint256` keys over time, without attaching any value to them.

```cpp
using KeyCache = TaggedCache<uint256, int, true>;
```

The three template arguments to `TaggedCache` are: the key type (`uint256`, the 256-bit hash used pervasively in XRPL), a value type placeholder (`int`), and the `IsKeyCache` boolean flag set to `true`. That third argument is the critical one. Inside `TaggedCache`, `IsKeyCache = true` activates a compile-time branch via `std::conditional` that substitutes `KeyOnlyEntry` — a struct holding nothing but a `last_access` timestamp — in place of the full `ValueEntry` that carries a `shared_ptr` to an actual object. The `int` value type is therefore never stored or accessed; it exists only to satisfy the template parameter list.

This design lets callers answer a single yes/no question efficiently: *"Have I seen this hash recently enough that I don't need to re-check it?"* The primary consumer is `FullBelowCache` in `include/xrpl/shamap/FullBelowCache.h`, which uses a `KeyCache` to remember which SHAMap tree nodes have all their descendants resident in the database. When a node is marked "full below," the ledger acquisition machinery can skip redundant subtree traversals, a meaningful performance win during sync.

Because `KeyCache` is built directly on `TaggedCache`, it inherits the full sweep-based expiry mechanism, thread-safe access under `std::recursive_mutex`, and the `touch_if_exists` / `insert` interface — with `insert` enabled only in the key-only overload path when `IsKeyCache` is `true`.