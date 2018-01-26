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

#ifndef RIPPLE_TEST_CSF_SIM_H_INCLUDED
#define RIPPLE_TEST_CSF_SIM_H_INCLUDED

#include <test/csf/Digraph.h>
#include <test/csf/SimTime.h>
#include <test/csf/BasicNetwork.h>
#include <test/csf/Scheduler.h>
#include <test/csf/Peer.h>
#include <test/csf/PeerGroup.h>
#include <test/csf/TrustGraph.h>
#include <test/csf/CollectorRef.h>

#include <iostream>
#include <deque>
#include <random>

namespace ripple {
namespace test {
namespace csf {

/** Sink that prepends simulation time to messages */
class BasicSink : public beast::Journal::Sink
{
    Scheduler::clock_type const & clock_;
public:
    BasicSink (Scheduler::clock_type const & clock)
        : Sink (beast::severities::kDisabled, false)
        , clock_{clock}
    {
    }

    void
    write (beast::severities::Severity level,
        std::string const& text) override
    {
        if (level < threshold())
            return;

        std::cout << clock_.now().time_since_epoch().count() << " " << text
                  << std::endl;
    }
};

class Sim
{
    // Use a deque to have stable pointers even when dynamically adding peers
    //  - Alternatively consider using unique_ptrs allocated from arena
    std::deque<Peer> peers;
    PeerGroup allPeers;

public:
    std::mt19937_64 rng;
    Scheduler scheduler;
    BasicSink sink;
    beast::Journal j;
    LedgerOracle oracle;
    BasicNetwork<Peer*> net;
    TrustGraph<Peer*> trustGraph;
    CollectorRefs collectors;

    /** Create a simulation

        Creates a new simulation. The simulation has no peers, no trust links
        and no network connections.

    */
    Sim() : sink{scheduler.clock()}, j{sink}, net{scheduler}
    {
    }

    /** Create a new group of peers.

        Creates a new group of peers. The peers do not have any trust relations
        or network connections by default. Those must be configured by the client.

        @param numPeers The number of peers in the group
        @return PeerGroup representing these new peers

        @note This increases the number of peers in the simulation by numPeers.
    */
    PeerGroup
    createGroup(std::size_t numPeers)
    {
        std::vector<Peer*> newPeers;
        newPeers.reserve(numPeers);
        for (std::size_t i = 0; i < numPeers; ++i)
        {
            peers.emplace_back(
                PeerID{static_cast<std::uint32_t>(peers.size())},
                scheduler,
                oracle,
                net,
                trustGraph,
                collectors,
                j);
            newPeers.emplace_back(&peers.back());
        }
        PeerGroup res{newPeers};
        allPeers = allPeers + res;
        return res;
    }

    //! The number of peers in the simulation
    std::size_t
    size() const
    {
        return peers.size();
    }

    /** Run consensus protocol to generate the provided number of ledgers.

        Has each peer run consensus until it closes `ledgers` more ledgers.

        @param ledgers The number of additional ledgers to close
    */
    void
    run(int ledgers);

    /** Run consensus for the given duration */
    void
    run(SimDuration const& dur);

    /** Check whether all peers in the group are synchronized.

        Nodes in the group are synchronized if they share the same last
        fully validated and last generated ledger.
    */
    bool
    synchronized(PeerGroup const& g) const;

    /** Check whether all peers in the network are synchronized
    */
    bool
    synchronized() const;

    /** Calculate the number of branches in the group.

        A branch occurs if two nodes in the group have fullyValidatedLedgers
       that are not on the same chain of ledgers.
    */
    std::size_t
    branches(PeerGroup const& g) const;

    /** Calculate the number  of branches in the network
    */
    std::size_t
    branches() const;

};

}  // namespace csf
}  // namespace test
}  // namespace ripple

#endif
