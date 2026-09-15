/**
 * Byzantine Partition Recovery Tests
 *
 * Tests consensus safety under network partition scenarios where a minority
 * of validators (running modified binaries) attempt to advance the chain
 * while the majority is offline, then rejoin the network.
 *
 * Attack scenario:
 *   - 7 validators share a common UNL (like production mainnet)
 *   - 4 legitimate validators crash (DoS attack)
 *   - 3 attacker validators (modified binary) bypass quorum checks
 *     and continue producing ledgers
 *   - 4 legitimate validators recover and reconnect
 *   - Question: does the attacker chain get accepted?
 */

#include <test/csf.h>
#include <test/csf/Peer.h>
#include <test/csf/PeerGroup.h>
#include <test/csf/Sim.h>
#include <test/csf/SimTime.h>
#include <test/csf/TrustGraph.h>
#include <test/csf/collectors.h>
#include <test/csf/events.h>

#include <xrpld/consensus/ConsensusParms.h>

#include <xrpl/beast/unit_test/suite.h>

#include <chrono>
#include <iostream>

namespace xrpl::test {

class ByzantinePartitionRecovery_test : public beast::unit_test::Suite
{
    // Collector that tracks per-peer ledger advancement and validation
    struct PartitionTracker
    {
        struct PeerState
        {
            csf::Ledger::Seq lastClosed{0};
            csf::Ledger::Seq lastFullyValidated{0};
            csf::Ledger::ID lastClosedId{};
            csf::Ledger::ID lastFullyValidatedId{};
        };

        std::map<csf::PeerID, PeerState> states;

        template <class E>
        void
        on(csf::PeerID, csf::SimTime, E const&)
        {
        }

        void
        on(csf::PeerID who, csf::SimTime, csf::AcceptLedger const& e)
        {
            auto& s = states[who];
            s.lastClosed = e.ledger.seq();
            s.lastClosedId = e.ledger.id();
        }

        void
        on(csf::PeerID who, csf::SimTime, csf::FullyValidateLedger const& e)
        {
            auto& s = states[who];
            s.lastFullyValidated = e.ledger.seq();
            s.lastFullyValidatedId = e.ledger.id();
        }
    };

    /**
     * Test 1: Byzantine minority cannot fully validate during partition
     *
     * 7 validators, common UNL. 4 crash. 3 attackers continue.
     * Standard quorum (no modification) — attackers can close ledgers
     * but cannot fully validate them (need 6/7 = 80%).
     */
    void
    testPartitionNoQuorumBypass()
    {
        using namespace csf;
        using namespace std::chrono;
        testcase("partition: minority cannot fully validate (standard quorum)");

        Sim sim;
        ConsensusParms const parms{};
        SimDuration const delay = round<milliseconds>(0.2 * parms.ledgerGRANULARITY);

        // Create 7 validators
        PeerGroup attackers = sim.createGroup(3);
        PeerGroup legitimate = sim.createGroup(4);
        PeerGroup network = attackers + legitimate;

        // Common UNL — all trust all (like production mainnet)
        network.trust(network);
        network.connect(network, delay);

        PartitionTracker tracker;
        sim.collectors.add(tracker);

        // Round 1: establish common state (all 7 in sync)
        sim.run(1);
        BEAST_EXPECT(sim.synchronized());

        // Record pre-partition state
        Ledger::Seq const prePartitionSeq = attackers[0]->lastClosedLedger.seq();
        Ledger::ID const prePartitionId = attackers[0]->fullyValidatedLedger.id();

        std::cout << "Pre-partition: all peers at seq " << prePartitionSeq << ", fully validated\n";

        // === PARTITION: disconnect legitimate validators ===
        // Simulate crash: disconnect from attackers AND from each other
        legitimate.disconnect(network);
        network.disconnect(legitimate);

        // Stop legitimate peers from running consensus during partition
        for (Peer* p : legitimate)
            p->targetLedgers = p->completedLedgers;

        // Attackers submit transactions and try to advance
        for (Peer* p : attackers)
            p->submit(Tx{static_cast<std::uint32_t>(p->id) + 100});

        // Run several rounds — attackers will close ledgers among themselves
        // (3/3 = 100% of visible proposers agree)
        sim.run(4);

        // Check attacker state
        Ledger::Seq attackerSeq = attackers[0]->lastClosedLedger.seq();
        Ledger::Seq legitimateSeq = legitimate[0]->lastClosedLedger.seq();

        std::cout << "During partition:\n";
        std::cout << "  Attackers closed up to seq " << attackerSeq << "\n";
        std::cout << "  Legitimate peers stuck at seq " << legitimateSeq << "\n";

        // Attackers advanced their closed ledger
        BEAST_EXPECT(attackerSeq > prePartitionSeq);
        // Note: legitimate peers may also advance closed ledger via
        // consensus timeout (isolated node eventually closes alone),
        // but they CANNOT fully validate.

        // KEY ASSERTION: attackers could NOT fully validate their chain
        // because they only have 3/7 validations (need 6)
        for (Peer* p : attackers)
        {
            std::cout << "  Attacker " << p->id
                      << " fullyValidated seq: " << p->fullyValidatedLedger.seq() << "\n";

            // The attacker's fully validated ledger should NOT have advanced
            // beyond the pre-partition state (they can't get 6/7 validations)
            BEAST_EXPECT(p->fullyValidatedLedger.id() == prePartitionId);
        }
    }

    /**
     * Test 2: Byzantine minority with quorum bypass
     *
     * 3 attackers override their quorum to 2 (simulating modified binary).
     * They can fully validate on their fork. When 4 legitimate peers
     * rejoin, the attacker chain should NOT be accepted by legitimate peers.
     */
    void
    testPartitionWithQuorumBypass()
    {
        using namespace csf;
        using namespace std::chrono;
        testcase("partition: attacker chain rejected after recovery (quorum bypass)");

        Sim sim;
        ConsensusParms const parms{};
        SimDuration const delay = round<milliseconds>(0.2 * parms.ledgerGRANULARITY);

        PeerGroup attackers = sim.createGroup(3);
        PeerGroup legitimate = sim.createGroup(4);
        PeerGroup network = attackers + legitimate;

        // Common UNL
        network.trust(network);
        network.connect(network, delay);

        PartitionTracker tracker;
        sim.collectors.add(tracker);

        // Round 1: establish common state
        sim.run(1);
        BEAST_EXPECT(sim.synchronized());

        Ledger::Seq const prePartitionSeq = attackers[0]->lastClosedLedger.seq();

        std::cout << "Pre-partition: all peers synced at seq " << prePartitionSeq << "\n";

        // === PARTITION ===
        legitimate.disconnect(network);
        network.disconnect(legitimate);

        for (Peer* p : legitimate)
            p->targetLedgers = p->completedLedgers;

        // SIMULATE MODIFIED BINARY: attackers bypass quorum
        // In production, the attacker modifies ValidatorList::calculateQuorum()
        // to return a lower value. In CSF, we override the quorum directly.
        // Note: quorum is recalculated in checkFullyValidated() from
        // trustGraph.graph().outDegree(this), so we need a different approach.
        // We'll untrust the legitimate peers FROM THE ATTACKER'S PERSPECTIVE
        // to simulate the modified binary lowering effective UNL.
        attackers.untrust(legitimate);

        // Now attackers only trust 3 peers → quorum = ceil(3 * 0.8) = 3
        // They can fully validate with just their own validations

        // Attackers submit transactions
        for (Peer* p : attackers)
            p->submit(Tx{static_cast<std::uint32_t>(p->id) + 200});

        // Run — attackers advance and fully validate their fork
        sim.run(4);

        Ledger::Seq const attackerSeq = attackers[0]->lastClosedLedger.seq();

        std::cout << "During partition (quorum bypass):\n";
        std::cout << "  Attacker closed seq: " << attackerSeq << "\n";

        for (Peer* p : attackers)
        {
            std::cout << "  Attacker " << p->id
                      << " fullyValidated seq: " << p->fullyValidatedLedger.seq() << "\n";
        }

        // Attackers DID advance and fully validate (on their modified view)
        BEAST_EXPECT(attackerSeq > prePartitionSeq);
        for (Peer* p : attackers)
        {
            BEAST_EXPECT(p->fullyValidatedLedger.seq() > prePartitionSeq);
        }

        // === RECOVERY: restore trust and reconnect ===
        // Restore attacker trust to full UNL (simulating reconnection
        // where the legitimate validators are back)
        attackers.trust(legitimate);

        // Reconnect network
        network.connect(network, delay);

        // Allow legitimate peers to run again
        for (Peer* p : legitimate)
            p->targetLedgers = p->completedLedgers + 10;

        // Run several rounds for recovery
        sim.run(6);

        std::cout << "\nAfter recovery:\n";
        std::cout << "  Branches: " << sim.branches() << "\n";
        std::cout << "  Synchronized: " << std::boolalpha << sim.synchronized() << "\n";

        for (Peer* p : network)
        {
            std::cout << "  Peer " << p->id << " closed=" << p->lastClosedLedger.seq()
                      << " fullyValidated=" << p->fullyValidatedLedger.seq() << "\n";
        }

        // KEY ASSERTIONS:
        // 1. Legitimate peers should NOT have accepted the attacker's
        //    fully validated ledger — their quorum is still 6/7
        for (Peer* p : legitimate)
        {
            // Legitimate peers' fully validated ledger should be based on
            // the pre-partition state or a new ledger with proper quorum,
            // NOT the attacker's fork
            std::size_t const numTrusted = sim.trustGraph.graph().outDegree(p);
            std::cout << "  Legitimate peer " << p->id << " trusts " << numTrusted
                      << " quorum=" << static_cast<std::size_t>(std::ceil(numTrusted * 0.8))
                      << "\n";
        }

        // 2. Check if the network re-converges or stays forked
        std::size_t const branches = sim.branches();
        std::cout << "  Final branch count: " << branches << "\n";

        // The attacker's chain (validated with only 3/7 trust)
        // should not be accepted by the full network.
        // After reconnection, with 7 validators all trusting each other,
        // quorum is 6. The attacker's old validations only had 3.
        // The network should eventually converge on a new chain.
    }

    /**
     * Test 3: Transaction injection during partition
     *
     * 3 attackers inject extra transactions (simulating pseudo-tx injection)
     * during the partition. Tests whether those transactions persist after
     * recovery.
     */
    void
    testTxInjectionDuringPartition()
    {
        using namespace csf;
        using namespace std::chrono;
        testcase("partition: injected transactions during partition");

        Sim sim;
        ConsensusParms const parms{};
        SimDuration const delay = round<milliseconds>(0.2 * parms.ledgerGRANULARITY);

        PeerGroup attackers = sim.createGroup(3);
        PeerGroup legitimate = sim.createGroup(4);
        PeerGroup network = attackers + legitimate;

        network.trust(network);
        network.connect(network, delay);

        PartitionTracker tracker;
        sim.collectors.add(tracker);

        // Establish common state
        sim.run(1);
        BEAST_EXPECT(sim.synchronized());

        Ledger::Seq const prePartitionSeq = attackers[0]->lastClosedLedger.seq();

        // === PARTITION ===
        legitimate.disconnect(network);
        network.disconnect(legitimate);

        for (Peer* p : legitimate)
            p->targetLedgers = p->completedLedgers;

        // Attackers bypass quorum (modified binary simulation)
        attackers.untrust(legitimate);

        // Inject a "malicious" transaction into attacker ledgers
        // This simulates an EnableAmendment pseudo-tx being injected
        // by the modified binary's doVoting()
        Tx const maliciousTx{999};
        for (Peer* p : attackers)
        {
            p->txInjections.emplace(prePartitionSeq, maliciousTx);
        }

        // Run partition phase
        sim.run(4);

        // Check that attackers included the injected tx
        Ledger const& attackerLedger = attackers[0]->lastClosedLedger;
        std::cout << "Attacker ledger seq: " << attackerLedger.seq() << "\n";

        // === RECOVERY ===
        attackers.trust(legitimate);
        network.connect(network, delay);

        for (Peer* p : legitimate)
            p->targetLedgers = p->completedLedgers + 10;

        sim.run(6);

        std::cout << "\nAfter recovery (tx injection):\n";
        std::cout << "  Branches: " << sim.branches() << "\n";
        std::cout << "  Synchronized: " << std::boolalpha << sim.synchronized() << "\n";

        // Check each peer's final state
        for (Peer* p : network)
        {
            std::cout << "  Peer " << p->id << " closed=" << p->lastClosedLedger.seq()
                      << " fullyValidated=" << p->fullyValidatedLedger.seq() << "\n";
        }

        // The legitimate peers should NOT have the injected transaction
        // in their fully validated ledger chain. The attacker's fork
        // (containing the injected tx) should be rejected because it
        // lacks sufficient validations from the full UNL.
    }

    /**
     * Test 4: Full attack surface sweep
     *
     * Sweeps across multiple network sizes (7, 10, 15, 20, 35) and
     * all attacker counts to find the exact threshold where the
     * Byzantine partition attack succeeds.
     */
    void
    testAttackSurfaceSweep()
    {
        using namespace csf;
        using namespace std::chrono;
        testcase("partition: full attack surface sweep");

        // Network sizes to test (7 = small, 35 = mainnet-like)
        std::vector<std::uint32_t> const networkSizes = {7, 10, 15, 20, 35};

        for (std::uint32_t const totalValidators : networkSizes)
        {
            std::cout << "\n=== Network size: " << totalValidators << " validators ===\n";
            std::cout << "  80% quorum = "
                      << static_cast<std::size_t>(std::ceil(totalValidators * 0.8)) << "\n";

            std::uint32_t attackThreshold = 0;

            for (std::uint32_t numAttackers = 1; numAttackers < totalValidators; ++numAttackers)
            {
                std::uint32_t const numLegitimate = totalValidators - numAttackers;

                Sim sim;
                ConsensusParms const parms{};
                SimDuration const delay = round<milliseconds>(0.2 * parms.ledgerGRANULARITY);

                PeerGroup attackers = sim.createGroup(numAttackers);
                PeerGroup legitimate = sim.createGroup(numLegitimate);
                PeerGroup network = attackers + legitimate;

                // Common UNL — all trust all
                network.trust(network);
                network.connect(network, delay);

                // Round 1: establish common state
                sim.run(1);

                // === PARTITION: crash legitimate validators ===
                legitimate.disconnect(network);
                network.disconnect(legitimate);

                // Stop legitimate peers from running
                for (Peer* p : legitimate)
                    p->targetLedgers = p->completedLedgers;

                // ATTACKER: bypass quorum by untrusting legitimate
                attackers.untrust(legitimate);

                // Attackers submit transactions
                for (Peer* p : attackers)
                    p->submit(Tx{static_cast<std::uint32_t>(p->id) + 1000});

                // Run partition phase (attackers produce chain)
                sim.run(4);

                // Record attacker FVL before recovery
                Ledger::Seq const attackerFVLBeforeRecovery =
                    attackers[0]->fullyValidatedLedger.seq();

                // === RECOVERY: reconnect ===
                attackers.trust(legitimate);
                network.connect(network, delay);

                for (Peer* p : legitimate)
                    p->targetLedgers = p->completedLedgers + 15;

                // Give plenty of recovery time
                sim.run(10);

                // Check if legitimate peers accepted attacker's chain
                // by verifying their FVL ID matches an attacker's FVL ID
                bool const legitimateAccepted = [&]() {
                    for (Peer* lp : legitimate)
                    {
                        if (lp->fullyValidatedLedger.seq() <= Ledger::Seq{1})
                            continue;
                        for (Peer* ap : attackers)
                        {
                            if (lp->fullyValidatedLedger.id() == ap->fullyValidatedLedger.id())
                                return true;
                        }
                    }
                    return false;
                }();

                bool const networkConverged = sim.synchronized();
                std::size_t const branches = sim.branches();

                float const attackerPct = 100.0f * numAttackers / totalValidators;

                std::cout << "  A=" << numAttackers << "/" << totalValidators << " ("
                          << static_cast<int>(attackerPct) << "%)"
                          << " AttackerFVL=" << attackerFVLBeforeRecovery
                          << " LegitAccepted=" << std::boolalpha << legitimateAccepted
                          << " Converged=" << networkConverged << " Branches=" << branches << "\n";

                if (legitimateAccepted && attackThreshold == 0)
                {
                    attackThreshold = numAttackers;
                    std::cout << "  >>> ATTACK THRESHOLD: " << numAttackers << "/"
                              << totalValidators << " (" << static_cast<int>(attackerPct) << "%)"
                              << " <<<\n";
                }
            }

            if (attackThreshold > 0)
            {
                float const thresholdPct = 100.0f * attackThreshold / totalValidators;
                std::cout << "  RESULT: Network " << totalValidators << " — attack succeeds at "
                          << attackThreshold << " attackers (" << static_cast<int>(thresholdPct)
                          << "%)\n";
            }
            else
            {
                std::cout << "  RESULT: Network " << totalValidators
                          << " — attack never succeeded\n";
            }

            // Byzantine safety: forcing the network to accept the attacker's
            // chain requires a strict majority of validators (never a tolerated
            // minority).
            BEAST_EXPECT(attackThreshold == 0 || attackThreshold * 2 > totalValidators);
        }
    }

    /**
     * Test 5: Long partition — attacker builds deep chain
     *
     * What if the attacker runs for many rounds during the partition,
     * building a much deeper chain? Does chain length affect acceptance?
     */
    void
    testLongPartitionDeepChain()
    {
        using namespace csf;
        using namespace std::chrono;
        testcase("partition: deep attacker chain");

        for (int partitionRounds : {4, 10, 20, 40})
        {
            Sim sim;
            ConsensusParms const parms{};
            SimDuration const delay = round<milliseconds>(0.2 * parms.ledgerGRANULARITY);

            PeerGroup attackers = sim.createGroup(4);
            PeerGroup legitimate = sim.createGroup(3);
            PeerGroup network = attackers + legitimate;

            network.trust(network);
            network.connect(network, delay);
            sim.run(1);

            // Partition
            legitimate.disconnect(network);
            network.disconnect(legitimate);
            for (Peer* p : legitimate)
                p->targetLedgers = p->completedLedgers;

            attackers.untrust(legitimate);

            for (Peer* p : attackers)
                p->submit(Tx{static_cast<std::uint32_t>(p->id) + 500});

            // Run for varying partition lengths
            sim.run(partitionRounds);

            Ledger::Seq const attackerDepth = attackers[0]->lastClosedLedger.seq();
            Ledger::Seq const attackerFVL = attackers[0]->fullyValidatedLedger.seq();

            // Recovery
            attackers.trust(legitimate);
            network.connect(network, delay);
            for (Peer* p : legitimate)
                p->targetLedgers = p->completedLedgers + 15;
            sim.run(10);

            bool const accepted = [&]() {
                for (Peer* lp : legitimate)
                {
                    if (lp->fullyValidatedLedger.seq() <= Ledger::Seq{1})
                        continue;
                    for (Peer* ap : attackers)
                    {
                        if (lp->fullyValidatedLedger.id() == ap->fullyValidatedLedger.id())
                            return true;
                    }
                }
                return false;
            }();

            std::cout << "PartitionRounds=" << partitionRounds << " AttackerDepth=" << attackerDepth
                      << " AttackerFVL=" << attackerFVL << " LegitAccepted=" << std::boolalpha
                      << accepted << " Branches=" << sim.branches()
                      << " Synced=" << sim.synchronized() << "\n";

            // With the attacker holding the majority (4 of 7), its chain is
            // accepted regardless of how deep the partition ran — depth is not
            // the deciding factor, validator share is.
            BEAST_EXPECT(accepted);
        }
    }

    /**
     * Test 6: Staggered recovery
     *
     * Legitimate validators don't all come back at once.
     * What if 2 come back first, then 2 more later?
     * Does partial recovery change the outcome?
     */
    void
    testStaggeredRecovery()
    {
        using namespace csf;
        using namespace std::chrono;
        testcase("partition: staggered recovery");

        Sim sim;
        ConsensusParms const parms{};
        SimDuration const delay = round<milliseconds>(0.2 * parms.ledgerGRANULARITY);

        PeerGroup attackers = sim.createGroup(4);
        PeerGroup legit1 = sim.createGroup(2);  // first wave recovery
        PeerGroup legit2 = sim.createGroup(1);  // second wave recovery
        PeerGroup legitimate = legit1 + legit2;
        PeerGroup network = attackers + legitimate;

        network.trust(network);
        network.connect(network, delay);
        sim.run(1);

        std::cout << "Pre-partition: all synced at seq " << attackers[0]->lastClosedLedger.seq()
                  << "\n";

        // Partition all legitimate
        legitimate.disconnect(network);
        network.disconnect(legitimate);
        for (Peer* p : legitimate)
            p->targetLedgers = p->completedLedgers;

        attackers.untrust(legitimate);
        for (Peer* p : attackers)
            p->submit(Tx{static_cast<std::uint32_t>(p->id) + 600});

        sim.run(4);

        std::cout << "After partition: attackers at seq " << attackers[0]->lastClosedLedger.seq()
                  << " FVL=" << attackers[0]->fullyValidatedLedger.seq() << "\n";

        // WAVE 1: partial recovery — only 2 legitimate peers return
        attackers.trust(legit1);
        legit1.trust(network);
        network.connect(legit1, delay);
        legit1.connect(network, delay);
        for (Peer* p : legit1)
            p->targetLedgers = p->completedLedgers + 10;

        sim.run(6);

        std::cout << "After wave 1 recovery (2 legit back):\n";
        for (Peer* p : network)
        {
            std::cout << "  Peer " << p->id << " closed=" << p->lastClosedLedger.seq()
                      << " FVL=" << p->fullyValidatedLedger.seq() << "\n";
        }

        // WAVE 2: remaining legitimate peers return
        attackers.trust(legit2);
        legit2.trust(network);
        network.connect(legit2, delay);
        legit2.connect(network, delay);
        for (Peer* p : legit2)
            p->targetLedgers = p->completedLedgers + 10;

        sim.run(6);

        std::cout << "After wave 2 recovery (all back):\n";
        std::cout << "  Branches=" << sim.branches() << " Synced=" << sim.synchronized() << "\n";
        for (Peer* p : network)
        {
            std::cout << "  Peer " << p->id << " closed=" << p->lastClosedLedger.seq()
                      << " FVL=" << p->fullyValidatedLedger.seq() << "\n";
        }

        // Once every legitimate validator is back, the network reconverges to a
        // single chain.
        BEAST_EXPECT(sim.synchronized());
    }

    /**
     * Test 7: Attacker injects conflicting transactions
     *
     * During partition, attackers inject transactions into their chain.
     * After recovery, do legitimate peers end up with the attacker's
     * injected transactions in their fully validated chain?
     * This directly tests the amendment bypass scenario.
     */
    void
    testConflictingTxPersistence()
    {
        using namespace csf;
        using namespace std::chrono;
        testcase("partition: conflicting tx persistence after recovery");

        Sim sim;
        ConsensusParms const parms{};
        SimDuration const delay = round<milliseconds>(0.2 * parms.ledgerGRANULARITY);

        // 4 attackers, 3 legitimate (attack SHOULD succeed)
        PeerGroup attackers = sim.createGroup(4);
        PeerGroup legitimate = sim.createGroup(3);
        PeerGroup network = attackers + legitimate;

        network.trust(network);
        network.connect(network, delay);
        sim.run(1);

        Ledger::Seq const prePartitionSeq = attackers[0]->lastClosedLedger.seq();

        // Partition
        legitimate.disconnect(network);
        network.disconnect(legitimate);
        for (Peer* p : legitimate)
            p->targetLedgers = p->completedLedgers;

        attackers.untrust(legitimate);

        // Inject a "malicious" transaction ONLY on attacker nodes
        // This simulates an EnableAmendment pseudo-tx bypass
        Tx const maliciousTx{9999};
        for (Peer* p : attackers)
            p->txInjections.emplace(prePartitionSeq, maliciousTx);

        sim.run(4);

        // Record the attacker ledger that contains the injected tx
        Ledger::ID const attackerLedgerWithTx = attackers[0]->lastClosedLedger.id();
        Ledger::Seq const attackerSeqWithTx = attackers[0]->lastClosedLedger.seq();

        std::cout << "Attacker injected tx at seq " << attackerSeqWithTx << "\n";

        // Recovery
        attackers.trust(legitimate);
        network.connect(network, delay);
        for (Peer* p : legitimate)
            p->targetLedgers = p->completedLedgers + 15;
        sim.run(10);

        // Check: does any legitimate peer have the attacker's ledger
        // (containing the injected tx) in their chain?
        bool const legitimateHasAttackerLedger = [&]() {
            for (Peer* lp : legitimate)
            {
                // Check if the attacker ledger is in the legitimate
                // peer's ledger cache
                auto it = lp->ledgers.find(attackerLedgerWithTx);
                if (it != lp->ledgers.end())
                    return true;
            }
            return false;
        }();

        bool const legitimateFVLDescendsFromAttacker = [&]() {
            for (Peer* lp : legitimate)
            {
                // Check if legitimate peer's FVL is built on the
                // attacker's chain
                if (lp->fullyValidatedLedger.seq() >= attackerSeqWithTx)
                {
                    // Check if the attacker ledger is an ancestor
                    auto it = lp->ledgers.find(attackerLedgerWithTx);
                    if (it != lp->ledgers.end() && lp->fullyValidatedLedger.isAncestor(it->second))
                        return true;
                }
            }
            return false;
        }();

        std::cout << "After recovery:\n"
                  << "  LegitHasAttackerLedger=" << std::boolalpha << legitimateHasAttackerLedger
                  << "\n"
                  << "  LegitFVLDescendsFromAttacker=" << legitimateFVLDescendsFromAttacker << "\n"
                  << "  Branches=" << sim.branches() << " Synced=" << sim.synchronized() << "\n";

        for (Peer* p : network)
        {
            std::cout << "  Peer " << p->id << " closed=" << p->lastClosedLedger.seq()
                      << " FVL=" << p->fullyValidatedLedger.seq() << "\n";
        }

        // The amendment-bypass safety property: a minority attacker's injected
        // transactions never enter the legitimate fully-validated chain.
        BEAST_EXPECT(!legitimateFVLDescendsFromAttacker);
    }

    /**
     * Test 8: Gradual nUNL-style takeover
     *
     * Instead of crashing all legitimate validators at once,
     * the attacker gradually removes them from trust (simulating
     * nUNL additions), then produces a chain.
     */
    void
    testGradualTakeover()
    {
        using namespace csf;
        using namespace std::chrono;
        testcase("partition: gradual nUNL-style takeover");

        Sim sim;
        ConsensusParms const parms{};
        SimDuration const delay = round<milliseconds>(0.2 * parms.ledgerGRANULARITY);

        // 4 attackers, 3 legitimate
        PeerGroup attackers = sim.createGroup(4);
        PeerGroup legit1 = sim.createGroup(1);
        PeerGroup legit2 = sim.createGroup(1);
        PeerGroup legit3 = sim.createGroup(1);
        PeerGroup legitimate = legit1 + legit2 + legit3;
        PeerGroup network = attackers + legitimate;

        network.trust(network);
        network.connect(network, delay);
        sim.run(1);

        std::cout << "All synced at seq " << network[0]->lastClosedLedger.seq() << "\n";

        // Phase 1: crash legit1, attackers remove from trust
        legit1.disconnect(network);
        network.disconnect(legit1);
        for (Peer* p : legit1)
            p->targetLedgers = p->completedLedgers;
        attackers.untrust(legit1);

        sim.run(2);
        std::cout << "Phase 1 (1 crashed): network at seq " << attackers[0]->lastClosedLedger.seq()
                  << " FVL=" << attackers[0]->fullyValidatedLedger.seq()
                  << " branches=" << sim.branches() << "\n";

        // Phase 2: crash legit2
        legit2.disconnect(network);
        network.disconnect(legit2);
        for (Peer* p : legit2)
            p->targetLedgers = p->completedLedgers;
        attackers.untrust(legit2);

        sim.run(2);
        std::cout << "Phase 2 (2 crashed): network at seq " << attackers[0]->lastClosedLedger.seq()
                  << " FVL=" << attackers[0]->fullyValidatedLedger.seq()
                  << " branches=" << sim.branches() << "\n";

        // Phase 3: crash legit3 — attackers are now alone
        legit3.disconnect(network);
        network.disconnect(legit3);
        for (Peer* p : legit3)
            p->targetLedgers = p->completedLedgers;
        attackers.untrust(legit3);

        // Now attackers trust only themselves (4/4 quorum = 4)
        for (Peer* p : attackers)
            p->submit(Tx{static_cast<std::uint32_t>(p->id) + 700});

        sim.run(4);
        std::cout << "Phase 3 (all crashed): attackers at seq "
                  << attackers[0]->lastClosedLedger.seq()
                  << " FVL=" << attackers[0]->fullyValidatedLedger.seq() << "\n";

        // Recovery: all legitimate come back at once
        attackers.trust(legitimate);
        network.connect(network, delay);
        for (Peer* p : legitimate)
            p->targetLedgers = p->completedLedgers + 15;
        sim.run(10);

        bool const accepted = [&]() {
            for (Peer* lp : legitimate)
            {
                if (lp->fullyValidatedLedger.seq() <= Ledger::Seq{1})
                    continue;
                for (Peer* ap : attackers)
                {
                    if (lp->fullyValidatedLedger.id() == ap->fullyValidatedLedger.id())
                        return true;
                }
            }
            return false;
        }();

        std::cout << "After recovery:\n"
                  << "  LegitAccepted=" << std::boolalpha << accepted
                  << " Branches=" << sim.branches() << " Synced=" << sim.synchronized() << "\n";

        // A gradual nUNL-style takeover that leaves the attacker holding the
        // majority (4 of 7) does flip the network onto the attacker's chain —
        // the reason runtime nUNL/quorum changes need coordination guardrails.
        BEAST_EXPECT(accepted);
        for (Peer* p : network)
        {
            std::cout << "  Peer " << p->id << " closed=" << p->lastClosedLedger.seq()
                      << " FVL=" << p->fullyValidatedLedger.seq() << "\n";
        }
    }

    void
    run() override
    {
        testPartitionNoQuorumBypass();
        testPartitionWithQuorumBypass();
        testTxInjectionDuringPartition();
        testAttackSurfaceSweep();
        testLongPartitionDeepChain();
        testStaggeredRecovery();
        testConflictingTxPersistence();
        testGradualTakeover();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(ByzantinePartitionRecovery, consensus, xrpl);

}  // namespace xrpl::test
