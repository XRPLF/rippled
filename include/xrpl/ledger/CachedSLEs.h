/** @file
 *  Process-wide cache of deserialized ledger state entries (SLEs).
 *
 *  Declares `CachedSLEs`, a named alias for the `TaggedCache` instantiation
 *  that backs the two-level SLE read cache used by `CachedView`. Any future
 *  change to the underlying container's key hasher, pointer policy, or mutex
 *  type can be made here without touching consumers.
 */

#pragma once

#include <xrpl/basics/TaggedCache.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/protocol/STLedgerEntry.h>

namespace xrpl {

/** Process-wide, thread-safe cache of immutable ledger state entries (SLEs).
 *
 *  Maps the cryptographic digest of a serialized SLE (`uint256`) to the
 *  deserialized `SLE const` object, allowing multiple read paths to share a
 *  single in-memory representation without re-deserializing from disk.
 *
 *  The `SLE const` mapped type enforces at compile time that stored objects
 *  are never mutated through the cache, satisfying `TaggedCache`'s requirement
 *  that callers must not modify stored objects unless they hold a lock over all
 *  cache operations. This makes cached entries safe to share across threads
 *  without additional per-object locking.
 *
 *  The key is the on-disk hash (digest) of the serialized entry — not an
 *  account ID or keylet — which integrates directly with `DigestAwareReadView`.
 *  `CachedView` delegates `read()` calls to `CachedSLEs::fetch(digest, ...)`,
 *  falling through to the underlying store only on a miss.
 *
 *  The application-wide instance is constructed with a target size of `0`
 *  (no fixed count limit) and a one-minute expiration window.
 *  `TaggedCache::sweep()` is called periodically to demote strong references
 *  to weak references and eventually reclaim memory.
 *
 *  @see CachedView
 *  @see TaggedCache
 */
using CachedSLEs = TaggedCache<uint256, SLE const>;

}  // namespace xrpl
