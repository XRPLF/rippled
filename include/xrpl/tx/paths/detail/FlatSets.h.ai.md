# `FlatSets.h` — In-Place Union for Sorted Flat Sets

`FlatSets.h` is a small utility header in the `xrpl/tx/paths/detail` module, providing a single template function `SetUnion` that merges one `boost::container::flat_set` into another in place. Despite its brevity, the function is carefully written to exploit a performance characteristic of the `flat_set` container that a naive alternative would miss.

## What Problem It Solves

The XRPL payment-path engine (in `StrandFlow.h` and `BookStep.cpp`) tracks sets of offer IDs (`boost::container::flat_set<uint256>`) that must be deleted from the ledger — either because they were consumed, expired, or found to be unfunded during a payment flow computation. As each strand or book step runs, it produces its own local set of such offers. These per-step sets must be merged into a single accumulator set so that all bad offers can be cleaned up atomically after the flow completes, regardless of whether the overall payment succeeded.

`SetUnion` encapsulates that merge operation.

## Why `flat_set` Instead of `std::set`

`boost::container::flat_set` stores its elements in a contiguous sorted array rather than a tree, giving it better cache locality for iteration and binary-search lookups. This matches the payment engine's usage pattern: offer ID sets are built up during a traversal and then iterated over at the end for deletion. The flat layout means the entire set fits in cache lines, which matters when iterating over potentially hundreds of entries.

## The Design of `SetUnion`

The implementation has two deliberate optimizations:

**Early exit on empty source.** The check `if (src.empty()) return;` avoids any allocation or work when the incoming set contributes nothing. This is the common case when a strand traverses a path that doesn't touch any bad offers.

**`reserve` before insert.** Calling `dst.reserve(dst.size() + src.size())` pre-allocates exactly enough capacity for the worst case (no overlap between the sets) before the insert. Without this, inserting elements one by one into a `flat_set` can trigger repeated reallocation and copying of the underlying sorted array, turning an O(n) operation into an O(n²) one.

**`ordered_unique_range_t` hint.** The insert uses `boost::container::ordered_unique_range_t{}` as a tag argument. This tells `flat_set` that the range being inserted is already sorted and contains no duplicates. Because `src` is itself a `flat_set`, this invariant is guaranteed. The tag lets Boost perform a merge-style insert (essentially `std::inplace_merge` semantics) instead of inserting elements one at a time, cutting the algorithmic cost from O(n log n) to O(n + m) where n and m are the sizes of the two sets.

Together, these three choices ensure the union is performed with a single allocation and a single linear pass over both sets — a meaningful win in a hot path where strand flows may loop many times before the payment engine finds a satisfactory result.

## Usage Context

In `StrandFlow.h`, `SetUnion` is called in two places: once to fold the offers-to-remove from each individual strand flow result into the outer accumulator `ofrsToRm`, and again to fold `ofrsToRm` into `ofrsToRmOnFail` — the set of offers that must be purged even when the overall payment fails. In `BookStep.cpp`, it is called inside both the reverse and forward offer-iteration loops to accumulate bad offers found during order-book traversal into the step's own `ofrsToRm` set. This consistent aggregation pattern means that by the time control returns to the top-level flow loop, no bad offer ever escapes cleanup.

The function is generic over element type `T` (inheriting whatever ordering `flat_set` requires), but in practice the entire codebase uses it only with `uint256` offer keys.