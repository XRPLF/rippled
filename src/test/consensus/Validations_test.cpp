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

#include <tuple>
#include <type_traits>
#include <vector>
#include <memory>

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
            Ledger::Seq seq,
            Ledger::ID id,
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
            Ledger::Seq seq,
            Ledger::ID id,
            NetClock::duration signOffset,
            NetClock::duration seenOffset) const
        {
            return validate(
                seq, id, signOffset, seenOffset, true);
        }

        Validation
        validate(Ledger::Seq seq,  Ledger::ID id) const
        {
            return validate(
                seq, id, NetClock::duration{0}, NetClock::duration{0}, true);
        }

        Validation
        partial(Ledger::Seq seq, Ledger::ID id) const
        {
            return validate(
                seq,
                id,
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
        beast::Journal j_;

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

        Adaptor(StaleData& sd, clock_type& c, beast::Journal j)
            : staleData_{sd}, c_{c}, j_{j}
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

        beast::Journal
        journal() const
        {
            return j_;
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
        beast::Journal j_;
        TestValidations tv_;
        PeerID nextNodeId_{0};

    public:
        TestHarness() : tv_(p_, clock_, staleData_, clock_, j_)
        {
        }

        // Helper to add an existing validation
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

    void
    testAddValidation()
    {
        // Test adding current,stale,repeat validations
        using namespace std::chrono_literals;

        TestHarness harness;
        Node a = harness.makeNode();
        {
            {
                auto const v = a.validate(Ledger::Seq{1}, Ledger::ID{1});

                // Add a current validation
                BEAST_EXPECT(ValStatus::current == harness.add(v));

                // Re-adding is repeat
                BEAST_EXPECT(ValStatus::badFullSeq == harness.add(v));
            }

            {
                harness.clock().advance(1s);
                // Replace with a new validation and ensure the old one is stale
                BEAST_EXPECT(harness.stale().empty());

                BEAST_EXPECT(ValStatus::current ==
                    harness.add(a.validate(Ledger::Seq{2}, Ledger::ID{2})));

                BEAST_EXPECT(harness.stale().size() == 1);

                BEAST_EXPECT(harness.stale()[0].ledgerID() == Ledger::ID{1});
            }

            {
                // Test the node changing signing key, then reissuing a ledger

                // Confirm old ledger on hand, but not new ledger
                BEAST_EXPECT(
                    harness.vals().numTrustedForLedger(Ledger::ID{2}) == 1);
                BEAST_EXPECT(
                    harness.vals().numTrustedForLedger(Ledger::ID{20}) == 0);

                // Issue a new signing key and re-issue the validation with a
                // new ID but the same sequence number
                a.advanceKey();

                // No validations following ID{2}
                BEAST_EXPECT(harness.vals().getNodesAfter(Ledger::ID{2}) == 0);

                BEAST_EXPECT(
                    ValStatus::badFullSeq ==
                    harness.add(a.validate(Ledger::Seq{2}, Ledger::ID{20})));

                BEAST_EXPECT(
                    harness.vals().numTrustedForLedger(Ledger::ID{2}) == 1);
                BEAST_EXPECT(
                    harness.vals().numTrustedForLedger(Ledger::ID{20}) == 0);
            }

            {
                // Processing validations out of order should ignore the older
                harness.clock().advance(2s);
                auto const val3 = a.validate(Ledger::Seq{4}, Ledger::ID{4});

                harness.clock().advance(4s);
                auto const val4 = a.validate(Ledger::Seq{3}, Ledger::ID{3});

                BEAST_EXPECT(ValStatus::current == harness.add(val4));

                BEAST_EXPECT(ValStatus::stale == harness.add(val3));

            }
            {
                // Process validations out of order with shifted times

                // flush old validations
                harness.clock().advance(1h);

                // Establish a new current validation
                BEAST_EXPECT(ValStatus::current ==
                    harness.add(a.validate(Ledger::Seq{8}, Ledger::ID{8})));

                // Process a validation that has "later" seq but early sign time
                BEAST_EXPECT(
                    ValStatus::stale ==
                    harness.add(
                        a.validate(Ledger::Seq{9}, Ledger::ID{9}, -1s, -1s)));
            }
            {
                // Test stale on arrival validations
                harness.clock().advance(1h);

                BEAST_EXPECT(ValStatus::stale ==
                    harness.add(a.validate(Ledger::Seq{15}, Ledger::ID{15},
                        -harness.parms().validationCURRENT_EARLY, 0s)));

                BEAST_EXPECT(ValStatus::stale ==
                    harness.add(a.validate(Ledger::Seq{15}, Ledger::ID{15},
                        harness.parms().validationCURRENT_WALL, 0s)));

                BEAST_EXPECT(ValStatus::stale ==
                    harness.add(a.validate(Ledger::Seq{15}, Ledger::ID{15}, 0s,
                        harness.parms().validationCURRENT_LOCAL)));
            }
        }
    }

    void
    testOnStale()
    {
        // Verify validation becomes stale based solely on time passing
        TestHarness harness;
        Node a = harness.makeNode();

        BEAST_EXPECT(ValStatus::current ==
            harness.add(a.validate(Ledger::Seq{1}, Ledger::ID{1})));
        harness.vals().currentTrusted();
        BEAST_EXPECT(harness.stale().empty());
        harness.clock().advance(harness.parms().validationCURRENT_LOCAL);

        // trigger iteration over current
        harness.vals().currentTrusted();

        BEAST_EXPECT(harness.stale().size() == 1);
        BEAST_EXPECT(harness.stale()[0].ledgerID() == Ledger::ID{1});
    }

    void
    testGetNodesAfter()
    {
        // Test getting number of nodes working on a validation following
        // a prescribed one
        using namespace std::chrono_literals;

        TestHarness harness;
        Node a = harness.makeNode(), b = harness.makeNode(),
             c = harness.makeNode(), d = harness.makeNode();

        c.untrust();

        // first round a,b,c agree, d has differing id
        for (auto const& node : {a, b, c})
            BEAST_EXPECT(ValStatus::current ==
                harness.add(node.validate(Ledger::Seq{1}, Ledger::ID{1})));
        BEAST_EXPECT(ValStatus::current ==
            harness.add(d.validate(Ledger::Seq{1}, Ledger::ID{10})));

        // Nothing past ledger 1 yet
        BEAST_EXPECT(harness.vals().getNodesAfter(Ledger::ID{1}) == 0);

        harness.clock().advance(5s);

        // a and b have the same prior id, but b has a different current id
        // c is untrusted but on the same prior id
        // d has a different prior id
        BEAST_EXPECT(ValStatus::current ==
            harness.add(a.validate(Ledger::Seq{2}, Ledger::ID{2})));
        BEAST_EXPECT(ValStatus::current ==
            harness.add(b.validate(Ledger::Seq{2}, Ledger::ID{20})));
        BEAST_EXPECT(ValStatus::current ==
            harness.add(c.validate(Ledger::Seq{2}, Ledger::ID{2})));
        BEAST_EXPECT(ValStatus::current ==
            harness.add(d.validate(Ledger::Seq{2}, Ledger::ID{2})));

        BEAST_EXPECT(harness.vals().getNodesAfter(Ledger::ID{1}) == 2);
    }

    void
    testCurrentTrusted()
    {
        // Test getting current trusted validations
        using namespace std::chrono_literals;

        TestHarness harness;
        Node a = harness.makeNode(), b = harness.makeNode();
        b.untrust();

        BEAST_EXPECT(ValStatus::current ==
            harness.add(a.validate(Ledger::Seq{1}, Ledger::ID{1})));
        BEAST_EXPECT(ValStatus::current ==
            harness.add(b.validate(Ledger::Seq{1}, Ledger::ID{3})));

        // Only a is trusted
        BEAST_EXPECT(harness.vals().currentTrusted().size() == 1);
        BEAST_EXPECT(
            harness.vals().currentTrusted()[0].ledgerID() == Ledger::ID{1});
        BEAST_EXPECT(
            harness.vals().currentTrusted()[0].seq() == Ledger::Seq{1});

        harness.clock().advance(3s);

        for (auto const& node : {a, b})
            BEAST_EXPECT(ValStatus::current ==
                harness.add(node.validate(Ledger::Seq{2}, Ledger::ID{2})));

        // New validation for a
        BEAST_EXPECT(harness.vals().currentTrusted().size() == 1);
        BEAST_EXPECT(
            harness.vals().currentTrusted()[0].ledgerID() == Ledger::ID{2});
        BEAST_EXPECT(
            harness.vals().currentTrusted()[0].seq() == Ledger::Seq{2});

        // Pass enough time for it to go stale
        harness.clock().advance(harness.parms().validationCURRENT_LOCAL);
        BEAST_EXPECT(harness.vals().currentTrusted().empty());
    }

    void
    testGetCurrentPublicKeys()
    {
        // Test getting current keys validations
        using namespace std::chrono_literals;

        TestHarness harness;
        Node a = harness.makeNode(), b = harness.makeNode();
        b.untrust();

        for (auto const& node : {a, b})
            BEAST_EXPECT(ValStatus::current ==
                harness.add(node.validate(Ledger::Seq{1}, Ledger::ID{1})));

        {
            hash_set<PeerKey> const expectedKeys = {
                a.masterKey(), b.masterKey()};
            BEAST_EXPECT(harness.vals().getCurrentPublicKeys() == expectedKeys);
        }

        harness.clock().advance(3s);

        // Change keys
        a.advanceKey();
        b.advanceKey();

        for (auto const& node : {a, b})
            BEAST_EXPECT(ValStatus::current ==
                harness.add(node.validate(Ledger::Seq{2}, Ledger::ID{2})));

        {
            hash_set<PeerKey> const expectedKeys = {
                a.masterKey(), b.masterKey()};
            BEAST_EXPECT(harness.vals().getCurrentPublicKeys() == expectedKeys);
        }

        // Pass enough time for them to go stale
        harness.clock().advance(harness.parms().validationCURRENT_LOCAL);
        BEAST_EXPECT(harness.vals().getCurrentPublicKeys().empty());
    }

    void
    testCurrentTrustedDistribution()
    {
        // Test the trusted distribution calculation, including ledger slips
        // and sequence cutoffs
        using namespace std::chrono_literals;

        TestHarness harness;

        Node baby = harness.makeNode(), papa = harness.makeNode(),
             mama = harness.makeNode(), goldilocks = harness.makeNode();
        goldilocks.untrust();

        // Stagger the validations around sequence 2
        //  papa on seq 1 is behind
        //  baby on seq 2 is just right
        //  mama on seq 3 is ahead
        //  goldilocks on seq 2, but is not trusted

        for (auto const& node : {baby, papa, mama, goldilocks})
            BEAST_EXPECT(ValStatus::current ==
                harness.add(node.validate(Ledger::Seq{1}, Ledger::ID{1})));

        harness.clock().advance(1s);
        for (auto const& node : {baby, mama, goldilocks})
            BEAST_EXPECT(ValStatus::current ==
                harness.add(node.validate(Ledger::Seq{2}, Ledger::ID{2})));

        harness.clock().advance(1s);
        BEAST_EXPECT(ValStatus::current ==
            harness.add(mama.validate(Ledger::Seq{3}, Ledger::ID{3})));

        {
            // Allow slippage that treats all trusted as the current ledger
            auto res = harness.vals().currentTrustedDistribution(
                Ledger::ID{2},    // Current ledger
                Ledger::ID{1},    // Prior ledger
                Ledger::Seq{0});  // No cutoff

            BEAST_EXPECT(res.size() == 1);
            BEAST_EXPECT(res[Ledger::ID{2}] == 3);
            BEAST_EXPECT(
                getPreferredLedger(Ledger::ID{2}, res) == Ledger::ID{2});
        }

        {
            // Don't allow slippage back for prior ledger
            auto res = harness.vals().currentTrustedDistribution(
                Ledger::ID{2},    // Current ledger
                Ledger::ID{0},    // No prior ledger
                Ledger::Seq{0});  // No cutoff

            BEAST_EXPECT(res.size() == 2);
            BEAST_EXPECT(res[Ledger::ID{2}] == 2);
            BEAST_EXPECT(res[Ledger::ID{1}] == 1);
            BEAST_EXPECT(
                getPreferredLedger(Ledger::ID{2}, res) == Ledger::ID{2});
        }

        {
            // Don't allow any slips
            auto res = harness.vals().currentTrustedDistribution(
                Ledger::ID{0},    // No current ledger
                Ledger::ID{0},    // No prior ledger
                Ledger::Seq{0});  // No cutoff

            BEAST_EXPECT(res.size() == 3);
            BEAST_EXPECT(res[Ledger::ID{1}] == 1);
            BEAST_EXPECT(res[Ledger::ID{2}] == 1);
            BEAST_EXPECT(res[Ledger::ID{3}] == 1);
            BEAST_EXPECT(
                getPreferredLedger(Ledger::ID{0}, res) == Ledger::ID{3});
        }

        {
            // Cutoff old sequence numbers
            auto res = harness.vals().currentTrustedDistribution(
                Ledger::ID{2},    // current ledger
                Ledger::ID{1},    // prior ledger
                Ledger::Seq{2});  // Only sequence 2 or later
            BEAST_EXPECT(res.size() == 1);
            BEAST_EXPECT(res[Ledger::ID{2}] == 2);
            BEAST_EXPECT(
                getPreferredLedger(Ledger::ID{2}, res) == Ledger::ID{2});
        }
    }

    void
    testTrustedByLedgerFunctions()
    {
        // Test the Validations functions that calculate a value by ledger ID
        using namespace std::chrono_literals;

        // Several Validations functions return a set of values associated
        // with trusted ledgers sharing the same ledger ID.  The tests below
        // exercise this logic by saving the set of trusted Validations, and
        // verifying that the Validations member functions all calculate the
        // proper transformation of the available ledgers.

        TestHarness harness;
        Node a = harness.makeNode(), b = harness.makeNode(),
             c = harness.makeNode(), d = harness.makeNode();
        c.untrust();
        // Mix of load fees
        a.setLoadFee(12);
        b.setLoadFee(1);
        c.setLoadFee(12);

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

                BEAST_EXPECT(harness.vals().numTrustedForLedger(id) ==
                    expectedValidations.size());
                BEAST_EXPECT(sorted(harness.vals().getTrustedForLedger(id)) ==
                    sorted(expectedValidations));

                std::vector<NetClock::time_point> expectedTimes;
                std::uint32_t baseFee = 0;
                std::vector<uint32_t> expectedFees;
                for (auto const& val : expectedValidations)
                {
                    expectedTimes.push_back(val.signTime());
                    expectedFees.push_back(val.loadFee().value_or(baseFee));
                }

                BEAST_EXPECT(sorted(harness.vals().fees(id, baseFee)) ==
                    sorted(expectedFees));

                BEAST_EXPECT(sorted(harness.vals().getTrustedValidationTimes(
                                 id)) == sorted(expectedTimes));
            }
        };

        //----------------------------------------------------------------------
        // Add a dummy ID to cover unknown ledger identifiers
        trustedValidations[Ledger::ID{100}] = {};

        // first round a,b,c agree, d differs
        for (auto const& node : {a, b, c})
        {
            auto const val = node.validate(Ledger::Seq{1}, Ledger::ID{1});
            BEAST_EXPECT(ValStatus::current == harness.add(val));
            if (val.trusted())
                trustedValidations[val.ledgerID()].emplace_back(val);
        }
        {
            auto const val = d.validate(Ledger::Seq{1}, Ledger::ID{11});
            BEAST_EXPECT(ValStatus::current == harness.add(val));
            trustedValidations[val.ledgerID()].emplace_back(val);
        }

        harness.clock().advance(5s);
        // second round, a,b,c move to ledger 2, d now thinks ledger 1
        for (auto const& node : {a, b, c})
        {
            auto const val = node.validate(Ledger::Seq{2}, Ledger::ID{2});
            BEAST_EXPECT(ValStatus::current == harness.add(val));
            if (val.trusted())
                trustedValidations[val.ledgerID()].emplace_back(val);
        }
        {
            auto const val = d.validate(Ledger::Seq{2}, Ledger::ID{1});
            BEAST_EXPECT(ValStatus::current == harness.add(val));
            trustedValidations[val.ledgerID()].emplace_back(val);
        }

        compare();
    }

    void
    testExpire()
    {
        // Verify expiring clears out validations stored by ledger

        TestHarness harness;
        Node a = harness.makeNode();

        BEAST_EXPECT(ValStatus::current ==
            harness.add(a.validate(Ledger::Seq{1}, Ledger::ID{1})));
        BEAST_EXPECT(harness.vals().numTrustedForLedger(Ledger::ID{1}));
        harness.clock().advance(harness.parms().validationSET_EXPIRES);
        harness.vals().expire();
        BEAST_EXPECT(!harness.vals().numTrustedForLedger(Ledger::ID{1}));
    }

    void
    testFlush()
    {
        // Test final flush of validations
        using namespace std::chrono_literals;

        TestHarness harness;

        Node a = harness.makeNode(), b = harness.makeNode(),
             c = harness.makeNode();
        c.untrust();

        hash_map<PeerKey, Validation> expected;
        for (auto const& node : {a, b, c})
        {
            auto const val = node.validate(Ledger::Seq{1}, Ledger::ID{1});
            BEAST_EXPECT(ValStatus::current == harness.add(val));
            expected.emplace(node.masterKey(), val);
        }
        Validation staleA = expected.find(a.masterKey())->second;


        // Send in a new validation for a, saving the new one into the expected
        // map after setting the proper prior ledger ID it replaced
        harness.clock().advance(1s);
        auto newVal = a.validate(Ledger::Seq{2}, Ledger::ID{2});
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
        using Distribution = hash_map<Ledger::ID, std::uint32_t>;

        {
            Ledger::ID const current{1};
            Distribution dist;
            BEAST_EXPECT(getPreferredLedger(current, dist) == current);
        }

        {
            Ledger::ID const current{1};
            Distribution dist;
            dist[Ledger::ID{2}] = 2;
            BEAST_EXPECT(getPreferredLedger(current, dist) == Ledger::ID{2});
        }

        {
            Ledger::ID const current{1};
            Distribution dist;
            dist[Ledger::ID{1}] = 1;
            dist[Ledger::ID{2}] = 2;
            BEAST_EXPECT(getPreferredLedger(current, dist) == Ledger::ID{2});
        }

        {
            Ledger::ID const current{1};
            Distribution dist;
            dist[Ledger::ID{1}] = 2;
            dist[Ledger::ID{2}] = 2;
            BEAST_EXPECT(getPreferredLedger(current, dist) == current);
        }

        {
            Ledger::ID const current{2};
            Distribution dist;
            dist[Ledger::ID{1}] = 2;
            dist[Ledger::ID{2}] = 2;
            BEAST_EXPECT(getPreferredLedger(current, dist) == current);
        }

        {
            Ledger::ID const current{1};
            Distribution dist;
            dist[Ledger::ID{2}] = 2;
            dist[Ledger::ID{3}] = 2;
            BEAST_EXPECT(getPreferredLedger(current, dist) == Ledger::ID{3});
        }
    }

    void
    run() override
    {
        testAddValidation();
        testOnStale();
        testGetNodesAfter();
        testCurrentTrusted();
        testGetCurrentPublicKeys();
        testCurrentTrustedDistribution();
        testTrustedByLedgerFunctions();
        testExpire();
        testFlush();
        testGetPreferredLedger();
    }
};

BEAST_DEFINE_TESTSUITE(Validations, consensus, ripple);
}  // csf
}  // test
}  // ripple
