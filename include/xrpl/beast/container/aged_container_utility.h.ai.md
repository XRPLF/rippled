# `aged_container_utility.h` — Temporal Expiry for Aged Containers

This header provides a single free function, `expire()`, that removes stale entries from any aged container in the `beast` namespace. It is the primary mechanism by which XRPL subsystems flush time-expired data from their aged maps and sets without scanning every element.

## What It Does

`expire(c, age)` sweeps the chronological front of an aged container and erases every element inserted more than `age` time units ago. The return value is the number of items removed. The caller provides the duration as any `std::chrono::duration<Rep, Period>` specialization, keeping the interface flexible across different time scales.

The iteration exploits a key structural invariant of the aged containers: the `chronological` memberspace maintains elements in insertion order (oldest first). Because new elements are always appended to the back of the underlying `boost::intrusive::list` and `touch()` moves elements to the back, the front of the chronological sequence is always the oldest cohort. The loop therefore needs only to walk forward until it finds the first element not yet expired, after which the rest of the container is guaranteed to be younger. This makes the sweep `O(k)` in the number of expired elements — it stops as soon as it reaches live entries.

The expiry threshold is computed once: `c.clock().now() - age`. Using the container's own `clock()` rather than a separately injected clock object ensures that the comparison is consistent with the timestamps that were recorded at insertion time. All aged containers are templated on a `Clock` type, defaulting to `std::chrono::steady_clock`, so this subtraction is always a well-typed `time_point` comparison.

## Why a Free Function

Eviction logic lives here as a free function rather than as a member of each container variant. This is deliberate: `aged_map`, `aged_set`, `aged_unordered_map`, `aged_unordered_set`, and their multi-key analogues are all distinct types (type aliases for `detail::aged_ordered_container` and `detail::aged_unordered_container`). Putting `expire` in each one would require either a virtual interface, a CRTP mixin, or eight near-identical member implementations. The free function avoids all of that — one algorithm, zero duplication, no runtime polymorphism.

## SFINAE Guard

The function signature uses `std::enable_if<is_aged_container<AgedContainer>::value, std::size_t>::type` as the return type. `is_aged_container` is defined in `aged_container.h` as a traits struct defaulting to `std::false_type`; the detail implementations (`aged_ordered_container.h` and `aged_unordered_container.h`) each provide a `std::true_type` specialization for their concrete type. Passing a non-aged type to `expire()` therefore fails at template substitution rather than producing a confusing missing-member error deep inside the function body.

## Usage in the Codebase

The function's primary consumer is `src/xrpld/consensus/Validations.h`, which calls `beast::expire` on its `byLedger_` and `bySequence_` aged containers using a `validationSET_EXPIRES` duration parameter. This is how the validator set garbage-collects stale ledger validations — rather than tracking individual entries or running a scheduled scan of all entries, the consensus engine calls `expire()` at appropriate checkpoints and relies on the O(k) sweep to do the right amount of work proportional to how much has actually aged out.