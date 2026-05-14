/** @file
 *  Registry of trusted cluster nodes in the XRPL overlay.
 *
 *  A "cluster" is a set of XRPL nodes run by the same administrative entity.
 *  Cluster members are treated with elevated trust by the overlay — for example,
 *  they are exempt from load-based fee throttling applied to anonymous peers.
 *  This file defines `Cluster`, the single authority for cluster membership
 *  queries and state maintenance throughout the overlay layer.
 */

#pragma once

#include <xrpld/overlay/ClusterNode.h>

#include <xrpl/basics/BasicConfig.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/PublicKey.h>

#include <functional>
#include <mutex>
#include <set>

namespace xrpl {

/** Manages the set of trusted cluster nodes for the local XRPL node.
 *
 *  Cluster membership is loaded at startup from the `[cluster_nodes]` config
 *  section and updated at runtime via gossip messages received from peers.
 *  All public methods are thread-safe.
 *
 *  @note `forEach` holds the internal lock for the duration of its callback.
 *      Calling `update` from inside a `forEach` callback will deadlock.
 */
class Cluster
{
private:
    /** Transparent comparator enabling heterogeneous lookup by `PublicKey`.
     *
     *  The `is_transparent` tag lets `nodes_.find(PublicKey)` accept a raw
     *  `PublicKey` directly, avoiding construction of a temporary `ClusterNode`
     *  on every membership check.
     */
    struct Comparator
    {
        explicit Comparator() = default;

        using is_transparent = std::true_type;

        bool
        operator()(ClusterNode const& lhs, ClusterNode const& rhs) const
        {
            return lhs.identity() < rhs.identity();
        }

        bool
        operator()(ClusterNode const& lhs, PublicKey const& rhs) const
        {
            return lhs.identity() < rhs;
        }

        bool
        operator()(PublicKey const& lhs, ClusterNode const& rhs) const
        {
            return lhs < rhs.identity();
        }
    };

    std::set<ClusterNode, Comparator> nodes_;
    std::mutex mutable mutex_;
    beast::Journal mutable j_;

public:
    explicit Cluster(beast::Journal j);

    /** Check whether a node is a member of this cluster.
     *
     *  Uses heterogeneous lookup so no temporary `ClusterNode` is allocated.
     *
     *  @param node The public key of the node to look up.
     *  @return `std::nullopt` if the node is not a cluster member; otherwise
     *      the human-readable name/comment associated with the node, which may
     *      be an empty string if no name was configured or gossiped.
     */
    std::optional<std::string>
    member(PublicKey const& node) const;

    /** Return the number of nodes currently registered in the cluster. */
    std::size_t
    size() const;

    /** Update the stored state for a cluster node.
     *
     *  Enforces a monotonic-time invariant: the update is applied only if
     *  `reportTime` is strictly newer than the currently stored report time.
     *  This prevents stale gossip arriving out of order from overwriting fresh
     *  data. If `name` is empty and an existing entry already has a non-empty
     *  name, the existing name is preserved — a status gossip that omits a
     *  name will never blank out a label loaded from config.
     *
     *  Because `std::set` elements are logically const after insertion, an
     *  accepted update erases the old entry and reinserts a new one at the
     *  same hint position (O(1) amortized).
     *
     *  Also used by `load()` to insert nodes during initial config parsing,
     *  with `reportTime` left at its zero default.
     *
     *  @param identity The public key identifying the cluster node.
     *  @param name     Human-readable label for the node; empty string is valid
     *      and will preserve any previously stored name.
     *  @param loadFee  The node's current load fee scalar (default 0).
     *  @param reportTime The NetClock time of this state report (default epoch).
     *      Updates with a `reportTime` equal to or earlier than the stored time
     *      are silently rejected.
     *  @return `true` if the entry was inserted or updated; `false` if the
     *      update was rejected because `reportTime` was not strictly newer.
     */
    bool
    update(
        PublicKey const& identity,
        std::string name,
        std::uint32_t loadFee = 0,
        NetClock::time_point reportTime = NetClock::time_point{});

    /** Invoke a callback once for every registered cluster node.
     *
     *  The lock is held for the entire iteration, so the callback must not
     *  call `update` — doing so would deadlock on the non-recursive mutex.
     *
     *  @param func Callable invoked with a `ClusterNode const&` for each
     *      registered node. The order of iteration is by public key (the set's
     *      natural ordering).
     */
    void
    forEach(std::function<void(ClusterNode const&)> func) const;

    /** Populate the cluster from a `[cluster_nodes]` config section.
     *
     *  Each line in `nodes` must be a base58-encoded node public key
     *  (`TokenType::NodePublic`) optionally followed by whitespace and a
     *  free-form comment. Leading and trailing whitespace in comments is
     *  trimmed. Duplicate entries emit a warning and are skipped (first entry
     *  wins). The parse is fail-fast: the first malformed line or invalid key
     *  causes an immediate `false` return and no further lines are processed —
     *  nodes parsed before the bad entry are retained.
     *
     *  @param nodes The configuration section to parse.
     *  @return `true` if all entries were loaded successfully (including the
     *      empty-section case); `false` if any entry could not be parsed or
     *      contained an invalid node public key.
     */
    bool
    load(Section const& nodes);
};

}  // namespace xrpl
