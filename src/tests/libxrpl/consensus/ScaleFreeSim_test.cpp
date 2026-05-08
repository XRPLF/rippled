#include <xrpld/consensus/ConsensusParms.h>

#include <csf/PeerGroup.h>
#include <csf/Sim.h>
#include <csf/collectors.h>
#include <csf/csf.h>
#include <csf/random.h>
#include <csf/submitters.h>
#include <csf/timers.h>
#include <gtest/gtest.h>

#include <chrono>
#include <iostream>
#include <ostream>
#include <random>
#include <vector>

namespace xrpl::test {

class ScaleFreeSimTest : public ::testing::Test
{
protected:
    std::ostream& log_ = std::cout;

    void
    run()
    {
        using namespace std::chrono;
        using namespace csf;

        // Generate a quasi-random scale free network and simulate consensus
        // as we vary transaction submission rates

        int const n = 100;  // Peers

        int const numUNLs = 15;  //  UNL lists
        int const minUNLSize = n / 4, maxUNLSize = n / 2;

        ConsensusParms const parms{};
        Sim sim;
        PeerGroup network = sim.createGroup(n);

        // generate trust ranks
        std::vector<double> const ranks =
            sample(network.size(), PowerLawDistribution{1, 3}, sim.rng);

        // generate scale-free trust graph
        randomRankedTrust(
            network,
            ranks,
            numUNLs,
            std::uniform_int_distribution<>{minUNLSize, maxUNLSize},
            sim.rng);

        // nodes with a trust line in either direction are network-connected
        network.connectFromTrust(round<milliseconds>(0.2 * parms.ledgerGRANULARITY));

        // Initialize collectors to track statistics to report
        TxCollector txCollector;
        LedgerCollector ledgerCollector;
        auto colls = makeCollectors(txCollector, ledgerCollector);
        sim.collectors.add(colls);

        // Initial round to set prior state
        sim.run(1);

        // Initialize timers
        HeartbeatTimer heart(sim.scheduler, seconds(10s));

        // Run for 10 minutes, submitting 100 tx/second
        std::chrono::nanoseconds const simDuration = 10min;
        std::chrono::nanoseconds const quiet = 10s;
        Rate const rate{.count = 100, .duration = 1000ms};

        // txs, start/stop/step, target
        auto peerSelector = makeSelector(network.begin(), network.end(), ranks, sim.rng);
        auto txSubmitter = makeSubmitter(
            ConstantDistribution{rate.inv()},
            sim.scheduler.now() + quiet,
            sim.scheduler.now() + (simDuration - quiet),
            peerSelector,
            sim.scheduler,
            sim.rng);

        // run simulation for given duration
        heart.start();
        sim.run(simDuration);

        EXPECT_TRUE(sim.branches() == 1);
        EXPECT_TRUE(sim.synchronized());

        // TODO: Clean up this formatting mess!!

        log_ << "Peers: " << network.size() << std::endl;
        log_ << "Simulated Duration: " << duration_cast<milliseconds>(simDuration).count() << " ms"
             << std::endl;
        log_ << "Branches: " << sim.branches() << std::endl;
        log_ << "Synchronized: " << (sim.synchronized() ? "Y" : "N") << std::endl;
        log_ << std::endl;

        txCollector.report(simDuration, log_);
        ledgerCollector.report(simDuration, log_);
        // Print summary?
        // # forks?  # of LCLs?
        // # peers
        // # tx submitted
        // # ledgers/sec etc.?
    }
};

TEST_F(ScaleFreeSimTest, DISABLED_scale_free_sim)
{
    run();
}

}  // namespace xrpl::test
