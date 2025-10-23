#ifndef XRPL_OVERLAY_CLUSTER_H_INCLUDED
#define XRPL_OVERLAY_CLUSTER_H_INCLUDED

#include <xrpld/overlay/ClusterNode.h>

#include <xrpl/basics/BasicConfig.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/protocol/PublicKey.h>

#include <functional>
#include <mutex>
#include <set>

namespace ripple {

class Cluster
{
private:
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
    Cluster(beast::Journal j);

    /** Determines whether a node belongs in the cluster
        @return std::nullopt if the node isn't a member,
                otherwise, the comment associated with the
                node (which may be an empty string).
    */
    std::optional<std::string>
    member(PublicKey const& node) const;

    /** The number of nodes in the cluster list. */
    std::size_t
    size() const;

    /** Store information about the state of a cluster node.
        @param identity The node's public identity
        @param name The node's name (may be empty)
        @return true if we updated our information
    */
    bool
    update(
        PublicKey const& identity,
        std::string name,
        std::uint32_t loadFee = 0,
        NetClock::time_point reportTime = NetClock::time_point{});

    /** Invokes the callback once for every cluster node.
        @note You are not allowed to call `update` from
              within the callback.
    */
    void
    for_each(std::function<void(ClusterNode const&)> func) const;

    /** Load the list of cluster nodes.

        The section contains entries consisting of a base58
        encoded node public key, optionally followed by
        a comment.

        @return false if an entry could not be parsed or
                contained an invalid node public key,
                true otherwise.
    */
    bool
    load(Section const& nodes);
};

}  // namespace ripple

#endif
