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
#ifndef RIPPLE_TEST_CSF_PEERGROUP_H_INCLUDED
#define RIPPLE_TEST_CSF_PEERGROUP_H_INCLUDED

#include <algorithm>
#include <test/csf/Peer.h>
#include <test/csf/random.h>
#include <vector>

namespace ripple {
namespace test {
namespace csf {

/** A group of simulation Peers

    A PeerGroup is a convenient handle for logically grouping peers together,
    and then creating trust or network relations for the group at large. Peer
    groups may also be combined to build out more complex structures.

    The PeerGroup provides random access style iterators and operator[]
*/
class PeerGroup
{
    using peers_type = std::vector<Peer*>;
    peers_type peers_;
public:
    using iterator = peers_type::iterator;
    using const_iterator = peers_type::const_iterator;
    using reference = peers_type::reference;
    using const_reference = peers_type::const_reference;

    PeerGroup() = default;
    PeerGroup(PeerGroup const&) = default;
    PeerGroup(Peer* peer) : peers_{1, peer}
    {
    }
    PeerGroup(std::vector<Peer*>&& peers) : peers_{std::move(peers)}
    {
        std::sort(peers_.begin(), peers_.end());
    }
    PeerGroup(std::vector<Peer*> const& peers) : peers_{peers}
    {
        std::sort(peers_.begin(), peers_.end());
    }

    PeerGroup(std::set<Peer*> const& peers) : peers_{peers.begin(), peers.end()}
    {

    }

    iterator
    begin()
    {
        return peers_.begin();
    }

    iterator
    end()
    {
        return peers_.end();
    }

    const_iterator
    begin() const
    {
        return peers_.begin();
    }

    const_iterator
    end() const
    {
        return peers_.end();
    }

    const_reference
    operator[](std::size_t i) const
    {
        return peers_[i];
    }

    bool
    contains(Peer const * p)
    {
        return std::find(peers_.begin(), peers_.end(), p) != peers_.end();
    }

    std::size_t
    size() const
    {
        return peers_.size();
    }

    /** Establish trust

        Establish trust from all peers in this group to all peers in o

        @param o The group of peers to trust
    */
    void
    trust(PeerGroup const & o)
    {
        for(Peer * p : peers_)
        {
            for (Peer * target : o.peers_)
            {
                p->trust(*target);
            }
        }
    }

    /** Revoke trust

        Revoke trust from all peers in this group to all peers in o

        @param o The group of peers to untrust
    */
    void
    untrust(PeerGroup const & o)
    {
        for(Peer * p : peers_)
        {
            for (Peer * target : o.peers_)
            {
                p->untrust(*target);
            }
        }
    }

    /** Establish network connection

        Establish outbound connections from all peers in this group to all peers in
        o. If a connection already exists, no new connection is established.

        @param o The group of peers to connect to (will get inbound connections)
        @param delay The fixed messaging delay for all established connections


    */
    void
    connect(PeerGroup const& o, SimDuration delay)
    {
        for(Peer * p : peers_)
        {
            for (Peer * target : o.peers_)
            {
                // cannot send messages to self over network
                if(p != target)
                    p->connect(*target, delay);
            }
        }
    }

    /** Destroy network connection

        Destroy connections from all peers in this group to all peers in o

        @param o The group of peers to disconnect from
    */
    void
    disconnect(PeerGroup const &o)
    {
        for(Peer * p : peers_)
        {
            for (Peer * target : o.peers_)
            {
                p->disconnect(*target);
            }
        }
    }

    /** Establish trust and network connection

        Establish trust and create a network connection with fixed delay
        from all peers in this group to all peers in o

        @param o The group of peers to trust and connect to
        @param delay The fixed messaging delay for all established connections
    */
    void
    trustAndConnect(PeerGroup const & o, SimDuration delay)
    {
        trust(o);
        connect(o, delay);
    }

    /** Establish network connections based on trust relations

        For each peers in this group, create outbound network connection
        to the set of peers it trusts. If a coonnection already exists, it is
        not recreated.

        @param delay The fixed messaging delay for all established connections

    */
    void
    connectFromTrust(SimDuration delay)
    {
        for (Peer * peer : peers_)
        {
            for (Peer * to : peer->trustGraph.trustedPeers(peer))
            {
                peer->connect(*to, delay);
            }
        }
    }

    // Union of PeerGroups
    friend
    PeerGroup
    operator+(PeerGroup const & a, PeerGroup const & b)
    {
        PeerGroup res;
        std::set_union(
            a.peers_.begin(),
            a.peers_.end(),
            b.peers_.begin(),
            b.peers_.end(),
            std::back_inserter(res.peers_));
        return res;
    }

    // Set difference of PeerGroups
    friend
    PeerGroup
    operator-(PeerGroup const & a, PeerGroup const & b)
    {
        PeerGroup res;

        std::set_difference(
            a.peers_.begin(),
            a.peers_.end(),
            b.peers_.begin(),
            b.peers_.end(),
            std::back_inserter(res.peers_));

        return res;
    }

    friend std::ostream&
    operator<<(std::ostream& o, PeerGroup const& t)
    {
        o << "{";
        bool first = true;
        for (Peer const* p : t)
        {
            if(!first)
                o << ", ";
            first = false;
            o << p->id;
        }
        o << "}";
        return o;
    }
};

/** Randomly generate peer groups according to ranks.

    Generates random peer groups based on a provided ranking of peers. This
    mimics a process of randomly generating UNLs, where more "important" peers
    are more likely to appear in a UNL.

    `numGroups` subgroups are generated by randomly sampling without without
    replacement from peers according to the `ranks`.



    @param peers The group of peers
    @param ranks The relative importance of each peer, must match the size of
                 peers. Higher relative rank means more likely to be sampled.
    @param numGroups The number of peer link groups to generate
    @param sizeDist The distribution that determines the size of a link group
    @param g The uniform random bit generator

*/
template <class RandomNumberDistribution, class Generator>
std::vector<PeerGroup>
randomRankedGroups(
    PeerGroup & peers,
    std::vector<double> const & ranks,
    int numGroups,
    RandomNumberDistribution sizeDist,
    Generator& g)
{
    assert(peers.size() == ranks.size());

    std::vector<PeerGroup> groups;
    groups.reserve(numGroups);
    std::vector<Peer*> rawPeers(peers.begin(), peers.end());
    std::generate_n(std::back_inserter(groups), numGroups, [&]() {
        std::vector<Peer*> res = random_weighted_shuffle(rawPeers, ranks, g);
        res.resize(sizeDist(g));
        return PeerGroup(std::move(res));
    });

    return groups;
}



/** Generate random trust groups based on peer rankings.

    @see randomRankedGroups for descriptions of the arguments
*/
template <class RandomNumberDistribution, class Generator>
void
randomRankedTrust(
    PeerGroup & peers,
    std::vector<double> const & ranks,
    int numGroups,
    RandomNumberDistribution sizeDist,
    Generator& g)
{
    std::vector<PeerGroup> const groups =
        randomRankedGroups(peers, ranks, numGroups, sizeDist, g);

    std::uniform_int_distribution<int> u(0, groups.size() - 1);
    for(auto & peer : peers)
    {
        for(auto & target : groups[u(g)])
             peer->trust(*target);
    }
}

/** Generate random network groups based on peer rankings.

    @see randomRankedGroups for descriptions of the arguments
*/
template <class RandomNumberDistribution, class Generator>
void
randomRankedConnect(
    PeerGroup & peers,
    std::vector<double> const & ranks,
    int numGroups,
    RandomNumberDistribution sizeDist,
    Generator& g,
    SimDuration delay)
{
    std::vector<PeerGroup> const groups =
        randomRankedGroups(peers, ranks, numGroups, sizeDist, g);

    std::uniform_int_distribution<int> u(0, groups.size() - 1);
    for(auto & peer : peers)
    {
        for(auto & target : groups[u(g)])
             peer->connect(*target, delay);
    }
}

}  // namespace csf
}  // namespace test
}  // namespace ripple
#endif

