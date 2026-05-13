/** @file
 *  Defines `TreeNodeCache`, the in-memory hot cache of deserialized
 *  `SHAMapTreeNode` objects used by every live SHAMap.
 *
 *  This file is intentionally minimal: it binds together `TaggedCache`'s
 *  two-level strong/weak eviction policy with intrusive pointer machinery
 *  that allows early memory reclamation and single-word strong/weak duality.
 *  The resulting alias is the canonical type name used throughout the
 *  SHAMap and `Family` interfaces.
 */

#pragma once

#include <xrpl/basics/IntrusivePointer.h>
#include <xrpl/basics/TaggedCache.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

namespace xrpl {

/** In-memory cache of deserialized `SHAMapTreeNode` objects, keyed by hash.
 *
 *  Every ledger's account-state and transaction SHAMap shares a single
 *  `TreeNodeCache` (via `Family::getTreeNodeCache()`). Nodes fetched from
 *  persistent storage are placed here after deserialization; subsequent
 *  lookups by the same `uint256` hash return the already-decoded object,
 *  avoiding redundant disk reads and ensuring that identical on-disk nodes
 *  are represented by a single in-memory object — essential for SHAMap's
 *  copy-on-write scheme, where unmodified nodes are shared freely across
 *  ledger generations.
 *
 *  The alias uses `intr_ptr::SharedWeakUnionPtr` and `intr_ptr::SharedPtr`
 *  instead of the `TaggedCache` defaults (`SharedWeakCachePointer` /
 *  `std::shared_ptr`) for two reasons:
 *
 *  - **Earlier memory reclamation.** With `std::make_shared`, the control
 *    block and object are co-allocated, so the memory block cannot be freed
 *    until all weak references (held by the cache) expire. The intrusive
 *    model stores reference counts inside the `SHAMapTreeNode` itself, and
 *    `SHAMapInnerNode::partialDestructor()` releases all 16 child pointers
 *    the moment the strong count hits zero, even while the cache still holds
 *    a weak reference to the parent.
 *
 *  - **Single-word strong/weak duality.** `SharedWeakUnionPtr<T>` stores
 *    either a strong or a weak intrusive reference in one pointer-sized word,
 *    using the low-order bit as a tag (alignment guarantees the bit is always
 *    zero in a real pointer). When the cache sweeper demotes a hot entry to a
 *    tracking-only entry, it calls `convertToWeak()` in-place — flipping one
 *    bit — rather than replacing a `shared_ptr`/`weak_ptr` pair.
 *
 *  `IsKeyCache = false` selects `TaggedCache`'s value-cache mode, where the
 *  map stores the actual `SHAMapTreeNode` objects (not just keys). Nodes
 *  remain strongly referenced while hot; they degrade to weak references as
 *  they age, and are removed entirely when both the cache entry expires and no
 *  external strong pointer holds the object live.
 *
 *  @note Nodes retrieved from the cache have `cowid_ == 0` by invariant —
 *      they are shared and must not be mutated. Any map that needs to modify
 *      such a node must call `clone()` first to obtain a private copy.
 *  @see Family::getTreeNodeCache()
 *  @see SHAMapTreeNode::partialDestructor()
 */
using TreeNodeCache = TaggedCache<
    uint256,
    SHAMapTreeNode,
    false,
    intr_ptr::SharedWeakUnionPtr<SHAMapTreeNode>,
    intr_ptr::SharedPtr<SHAMapTreeNode>>;

}  // namespace xrpl
