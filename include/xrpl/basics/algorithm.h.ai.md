# `include/xrpl/basics/algorithm.h`

This header provides two generic, header-only algorithms for working with sorted ranges. Both are evolutionary descendants of standard library algorithms — `std::set_intersection` and `std::remove_if` — extended to handle cases that the standard versions cannot express cleanly. The file has no dependencies beyond `<utility>` and serves as a low-level utility for the broader `xrpl::basics` module.

## `generalized_set_intersection`

The standard `std::set_intersection` copies matched elements into an output iterator. That is sufficient when you only need one element from each matched pair and want no side effects beyond writing to output. `generalized_set_intersection` replaces the output iterator with an `Action` functor that receives *both* matched elements — `action(*first1, *first2)` — giving the caller full read/write access to the matched pair.

The traversal is the same two-pointer walk used by `std::set_intersection`: advance the iterator pointing to the smaller element, and when both are equal (equivalence under `comp`), fire the action and advance both. The implementation uses the strict-weak-ordering identity `a == b ⟺ !comp(a,b) && !comp(b,a)` to detect equality without requiring a separate equality operator.

The sole user in the codebase is `RCLCensorshipDetector::propose()`, which calls this function to carry sequence numbers forward. The detector tracks how many consecutive consensus rounds each transaction has failed to be included, and the sequence counter must survive across rounds. When the node re-proposes a transaction it already tracked, `generalized_set_intersection` finds the match and copies the old `seq` field into the new `TxIDSeq` entry via the action lambda — `[](auto& x, auto const& y) { x.seq = y.seq; }`. A standard `set_intersection` can't do this because the output range would only hold one element type, not a reference to both.

## `remove_if_intersect_or_match`

This algorithm fuses `std::remove_if` with set intersection into a single O(n + m) pass. It removes elements from the first range `[first1, last1)` if either condition holds: the element appears in the sorted second range `[first2, last2)`, or a predicate `pred` returns true for it. The caller must subsequently call `erase` on the returned end iterator to actually shrink the container, following the standard erase-remove idiom.

The internal state is maintained as three contiguous, non-overlapping regions in the original first range: `[original-first1, first1)` holds preserved elements compacted to the front; `[first1, i)` holds removed elements awaiting overwrite; `[i, last1)` holds untested elements. Each iteration either compacts `*i` into the preserved prefix (if it should be kept) or leaves a gap. Movement uses `std::move` rather than copy, which is important for types with expensive or move-only semantics.

The fusion with intersection logic works because both ranges are sorted: when `*i >= *first2`, the only way `*i` could equal some element in the second range is if that element is at or near `first2`. The algorithm checks `!comp(*first2, *i)` to detect equality, then advances `i` (marking it removed) before stepping `first2`. Crucially, once `first2` has moved past `*i`, no earlier element in the second range can match future elements of the first range — a guarantee that depends entirely on sorted order.

`RCLCensorshipDetector::check()` uses this to clean up its tracker in one pass after a consensus round. The second range is the set of accepted transaction IDs; the predicate fires `pred(x.txid, x.seq)` to let the caller detect potential censorship (e.g., if a transaction has been blocked for too many rounds). The comparator passed is `std::less<void>` — C++14's heterogeneous comparator — which dispatches to whichever `operator<` overload best matches the operand types. Because `RCLCensorshipDetector` defines heterogeneous `operator<` between `TxIDSeq` and raw `TxID`, the intersection check compares across the two different element types of the two ranges without constructing temporary objects.

## Design context

Both algorithms enforce sorted-range preconditions but do not assert or check them. Violations silently produce incorrect results, consistent with the standard library's approach for range algorithms. The decision to keep these in a separate `algorithm.h` rather than inline inside `RCLCensorshipDetector` reflects the conventional separation of algorithmic mechanics from domain logic — the censorship detector is free to express its `propose` and `check` operations at the level of intent, delegating the traversal bookkeeping here.