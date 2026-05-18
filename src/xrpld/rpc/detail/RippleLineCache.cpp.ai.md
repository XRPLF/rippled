# `RippleLineCache.cpp`

This file is empty — it contains no source code. It is a 0-byte stub that has been fully gutted, retaining only its filename and build-system entry point.

## Historical Role

Based on the name, adjacent files, and preserved AI metadata, `RippleLineCache` was the original per-request, ledger-scoped cache for trust line (`RippleState`) objects consumed by the pathfinding subsystem. Its interface, reconstructable from the metadata, included a constructor accepting a ledger reference, a `getLedger()` accessor, and a `getRippleLines(AccountID)` method that returned a vector of trust lines for a given account. The motivation was straightforward: path-finding over the trust-line graph requires many lookups of the same account's lines, so a session-level cache avoided repeated ledger reads for any single path search.

## What Replaced It

This functionality is now handled entirely by `AssetCache` (`AssetCache.h` / `AssetCache.cpp` in the same directory). `AssetCache` generalises the original concept in two significant ways. First, it caches both trust lines (via `getRippleLines()`) and MPT (Multi-Purpose Token) issuance entries (via `getMPTs()`), reflecting the ledger's evolution to support alternative token standards. Second, `getRippleLines()` accepts a `LineDirection` parameter so that the pathfinder can request either the full set of an account's trust lines (when the account is an "outgoing" node — a source or a rippling-enabled hop) or only the subset with rippling enabled on the account's own side (when the account is "incoming"). The cache key is an `AccountKey` struct that bundles `AccountID`, `LineDirection`, and a precomputed `hardened_hash` value. Entries are stored as `shared_ptr<vector<...>>` inside a `hash_map`, so entries can be removed without invalidating live references held by concurrent readers. Access is serialised with a `std::mutex`.

## Current Status

Both `RippleLineCache.cpp` and `RippleLineCache.h` are empty. Despite this, two files still `#include` the now-empty header: `Pathfinder.cpp` and `PathRequestManager.h`. Because the header provides no declarations, those includes are entirely inert — they compile cleanly but contribute nothing. `PathRequestManager.h` simultaneously includes `AssetCache.h` and declares `assetCache_` as a `std::weak_ptr<AssetCache>`, confirming that `AssetCache` is the live implementation. No translation unit anywhere in the repository references the `RippleLineCache` type by name; the only occurrences are in AI metadata files and the empty source stubs themselves. The files are safe-to-delete dead weight, likely retained to avoid breaking a build-system glob or as an artefact of the refactoring that produced `AssetCache`.