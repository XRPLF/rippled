# `include/xrpl/basics/RangeSet.h`

## Purpose and Context

This header provides the XRPL ledger's primary abstraction for representing sparse sets of sequence numbers: specifically, which ledger indexes a node has fully acquired and validated. The core data structure, `RangeSet<T>`, is used by `LedgerMaster` to maintain `mCompleteLedgers` — a live record of which historical ledgers are locally available — and supports serialization for network peer advertising and database persistence.

The design is deliberately thin: both `ClosedInterval<T>` and `RangeSet<T>` are pure type aliases over Boost ICL (`boost::icl::closed_interval` and `boost::icl::interval_set`). All the algebraic machinery — automatic coalescing of adjacent intervals, set difference, containment queries — is delegated directly to Boost ICL, and the header simply layers XRPL-specific string serialization and one domain-relevant query (`prevMissing`) on top.

## Types and Construction

`ClosedInterval<T>` represents a single contiguous range `[low, high]` where both endpoints are included. The `range(low, high)` helper exists purely to avoid repeating the template argument when constructing intervals inline — compare `range(10u, 15u)` vs. `ClosedInterval<std::uint32_t>(10u, 15u)`.

`RangeSet<T>` is an ordered, normalized collection of disjoint `ClosedInterval<T>` objects. The key property Boost ICL provides automatically is coalescing: inserting ledger 6 into `{1-5, 7-10}` yields `{1-10}` with no extra code. This invariant — the set always contains the minimum number of disjoint intervals — is what makes the format both compact in memory and straightforward to serialize.

## Serialization

The `to_string`/`from_string` pair implements a human-readable canonical format: `"1-2,4-6,9"` where a single-element interval is written as a bare number and a range uses a dash. `to_string` for an empty set returns `"empty"` rather than an empty string, providing a safe diagnostic representation.

`from_string` is more carefully designed. It bears the `[[nodiscard]]` attribute, forcing callers to check success. On any parse failure — an unrecognized token, a lexical cast that fails, a dash-split producing more than two parts — the function immediately clears the output set and returns `false`. This all-or-nothing contract means the output `RangeSet` is never left in a partial or corrupt state, which matters because a partial ledger set could cause `LedgerMaster` to believe it has acquired ledgers it hasn't.

Parsing uses `beast::lexicalCastChecked` for safe numeric conversion rather than `std::stoi` or `atoi`, which would silently truncate or throw on bad input. The loop eagerly clears `intervals` between tokens, avoiding stale data from a previous successful parse contaminating the next one.

## `prevMissing`: Gap-Driven Acquisition

The most algorithmically interesting function is `prevMissing`. Given a `RangeSet`, a target value `t`, and a lower bound `minVal`, it returns the largest value strictly less than `t` that is **not** in the set — that is, the largest gap below the query point.

This is the engine behind `LedgerMaster`'s historical ledger acquisition loop. When the node is filling in its ledger history, it repeatedly calls `prevMissing(mCompleteLedgers, maxVal)` to find the next sequence it still needs to fetch. Scanning backward from the most recent known ledger and prioritizing the largest missing sequence number is an efficient greedy strategy: it minimizes the number of passes needed to converge on a contiguous range.

The implementation is elegant: rather than iterating candidate values, it constructs the interval `[minVal, t-1]`, subtracts the existing set from it (Boost ICL set-difference), and returns the last element of the complement. This computes the answer in terms of interval arithmetic rather than element-by-element search, making it efficient even when the set contains thousands of intervals.

The two early-exit conditions — empty set (everything is missing, so answer is `t-1`) and `t == minVal` (no valid predecessor exists) — are handled by returning `std::nullopt`, which the caller must check before dereferencing.

## Concurrency Notes

`RangeSet` itself has no internal synchronization. In `LedgerMaster`, every access to `mCompleteLedgers` is guarded by a separate `mCompleteLock` mutex — a deliberate externalized-locking design that keeps the data structure lightweight and composable while still protecting concurrent reads and writes during ledger validation, gap fill, and peer advertisement.

## Relationship to Adjacent Code

`RelationalDatabase.h` includes this header, indicating that ledger range information flows through the database layer as well — sequences of complete ledger ranges are stored and queried as part of persistent state. The string format serves double duty as both a wire representation for peer protocol messages and a storage format for the database, making `to_string`/`from_string` round-trip fidelity essential.