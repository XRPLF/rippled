/**
 * @file RippleLineCache.cpp
 * @brief Empty stub — implementation superseded by AssetCache.
 *
 * `RippleLineCache` was the original per-request, ledger-scoped cache for
 * `RippleState` (trust-line) objects used by the pathfinding subsystem.
 * Its interface provided a constructor accepting a ledger reference,
 * a `getLedger()` accessor, and a `getRippleLines(AccountID)` method that
 * returned a vector of trust lines for an account.  The cache existed to
 * avoid repeated ledger reads when the path-finder traversed the same
 * account's trust-line graph multiple times in a single search.
 *
 * The entire implementation has been replaced by `AssetCache`
 * (`AssetCache.h` / `AssetCache.cpp`), which generalises the concept in
 * two ways:
 *  - It caches both trust lines (`getRippleLines()`) and Multi-Purpose Token
 *    issuance entries (`getMPTs()`).
 *  - `getRippleLines()` accepts a `LineDirection` parameter so the
 *    pathfinder can request either the full set of an account's lines or
 *    only the rippling-enabled subset, depending on whether the account is
 *    acting as a source/rippling-hop or a destination.
 *
 * Two files still `#include` the now-empty `RippleLineCache.h`
 * (`Pathfinder.cpp` and `PathRequestManager.h`); because the header
 * declares nothing, those includes are entirely inert.  The type name
 * `RippleLineCache` is not referenced anywhere in the active codebase.
 * Both the header and this translation unit are dead weight, retained
 * to avoid disturbing the build system during the refactoring that
 * produced `AssetCache`.
 *
 * @see AssetCache
 */
