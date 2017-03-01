//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2016 Ripple Labs Inc->

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
#include <BeastConfig.h>
#include <ripple/beast/unit_test.h>
#include <ripple/consensus/Consensus.h>
#include <ripple/consensus/ConsensusProposal.h>
#include <ripple/beast/clock/manual_clock.h>
#include <boost/function_output_iterator.hpp>
#include <test/csf.h>
#include <utility>


namespace ripple {
namespace test {

class Consensus_test : public beast::unit_test::suite
{
public:

    void
    testStandalone()
    {
        using namespace csf;

        auto tg = TrustGraph::makeComplete(1);
        Sim s(tg, topology(tg, fixed{LEDGER_GRANULARITY}));

        auto & p = s.peers[0];

        p.targetLedgers = 1;
        p.start();
        p.submit(Tx{ 1 });

        s.net.step();

        // Inspect that the proper ledger was created
        BEAST_EXPECT(p.LCL().seq == 1);
        BEAST_EXPECT(p.LCL() == p.lastClosedLedger.id());
        BEAST_EXPECT(p.lastClosedLedger.id().txs.size() == 1);
        BEAST_EXPECT(p.lastClosedLedger.id().txs.find(Tx{ 1 })
            != p.lastClosedLedger.id().txs.end());
        BEAST_EXPECT(p.getLastCloseProposers() == 0);
    }

    void
    testPeersAgree()
    {
        using namespace csf;
        using namespace std::chrono;

        auto tg = TrustGraph::makeComplete(5);
        Sim sim(tg,
            topology(tg, fixed{round<milliseconds>(0.2 * LEDGER_GRANULARITY)}));

        // everyone submits their own ID as a TX and relay it to peers
        for (auto & p : sim.peers)
            p.submit(Tx(p.id));

        // Verify all peers have the same LCL and it has all the Txs
        sim.run(1);
        for (auto & p : sim.peers)
        {
            auto const &lgrID = p.LCL();
            BEAST_EXPECT(lgrID.seq == 1);
            BEAST_EXPECT(p.getLastCloseProposers() == sim.peers.size() - 1);
            for(std::uint32_t i = 0; i < sim.peers.size(); ++i)
                BEAST_EXPECT(lgrID.txs.find(Tx{ i }) != lgrID.txs.end());
            // Matches peer 0 ledger
            BEAST_EXPECT(lgrID.txs == sim.peers[0].LCL().txs);
        }
    }

    void
    testSlowPeer()
    {
        using namespace csf;
        using namespace std::chrono;

        // Run two tests
        //  1. The slow peer is participating inconsensus
        //  2. The slow peer is just observing

        for(auto isParticipant : {true, false})
        {
            auto tg = TrustGraph::makeComplete(5);

            Sim sim(tg, topology(tg,[](PeerID i, PeerID j)
            {
                auto delayFactor = (i == 0 || j == 0) ? 1.1 : 0.2;
                return round<milliseconds>(delayFactor* LEDGER_GRANULARITY);
            }));

            sim.peers[0].proposing = sim.peers[0].validating = isParticipant;

            // All peers submit their own ID as a transaction and relay it to peers
            for (auto & p : sim.peers)
            {
                p.submit(Tx{ p.id });
            }

            sim.run(1);

            // Verify all peers have same LCL but are missing transaction 0 which
            // was not received by all peers before the ledger closed
            for (auto & p : sim.peers)
            {
                auto const &lgrID = p.LCL();
                BEAST_EXPECT(lgrID.seq == 1);


                // If peer 0 is participating
                if(isParticipant)
                {
                    BEAST_EXPECT(p.getLastCloseProposers()
                        == sim.peers.size() - 1);
                    // Peer 0 closes first because it sees a quorum of agreeing positions
                    // from all other peers in one hop (1->0, 2->0, ..)
                    // The other peers take an extra timer period before they find that
                    // Peer 0 agrees with them ( 1->0->1,  2->0->2, ...)
                    if(p.id != 0)
                        BEAST_EXPECT(p.getLastConvergeDuration()
                            > sim.peers[0].getLastConvergeDuration());
                }
                else // peer 0 is not participating
                {
                    auto const proposers = p.getLastCloseProposers();
                    if(p.id == 0)
                        BEAST_EXPECT(proposers == sim.peers.size() - 1);
                    else
                        BEAST_EXPECT(proposers == sim.peers.size() - 2);

                    // so all peers should have closed together
                        BEAST_EXPECT(p.getLastConvergeDuration()
                            == sim.peers[0].getLastConvergeDuration());
                }


                BEAST_EXPECT(lgrID.txs.find(Tx{ 0 }) == lgrID.txs.end());
                for(std::uint32_t i = 1; i < sim.peers.size(); ++i)
                    BEAST_EXPECT(lgrID.txs.find(Tx{ i }) != lgrID.txs.end());
                // Matches peer 0 ledger
                BEAST_EXPECT(lgrID.txs == sim.peers[0].LCL().txs);
            }
            BEAST_EXPECT(sim.peers[0].openTxs.find(Tx{ 0 })
                               != sim.peers[0].openTxs.end());
         }
    }

    void
    testCloseTimeDisagree()
    {
        using namespace csf;
        using namespace std::chrono;

        // This is a very specialized test to get ledgers to disagree on
        // the close time.  It unfortunately assumes knowledge about current
        // timing constants.  This is a necessary evil to get coverage up
        // pending more extensive refactorings of timing constants.

        // In order to agree-to-disagree on the close time, there must be no
        // clear majority of nodes agreeing on a close time.  This test
        // sets a relative offset to the peers internal clocks so that they
        // send proposals with differing times.

        // However, they have to agree on the effective close time, not the
        // exact close time.  The minimum closeTimeResolution is given by
        // ledgerPossibleTimeResolutions[0], which is currently 10s. This means
        // the skews need to be at least 10 seconds.

        // Complicating this matter is that nodes will ignore proposals
        // with times more than PROPOSE_FRESHNESS =20s in the past. So at
        // the minimum granularity, we have at most 3 types of skews (0s,10s,20s).

        // This test therefore has 6 nodes, with 2 nodes having each type of
        // skew.  Then no majority (1/3 < 1/2) of nodes will agree on an
        // actual close time.

        auto tg = TrustGraph::makeComplete(6);
        Sim sim(tg,
            topology(tg, fixed{round<milliseconds>(0.2 * LEDGER_GRANULARITY)}));

        // Run consensus without skew until we have a short close time resolution
        while(sim.peers.front().lastClosedLedger.closeTimeResolution() >=
            PROPOSE_FRESHNESS)
            sim.run(1);

        // Introduce a shift on the time of half the peers
        sim.peers[0].clockSkew = PROPOSE_FRESHNESS/2;
        sim.peers[1].clockSkew = PROPOSE_FRESHNESS/2;
        sim.peers[2].clockSkew = PROPOSE_FRESHNESS;
        sim.peers[3].clockSkew = PROPOSE_FRESHNESS;

        // Verify all peers have the same LCL and it has all the Txs
        sim.run(1);
        for (auto & p : sim.peers)
        {
            BEAST_EXPECT(! p.lastClosedLedger.closeAgree());
        }
    }

    void
    testFork()
    {
        using namespace csf;
        using namespace std::chrono;

        int numPeers = 10;
        for(int overlap = 0;  overlap <= numPeers; ++overlap)
        {
            auto tg = TrustGraph::makeClique(numPeers, overlap);
            Sim sim(tg, topology(tg,
                        fixed{round<milliseconds>(0.2 * LEDGER_GRANULARITY)}));

            // Initial round to set prior state
            sim.run(1);
            for (auto & p : sim.peers)
            {
                // Nodes have only seen transactions from their neighbors
                p.openTxs.insert(Tx{p.id});
                for(auto const link : sim.net.links(&p))
                    p.openTxs.insert(Tx{link.to->id});
            }
            sim.run(1);


            // See if the network forked
            bc::flat_set<Ledger::ID> ledgers;
            for (auto & p : sim.peers)
            {
                ledgers.insert(p.LCL());
            }

            // Fork should not happen for 40% or greater overlap
            // Since the overlapped nodes have a UNL that is the union of the
            // two cliques, the maximum sized UNL list is the number of peers
            if(overlap > 0.4 * numPeers)
                BEAST_EXPECT(ledgers.size() == 1);
            else // Even if we do fork, there shouldn't be more than 3 ledgers
                 // One for cliqueA, one for cliqueB and one for nodes in both
                 BEAST_EXPECT(ledgers.size() <= 3);
        }
    }


    void
    simClockSkew()
    {
        using namespace csf;

        // Attempting to test what happens if peers enter consensus well
        // separated in time.  Initial round (in which peers are not staggered)
        // is used to get the network going, then transactions are submitted
        // together and consensus continues.

        // For all the times below, the same ledger is built but the close times
        // disgree.  BUT THE LEDGER DOES NOT SHOW disagreeing close times.
        // It is probably because peer proposals are stale, so they get ignored
        // but with no peer proposals, we always assume close time consensus is
        // true.

        // Disabled while continuing to understand testt.


        for(auto stagger : {800ms, 1600ms, 3200ms, 30000ms, 45000ms, 300000ms})
        {

            auto tg = TrustGraph::makeComplete(5);
            Sim sim(tg, topology(tg, [](PeerID i, PeerID)
            {
                return 200ms * (i + 1);
            }));


            // all transactions submitted before starting
            // Initial round to set prior state
            sim.run(1);

            for (auto & p : sim.peers)
            {
                p.openTxs.insert(Tx{ 0 });
                p.targetLedgers = p.completedLedgers + 1;

            }

            // stagger start of consensus
            for (auto & p : sim.peers)
            {
                p.start();
                sim.net.step_for(stagger);
            }

            // run until all peers have accepted all transactions
            sim.net.step_while([&]()
            {
                  for(auto & p : sim.peers)
                    if(p.LCL().txs.size() != 1)
                        return true;
                    return false;
            });
        }
    }



    void
    simScaleFree()
    {
        using namespace std::chrono;
        using namespace csf;
        // Generate a quasi-random scale free network and simulate consensus
        // for a single transaction

        int N = 100; // Peers

        int numUNLs = 15;  //  UNL lists
        int minUNLSize = N/4, maxUNLSize = N / 2;

        double transProb = 0.5;

        std::mt19937_64 rng;

        auto tg = TrustGraph::makeRandomRanked(N, numUNLs,
          PowerLawDistribution{1,3},
          std::uniform_int_distribution<>{minUNLSize, maxUNLSize},
          rng);

        Sim sim{tg, topology(tg, fixed{round<milliseconds>(0.2 * LEDGER_GRANULARITY)})};

        // Initial round to set prior state
        sim.run(1);

        std::uniform_real_distribution<> u{};
        for (auto & p : sim.peers)
        {
            // 50-50 chance to have seen a transaction
            if(u(rng) >= transProb)
                p.openTxs.insert(Tx{0});

        }
        sim.run(1);


        // See if the network forked
        bc::flat_set<Ledger::ID> ledgers;
        for (auto & p : sim.peers)
        {
            ledgers.insert(p.LCL());
        }

        BEAST_EXPECT(ledgers.size() == 1);

    }

    void
    run() override
    {
        testStandalone();
        testPeersAgree();
        testSlowPeer();
        testCloseTimeDisagree();
        //testStartRoundMissingLCL();
        //testHandleLCLmidround();
        //testCreateDisputesDuringInitialPosition();

        testFork();
        simClockSkew();
        simScaleFree();
    }
};

BEAST_DEFINE_TESTSUITE(Consensus, consensus, ripple);
} // test
} // ripple
