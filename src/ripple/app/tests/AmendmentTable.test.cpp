//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <ripple/app/misc/AmendmentTable.h>
#include <ripple/basics/BasicConfig.h>
#include <ripple/basics/chrono.h>
#include <ripple/basics/Log.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/TxFlags.h>
#include <beast/unit_test/suite.h>

namespace ripple
{

class AmendmentTable_test final : public beast::unit_test::suite
{
public:
    using StringPairVec = std::vector<std::pair<std::string, std::string>>;

private:
    // 204/256 about 80% (we round down because the implementation rounds up)
    static int const majorityFraction{204};

    static
    uint256
    amendmentId (std::string in)
    {
        sha256_hasher h;
        using beast::hash_append;
        hash_append(h, in);
        auto const d = static_cast<sha256_hasher::result_type>(h);
        uint256 result;
        std::memcpy(result.data(), d.data(), d.size());
        return result;
    }

    static
    std::unique_ptr<AmendmentTable>
    makeTable (int w)
    {
        return make_AmendmentTable (
            weeks (w),
            majorityFraction,
            beast::Journal{});
    };

    static
    std::vector<std::string>
    createSet (int group, int count)
    {
        std::vector<std::string> amendments;
        for (int i = 0; i < count; i++)
            amendments.push_back (
                "Amendment" + std::to_string ((1000000 * group) + i));
        return amendments;
    }

    static
    Section
    makeSection (std::vector<std::string> amendments)
    {
        Section section ("Test");
        for (auto const& a : amendments)
            section.append (to_string(amendmentId (a)) + " " + a);
        return section;
    }

    // Create the amendments by string pairs instead of AmendmentNames
    // as this helps test the AmendmentNames class
    std::vector<std::string> const m_amendmentSet1;
    std::vector<std::string> const m_amendmentSet2;
    std::vector<std::string> const m_amendmentSet3;

public:
    AmendmentTable_test ()
        : m_amendmentSet1 (createSet (1, 6))
        , m_amendmentSet2 (createSet (2, 6))
        , m_amendmentSet3 (createSet (3, 6))
    {
    }

    void testGet ()
    {
        testcase ("get");
        auto table = makeTable (2);
        for (auto const& a : m_amendmentSet1)
            table->addKnown ({ amendmentId (a), a });
        for (auto const& a : m_amendmentSet1)
            expect (table->get (a) == amendmentId (a));
        for (auto const& a : m_amendmentSet2)
            expect (table->get (a) == beast::zero);
    }

    void testAddKnown ()
    {
        testcase ("addKnown");
        auto table = makeTable (2);
        for (auto const& a : m_amendmentSet1)
            table->addKnown ({ amendmentId (a), a });
        for (auto const& a : m_amendmentSet1)
        {
            auto const id = amendmentId (a);
            expect (table->isSupported (id));
            expect (!table->isEnabled (id));
        }
        for (auto const& a : m_amendmentSet2)
        {
            auto const id = amendmentId (a);
            expect (!table->isSupported (id));
            expect (!table->isEnabled (id));
        }
    }

    void testAddInitial ()
    {
        testcase ("addInitial");

        auto table = makeTable (2);
        table->addInitial (makeSection (m_amendmentSet1));
        for (auto const& a : m_amendmentSet1)
        {
            auto const id = amendmentId (a);
            expect (table->isSupported (id));
            expect (table->isEnabled (id));
        }
        for (auto const& a : m_amendmentSet2)
        {
            auto const id = amendmentId (a);
            expect (!table->isSupported (id));
            expect (!table->isEnabled (id));
        }
    }

    void testAddKnownAddInitial ()
    {
        testcase ("addKnown");
        auto table = makeTable (2);
        for (auto const& a : m_amendmentSet1)
            table->addKnown ({ amendmentId (a), a });
        table->addInitial (makeSection (m_amendmentSet2));
        for (auto const& a : m_amendmentSet1)
        {
            auto const id = amendmentId (a);
            expect (table->isSupported (id));
            expect (!table->isEnabled (id));
        }
        for (auto const& a : m_amendmentSet2)
        {
            auto const id = amendmentId (a);
            expect (table->isSupported (id));
            expect (table->isEnabled (id));
        }
        for (auto const& a : m_amendmentSet3)
        {
            auto const id = amendmentId (a);
            expect (!table->isSupported (id));
            expect (!table->isEnabled (id));
        }
    }

    void testBadConfig ()
    {
        testcase ("Bad Config");

        auto const section = makeSection (m_amendmentSet1);
        auto const id = to_string (amendmentId (m_amendmentSet2[0]));

        { // Two arguments are required - we pass one
            Section test = section;
            test.append (id);

            try
            {
                auto table = makeTable (2);
                table->addInitial (test);
                fail ("Accepted only amendment ID");
            }
            catch (...)
            {
                pass();
            }
        }

        { // Two arguments are required - we pass three
            Section test = section;
            test.append (id + "Test Name");

            try
            {
                auto table = makeTable (2);
                table->addInitial (test);
                fail ("Accepted extra arguments");
            }
            catch (...)
            {
                pass();
            }
        }

        {
            auto sid = id;
            sid.resize (sid.length() - 1);

            Section test = section;
            test.append (sid + " Name");

            try
            {
                auto table = makeTable (2);
                table->addInitial (test);
                fail ("Accepted short amendment ID");
            }
            catch (...)
            {
                pass();
            }
        }

        {
            auto sid = id;
            sid.resize (sid.length() + 1, '0');

            Section test = section;
            test.append (sid + " Name");

            try
            {
                auto table = makeTable (2);
                table->addInitial (test);
                fail ("Accepted long amendment ID");
            }
            catch (...)
            {
                pass();
            }
        }

        {
            auto sid = id;
            sid.resize (sid.length() - 1);
            sid.push_back ('Q');

            Section test = section;
            test.append (sid + " Name");

            try
            {
                auto table = makeTable (2);
                table->addInitial (test);
                fail ("Accepted non-hex amendment ID");
            }
            catch (...)
            {
                pass();
            }
        }
    }

    void testEnable ()
    {
        testcase ("enable & disable");

        auto table = makeTable (2);

        for (auto const& a : m_amendmentSet1)
            table->addKnown ({ amendmentId (a), a });
        for (auto const& a : m_amendmentSet2)
            table->addKnown ({ amendmentId (a), a });
        for (auto const& a : m_amendmentSet3)
            table->addKnown ({ amendmentId (a), a });

        // Enable some amendments, verify they are enabled
        std::set<uint256> enabled;
        enabled.insert (amendmentId(m_amendmentSet1[0]));
        enabled.insert (amendmentId(m_amendmentSet2[0]));
        enabled.insert (amendmentId(m_amendmentSet3[0]));

        for (auto const& a : enabled)
            table->enable (a);

        for (auto const& a : m_amendmentSet1)
        {
            auto const id = amendmentId (a);
            expect (table->isEnabled (id) == (enabled.count (id) != 0));
        }
        for (auto const& a : m_amendmentSet2)
        {
            auto const id = amendmentId (a);
            expect (table->isEnabled (id) == (enabled.count (id) != 0));
        }
        for (auto const& a : m_amendmentSet3)
        {
            auto const id = amendmentId (a);
            expect (table->isEnabled (id) == (enabled.count (id) != 0));
        }

        // Disable everything we enabled
        for (auto const& a : enabled)
            table->disable (a);

        // Verify they're disabled
        for (auto const& a : m_amendmentSet1)
            expect (!table->isEnabled (amendmentId (a)));
        for (auto const& a : m_amendmentSet2)
            expect (!table->isEnabled (amendmentId (a)));
        for (auto const& a : m_amendmentSet2)
            expect (!table->isEnabled (amendmentId (a)));
    }

    std::vector <RippleAddress> makeValidators (int num)
    {
        std::vector <RippleAddress> ret;
        ret.reserve (num);
        for (int i = 0; i < num; ++i)
            ret.push_back (RippleAddress::createNodePublic (
                RippleAddress::createSeedRandom ()));
        return ret;
    }

    static std::uint32_t weekTime (int w)
    {
        return w * (7*24*60*60);
    }

    // Execute a pretend consensus round for a flag ledger
    void doRound(
        AmendmentTable& table,
        int week,
        std::vector <RippleAddress> const& validators,
        std::vector <std::pair <uint256, int> > const& votes,
        std::vector <uint256>& ourVotes,
        enabledAmendments_t& enabled,
        majorityAmendments_t& majority)
    {
        // Do a round at the specified time
        // Returns the amendments we voted for

        // Parameters:
        // table:      Our table of known and vetoed amendments
        // validators: The addreses of validators we trust
        // votes:      Amendments and the number of validators who vote for them
        // ourVotes:   The amendments we vote for in our validation
        // enabled:    In/out enabled amendments
        // majority:   In/our majority amendments (and when they got a majority)

        std::uint32_t const roundTime = weekTime (week);

        // Build validations
        ValidationSet validations;
        validations.reserve (validators.size ());

        int i = 0;
        for (auto const& val : validators)
        {
            auto v = std::make_shared <STValidation> (
                uint256(), roundTime, val, true);

            ++i;
            STVector256 field (sfAmendments);

            for (auto const& amendment : votes)
            {
                if ((256 * i) < (validators.size() * amendment.second))
                {
                    // We vote yes on this amendment
                    field.push_back (amendment.first);
                }
            }
            if (!field.empty ())
                v->setFieldV256 (sfAmendments, field);

            v->setTrusted();
            validations [val.getNodeID()] = v;
        }

        ourVotes = table.doValidation (enabled);

        auto actions = table.doVoting (
            roundTime, enabled, majority, validations);
        for (auto const& action : actions)
        {
            // This code assumes other validators do as we do

            auto const& hash = action.first;
            switch (action.second)
            {
                case 0:
                // amendment goes from majority to enabled
                    if (enabled.find (hash) != enabled.end ())
                        throw std::runtime_error ("enabling already enabled");
                    if (majority.find (hash) == majority.end ())
                        throw std::runtime_error ("enabling without majority");
                    enabled.insert (hash);
                    majority.erase (hash);
                    break;

                case tfGotMajority:
                    if (majority.find (hash) != majority.end ())
                        throw std::runtime_error ("got majority while having majority");
                    majority[hash] = roundTime;
                    break;

                case tfLostMajority:
                    if (majority.find (hash) == majority.end ())
                        throw std::runtime_error ("lost majority without majority");
                    majority.erase (hash);
                    break;

                default:
                    throw std::runtime_error ("unknown action");
            }
        }
    }

    // No vote on unknown amendment
    void testNoOnUnknown ()
    {
        testcase ("Vote NO on unknown");

        auto table (makeTable (2));
        auto const validators = makeValidators (10);
        auto const testAmendment = amendmentId (m_amendmentSet1[0]);

        std::vector <std::pair <uint256, int>> votes;
        std::vector <uint256> ourVotes;
        enabledAmendments_t enabled;
        majorityAmendments_t majority;

        doRound (*table, 1,
            validators,
            votes,
            ourVotes,
            enabled,
            majority);
        expect (ourVotes.empty(), "Voted with nothing to vote on");
        expect (enabled.empty(), "Enabled amendment for no reason");
        expect (majority.empty(), "Majority found for no reason");

        votes.emplace_back (testAmendment, 256);

        doRound (*table, 2,
                validators,
                votes,
                ourVotes,
                enabled,
                majority);
        expect (ourVotes.empty(), "Voted on unknown because others did");
        expect (enabled.empty(), "Enabled amendment for no reason");

        majority[testAmendment] = weekTime(1);

        // Note that the simulation code assumes others behave as we do,
        // so the amendment won't get enabled
        doRound (*table, 5,
                validators,
                votes,
                ourVotes,
                enabled,
                majority);
        expect (ourVotes.empty(), "Voted on unknown because it had majority");
        expect (enabled.empty(), "Pseudo-transaction from nowhere");
    }

    // No vote on vetoed amendment
    void testNoOnVetoed ()
    {
        testcase ("Vote NO on vetoed");

        auto table (makeTable (2));
        auto const validators = makeValidators (10);
        auto const testAmendment = amendmentId (m_amendmentSet1[0]);
        table->veto(testAmendment);

        std::vector <std::pair <uint256, int>> votes;
        std::vector <uint256> ourVotes;
        enabledAmendments_t enabled;
        majorityAmendments_t majority;

        doRound (*table, 1,
            validators,
            votes,
            ourVotes,
            enabled,
            majority);
        expect (ourVotes.empty(), "Voted with nothing to vote on");
        expect (enabled.empty(), "Enabled amendment for no reason");
        expect (majority.empty(), "Majority found for no reason");

        votes.emplace_back (testAmendment, 256);

        doRound (*table, 2,
                validators,
                votes,
                ourVotes,
                enabled,
                majority);
        expect (ourVotes.empty(), "Voted on vetoed amendment because others did");
        expect (enabled.empty(), "Enabled amendment for no reason");

        majority[testAmendment] = weekTime(1);

        doRound (*table, 5,
                validators,
                votes,
                ourVotes,
                enabled,
                majority);
        expect (ourVotes.empty(), "Voted on vetoed because it had majority");
        expect (enabled.empty(), "Enabled amendment for no reason");
    }

    // Vote on and enable known, not-enabled amendment
    void testVoteEnable ()
    {
        testcase ("voteEnable");

        auto table (makeTable (2));
        for (auto const& a : m_amendmentSet1)
            table->addKnown ({ amendmentId (a), a });

        auto const validators = makeValidators (10);
        std::vector <std::pair <uint256, int>> votes;
        std::vector <uint256> ourVotes;
        enabledAmendments_t enabled;
        majorityAmendments_t majority;

        // Week 1: We should vote for all known amendments not enabled
        doRound (*table, 1,
            validators,
            votes,
            ourVotes,
            enabled,
            majority);
        expect (ourVotes.size() == m_amendmentSet1.size(), "Did not vote");
        expect (enabled.empty(), "Enabled amendment for no reason");
        for (auto const& i : m_amendmentSet1)
            expect(majority.find(amendmentId (i)) == majority.end(),
                "majority detected for no reason");

        // Now, everyone votes for this feature
        for (auto const& i : m_amendmentSet1)
            votes.emplace_back (amendmentId(i), 256);

        // Week 2: We should recognize a majority
        doRound (*table, 2,
                validators,
                votes,
                ourVotes,
                enabled,
                majority);
        expect (ourVotes.size() == m_amendmentSet1.size(), "Did not vote");
        expect (enabled.empty(), "Enabled amendment for no reason");
        for (auto const& i : m_amendmentSet1)
            expect (majority[amendmentId (i)] == weekTime(2),
                "majority not detected");

        // Week 5: We should enable the amendment
        doRound (*table, 5,
                validators,
                votes,
                ourVotes,
                enabled,
                majority);
        expect (enabled.size() == m_amendmentSet1.size(), "Did not enable");

        // Week 6: We should remove it from our votes and from having a majority
        doRound (*table, 6,
                validators,
                votes,
                ourVotes,
                enabled,
                majority);
        expect (enabled.size() == m_amendmentSet1.size(), "Disabled");
        expect (ourVotes.empty(), "Voted after enabling");
        for (auto const& i : m_amendmentSet1)
            expect(majority.find(amendmentId (i)) == majority.end(),
                "majority not removed");
    }

    // Detect majority at 80%, enable later
    void testDetectMajority ()
    {
        testcase ("detectMajority");
        auto table (makeTable (2));

        auto const testAmendment = amendmentId (m_amendmentSet1[0]);
        table->addKnown({ testAmendment, m_amendmentSet1[0] });

        auto const validators = makeValidators (16);

        enabledAmendments_t enabled;
        majorityAmendments_t majority;

        for (int i = 0; i <= 16; ++i)
        {
            std::vector <std::pair <uint256, int>> votes;
            std::vector <uint256> ourVotes;

            if ((i > 0) && (i < 16))
                votes.emplace_back (testAmendment, i * 16);

            doRound (*table, i,
                validators, votes, ourVotes, enabled, majority);

            if (i < 13)
            {
                // We are voting yes, not enabled, no majority
                expect (!ourVotes.empty(), "We aren't voting");
                expect (enabled.empty(), "Enabled too early");
                expect (majority.empty(), "Majority too early");
                for (auto const& m : majority)
                    log << "{" << m.first << ", " << m.second;
            }
            else if (i < 15)
            {
                // We have a majority, not enabled, keep voting
                expect (!ourVotes.empty(), "We stopped voting");
                expect (!majority.empty(), "Failed to detect majority");
                expect (enabled.empty(), "Enabled too early");
            }
            else if (i == 15)
            {
                // enable, keep voting, remove from majority
                expect (!ourVotes.empty(), "We stopped voting");
                expect (majority.empty(), "Failed to remove from majority");
                expect (!enabled.empty(), "Did not enable");
            }
            else
            {
                // Done, we should be enabled and not voting
                expect (ourVotes.empty(), "We did not stop voting");
                expect (majority.empty(), "Failed to revove from majority");
                expect (!enabled.empty(), "Did not enable");
            }
        }
    }

    // Detect loss of majority
    void testLostMajority ()
    {
        testcase ("lostMajority");

        auto table (makeTable (8));

        auto const testAmendment = amendmentId (m_amendmentSet1[0]);
        table->addKnown({ testAmendment, m_amendmentSet1[0] });

        auto const validators = makeValidators (16);

        enabledAmendments_t enabled;
        majorityAmendments_t majority;

        {
            // establish majority
            std::vector <std::pair <uint256, int>> votes;
            std::vector <uint256> ourVotes;

            votes.emplace_back (testAmendment, 250);

            doRound (*table, 1,
                validators, votes, ourVotes, enabled, majority);

            expect (enabled.empty(), "Enabled for no reason");
            expect (!majority.empty(), "Failed to detect majority");
        }

        for (int i = 1; i < 16; ++i)
        {
            std::vector <std::pair <uint256, int>> votes;
            std::vector <uint256> ourVotes;

            // Gradually reduce support
            votes.emplace_back (testAmendment, 256 - i * 8);

            doRound (*table, i + 1,
                validators, votes, ourVotes, enabled, majority);

            if (i < 8)
            {
                // We are voting yes, not enabled, majority
                expect (!ourVotes.empty(), "We aren't voting");
                expect (enabled.empty(), "Enabled for no reason");
                expect (!majority.empty(), "Lost majority too early");
            }
            else
            {
                // No majority, not enabled, keep voting
                expect (!ourVotes.empty(), "We stopped voting");
                expect (majority.empty(), "Failed to detect loss of majority");
                expect (enabled.empty(), "Enabled errneously");
            }
        }
    }

    void run ()
    {
        testGet ();
        testAddInitial ();
        testAddKnown ();
        testAddKnownAddInitial ();
        testEnable ();
        testNoOnUnknown ();
        testNoOnVetoed ();
        testVoteEnable ();
        testDetectMajority ();
        testLostMajority ();
    }
};

BEAST_DEFINE_TESTSUITE (AmendmentTable, app, ripple);

}  // ripple
