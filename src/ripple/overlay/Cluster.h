//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_OVERLAY_CLUSTER_H_INCLUDED
#define RIPPLE_OVERLAY_CLUSTER_H_INCLUDED

#include <BeastConfig.h>
#include <ripple/app/main/Application.h>
#include <ripple/core/Config.h>
#include <ripple/overlay/ClusterNode.h>
#include <ripple/protocol/RippleAddress.h>
#include <beast/hash/uhash.h>
#include <beast/utility/Journal.h>
#include <functional>
#include <memory>
#include <mutex>
#include <set>
#include <type_traits>

namespace ripple {

class Cluster
{
private:
    struct Comparator
    {
        using is_transparent = std::true_type;

        bool
        operator() (
            ClusterNode const& lhs,
            ClusterNode const& rhs) const
        {
            return lhs.identity() < rhs.identity();
        }

        bool
        operator() (
            ClusterNode const& lhs,
            RippleAddress const& rhs) const
        {
            return lhs.identity() < rhs;
        }

        bool
        operator() (
            RippleAddress const& lhs,
            ClusterNode const& rhs) const
        {
            return lhs < rhs.identity();
        }
    };

    std::set<ClusterNode, Comparator> nodes_;
    std::mutex mutable mutex_;
    beast::Journal mutable j_;

public:
    Cluster (beast::Journal j);

    /** Determines whether a node belongs in the cluster
        @return empty optional if the node isn't a member,
                otherwise, the node's name (which may be
                empty).
    */
    boost::optional<std::string>
    member (RippleAddress const& node) const;

    /** The number of nodes in the cluster list. */
    std::size_t
    size() const;

    /** Store information about the state of a cluster node.
        @param identity The node's public identity
        @param name The node's name (may be empty)
        @return true if we updated our information
    */
    bool
    update (
        RippleAddress const& identity,
        std::string name,
        std::uint32_t loadFee = 0,
        std::uint32_t reportTime = 0);

    /** Invokes the callback once for every cluster node.
        @note You are not allowed to call `update` from
              within the callback.
    */
    void
    for_each (
        std::function<void(ClusterNode const&)> func) const;
};

std::unique_ptr<Cluster>
make_Cluster (Config const& config, beast::Journal j);

} // ripple

#endif
