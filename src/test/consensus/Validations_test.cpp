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
#include <ripple/beast/clock/manual_clock.h>
#include <ripple/beast/unit_test.h>
#include <ripple/consensus/Validations.h>

#include <tuple>
#include <type_traits>
#include <vector>
#include <memory>

namespace ripple {
namespace test {

class Validations_test : public beast::unit_test::suite
{
    using clock_type = beast::abstract_clock<std::chrono::steady_clock> const;
    //--------------------------------------------------------------------------
    // Basic type wrappers for validation types

    // Represents a ledger sequence number
    struct Seq
    {
        explicit Seq(std::uint32_t sIn) : s{sIn}
        {
        }

        Seq() : s{0}
        {
        }

        operator std::uint32_t() const
        {
            return s;
        }

        std::uint32_t s;
    };

    // Represents a unique ledger identifier
    struct ID
    {
        explicit ID(std::uint32_t idIn) : id{idIn}
        {
        }

        ID() : id{0}
        {
        }

        int
        signum() const
        {
            return id == 0 ? 0 : 1;
        }

        operator std::size_t() const
        {
            return id;
        }

        template <class Hasher>
        friend void
        hash_append(Hasher& h, ID const& id)
        {
            using beast::hash_append;
            hash_append(h, id.id);
        }

        std::uint32_t id;
    };

    class Node;

    // Basic implementation of the requirements of Validation in the generic
    // Validations class
    class Validation
    {
        friend class Node;

        ID ledgerID_ = ID{0};
        Seq seq_ = Seq{0};
        NetClock::time_point signTime_;
        NetClock::time_point seenTime_;
        std::string key_;
        std::size_t nodeID_ = 0;
        bool trusted_ = true;
        boost::optional<std::uint32_t> loadFee_;

    public:
        Validation()
        {
        }

        ID
        ledgerID() const
        {
            return ledgerID_;
        }

        Seq
        seq() const
        {
            return seq_;
        }

        NetClock::time_point
        signTime() const
        {
            return signTime_;
        }

        NetClock::time_point
        seenTime() const
        {
            return seenTime_;
        }

        std::string
        key() const
        {
            return key_;
        }

        std::uint32_t
        nodeID() const
        {
            return nodeID_;
        }

        bool
        trusted() const
        {
            return trusted_;
        }

        boost::optional<std::uint32_t>
        loadFee() const
        {
            return loadFee_;
        }

        Validation const&
        unwrap() const
        {
            return *this;
        }

        auto
        asTie() const
        {
            return std::tie(
                ledgerID_,
                seq_,
                signTime_,
                seenTime_,
                key_,
                nodeID_,
                trusted_,
                loadFee_);
        }
        bool
        operator==(Validation const& o) const
        {
            return asTie() == o.asTie();
        }

        bool
        operator<(Validation const& o) const
        {
            return asTie() < o.asTie();
        }
    };

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
        std::size_t nodeID_;
        bool trusted_ = true;
        std::size_t signIdx_ = 0;
        boost::optional<std::uint32_t> loadFee_;

    public:
        Node(std::uint32_t nodeID, clock_type const& c) : c_(c), nodeID_(nodeID)
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

        std::size_t
        nodeID() const
        {
            return nodeID_;
        }

        void
        advanceKey()
        {
            signIdx_++;
        }

        std::string
        masterKey() const
        {
            return std::to_string(nodeID_);
        }

        std::string
        currKey() const
        {
            return masterKey() + "_" + std::to_string(signIdx_);
        }

        NetClock::time_point
        now() const
        {
            return toNetClock(c_);
        }

        // Issue a new validation with given sequence number and id and
        // with signing and seen times offset from the common clock
        Validation
        validation(
            Seq seq,
            ID i,
            NetClock::duration signOffset,
            NetClock::duration seenOffset) const
        {
            Validation v;
            v.seq_ = seq;
            v.ledgerID_ = i;

            v.signTime_ = now() + signOffset;
            v.seenTime_ = now() + seenOffset;

            v.nodeID_ = nodeID_;
            v.key_ = currKey();
            v.trusted_ = trusted_;
            v.loadFee_ = loadFee_;
            return v;
        }

        // Issue a new validation with the given sequence number and id
        Validation
        validation(Seq seq, ID i) const
        {
            return validation(
                seq, i, NetClock::duration{0}, NetClock::duration{0});
        }
    };

    // Non-locking mutex to avoid the need for testing generic Validations
    struct DummyMutex
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

    // Saved StaleData for inspection in test
    struct StaleData
    {
        std::vector<Validation> stale;
        hash_map<std::string, Validation> flushed;
    };

    // Generic Validations policy that saves stale/flushed data into
    // a StaleData instance.
    class StalePolicy
    {
        StaleData& staleData_;
        clock_type& c_;

    public:
        StalePolicy(StaleData& sd, clock_type& c)
            : staleData_{sd}, c_{c}
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
        flush(hash_map<std::string, Validation>&& remaining)
        {
            staleData_.flushed = std::move(remaining);
        }
    };

    // Specialize generic Validations using the above types
    using TestValidations =
        Validations<StalePolicy, Validation, DummyMutex>;

    // Hoist enum for writing simpler tests
    using AddOutcome = TestValidations::AddOutcome;

    // Gather the dependencies of TestValidations in a single class and provide
    // accessors for simplifying test logic
    class TestHarness
    {
        StaleData staleData_;
        ValidationParms p_;
        beast::manual_clock<std::chrono::steady_clock> clock_;
        beast::Journal j_;
        TestValidations tv_;
        int nextNodeId_ = 0;

    public:
        TestHarness() : tv_(p_, clock_, j_, staleData_, clock_)
        {
        }

        // Helper to add an existing validation
        AddOutcome
        add(Node const& n, Validation const& v)
        {
            return tv_.add(n.masterKey(), v);
        }

        // Helper to directly create the validation
        template <class... Ts>
        std::enable_if_t<(sizeof...(Ts) > 1), AddOutcome>
        add(Node const& n, Ts&&... ts)
        {
            return add(n, n.validation(std::forward<Ts>(ts)...));
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

        hash_map<std::string, Validation> const&
        flushed() const
        {
            return staleData_.flushed;
        }
    };

    void
    testAddValidation()
    {
        // Test adding current,stale,repeat,sameSeq validations
        using namespace std::chrono_literals;

        TestHarness harness;
        Node a = harness.makeNode();
        {
            {
                auto const v = a.validation(Seq{1}, ID{1});

                // Add a current validation
                BEAST_EXPECT(AddOutcome::current == harness.add(a, v));

                // Re-adding is repeat
                BEAST_EXPECT(AddOutcome::repeat == harness.add(a, v));
            }

            {
                harness.clock().advance(1s);
                // Replace with a new validation and ensure the old one is stale
                BEAST_EXPECT(harness.stale().empty());

                BEAST_EXPECT(
                    AddOutcome::current == harness.add(a, Seq{2}, ID{2}));

                BEAST_EXPECT(harness.stale().size() == 1);

                BEAST_EXPECT(harness.stale()[0].ledgerID() == 1);
            }

            {
                // Test the node changing signing key, then reissuing a ledger

                // Confirm old ledger on hand, but not new ledger
                BEAST_EXPECT(harness.vals().numTrustedForLedger(ID{2}) == 1);
                BEAST_EXPECT(harness.vals().numTrustedForLedger(ID{20}) == 0);

                // Issue a new signing key and re-issue the validation with a
                // new ID but the same sequence number
                a.advanceKey();

                // No validations following ID{2}
                BEAST_EXPECT(harness.vals().getNodesAfter(ID{2}) == 0);

                BEAST_EXPECT(
                    AddOutcome::sameSeq == harness.add(a, Seq{2}, ID{20}));

                // Old ID should be gone ...
                BEAST_EXPECT(harness.vals().numTrustedForLedger(ID{2}) == 0);
                BEAST_EXPECT(harness.vals().numTrustedForLedger(ID{20}) == 1);
                {
                    // Should be the only trusted for ID{20}
                    auto trustedVals =
                        harness.vals().getTrustedForLedger(ID{20});
                    BEAST_EXPECT(trustedVals.size() == 1);
                    BEAST_EXPECT(trustedVals[0].key() == a.currKey());
                    // ... and should be the only node after ID{2}
                    BEAST_EXPECT(harness.vals().getNodesAfter(ID{2}) == 1);

                }

                // A new key, but re-issue a validation with the same ID and
                // Sequence
                a.advanceKey();

                BEAST_EXPECT(
                    AddOutcome::sameSeq == harness.add(a, Seq{2}, ID{20}));
                {
                    // Still the only trusted validation for ID{20}
                    auto trustedVals =
                        harness.vals().getTrustedForLedger(ID{20});
                    BEAST_EXPECT(trustedVals.size() == 1);
                    BEAST_EXPECT(trustedVals[0].key() == a.currKey());
                    // and still follows ID{2} since it was a re-issue
                    BEAST_EXPECT(harness.vals().getNodesAfter(ID{2}) == 1);
                }
            }

            {
                // Processing validations out of order should ignore the older
                harness.clock().advance(2s);
                auto const val3 = a.validation(Seq{3}, ID{3});

                harness.clock().advance(4s);
                auto const val4 = a.validation(Seq{4}, ID{4});

                BEAST_EXPECT(AddOutcome::current == harness.add(a, val4));

                BEAST_EXPECT(AddOutcome::stale == harness.add(a, val3));

                // re-issued should not be added
                auto const val4reissue = a.validation(Seq{4}, ID{44});

                BEAST_EXPECT(AddOutcome::stale == harness.add(a, val4reissue));

            }
            {
                // Process validations out of order with shifted times

                // flush old validations
                harness.clock().advance(1h);

                // Establish a new current validation
                BEAST_EXPECT(
                    AddOutcome::current == harness.add(a, Seq{8}, ID{8}));

                // Process a validation that has "later" seq but early sign time
                BEAST_EXPECT(
                    AddOutcome::stale ==
                    harness.add(a, Seq{9}, ID{9}, -1s, -1s));

                // Process a validation that has an "earlier" seq but later sign time
                BEAST_EXPECT(
                    AddOutcome::current ==
                    harness.add(a, Seq{7}, ID{7}, 1s, 1s));
            }
            {
                // Test stale on arrival validations
                harness.clock().advance(1h);

                BEAST_EXPECT(
                    AddOutcome::stale ==
                    harness.add(
                        a,
                        Seq{15},
                        ID{15},
                        -harness.parms().validationCURRENT_EARLY,
                        0s));

                BEAST_EXPECT(
                    AddOutcome::stale ==
                    harness.add(
                        a,
                        Seq{15},
                        ID{15},
                        harness.parms().validationCURRENT_WALL,
                        0s));

                BEAST_EXPECT(
                    AddOutcome::stale ==
                    harness.add(
                        a,
                        Seq{15},
                        ID{15},
                        0s,
                        harness.parms().validationCURRENT_LOCAL));
            }
        }
    }

    void
    testOnStale()
    {
        // Verify validation becomes stale based solely on time passing
        TestHarness harness;
        Node a = harness.makeNode();

        BEAST_EXPECT(AddOutcome::current == harness.add(a, Seq{1}, ID{1}));
        harness.vals().currentTrusted();
        BEAST_EXPECT(harness.stale().empty());
        harness.clock().advance(harness.parms().validationCURRENT_LOCAL);

        // trigger iteration over current
        harness.vals().currentTrusted();

        BEAST_EXPECT(harness.stale().size() == 1);
        BEAST_EXPECT(harness.stale()[0].ledgerID() == 1);
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
            BEAST_EXPECT(
                AddOutcome::current == harness.add(node, Seq{1}, ID{1}));
        BEAST_EXPECT(AddOutcome::current == harness.add(d, Seq{1}, ID{10}));

        // Nothing past ledger 1 yet
        BEAST_EXPECT(harness.vals().getNodesAfter(ID{1}) == 0);

        harness.clock().advance(5s);

        // a and b have the same prior id, but b has a different current id
        // c is untrusted but on the same prior id
        // d has a different prior id
        BEAST_EXPECT(AddOutcome::current == harness.add(a, Seq{2}, ID{2}));
        BEAST_EXPECT(AddOutcome::current == harness.add(b, Seq{2}, ID{20}));
        BEAST_EXPECT(AddOutcome::current == harness.add(c, Seq{2}, ID{2}));
        BEAST_EXPECT(AddOutcome::current == harness.add(d, Seq{2}, ID{2}));

        BEAST_EXPECT(harness.vals().getNodesAfter(ID{1}) == 2);
    }

    void
    testCurrentTrusted()
    {
        // Test getting current trusted validations
        using namespace std::chrono_literals;

        TestHarness harness;
        Node a = harness.makeNode(), b = harness.makeNode();
        b.untrust();

        BEAST_EXPECT(AddOutcome::current == harness.add(a, Seq{1}, ID{1}));
        BEAST_EXPECT(AddOutcome::current == harness.add(b, Seq{1}, ID{3}));

        // Only a is trusted
        BEAST_EXPECT(harness.vals().currentTrusted().size() == 1);
        BEAST_EXPECT(harness.vals().currentTrusted()[0].ledgerID() == ID{1});
        BEAST_EXPECT(harness.vals().currentTrusted()[0].seq() == Seq{1});

        harness.clock().advance(3s);

        for (auto const& node : {a, b})
            BEAST_EXPECT(
                AddOutcome::current == harness.add(node, Seq{2}, ID{2}));

        // New validation for a
        BEAST_EXPECT(harness.vals().currentTrusted().size() == 1);
        BEAST_EXPECT(harness.vals().currentTrusted()[0].ledgerID() == ID{2});
        BEAST_EXPECT(harness.vals().currentTrusted()[0].seq() == Seq{2});

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
            BEAST_EXPECT(
                AddOutcome::current == harness.add(node, Seq{1}, ID{1}));

        {
            hash_set<std::string> const expectedKeys = {a.masterKey(),
                                                        b.masterKey()};
            BEAST_EXPECT(
                harness.vals().getCurrentPublicKeys() == expectedKeys);
        }

        harness.clock().advance(3s);

        // Change keys
        a.advanceKey();
        b.advanceKey();

        for (auto const& node : {a, b})
            BEAST_EXPECT(
                AddOutcome::current == harness.add(node, Seq{2}, ID{2}));

        {
            hash_set<std::string> const expectedKeys = {a.masterKey(),
                                                        b.masterKey()};
            BEAST_EXPECT(
                harness.vals().getCurrentPublicKeys() == expectedKeys);
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
            BEAST_EXPECT(
                AddOutcome::current == harness.add(node, Seq{1}, ID{1}));

        harness.clock().advance(1s);
        for (auto const& node : {baby, mama, goldilocks})
            BEAST_EXPECT(
                AddOutcome::current == harness.add(node, Seq{2}, ID{2}));

        harness.clock().advance(1s);
        BEAST_EXPECT(AddOutcome::current == harness.add(mama, Seq{3}, ID{3}));

        {
            // Allow slippage that treats all trusted as the current ledger
            auto res = harness.vals().currentTrustedDistribution(
                ID{2},    // Current ledger
                ID{1},    // Prior ledger
                Seq{0});  // No cutoff

            BEAST_EXPECT(res.size() == 1);
            BEAST_EXPECT(res[ID{2}] == 3);
        }

        {
            // Don't allow slippage back for prior ledger
            auto res = harness.vals().currentTrustedDistribution(
                ID{2},    // Current ledger
                ID{0},    // No prior ledger
                Seq{0});  // No cutoff

            BEAST_EXPECT(res.size() == 2);
            BEAST_EXPECT(res[ID{2}] == 2);
            BEAST_EXPECT(res[ID{1}] == 1);
        }

        {
            // Don't allow any slips
            auto res = harness.vals().currentTrustedDistribution(
                ID{0},    // No current ledger
                ID{0},    // No prior ledger
                Seq{0});  // No cutoff

            BEAST_EXPECT(res.size() == 3);
            BEAST_EXPECT(res[ID{1}] == 1);
            BEAST_EXPECT(res[ID{2}] == 1);
            BEAST_EXPECT(res[ID{3}] == 1);
        }

        {
            // Cutoff old sequence numbers
            auto res = harness.vals().currentTrustedDistribution(
                ID{2},    // current ledger
                ID{1},    // prior ledger
                Seq{2});  // Only sequence 2 or later
            BEAST_EXPECT(res.size() == 1);
            BEAST_EXPECT(res[ID{2}] == 2);
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

        hash_map<ID, std::vector<Validation>> trustedValidations;

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
        // Add a dummy ID to cover unknown ledger identifiers
        trustedValidations[ID{100}] = {};

        // first round a,b,c agree, d differs
        for (auto const& node : {a, b, c})
        {
            auto const val = node.validation(Seq{1}, ID{1});
            BEAST_EXPECT(AddOutcome::current == harness.add(node, val));
            if (val.trusted())
                trustedValidations[val.ledgerID()].emplace_back(val);
        }
        {
            auto const val = d.validation(Seq{1}, ID{11});
            BEAST_EXPECT(AddOutcome::current == harness.add(d, val));
            trustedValidations[val.ledgerID()].emplace_back(val);
        }

        harness.clock().advance(5s);
        // second round, a,b,c move to ledger 2, d now thinks ledger 1
        for (auto const& node : {a, b, c})
        {
            auto const val = node.validation(Seq{2}, ID{2});
            BEAST_EXPECT(AddOutcome::current == harness.add(node, val));
            if (val.trusted())
                trustedValidations[val.ledgerID()].emplace_back(val);
        }
        {
            auto const val = d.validation(Seq{2}, ID{1});
            BEAST_EXPECT(AddOutcome::current == harness.add(d, val));
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

        BEAST_EXPECT(AddOutcome::current == harness.add(a, Seq{1}, ID{1}));
        BEAST_EXPECT(harness.vals().numTrustedForLedger(ID{1}));
        harness.clock().advance(harness.parms().validationSET_EXPIRES);
        harness.vals().expire();
        BEAST_EXPECT(!harness.vals().numTrustedForLedger(ID{1}));
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

        hash_map<std::string, Validation> expected;
        Validation staleA;
        for (auto const& node : {a, b, c})
        {
            auto const val = node.validation(Seq{1}, ID{1});
            BEAST_EXPECT(AddOutcome::current == harness.add(node, val));
            if (node.nodeID() == a.nodeID())
            {
                staleA = val;
            }
            else
                expected[node.masterKey()] = val;
        }

        // Send in a new validation for a, saving the new one into the expected
        // map after setting the proper prior ledger ID it replaced
        harness.clock().advance(1s);
        auto newVal = a.validation(Seq{2}, ID{2});
        BEAST_EXPECT(AddOutcome::current == harness.add(a, newVal));
        expected[a.masterKey()] = newVal;

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
    }
};

BEAST_DEFINE_TESTSUITE(Validations, consensus, ripple);
}  // namespace test
}  // namespace ripple
