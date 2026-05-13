/** @file
 *  Defines `ClusterNode`, the value type for a single trusted cluster peer.
 */

#pragma once

#include <xrpl/basics/chrono.h>
#include <xrpl/protocol/PublicKey.h>

#include <cstdint>
#include <string>
#include <utility>

namespace xrpl {

/** Immutable-identity record for a single trusted peer in an XRPL cluster.
 *
 *  A cluster is a set of XRPL validator nodes operated by the same
 *  administrative entity that extend automatic trust to each other.
 *  `ClusterNode` captures the four pieces of state exchanged via `TMCluster`
 *  gossip: node identity, human-readable name, current load fee, and the
 *  NetClock timestamp of the last report.
 *
 *  `identity_` is `const` and serves as the primary key inside
 *  `Cluster`'s `std::set`. Because `std::set` elements are logically
 *  immutable after insertion, `Cluster::update()` must erase and reinsert
 *  rather than mutate in place.
 *
 *  All data fields are written exactly once at construction and then only
 *  read, making instances safe to observe concurrently provided the caller
 *  holds the enclosing `Cluster::mutex_` across the lookup and the read.
 *
 *  @see Cluster
 */
class ClusterNode
{
public:
    ClusterNode() = delete;

    /** Construct a cluster node record.
     *
     *  @param identity The node's public key; used as the set key in `Cluster`
     *      and must remain stable for the lifetime of this object.
     *  @param name     Human-readable label from config or peer gossip; may be
     *      an empty string.
     *  @param fee      The load fee scalar the node is currently advertising.
     *      Defaults to 0 for newly-loaded config entries that have not yet
     *      received a live status report.
     *  @param rtime    The NetClock time of the originating status report.
     *      Defaults to the epoch so that `Cluster::update()` will always
     *      accept the first real gossip update for a freshly configured node.
     */
    ClusterNode(
        PublicKey const& identity,
        std::string name,
        std::uint32_t fee = 0,
        NetClock::time_point rtime = NetClock::time_point{})
        : identity_(identity), name_(std::move(name)), loadFee_(fee), reportTime_(rtime)
    {
    }

    /** Return the human-readable label for this node.
     *
     *  May be an empty string if no name was configured or gossiped.
     *  `Cluster::update()` preserves a non-empty name when a subsequent
     *  gossip update arrives with an empty one.
     *
     *  @return The node name, or an empty string if none is known.
     */
    [[nodiscard]] std::string const&
    name() const
    {
        return name_;
    }

    /** Return the load fee scalar most recently reported by this node.
     *
     *  Aggregated across all cluster peers by `NetworkOPs` via
     *  `Cluster::forEach()` to compute a cluster-wide fee floor.
     *
     *  @return The load fee scalar; 0 if no live report has been received.
     */
    [[nodiscard]] std::uint32_t
    getLoadFee() const
    {
        return loadFee_;
    }

    /** Return the NetClock time of the most recent status report.
     *
     *  `Cluster::update()` rejects any incoming report whose timestamp is
     *  not strictly greater than this value, ensuring cluster state advances
     *  monotonically. Uses `NetClock` (logical network time) rather than
     *  wall-clock time to avoid skew between nodes.
     *
     *  @return The report timestamp; the epoch if no live report has arrived.
     */
    [[nodiscard]] NetClock::time_point
    getReportTime() const
    {
        return reportTime_;
    }

    /** Return the public key that uniquely identifies this cluster node.
     *
     *  Acts as the sort key for `Cluster`'s `std::set` and is used by
     *  `Cluster::Comparator` for heterogeneous `PublicKey` lookups.
     *
     *  @return A const reference to the node's public key.
     */
    [[nodiscard]] PublicKey const&
    identity() const
    {
        return identity_;
    }

private:
    PublicKey const identity_;
    std::string name_;
    std::uint32_t loadFee_ = 0;
    NetClock::time_point reportTime_;
};

}  // namespace xrpl
