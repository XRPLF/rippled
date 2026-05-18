# `RippleLineCache.h`

This file is empty — it contains zero bytes. Both `RippleLineCache.h` and its companion `RippleLineCache.cpp` are 0-byte stub files that have been fully gutted, leaving only their filenames in the repository.

## Historical Role

Based on the name, sibling files, and preserved AI metadata, `RippleLineCache` formerly provided a ledger-scoped, in-memory cache of trust line (`RippleState`) objects for use by the pathfinding subsystem. Its interface included a constructor that accepted a ledger reference, a `getLedger()` accessor, and a `getRippleLines(AccountID)` method that returned a vector of trust lines for a given account — avoiding repeated ledger reads during the computationally intensive path search.

## What Replaced It

This functionality now lives in `AssetCache` (`AssetCache.h` / `AssetCache.cpp` in the same directory). `AssetCache` expands the old concept in two ways: it caches both trust lines (via `getRippleLines()`) and MPT (Multi-Purpose Token) entries (via `getMPTs()`), and it introduces a `LineDirection` parameter to `getRippleLines()` so that the pathfinder can request only the subset of trust lines usable in a given traversal direction — returning all lines for an "outgoing" account but filtering to rippling-enabled lines only for "incoming" accounts. The cache key is an `AccountKey` struct combining `AccountID`, `LineDirection`, and a precomputed `hardened_hash` value, stored in an `unordered_map` backed by `shared_ptr<vector<...>>` entries to allow safe concurrent removal.

## Current Status

No source file in the repository `#include`s either stub file. Grepping the entire codebase for `RippleLineCache` returns only the two empty source files and their AI metadata — no consumer translation units. The files are safe-to-delete dead weight, likely retained as a historical artifact of the refactoring that produced `AssetCache`, or to avoid breaking a build-system glob that enumerates `.cpp` files by pattern. Any engineer encountering these files should confirm deletion safety with `git log` to recover the original implementation history.