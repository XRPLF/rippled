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
#include <BeastConfig.h>
#include <ripple/beast/clock/manual_clock.h>
#include <ripple/beast/unit_test.h>
#include <test/csf.h>
#include <utility>

#include <boost/algorithm/string/split.hpp>
#include <boost/algorithm/string/classification.hpp>
#include <algorithm>
#include <sstream>
#include <fstream>
#include <string>

namespace ripple {
namespace test {

/** In progress simulations for diversifying and distributing validators
*/
class DistributedValidators_test : public beast::unit_test::suite
{

    void
    completeTrustCompleteConnectFixedDelay(
            std::size_t numPeers,
            std::chrono::milliseconds delay = std::chrono::milliseconds(200),
            bool printHeaders = false)
    {
        using namespace csf;
        using namespace std::chrono;

        // Initialize persistent collector logs specific to this method
        std::string const prefix =
                "DistributedValidators_"
                "completeTrustCompleteConnectFixedDelay";
        std::fstream
                txLog(prefix + "_tx.csv", std::ofstream::app),
                ledgerLog(prefix + "_ledger.csv", std::ofstream::app);

        // title
        log << prefix << "(" << numPeers << "," << delay.count() << ")"
            << std::endl;

        // number of peers, UNLs, connections
        BEAST_EXPECT(numPeers >= 1);

        ConsensusParms const parms;
        Sim sim;
        PeerGroup peers = sim.createGroup(numPeers);

        // complete trust graph
        peers.trust(peers);

        // complete connect graph with fixed delay
        peers.connect(peers, delay);

        // Initialize collectors to track statistics to report
        TxCollector txCollector;
        LedgerCollector ledgerCollector;
        auto colls = makeCollectors(txCollector, ledgerCollector);
        sim.collectors.add(colls);

        // Initial round to set prior state
        sim.run(1);

        // Run for 10 minues, submitting 100 tx/second
        std::chrono::nanoseconds const simDuration = 10min;
        std::chrono::nanoseconds const quiet = 10s;
        Rate const rate{100, 1000ms};

        // Initialize timers
        HeartbeatTimer heart(sim.scheduler);

        // txs, start/stop/step, target
        auto peerSelector = makeSelector(peers.begin(),
                                     peers.end(),
                                     std::vector<double>(numPeers, 1.),
                                     sim.rng);
        auto txSubmitter = makeSubmitter(ConstantDistribution{rate.inv()},
                                     sim.scheduler.now() + quiet,
                                     sim.scheduler.now() + simDuration - quiet,
                                     peerSelector,
                                     sim.scheduler,
                                     sim.rng);

        // run simulation for given duration
        heart.start();
        sim.run(simDuration);

        //BEAST_EXPECT(sim.branches() == 1);
        //BEAST_EXPECT(sim.synchronized());

        log << std::right;
        log << "| Peers: "<< std::setw(2) << peers.size();
        log << " | Duration: " << std::setw(6)
            << duration_cast<milliseconds>(simDuration).count() << " ms";
        log << " | Branches: " << std::setw(1) << sim.branches();
        log << " | Synchronized: " << std::setw(1)
            << (sim.synchronized() ? "Y" : "N");
        log << " |" << std::endl;

        txCollector.report(simDuration, log, true);
        ledgerCollector.report(simDuration, log, false);

        std::string const tag = std::to_string(numPeers);
        txCollector.csv(simDuration, txLog, tag, printHeaders);
        ledgerCollector.csv(simDuration, ledgerLog, tag, printHeaders);

        log << std::endl;
    }

    void
    completeTrustScaleFreeConnectFixedDelay(
            std::size_t numPeers,
            std::chrono::milliseconds delay = std::chrono::milliseconds(200),
            bool printHeaders = false)
    {
        using namespace csf;
        using namespace std::chrono;

        // Initialize persistent collector logs specific to this method
        std::string const prefix =
                "DistributedValidators__"
                "completeTrustScaleFreeConnectFixedDelay";
        std::fstream
                txLog(prefix + "_tx.csv", std::ofstream::app),
                ledgerLog(prefix + "_ledger.csv", std::ofstream::app);

        // title
        log << prefix << "(" << numPeers << "," << delay.count() << ")"
            << std::endl;

        // number of peers, UNLs, connections
        int const numCNLs    = std::max(int(1.00 * numPeers), 1);
        int const minCNLSize = std::max(int(0.25 * numCNLs),  1);
        int const maxCNLSize = std::max(int(0.50 * numCNLs),  1);
        BEAST_EXPECT(numPeers >= 1);
        BEAST_EXPECT(numCNLs >= 1);
        BEAST_EXPECT(1 <= minCNLSize
                && minCNLSize <= maxCNLSize
                && maxCNLSize <= numPeers);

        ConsensusParms const parms;
        Sim sim;
        PeerGroup peers = sim.createGroup(numPeers);

        // complete trust graph
        peers.trust(peers);

        // scale-free connect graph with fixed delay
        std::vector<double> const ranks =
                sample(peers.size(), PowerLawDistribution{1, 3}, sim.rng);
        randomRankedConnect(peers, ranks, numCNLs,
                std::uniform_int_distribution<>{minCNLSize, maxCNLSize},
                sim.rng, delay);

        // Initialize collectors to track statistics to report
        TxCollector txCollector;
        LedgerCollector ledgerCollector;
        auto colls = makeCollectors(txCollector, ledgerCollector);
        sim.collectors.add(colls);

        // Initial round to set prior state
        sim.run(1);

        // Run for 10 minues, submitting 100 tx/second
        std::chrono::nanoseconds simDuration = 10min;
        std::chrono::nanoseconds quiet = 10s;
        Rate rate{100, 1000ms};

        // Initialize timers
        HeartbeatTimer heart(sim.scheduler);

        // txs, start/stop/step, target
        auto peerSelector = makeSelector(peers.begin(),
                                     peers.end(),
                                     std::vector<double>(numPeers, 1.),
                                     sim.rng);
        auto txSubmitter = makeSubmitter(ConstantDistribution{rate.inv()},
                                     sim.scheduler.now() + quiet,
                                     sim.scheduler.now() + simDuration - quiet,
                                     peerSelector,
                                     sim.scheduler,
                                     sim.rng);

        // run simulation for given duration
        heart.start();
        sim.run(simDuration);

        //BEAST_EXPECT(sim.branches() == 1);
        //BEAST_EXPECT(sim.synchronized());

        log << std::right;
        log << "| Peers: "<< std::setw(2) << peers.size();
        log << " | Duration: " << std::setw(6)
            << duration_cast<milliseconds>(simDuration).count() << " ms";
        log << " | Branches: " << std::setw(1) << sim.branches();
        log << " | Synchronized: " << std::setw(1)
            << (sim.synchronized() ? "Y" : "N");
        log << " |" << std::endl;

        txCollector.report(simDuration, log, true);
        ledgerCollector.report(simDuration, log, false);

        std::string const tag = std::to_string(numPeers);
        txCollector.csv(simDuration, txLog, tag, printHeaders);
        ledgerCollector.csv(simDuration, ledgerLog, tag, printHeaders);

        log << std::endl;
    }

    void
    run() override
    {
        std::string const defaultArgs = "5 200";
        std::string const args = arg().empty() ? defaultArgs : arg();
        std::stringstream argStream(args);

        int maxNumValidators = 0;
        int delayCount(200);
        argStream >> maxNumValidators;
        argStream >> delayCount;

        std::chrono::milliseconds const delay(delayCount);

        log << "DistributedValidators: 1 to " << maxNumValidators << " Peers"
            << std::endl;

        /**
         * Simulate with N = 1 to N
         * - complete trust graph is complete
         * - complete network connectivity
         * - fixed delay for network links
         */
        completeTrustCompleteConnectFixedDelay(1, delay, true);
        for(int i = 2; i <= maxNumValidators; i++)
        {
            completeTrustCompleteConnectFixedDelay(i, delay);
        }

        /**
         * Simulate with N = 1 to N
         * - complete trust graph is complete
         * - scale-free network connectivity
         * - fixed delay for network links
         */
        completeTrustScaleFreeConnectFixedDelay(1, delay, true);
        for(int i = 2; i <= maxNumValidators; i++)
        {
            completeTrustScaleFreeConnectFixedDelay(i, delay);
        }
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(DistributedValidators, consensus, ripple);

}  // namespace test
}  // namespace ripple
