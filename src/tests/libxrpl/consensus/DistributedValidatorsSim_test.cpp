#include <csf/PeerGroup.h>
#include <csf/Sim.h>
#include <csf/collectors.h>
#include <csf/csf.h>
#include <csf/random.h>
#include <csf/submitters.h>
#include <csf/timers.h>
#include <gtest/gtest.h>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <fstream>
#include <iomanip>
#include <ios>
#include <iostream>
#include <ostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

namespace xrpl::test {

/** In progress simulations for diversifying and distributing validators
 */
class DistributedValidators_test : public ::testing::Test
{
protected:
    std::ostream& log = std::cout;

    std::string const&
    arg() const
    {
        static std::string const empty;
        return empty;
    }

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
        std::fstream txLog(prefix + "_tx.csv", std::ofstream::app),
            ledgerLog(prefix + "_ledger.csv", std::ofstream::app);

        // title
        log << prefix << "(" << numPeers << "," << delay.count() << ")" << std::endl;

        // number of peers, UNLs, connections
        EXPECT_TRUE(numPeers >= 1);

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

        // Run for 10 minutes, submitting 100 tx/second
        std::chrono::nanoseconds const simDuration = 10min;
        std::chrono::nanoseconds const quiet = 10s;
        Rate const rate{.count = 100, .duration = 1000ms};

        // Initialize timers
        HeartbeatTimer heart(sim.scheduler);

        // txs, start/stop/step, target
        auto peerSelector =
            makeSelector(peers.begin(), peers.end(), std::vector<double>(numPeers, 1.), sim.rng);
        auto txSubmitter = makeSubmitter(
            ConstantDistribution{rate.inv()},
            sim.scheduler.now() + quiet,
            sim.scheduler.now() + simDuration - quiet,
            peerSelector,
            sim.scheduler,
            sim.rng);

        // run simulation for given duration
        heart.start();
        sim.run(simDuration);

        // EXPECT_TRUE(sim.branches() == 1);
        // EXPECT_TRUE(sim.synchronized());

        log << std::right;
        log << "| Peers: " << std::setw(2) << peers.size();
        log << " | Duration: " << std::setw(6) << duration_cast<milliseconds>(simDuration).count()
            << " ms";
        log << " | Branches: " << std::setw(1) << sim.branches();
        log << " | Synchronized: " << std::setw(1) << (sim.synchronized() ? "Y" : "N");
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
        std::fstream txLog(prefix + "_tx.csv", std::ofstream::app),
            ledgerLog(prefix + "_ledger.csv", std::ofstream::app);

        // title
        log << prefix << "(" << numPeers << "," << delay.count() << ")" << std::endl;

        // number of peers, UNLs, connections
        int const numCNLs = std::max(int(1.00 * numPeers), 1);
        int const minCNLSize = std::max(int(0.25 * numCNLs), 1);
        int const maxCNLSize = std::max(int(0.50 * numCNLs), 1);
        EXPECT_TRUE(numPeers >= 1);
        EXPECT_TRUE(numCNLs >= 1);
        EXPECT_TRUE(1 <= minCNLSize && minCNLSize <= maxCNLSize && maxCNLSize <= numPeers);

        Sim sim;
        PeerGroup peers = sim.createGroup(numPeers);

        // complete trust graph
        peers.trust(peers);

        // scale-free connect graph with fixed delay
        std::vector<double> const ranks = sample(peers.size(), PowerLawDistribution{1, 3}, sim.rng);
        randomRankedConnect(
            peers,
            ranks,
            numCNLs,
            std::uniform_int_distribution<>{minCNLSize, maxCNLSize},
            sim.rng,
            delay);

        // Initialize collectors to track statistics to report
        TxCollector txCollector;
        LedgerCollector ledgerCollector;
        auto colls = makeCollectors(txCollector, ledgerCollector);
        sim.collectors.add(colls);

        // Initial round to set prior state
        sim.run(1);

        // Run for 10 minutes, submitting 100 tx/second
        std::chrono::nanoseconds const simDuration = 10min;
        std::chrono::nanoseconds const quiet = 10s;
        Rate const rate{.count = 100, .duration = 1000ms};

        // Initialize timers
        HeartbeatTimer heart(sim.scheduler);

        // txs, start/stop/step, target
        auto peerSelector =
            makeSelector(peers.begin(), peers.end(), std::vector<double>(numPeers, 1.), sim.rng);
        auto txSubmitter = makeSubmitter(
            ConstantDistribution{rate.inv()},
            sim.scheduler.now() + quiet,
            sim.scheduler.now() + simDuration - quiet,
            peerSelector,
            sim.scheduler,
            sim.rng);

        // run simulation for given duration
        heart.start();
        sim.run(simDuration);

        // EXPECT_TRUE(sim.branches() == 1);
        // EXPECT_TRUE(sim.synchronized());

        log << std::right;
        log << "| Peers: " << std::setw(2) << peers.size();
        log << " | Duration: " << std::setw(6) << duration_cast<milliseconds>(simDuration).count()
            << " ms";
        log << " | Branches: " << std::setw(1) << sim.branches();
        log << " | Synchronized: " << std::setw(1) << (sim.synchronized() ? "Y" : "N");
        log << " |" << std::endl;

        txCollector.report(simDuration, log, true);
        ledgerCollector.report(simDuration, log, false);

        std::string const tag = std::to_string(numPeers);
        txCollector.csv(simDuration, txLog, tag, printHeaders);
        ledgerCollector.csv(simDuration, ledgerLog, tag, printHeaders);

        log << std::endl;
    }

    void
    run()
    {
        std::string const defaultArgs = "5 200";
        std::string const args = arg().empty() ? defaultArgs : arg();
        std::stringstream argStream(args);

        int maxNumValidators = 0;
        int delayCount(200);
        argStream >> maxNumValidators;
        argStream >> delayCount;

        std::chrono::milliseconds const delay(delayCount);

        log << "DistributedValidators: 1 to " << maxNumValidators << " Peers" << std::endl;

        /**
         * Simulate with N = 1 to N
         * - complete trust graph is complete
         * - complete network connectivity
         * - fixed delay for network links
         */
        completeTrustCompleteConnectFixedDelay(1, delay, true);
        for (int i = 2; i <= maxNumValidators; i++)
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
        for (int i = 2; i <= maxNumValidators; i++)
        {
            completeTrustScaleFreeConnectFixedDelay(i, delay);
        }
    }
};

TEST_F(DistributedValidators_test, DISABLED_distributed_validators)
{
    run();
}

}  // namespace xrpl::test
