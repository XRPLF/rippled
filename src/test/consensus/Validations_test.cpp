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
#include <BeastConfig.h>
#include <ripple/basics/tagged_integer.h>
#include <ripple/beast/clock/manual_clock.h>
#include <ripple/beast/unit_test.h>
#include <ripple/consensus/Validations.h>
#include <test/csf/Validation.h>

#include <memory>
#include <tuple>
#include <type_traits>
#include <vector>

namespace ripple {
namespace test {
namespace csf {
class Validations_test : public beast::unit_test::suite
{
    using clock_type = beast::abstract_clock<std::chrono::steady_clock> const;

    // Helper to convert steady_clock to a reasonable NetClock
    // This allows a single manual clock in the unit tests
    static NetClock::time_point
    toNetClock(clock_type const& c)
    {
        // We don't care about the actual epochs, but do want the
        // generated NetClock time to be well past its epoch to ensure
        // any subtractions are positive
        using namespace std::chrono;
        return NetClock::time_point(duration_cast<NetClock::duration>(
            c.now().time_since_epoch() + 86400s));
    }

    // Represents a node that can issue validations
    class Node
    {
        clock_type const& c_;
        PeerID nodeID_;
        bool trusted_ = true;
        std::size_t signIdx_ = 1;
        boost::optional<std::uint32_t> loadFee_;

    public:
        Node(PeerID nodeID, clock_type const& c) : c_(c), nodeID_(nodeID)
        {
        }

        void
        untrust()
        {
            trusted_ = false;
        }

        void
        trust()
        {
            trusted_ = true;
        }

        void
        setLoadFee(std::uint32_t fee)
        {
            loadFee_ = fee;
        }

        PeerID
        nodeID() const
        {
            return nodeID_;
        }

        void
        advanceKey()
        {
            signIdx_++;
        }

        PeerKey
        currKey() const
        {
            return std::make_pair(nodeID_, signIdx_);
        }

        PeerKey
        masterKey() const
        {
            return std::make_pair(nodeID_, 0);
        }
        NetClock::time_point
        now() const
        {
            return toNetClock(c_);
        }

        // Issue a new validation with given sequence number and id and
        // with signing and seen times offset from the common clock
        Validation
        validate(
            Ledger::ID id,
            Ledger::Seq seq,
            NetClock::duration signOffset,
            NetClock::duration seenOffset,
            bool full) const
        {
            Validation v{id,
                         seq,
                         now() + signOffset,
                         now() + seenOffset,
                         currKey(),
                         nodeID_,
                         full,
                         loadFee_};
            if (trusted_)
                v.setTrusted();
            return v;
        }

        Validation
        validate(
            Ledger ledger,
            NetClock::duration signOffset,
            NetClock::duration seenOffset) const
        {
            return validate(
                ledger.id(), ledger.seq(), signOffset, seenOffset, true);
        }

        Validation
        validate(Ledger ledger) const
        {
            return validate(
                ledger.id(),
                ledger.seq(),
                NetClock::duration{0},
                NetClock::duration{0},
                true);
        }

        Validation
        partial(Ledger ledger) const
        {
            return validate(
                ledger.id(),
                ledger.seq(),
                NetClock::duration{0},
                NetClock::duration{0},
                false);
        }
    };

    // Saved StaleData for inspection in test
    struct StaleData
    {
        std::vector<Validation> stale;
        hash_map<PeerKey, Validation> flushed;
    };

    // Generic Validations adaptor that saves stale/flushed data into
    // a StaleData instance.
    class Adaptor
    {
        StaleData& staleData_;
        clock_type& c_;
        LedgerOracle& oracle_;

    public:
        // Non-locking mutex to avoid locks in generic Validations
        struct Mutex
        {
            void
            lock()
            {
            }

            void
            unlock()
            {
            }
        };

        using Validation = csf::Validation;
        using Ledger = csf::Ledger;

        Adaptor(StaleData& sd, clock_type& c, LedgerOracle& o)
            : staleData_{sd}, c_{c}, oracle_{o}
        {
        }

        NetClock::time_point
        now() const
        {
            return toNetClock(c_);
        }

        void
        onStale(Validation&& v)
        {
            staleData_.stale.emplace_back(std::move(v));
        }

        void
        flush(hash_map<PeerKey, Validation>&& remaining)
        {
            staleData_.flushed = std::move(remaining);
        }

        boost::optional<Ledger>
        acquire(Ledger::ID const& id)
        {
            return oracle_.lookup(id);
        }
    };

    // Specialize generic Validations using the above types
    using TestValidations = Validations<Adaptor>;

    // Gather the dependencies of TestValidations in a single class and provide
    // accessors for simplifying test logic
    class TestHarness
    {
        StaleData staleData_;
        ValidationParms p_;
        beast::manual_clock<std::chrono::steady_clock> clock_;
        TestValidations tv_;
        PeerID nextNodeId_{0};

    public:
        TestHarness(LedgerOracle& o)
            : tv_(p_, clock_, staleData_, clock_, o)
        {
        }

        ValStatus
        add(Validation const& v)
        {
            PeerKey masterKey{v.nodeID(), 0};
            return tv_.add(masterKey, v);
        }

        TestValidations&
        vals()
        {
            return tv_;
        }

        Node
        makeNode()
        {
            return Node(nextNodeId_++, clock_);
        }

        ValidationParms
        parms() const
        {
            return p_;
        }

        auto&
        clock()
        {
            return clock_;
        }

        std::vector<Validation> const&
        stale() const
        {
            return staleData_.stale;
        }

        hash_map<PeerKey, Validation> const&
        flushed() const
        {
            return staleData_.flushed;
        }
    };

     Ledger const genesisLedger{Ledger::MakeGenesis{}};

    void
    testAddValidation()
    {
        using namespace std::chrono_literals;

        testcase("Add validation");
        LedgerHistoryHelper h;
        Ledger ledgerA = h["a"];
        Ledger ledgerAB = h["ab"];
        Ledger ledgerAZ = h["az"];
        Ledger ledgerABC = h["abc"];
        Ledger ledgerABCD = h["abcd"];
        Ledger ledgerABCDE = h["abcde"];

        {
            TestHarness harness(h.oracle);
            Node n = harness.makeNode();

            auto const v = n.validate(ledgerA);

            // Add a current validation
            BEAST_EXPECT(ValStatus::current == harness.add(v));

            // Re-adding violates the increasing seq requirement for full
            // validations
            BEAST_EXPECT(ValStatus::badSeq == harness.add(v));

            harness.clock().advance(1s);
            // Replace with a new validation and ensure the old one is stale
            BEAST_EXPECT(harness.stale().empty());

            BEAST_EXPECT(
                ValStatus::current == harness.add(n.validate(ledgerAB)));

            BEAST_EXPECT(harness.stale().size() == 1);

            BEAST_EXPECT(harness.stale()[0].ledgerID() == ledgerA.id());

            // Test the node changing signing key

            // Confirm old ledger on hand, but not new ledger
            BEAST_EXPECT(
                harness.vals().numTrustedForLedger(ledgerAB.id()) == 1);
            BEAST_EXPECT(
                harness.vals().numTrustedForLedger(ledgerABC.id()) == 0);

            // Rotate signing keys
            n.advanceKey();

            harness.clock().advance(1s);

            // Cannot re-do the same full validation sequence
            BEAST_EXPECT(
                ValStatus::badSeq == harness.add(n.validate(ledgerAB)));
            // Cannot send the same partial validation sequence
            BEAST_EXPECT(
                ValStatus::badSeq == harness.add(n.partial(ledgerAB)));

            // Now trusts the newest ledger too
            harness.clock().advance(1s);
            BEAST_EXPECT(
                ValStatus::current == harness.add(n.validate(ledgerABC)));
            BEAST_EXPECT(
                harness.vals().numTrustedForLedger(ledgerAB.id()) == 1);
            BEAST_EXPECT(
                harness.vals().numTrustedForLedger(ledgerABC.id()) == 1);

            // Processing validations out of order should ignore the older
            // validation
            harness.clock().advance(2s);
            auto const valABCDE = n.validate(ledgerABCDE);

            harness.clock().advance(4s);
            auto const valABCD = n.validate(ledgerABCD);

            BEAST_EXPECT(ValStatus::current == harness.add(valABCD));

            BEAST_EXPECT(ValStatus::stale == harness.add(valABCDE));
        }

        {
            // Process validations out of order with shifted times

            TestHarness harness(h.oracle);
            Node n = harness.makeNode();

            // Establish a new current validation
            BEAST_EXPECT(
                ValStatus::current == harness.add(n.validate(ledgerA)));

            // Process a validation that has "later" seq but early sign time
            BEAST_EXPECT(
                ValStatus::stale ==
                harness.add(n.validate(ledgerAB, -1s, -1s)));

            // Process a validation that has a later seq and later sign
            // time
            BEAST_EXPECT(
                ValStatus::current ==
                harness.add(n.validate(ledgerABC, 1s, 1s)));
        }

        {
            // Test stale on arrival validations
            TestHarness harness(h.oracle);
            Node n = harness.makeNode();

            BEAST_EXPECT(
                ValStatus::stale ==
                harness.add(n.validate(
                    ledgerA, -harness.parms().validationCURRENT_EARLY, 0s)));

            BEAST_EXPECT(
                ValStatus::stale ==
                harness.add(n.validate(
                    ledgerA, harness.parms().validationCURRENT_WALL, 0s)));

            BEAST_EXPECT(
                ValStatus::stale ==
                harness.add(n.validate(
                    ledgerA, 0s, harness.parms().validationCURRENT_LOCAL)));
        }

        {
            // Test that full or partials cannot be sent for older sequence
            // numbers, unless time-out has happened
            for (bool doFull : {true, false})
            {
                TestHarness harness(h.oracle);
                Node n = harness.makeNode();

                auto process = [&](Ledger & lgr)
                {
                    if(doFull)
                        return harness.add(n.validate(lgr));
                    return harness.add(n.partial(lgr));
                };

                BEAST_EXPECT(ValStatus::current == process(ledgerABC));
                harness.clock().advance(1s);
                BEAST_EXPECT(ledgerAB.seq() < ledgerABC.seq());
                BEAST_EXPECT(ValStatus::badSeq == process(ledgerAB));

                // If we advance far enough for AB to expire, we can fully
                // validate or partially validate that sequence number again
                BEAST_EXPECT(ValStatus::badSeq == process(ledgerAZ));
                harness.clock().advance(
                    harness.parms().validationSET_EXPIRES + 1ms);
                BEAST_EXPECT(ValStatus::current == process(ledgerAZ));
            }
        }
    }

    void
    testOnStale()
    {
        testcase("Stale validation");
        // Verify validation becomes stale based solely on time passing, but
        // use different functions to trigger the check for staleness

        LedgerHistoryHelper h;
        Ledger ledgerA = h["a"];
        Ledger ledgerAB = h["ab"];


        using Trigger = std::function<void(TestValidations&)>;

        std::vector<Trigger> triggers = {
            [&](TestValidations& vals) { vals.currentTrusted(); },
            [&](TestValidations& vals) { vals.getPreferred(genesisLedger); },
            [&](TestValidations& vals) {
                vals.getNodesAfter(ledgerA, ledgerA.id());
            }};
        for (Trigger trigger : triggers)
        {
            TestHarness harness(h.oracle);
            Node n = harness.makeNode();

            BEAST_EXPECT(
                ValStatus::current == harness.add(n.validate(ledgerAB)));
            trigger(harness.vals());
            BEAST_EXPECT(
                harness.vals().getNodesAfter(ledgerA, ledgerA.id()) == 1);
            BEAST_EXPECT(
                harness.vals().getPreferred(genesisLedger) ==
                std::make_pair(ledgerAB.seq(), ledgerAB.id()));
            BEAST_EXPECT(harness.stale().empty());
            harness.clock().advance(harness.parms().validationCURRENT_LOCAL);

            // trigger check for stale
            trigger(harness.vals());

            BEAST_EXPECT(harness.stale().size() == 1);
            BEAST_EXPECT(harness.stale()[0].ledgerID() == ledgerAB.id());
            BEAST_EXPECT(
                harness.vals().getNodesAfter(ledgerA, ledgerA.id()) == 0);
            BEAST_EXPECT(
                harness.vals().getPreferred(genesisLedger) ==
                std::make_pair(Ledger::Seq{0}, Ledger::ID{0}));
        }
    }

    void
    testGetNodesAfter()
    {
        // Test getting number of nodes working on a validation descending
        // a prescribed one. This count should only be for trusted nodes, but
        // includes partial and full validations

        using namespace std::chrono_literals;
        testcase("Get nodes after");

        LedgerHistoryHelper h;
        Ledger ledgerA = h["a"];
        Ledger ledgerAB = h["ab"];
        Ledger ledgerABC = h["abc"];
        Ledger ledgerAD = h["ad"];

        TestHarness harness(h.oracle);
        Node a = harness.makeNode(), b = harness.makeNode(),
             c = harness.makeNode(), d = harness.makeNode();
        c.untrust();

        // first round a,b,c agree, d has differing id
        BEAST_EXPECT(ValStatus::current == harness.add(a.validate(ledgerA)));
        BEAST_EXPECT(ValStatus::current == harness.add(b.validate(ledgerA)));
        BEAST_EXPECT(ValStatus::current == harness.add(c.validate(ledgerA)));
        BEAST_EXPECT(ValStatus::current == harness.add(d.partial(ledgerA)));

        for (Ledger const& ledger : {ledgerA, ledgerAB, ledgerABC, ledgerAD})
            BEAST_EXPECT(
                harness.vals().getNodesAfter(ledger, ledger.id()) == 0);

        harness.clock().advance(5s);

        BEAST_EXPECT(ValStatus::current == harness.add(a.validate(ledgerAB)));
        BEAST_EXPECT(ValStatus::current == harness.add(b.validate(ledgerABC)));
        BEAST_EXPECT(ValStatus::current == harness.add(c.validate(ledgerAB)));
        BEAST_EXPECT(ValStatus::current == harness.add(d.partial(ledgerABC)));

        BEAST_EXPECT(harness.vals().getNodesAfter(ledgerA, ledgerA.id()) == 3);
        BEAST_EXPECT(
            harness.vals().getNodesAfter(ledgerAB, ledgerAB.id()) == 2);
        BEAST_EXPECT(
            harness.vals().getNodesAfter(ledgerABC, ledgerABC.id()) == 0);
        BEAST_EXPECT(
            harness.vals().getNodesAfter(ledgerAD, ledgerAD.id()) == 0);

        // If given a ledger inconsistent with the id, is still able to check
        // using slower method
        BEAST_EXPECT(harness.vals().getNodesAfter(ledgerAD, ledgerA.id()) == 1);
        BEAST_EXPECT(
            harness.vals().getNodesAfter(ledgerAD, ledgerAB.id()) == 2);
    }

    void
    testCurrentTrusted()
    {
        using namespace std::chrono_literals;
        testcase("Current trusted validations");

        LedgerHistoryHelper h;
        Ledger ledgerA = h["a"];
        Ledger ledgerB = h["b"];
        Ledger ledgerAC = h["ac"];

        TestHarness harness(h.oracle);
        Node a = harness.makeNode(), b = harness.makeNode();
        b.untrust();

        BEAST_EXPECT(ValStatus::current == harness.add(a.validate(ledgerA)));
        BEAST_EXPECT(ValStatus::current == harness.add(b.validate(ledgerB)));

        // Only a is trusted
        BEAST_EXPECT(harness.vals().currentTrusted().size() == 1);
        BEAST_EXPECT(
            harness.vals().currentTrusted()[0].ledgerID() == ledgerA.id());
        BEAST_EXPECT(harness.vals().currentTrusted()[0].seq() == ledgerA.seq());

        harness.clock().advance(3s);

        for (auto const& node : {a, b})
            BEAST_EXPECT(
                ValStatus::current == harness.add(node.validate(ledgerAC)));

        // New validation for a
        BEAST_EXPECT(harness.vals().currentTrusted().size() == 1);
        BEAST_EXPECT(
            harness.vals().currentTrusted()[0].ledgerID() == ledgerAC.id());
        BEAST_EXPECT(
            harness.vals().currentTrusted()[0].seq() == ledgerAC.seq());

        // Pass enough time for it to go stale
        harness.clock().advance(harness.parms().validationCURRENT_LOCAL);
        BEAST_EXPECT(harness.vals().currentTrusted().empty());
    }

    void
    testGetCurrentPublicKeys()
    {
        using namespace std::chrono_literals;
        testcase("Current public keys");

        LedgerHistoryHelper h;
        Ledger ledgerA = h["a"];
        Ledger ledgerAC = h["ac"];

        TestHarness harness(h.oracle);
        Node a = harness.makeNode(), b = harness.makeNode();
        b.untrust();

        for (auto const& node : {a, b})
            BEAST_EXPECT(
                ValStatus::current == harness.add(node.validate(ledgerA)));

        {
            hash_set<PeerKey> const expectedKeys = {a.masterKey(),
                                                    b.masterKey()};
            BEAST_EXPECT(harness.vals().getCurrentPublicKeys() == expectedKeys);
        }

        harness.clock().advance(3s);

        // Change keys and issue partials
        a.advanceKey();
        b.advanceKey();

        for (auto const& node : {a, b})
            BEAST_EXPECT(
                ValStatus::current == harness.add(node.partial(ledgerAC)));

        {
            hash_set<PeerKey> const expectedKeys = {a.masterKey(),
                                                    b.masterKey()};
            BEAST_EXPECT(harness.vals().getCurrentPublicKeys() == expectedKeys);
        }

        // Pass enough time for them to go stale
        harness.clock().advance(harness.parms().validationCURRENT_LOCAL);
        BEAST_EXPECT(harness.vals().getCurrentPublicKeys().empty());
    }

    void
    testTrustedByLedgerFunctions()
    {
        // Test the Validations functions that calculate a value by ledger ID
        using namespace std::chrono_literals;
        testcase("By ledger functions");

        // Several Validations functions return a set of values associated
        // with trusted ledgers sharing the same ledger ID.  The tests below
        // exercise this logic by saving the set of trusted Validations, and
        // verifying that the Validations member functions all calculate the
        // proper transformation of the available ledgers.

        LedgerHistoryHelper h;
        TestHarness harness(h.oracle);

        Node a = harness.makeNode(), b = harness.makeNode(),
             c = harness.makeNode(), d = harness.makeNode(),
             e = harness.makeNode();

        c.untrust();
        // Mix of load fees
        a.setLoadFee(12);
        b.setLoadFee(1);
        c.setLoadFee(12);
        e.setLoadFee(12);

        hash_map<Ledger::ID, std::vector<Validation>> trustedValidations;

        //----------------------------------------------------------------------
        // checkers
        auto sorted = [](auto vec) {
            std::sort(vec.begin(), vec.end());
            return vec;
        };
        auto compare = [&]() {
            for (auto& it : trustedValidations)
            {
                auto const& id = it.first;
                auto const& expectedValidations = it.second;

                BEAST_EXPECT(
                    harness.vals().numTrustedForLedger(id) ==
                    expectedValidations.size());
                BEAST_EXPECT(
                    sorted(harness.vals().getTrustedForLedger(id)) ==
                    sorted(expectedValidations));

                std::vector<NetClock::time_point> expectedTimes;
                std::uint32_t baseFee = 0;
                std::vector<uint32_t> expectedFees;
                for (auto const& val : expectedValidations)
                {
                    expectedTimes.push_back(val.signTime());
                    expectedFees.push_back(val.loadFee().value_or(baseFee));
                }

                BEAST_EXPECT(
                    sorted(harness.vals().fees(id, baseFee)) ==
                    sorted(expectedFees));

                BEAST_EXPECT(
                    sorted(harness.vals().getTrustedValidationTimes(id)) ==
                    sorted(expectedTimes));
            }
        };

        //----------------------------------------------------------------------
        Ledger ledgerA = h["a"];
        Ledger ledgerB = h["b"];
        Ledger ledgerAC = h["ac"];

        // Add a dummy ID to cover unknown ledger identifiers
        trustedValidations[Ledger::ID{100}] = {};

        // first round a,b,c agree
        for (auto const& node : {a, b, c})
        {
            auto const val = node.validate(ledgerA);
            BEAST_EXPECT(ValStatus::current == harness.add(val));
            if (val.trusted())
                trustedValidations[val.ledgerID()].emplace_back(val);
        }
        // d diagrees
        {
            auto const val = d.validate(ledgerB);
            BEAST_EXPECT(ValStatus::current == harness.add(val));
            trustedValidations[val.ledgerID()].emplace_back(val);
        }
        // e only issues partials
        {
            BEAST_EXPECT(ValStatus::current == harness.add(e.partial(ledgerA)));
        }

        harness.clock().advance(5s);
        // second round, a,b,c move to ledger 2
        for (auto const& node : {a, b, c})
        {
            auto const val = node.validate(ledgerAC);
            BEAST_EXPECT(ValStatus::current == harness.add(val));
            if (val.trusted())
                trustedValidations[val.ledgerID()].emplace_back(val);
        }
        // d now thinks ledger 1, but cannot re-issue a previously used seq
        {
            BEAST_EXPECT(ValStatus::badSeq == harness.add(d.partial(ledgerA)));
        }
        // e only issues partials
        {
            BEAST_EXPECT(
                ValStatus::current == harness.add(e.partial(ledgerAC)));
        }

        compare();
    }

    void
    testExpire()
    {
        // Verify expiring clears out validations stored by ledger
        testcase("Expire validations");
        LedgerHistoryHelper h;
        TestHarness harness(h.oracle);
        Node a = harness.makeNode();

        Ledger ledgerA = h["a"];

        BEAST_EXPECT(ValStatus::current == harness.add(a.validate(ledgerA)));
        BEAST_EXPECT(harness.vals().numTrustedForLedger(ledgerA.id()));
        harness.clock().advance(harness.parms().validationSET_EXPIRES);
        harness.vals().expire();
        BEAST_EXPECT(!harness.vals().numTrustedForLedger(ledgerA.id()));
    }

    void
    testFlush()
    {
        // Test final flush of validations
        using namespace std::chrono_literals;
        testcase("Flush validations");

        LedgerHistoryHelper h;
        TestHarness harness(h.oracle);
        Node a = harness.makeNode(), b = harness.makeNode(),
             c = harness.makeNode();
        c.untrust();

        Ledger ledgerA = h["a"];
        Ledger ledgerAB = h["ab"];

        hash_map<PeerKey, Validation> expected;
        for (auto const& node : {a, b, c})
        {
            auto const val = node.validate(ledgerA);
            BEAST_EXPECT(ValStatus::current == harness.add(val));
            expected.emplace(node.masterKey(), val);
        }
        Validation staleA = expected.find(a.masterKey())->second;

        // Send in a new validation for a, saving the new one into the expected
        // map after setting the proper prior ledger ID it replaced
        harness.clock().advance(1s);
        auto newVal = a.validate(ledgerAB);
        BEAST_EXPECT(ValStatus::current == harness.add(newVal));
        expected.find(a.masterKey())->second = newVal;

        // Now flush
        harness.vals().flush();

        // Original a validation was stale
        BEAST_EXPECT(harness.stale().size() == 1);
        BEAST_EXPECT(harness.stale()[0] == staleA);
        BEAST_EXPECT(harness.stale()[0].nodeID() == a.nodeID());

        auto const& flushed = harness.flushed();

        BEAST_EXPECT(flushed == expected);
    }

    void
    testGetPreferredLedger()
    {
        using namespace std::chrono_literals;
        testcase("Preferred Ledger");

        LedgerHistoryHelper h;
        TestHarness harness(h.oracle);
        Node a = harness.makeNode(), b = harness.makeNode(),
             c = harness.makeNode(), d = harness.makeNode();
        c.untrust();

        Ledger ledgerA = h["a"];
        Ledger ledgerB = h["b"];
        Ledger ledgerAC = h["ac"];
        Ledger ledgerACD = h["acd"];

        using Seq = Ledger::Seq;
        using ID = Ledger::ID;

        auto pref = [](Ledger ledger) {
            return std::make_pair(ledger.seq(), ledger.id());
        };

        // Empty (no ledgers)
        BEAST_EXPECT(
            harness.vals().getPreferred(ledgerA) == pref(genesisLedger));

        // Single ledger
        BEAST_EXPECT(ValStatus::current == harness.add(a.validate(ledgerB)));
        BEAST_EXPECT(harness.vals().getPreferred(ledgerA) == pref(ledgerB));
        BEAST_EXPECT(harness.vals().getPreferred(ledgerB) == pref(ledgerB));

        // Minimum valid sequence
        BEAST_EXPECT(
            harness.vals().getPreferred(ledgerA, Seq{10}) == ledgerA.id());

        // Untrusted doesn't impact preferred ledger
        // (ledgerB has tie-break over ledgerA)
        BEAST_EXPECT(ValStatus::current == harness.add(b.validate(ledgerA)));
        BEAST_EXPECT(ValStatus::current == harness.add(c.validate(ledgerA)));
        BEAST_EXPECT(ledgerB.id() > ledgerA.id());
        BEAST_EXPECT(harness.vals().getPreferred(ledgerA) == pref(ledgerB));
        BEAST_EXPECT(harness.vals().getPreferred(ledgerB) == pref(ledgerB));

        // Partial does break ties
        BEAST_EXPECT(ValStatus::current == harness.add(d.partial(ledgerA)));
        BEAST_EXPECT(harness.vals().getPreferred(ledgerA) == pref(ledgerA));
        BEAST_EXPECT(harness.vals().getPreferred(ledgerB) == pref(ledgerA));

        harness.clock().advance(5s);

        // Parent of preferred-> stick with ledger
        for (auto const& node : {a, b, c, d})
            BEAST_EXPECT(
                ValStatus::current == harness.add(node.validate(ledgerAC)));
        // Parent of preferred stays put
        BEAST_EXPECT(harness.vals().getPreferred(ledgerA) == pref(ledgerA));
        // Earlier different chain, switch
        BEAST_EXPECT(harness.vals().getPreferred(ledgerB) == pref(ledgerAC));
        // Later on chain, stays where it is
        BEAST_EXPECT(harness.vals().getPreferred(ledgerACD) == pref(ledgerACD));

        // Any later grandchild or different chain is preferred
        harness.clock().advance(5s);
        for (auto const& node : {a, b, c, d})
            BEAST_EXPECT(
                ValStatus::current == harness.add(node.validate(ledgerACD)));
        for (auto const& ledger : {ledgerA, ledgerB, ledgerACD})
            BEAST_EXPECT(
                harness.vals().getPreferred(ledger) == pref(ledgerACD));
    }

    void
    testGetPreferredLCL()
    {
        using namespace std::chrono_literals;
        testcase("Get preferred LCL");

        LedgerHistoryHelper h;
        TestHarness harness(h.oracle);
        Node a = harness.makeNode();

        Ledger ledgerA = h["a"];
        Ledger ledgerB = h["b"];
        Ledger ledgerC = h["c"];

        using ID = Ledger::ID;
        using Seq = Ledger::Seq;

        hash_map<ID, std::uint32_t> peerCounts;

        // No trusted validations or counts sticks with current ledger
        BEAST_EXPECT(
            harness.vals().getPreferredLCL(ledgerA, Seq{0}, peerCounts) ==
            ledgerA.id());

        ++peerCounts[ledgerB.id()];

        // No trusted validations, rely on peer counts
        BEAST_EXPECT(
            harness.vals().getPreferredLCL(ledgerA, Seq{0}, peerCounts) ==
            ledgerB.id());

        ++peerCounts[ledgerC.id()];
        // No trusted validations, tied peers goes with larger ID
        BEAST_EXPECT(ledgerC.id() > ledgerB.id());

        BEAST_EXPECT(
            harness.vals().getPreferredLCL(ledgerA, Seq{0}, peerCounts) ==
            ledgerC.id());

        peerCounts[ledgerC.id()] += 1000;

        // Single trusted always wins over peer counts
        BEAST_EXPECT(ValStatus::current == harness.add(a.validate(ledgerA)));
        BEAST_EXPECT(
            harness.vals().getPreferredLCL(ledgerA, Seq{0}, peerCounts) ==
            ledgerA.id());
        BEAST_EXPECT(
            harness.vals().getPreferredLCL(ledgerB, Seq{0}, peerCounts) ==
            ledgerA.id());
        BEAST_EXPECT(
            harness.vals().getPreferredLCL(ledgerC, Seq{0}, peerCounts) ==
            ledgerA.id());

        // Stick with current ledger if trusted validation ledger has too old
        // of a sequence
        BEAST_EXPECT(
            harness.vals().getPreferredLCL(ledgerB, Seq{2}, peerCounts) ==
            ledgerB.id());
    }

    void
    testAcquireValidatedLedger()
    {
        using namespace std::chrono_literals;
        testcase("Acquire validated ledger");

        LedgerHistoryHelper h;
        TestHarness harness(h.oracle);
        Node a = harness.makeNode();
        Node b = harness.makeNode();

        using ID = Ledger::ID;
        using Seq = Ledger::Seq;

        // Validate the ledger before it is actually available
        Validation val = a.validate(ID{2}, Seq{2}, 0s, 0s, true);

        BEAST_EXPECT(ValStatus::current == harness.add(val));
        // Validation is available
        BEAST_EXPECT(harness.vals().numTrustedForLedger(ID{2}) == 1);
        // but ledger based data is not
        BEAST_EXPECT(harness.vals().getNodesAfter(genesisLedger, ID{0}) == 0);

        // Create the ledger
        Ledger ledgerAB = h["ab"];
        // Now it should be available
        BEAST_EXPECT(harness.vals().getNodesAfter(genesisLedger, ID{0}) == 1);

        // Create a validation that is not available
        harness.clock().advance(5s);
        Validation val2 = a.validate(ID{4}, Seq{4}, 0s, 0s, true);
        BEAST_EXPECT(ValStatus::current == harness.add(val2));
        BEAST_EXPECT(harness.vals().numTrustedForLedger(ID{4}) == 1);
        BEAST_EXPECT(
            harness.vals().getPreferred(genesisLedger) ==
            std::make_pair(ledgerAB.seq(), ledgerAB.id()));

        // Another node requesting that ledger still doesn't change things
        Validation val3 = b.validate(ID{4}, Seq{4}, 0s, 0s, true);
        BEAST_EXPECT(ValStatus::current == harness.add(val3));
        BEAST_EXPECT(harness.vals().numTrustedForLedger(ID{4}) == 2);
        BEAST_EXPECT(
            harness.vals().getPreferred(genesisLedger) ==
            std::make_pair(ledgerAB.seq(), ledgerAB.id()));

        // Switch to validation that is available
        harness.clock().advance(5s);
        Ledger ledgerABCDE = h["abcde"];
        BEAST_EXPECT(ValStatus::current == harness.add(a.partial(ledgerABCDE)));
        BEAST_EXPECT(ValStatus::current == harness.add(b.partial(ledgerABCDE)));
        BEAST_EXPECT(
            harness.vals().getPreferred(genesisLedger) ==
            std::make_pair(ledgerABCDE.seq(), ledgerABCDE.id()));
    }

    void
    testNumTrustedForLedger()
    {
        testcase("NumTrustedForLedger");
        LedgerHistoryHelper h;
        TestHarness harness(h.oracle);
        Node a = harness.makeNode();
        Node b = harness.makeNode();
        Ledger ledgerA = h["a"];

        BEAST_EXPECT(ValStatus::current == harness.add(a.partial(ledgerA)));
        BEAST_EXPECT(harness.vals().numTrustedForLedger(ledgerA.id()) == 0);

        BEAST_EXPECT(ValStatus::current == harness.add(b.validate(ledgerA)));
        BEAST_EXPECT(harness.vals().numTrustedForLedger(ledgerA.id()) == 1);
    }

    void
    testSeqEnforcer()
    {
        testcase("SeqEnforcer");
        using Seq = Ledger::Seq;
        using namespace std::chrono;

        beast::manual_clock<steady_clock> clock;
        SeqEnforcer<Seq> enforcer;

        ValidationParms p;

        BEAST_EXPECT(enforcer(clock.now(), Seq{1}, p));
        BEAST_EXPECT(enforcer(clock.now(), Seq{10}, p));
        BEAST_EXPECT(!enforcer(clock.now(), Seq{5}, p));
        BEAST_EXPECT(!enforcer(clock.now(), Seq{9}, p));
        clock.advance(p.validationSET_EXPIRES - 1ms);
        BEAST_EXPECT(!enforcer(clock.now(), Seq{1}, p));
        clock.advance(2ms);
        BEAST_EXPECT(enforcer(clock.now(), Seq{1}, p));
    }

    void
    run() override
    {
        testAddValidation();
        testOnStale();
        testGetNodesAfter();
        testCurrentTrusted();
        testGetCurrentPublicKeys();
        testTrustedByLedgerFunctions();
        testExpire();
        testFlush();
        testGetPreferredLedger();
        testGetPreferredLCL();
        testAcquireValidatedLedger();
        testNumTrustedForLedger();
        testSeqEnforcer();
    }
};

BEAST_DEFINE_TESTSUITE(Validations, consensus, ripple);
}  // namespace csf
}  // namespace test
}  // namespace ripple
