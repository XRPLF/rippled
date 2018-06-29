//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2016 Ripple Labs Inc.

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
#include <test/csf/random.h>
#include <utility>

namespace ripple {
namespace test {

class ScaleFreeSim_test : public beast::unit_test::suite
{
    void
    run() override
    {
        using namespace std::chrono;
        using namespace csf;

        // Generate a quasi-random scale free network and simulate consensus
        // as we vary transaction submission rates


        int const N = 100;  // Peers

        int const numUNLs = 15;  //  UNL lists
        int const minUNLSize = N / 4, maxUNLSize = N / 2;

        ConsensusParms const parms{};
        Sim sim;
        PeerGroup network = sim.createGroup(N);

        // generate trust ranks
        std::vector<double> const ranks =
            sample(network.size(), PowerLawDistribution{1, 3}, sim.rng);

        // generate scale-free trust graph
        randomRankedTrust(network, ranks, numUNLs,
            std::uniform_int_distribution<>{minUNLSize, maxUNLSize},
            sim.rng);

        // nodes with a trust line in either direction are network-connected
        network.connectFromTrust(
            date::round<milliseconds>(0.2 * parms.ledgerGranularity));

        // Initialize collectors to track statistics to report
        TxCollector txCollector;
        LedgerCollector ledgerCollector;
        auto colls = makeCollectors(txCollector, ledgerCollector);
        sim.collectors.add(colls);

        // Initial round to set prior state
        sim.run(1);

        // Initialize timers
        HeartbeatTimer heart(sim.scheduler, seconds(10s));

        // Run for 10 minues, submitting 100 tx/second
        std::chrono::nanoseconds const simDuration = 10min;
        std::chrono::nanoseconds const quiet = 10s;
        Rate const rate{100, 1000ms};

        // txs, start/stop/step, target
        auto peerSelector = makeSelector(network.begin(),
                                     network.end(),
                                     ranks,
                                     sim.rng);
        auto txSubmitter = makeSubmitter(ConstantDistribution{rate.inv()},
                          sim.scheduler.now() + quiet,
                          sim.scheduler.now() + (simDuration - quiet),
                          peerSelector,
                          sim.scheduler,
                          sim.rng);

        // run simulation for given duration
        heart.start();
        sim.run(simDuration);

        BEAST_EXPECT(sim.branches() == 1);
        BEAST_EXPECT(sim.synchronized());

        // TODO: Clean up this formatting mess!!

        log << "Peers: " << network.size() << std::endl;
        log << "Simulated Duration: "
            << duration_cast<milliseconds>(simDuration).count()
            << " ms" << std::endl;
        log << "Branches: " << sim.branches() << std::endl;
        log << "Synchronized: " << (sim.synchronized() ? "Y" : "N")
            << std::endl;
        log << std::endl;

        txCollector.report(simDuration, log);
        ledgerCollector.report(simDuration, log);
        // Print summary?
        // # forks?  # of LCLs?
        // # peers
        // # tx submitted
        // # ledgers/sec etc.?
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL_PRIO(ScaleFreeSim, consensus, ripple, 80);

}  // namespace test
}  // namespace ripple
