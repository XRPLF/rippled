//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2017 Ripple Labs Inc.

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
#include <ripple/beast/clock/manual_clock.h>
#include <ripple/beast/unit_test.h>
#include <test/csf.h>
#include <utility>

namespace ripple {
namespace test {

class ByzantineFailureSim_test : public beast::unit_test::suite
{
    void
    run() override
    {
        using namespace csf;
        using namespace std::chrono;

        // This test simulates a specific topology with nodes generating
        // different ledgers due to a simulated byzantine failure (injecting
        // an extra non-consensus transaction).

        Sim sim;
        ConsensusParms const parms{};

        SimDuration const delay =
            round<milliseconds>(0.2 * parms.ledgerGRANULARITY);
        PeerGroup a = sim.createGroup(1);
        PeerGroup b = sim.createGroup(1);
        PeerGroup c = sim.createGroup(1);
        PeerGroup d = sim.createGroup(1);
        PeerGroup e = sim.createGroup(1);
        PeerGroup f = sim.createGroup(1);
        PeerGroup g = sim.createGroup(1);

        a.trustAndConnect(a + b + c + g, delay);
        b.trustAndConnect(b + a + c + d + e, delay);
        c.trustAndConnect(c + a + b + d + e, delay);
        d.trustAndConnect(d + b + c + e + f, delay);
        e.trustAndConnect(e + b + c + d + f, delay);
        f.trustAndConnect(f + d + e + g, delay);
        g.trustAndConnect(g + a + f, delay);

        PeerGroup network = a + b + c + d + e + f + g;

        StreamCollector sc{std::cout};

        sim.collectors.add(sc);

        for (TrustGraph<Peer*>::ForkInfo const& fi :
             sim.trustGraph.forkablePairs(0.8))
        {
            std::cout << "Can fork " << PeerGroup{fi.unlA} << " "
                      << " " << PeerGroup{fi.unlB} << " overlap " << fi.overlap
                      << " required " << fi.required << "\n";
        };

        // set prior state
        sim.run(1);

        PeerGroup byzantineNodes = a + b + c + g;
        // All peers see some TX 0
        for (Peer* peer : network)
        {
            peer->submit(Tx(0));
            // Peers 0,1,2,6 will close the next ledger differently by injecting
            // a non-consensus approved transaciton
            if (byzantineNodes.contains(peer))
            {
                peer->txInjections.emplace(
                    peer->lastClosedLedger.seq(), Tx{42});
            }
        }
        sim.run(4);
        std::cout << "Branches: " << sim.branches() << "\n";
        std::cout << "Fully synchronized: " << std::boolalpha
                  << sim.synchronized() << "\n";
        // Not tessting anything currently.
        pass();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(ByzantineFailureSim, consensus, ripple);

}  // namespace test
}  // namespace ripple
