## `aged_associative_container_extract_t`

This small header solves a single, focused problem: how to uniformly extract a key from a stored element when the container may be either map-like (where elements are `std::pair<Key const, T>`) or set-like (where elements are the key itself).

Both `aged_ordered_container` and `aged_unordered_container` are unified templates parameterised by a boolean `IsMap` flag. Internally they store a single `value_type` that is either a `std::pair<Key const, T>` or bare `Key`, depending on that flag. In both containers, the private `extract()` static method needs to pull the key out of whatever is stored — for comparisons in the ordered case and for hashing/equality in the unordered case. Rather than duplicating that conditional logic or scattering `if constexpr` branches inside two large container bodies, the dispatch is centralised here into a tiny stateless functor.

The primary template, `aged_associative_container_extract_t<true>` (the `maybe_map` case), implements `operator()` by returning `value.first` — the key half of a `std::pair`. The full specialisation for `false` is the set case: `operator()` returns the whole value unchanged, since the element *is* the key. The call sites in both container headers are identical:

```cpp
return aged_associative_container_extract_t<IsMap>()(value);
```

Because `IsMap` is already a compile-time constant in those templates, the compiler selects the correct specialisation at instantiation time with zero runtime cost.

The template parameter is named `maybe_map` rather than `is_map` because it represents a conservative, "this might be a map" interpretation: the primary template handles the map path, and the specialisation handles the non-map (set) path. This naming also signals that the boolean is not a perfect semantic guarantee about the container type — it's a dispatch hint.

The `explicit` default constructor on both specialisations is a deliberate defensiveness choice. It prevents implicit construction while still allowing straightforward instantiation, which is important for a stateless functor that may be stored inside larger containers using empty-base optimisation (the sibling `empty_base_optimization.h` in the same directory exists for exactly this purpose).

The `operator()` in both variants is templated on `Value` rather than fixed to a concrete type. This keeps the extractor generic enough to work with any pair-like or key-identity element without requiring the container to bind the exact stored type at the extractor's own instantiation point — the container instantiates the functor first and passes the value later.