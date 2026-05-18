# `aged_multiset.h` — Time-Aware Ordered Multiset Alias

This file defines `beast::aged_multiset`, a thin template alias that exposes the multi-key, non-map variant of the underlying `detail::aged_ordered_container`.

The alias maps directly to `aged_ordered_container<true, false, Key, void, Clock, Compare, Allocator>`. The first boolean (`IsMulti = true`) switches the internal Boost.Intrusive storage from `boost::intrusive::set` to `boost::intrusive::multiset`, permitting duplicate keys. The second boolean (`IsMap = false`) means there is no mapped value type — the container stores keys only, with `void` passed as the `T` parameter.

This fits into a four-way family of ordered aged containers, all backed by the same implementation template:

| Alias | `IsMulti` | `IsMap` |
|---|---|---|
| `aged_set` | `false` | `false` |
| `aged_multiset` | `true` | `false` |
| `aged_map` | `false` | `true` |
| `aged_multimap` | `true` | `true` |

The `aged_ordered_container` backing class augments every stored element with a `time_point` (`when`) drawn from the supplied `Clock`. A `chronological` member-space exposes begin/end iterators that traverse elements in insertion-time order, enabling efficient LRU- or TTL-style eviction without a secondary data structure. The `Clock` parameter defaults to `std::chrono::steady_clock` but is accessed through the `abstract_clock<Clock>` wrapper, allowing test code to inject a mock clock.

`aged_multiset` itself adds nothing beyond the alias — all interface, iterator, and eviction logic live in `detail/aged_ordered_container.h`.