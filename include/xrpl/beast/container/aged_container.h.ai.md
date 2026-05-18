# `aged_container.h` — Type Trait Base for Aged Containers

This small header establishes the foundation of the aged-container type system in the `beast` namespace. It defines `is_aged_container<T>`, a type trait that answers the compile-time question: *is this type an aged container?* By default the answer is `std::false_type` — no type qualifies unless it explicitly opts in through a template specialization elsewhere.

## Why This Exists

The aged-container family (`aged_set`, `aged_map`, `aged_unordered_set`, etc.) all track the insertion time of each element using a `Clock` and expose a `chronological` range view. This enables time-based expiry — erasing elements older than some duration. Utility functions like `expire()` in `aged_container_utility.h` need to constrain their template parameters to types that actually provide this interface. Without a mechanism to distinguish aged containers from arbitrary types, such a function would compile against any container and fail at the point of use when `c.clock()` or `c.chronological` doesn't exist.

The trait solves this cleanly via SFINAE:

```cpp
template <class AgedContainer, class Rep, class Period>
typename std::enable_if<is_aged_container<AgedContainer>::value, std::size_t>::type
expire(AgedContainer& c, std::chrono::duration<Rep, Period> const& age);
```

If `is_aged_container<T>::value` is `false`, the `enable_if` substitution fails and the overload is dropped from consideration, producing a clear compile error rather than a cryptic missing-member error.

## Opt-In Specialization Pattern

The base template in this file is deliberately minimal — a single `std::false_type` default. The `std::true_type` specializations live in the concrete container implementation headers:

- `detail/aged_ordered_container.h` specializes `is_aged_container` for `beast::detail::aged_ordered_container<...>`, covering `aged_set`, `aged_map`, `aged_multiset`, and `aged_multimap`.
- `detail/aged_unordered_container.h` does the same for `beast::detail::aged_unordered_container<...>`, covering the unordered variants.

This separation is intentional. Code that only needs to write constrained templates over aged containers can include just this lightweight header without pulling in the full container machinery. The implementations, which are substantially heavier (hundreds of lines of intrusive-list bookkeeping and iterator logic), are kept entirely separate.

## The Explicit Default Constructor

Both the base template and each `std::true_type` specialization carry `explicit is_aged_container() = default;`. This suppresses aggregate-initialization warnings on older compilers that would otherwise treat a struct inheriting from `std::false_type` or `std::true_type` as an aggregate. It's a minor defensive pattern consistent across the entire trait hierarchy.

## Relationship to the Broader Container Family

All public aged container names (`aged_set`, `aged_map`, etc.) are type aliases for the private `detail::aged_ordered_container` or `detail::aged_unordered_container` templates, parameterized by `IsMulti` and `IsMap` booleans. The `is_aged_container` specializations match on those concrete implementation types, meaning the trait correctly identifies all eight public aliases as aged containers without requiring any additional specializations per alias.