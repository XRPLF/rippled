//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2017 Ripple Labs Inc

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

#ifndef RIPPLE_TEST_CSF_UNL_H_INCLUDED
#define RIPPLE_TEST_CSF_UNL_H_INCLUDED

#include <boost/container/flat_set.hpp>
#include <test/csf/random.h>

#include <chrono>
#include <numeric>
#include <random>
#include <vector>

namespace ripple {
namespace test {
namespace csf {

/** Trust graph

    Trust is a directed relationship from a node i to node j.
    If node i trusts node j, then node i has node j in its UNL.
    This class wraps a digraph representing the trust relationships for all
   peers in the simulation.
*/
template <class Peer>
class TrustGraph
{
    using Graph = Digraph<Peer>;

    Graph graph_;

public:
    /** Create an empty trust graph
     */
    TrustGraph() = default;

    Graph const&
    graph()
    {
        return graph_;
    }

    /** Create trust

        Establish trust between Peer `from` and Peer `to`; as if `from` put `to`
        in its UNL.

        @param from The peer granting trust
        @param to The peer receiving trust

    */
    void
    trust(Peer const& from, Peer const& to)
    {
        graph_.connect(from, to);
    }

    /** Remove trust

        Revoke trust from Peer `from` to Peer `to`; as if `from` removed `to`
        from its  UNL.

        @param from The peer revoking trust
        @param to The peer being revoked
    */
    void
    untrust(Peer const& from, Peer const& to)
    {
        graph_.disconnect(from, to);
    }

    //< Whether from trusts to
    bool
    trusts(Peer const& from, Peer const& to) const
    {
        return graph_.connected(from, to);
    }

    /** Range over trusted peers

        @param a The node granting trust
        @return boost transformed range over nodes `a` trusts, i.e. the nodes
                in its UNL
    */
    auto
    trustedPeers(Peer const& a) const
    {
        return graph_.outVertices(a);
    }

    /** An example of nodes that fail the whitepaper no-forking condition
     */
    struct ForkInfo
    {
        std::set<Peer> unlA;
        std::set<Peer> unlB;
        int overlap;
        double required;
    };

    //< Return nodes that fail the white-paper no-forking condition
    std::vector<ForkInfo>
    forkablePairs(double quorum) const
    {
        // Check the forking condition by looking at intersection
        // of UNL between all pairs of nodes.

        // TODO: Use the improved bound instead of the whitepaper bound.

        using UNL = std::set<Peer>;
        std::set<UNL> unique;
        for (Peer const peer : graph_.outVertices())
        {
            unique.emplace(
                std::begin(trustedPeers(peer)), std::end(trustedPeers(peer)));
        }

        std::vector<UNL> uniqueUNLs(unique.begin(), unique.end());
        std::vector<ForkInfo> res;

        // Loop over all pairs of uniqueUNLs
        for (int i = 0; i < uniqueUNLs.size(); ++i)
        {
            for (int j = (i + 1); j < uniqueUNLs.size(); ++j)
            {
                auto const& unlA = uniqueUNLs[i];
                auto const& unlB = uniqueUNLs[j];
                double rhs =
                    2.0 * (1. - quorum) * std::max(unlA.size(), unlB.size());

                int intersectionSize =
                    std::count_if(unlA.begin(), unlA.end(), [&](Peer p) {
                        return unlB.find(p) != unlB.end();
                    });

                if (intersectionSize < rhs)
                {
                    res.emplace_back(
                        ForkInfo{unlA, unlB, intersectionSize, rhs});
                }
            }
        }
        return res;
    }

    /** Check whether this trust graph satisfies the whitepaper no-forking
        condition
    */
    bool
    canFork(double quorum) const
    {
        return !forkablePairs(quorum).empty();
    }
};

}  // namespace csf
}  // namespace test
}  // namespace ripple

#endif
