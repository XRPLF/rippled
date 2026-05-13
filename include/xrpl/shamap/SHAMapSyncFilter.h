#pragma once

#include <xrpl/shamap/SHAMapTreeNode.h>

#include <optional>

namespace xrpl {

/** Abstract callback interface connecting SHAMap sync traversal to node
 *  persistence and transient caching infrastructure.
 *
 *  The SHAMap engine is decoupled from knowledge about where nodes come from
 *  or where they should go. `SHAMapSyncFilter` provides exactly two hooks that
 *  let the application layer supply that knowledge:
 *
 *  - `getNode()` — pull: called when a node is needed but absent from the
 *    in-memory cache and backing database. The filter may satisfy the request
 *    from a transient source such as a peer fetch pack or consensus cache.
 *
 *  - `gotNode()` — notify: called after a node has been successfully obtained
 *    and deserialized, regardless of source. The filter uses this to persist
 *    nodes received from the network.
 *
 *  The two-method split keeps filter implementations small: they operate purely
 *  on flat `Blob` data keyed by `SHAMapHash`; all deserialization and tree
 *  structure are handled by the SHAMap engine.
 *
 *  Non-copyable because implementations hold non-owning references to
 *  databases and caches with independent lifetimes.
 *
 *  @see AccountStateSF
 *  @see TransactionStateSF
 *  @see ConsensusTransSetSF
 */
class SHAMapSyncFilter
{
public:
    virtual ~SHAMapSyncFilter() = default;
    SHAMapSyncFilter() = default;
    SHAMapSyncFilter(SHAMapSyncFilter const&) = delete;
    SHAMapSyncFilter&
    operator=(SHAMapSyncFilter const&) = delete;

    /** Notify the filter that a node has been successfully obtained and
     *  integrated into the map.
     *
     *  Called by `addRootNode` and `addKnownNode` (with `fromFilter = false`)
     *  after a peer-supplied node passes hash and position validation, and by
     *  the internal `checkFilter` helper (with `fromFilter = true`) after a
     *  node supplied by this filter's own `getNode()` has been deserialized.
     *
     *  Implementations use `fromFilter` to avoid redundant writes: when
     *  `fromFilter` is `true` the data originated here and is already known
     *  locally, so re-storing it is unnecessary. When `fromFilter` is `false`
     *  the node arrived from the network and should be persisted durably.
     *
     *  @param fromFilter  `true` if the data was originally returned by this
     *      filter's `getNode()` call; `false` if it arrived from a peer.
     *  @param nodeHash    Hash of the node that was received.
     *  @param ledgerSeq   Sequence number of the ledger this node belongs to,
     *      allowing the filter to associate stored nodes with a specific ledger
     *      for expiry or prioritization decisions.
     *  @param nodeData    Serialized node bytes. Passed by rvalue reference;
     *      the implementation may move or destroy the buffer — callers must
     *      not rely on its contents after this call returns.
     *  @param type        Wire type of the received node.
     */
    virtual void
    gotNode(
        bool fromFilter,
        SHAMapHash const& nodeHash,
        std::uint32_t ledgerSeq,
        Blob&& nodeData,
        SHAMapNodeType type) const = 0;

    /** Attempt to retrieve a node from a transient source.
     *
     *  Called by the SHAMap engine when a node identified by `nodeHash` is
     *  not present in the in-memory cache or backing NodeStore. The filter
     *  may satisfy the request from an ephemeral store such as a peer fetch
     *  pack or consensus transaction cache, avoiding a network round-trip.
     *
     *  If data is returned, the engine will immediately call
     *  `gotNode(true, nodeHash, ...)` to notify the filter that the node was
     *  consumed.
     *
     *  @param nodeHash  Hash of the node the engine is looking for.
     *  @return The raw serialized node bytes if available, or `std::nullopt`
     *      if the filter cannot supply the node.
     */
    [[nodiscard]] virtual std::optional<Blob>
    getNode(SHAMapHash const& nodeHash) const = 0;
};

}  // namespace xrpl
