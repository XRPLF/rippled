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
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/PublicKey.h>
#include <ripple/protocol/SecretKey.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/beast/unit_test.h>

namespace ripple
{

class AmendmentTable_test final : public beast::unit_test::suite
{
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
    makeSection (std::vector<std::string> const& amendments)
    {
        Section section ("Test");
        for (auto const& a : amendments)
            section.append (to_string(amendmentId (a)) + " " + a);
        return section;
    }

    static
    Section
    makeSection (uint256 const& amendment)
    {
        Section section ("Test");
        section.append (to_string (amendment) + " " + to_string(amendment));
        return section;
    }

    std::vector<std::string> const m_set1;
    std::vector<std::string> const m_set2;
    std::vector<std::string> const m_set3;
    std::vector<std::string> const m_set4;

    Section const emptySection;

public:
    AmendmentTable_test ()
        : m_set1 (createSet (1, 12))
        , m_set2 (createSet (2, 12))
        , m_set3 (createSet (3, 12))
        , m_set4 (createSet (4, 12))
    {
    }

    std::unique_ptr<AmendmentTable>
    makeTable(
        int w,
        Section const supported,
        Section const enabled,
        Section const vetoed)
    {
        return make_AmendmentTable (
            weeks (w),
            majorityFraction,
            supported,
            enabled,
            vetoed,
            beast::Journal{});
    }

    std::unique_ptr<AmendmentTable>
    makeTable (int w)
    {
        return makeTable (
            w,
            makeSection (m_set1),
            makeSection (m_set2),
            makeSection (m_set3));
    };

    void testConstruct ()
    {
        testcase ("Construction");

        auto table = makeTable(1);

        for (auto const& a : m_set1)
        {
            BEAST_EXPECT(table->isSupported (amendmentId (a)));
            BEAST_EXPECT(!table->isEnabled (amendmentId (a)));
        }

        for (auto const& a : m_set2)
        {
            BEAST_EXPECT(table->isSupported (amendmentId (a)));
            BEAST_EXPECT(table->isEnabled (amendmentId (a)));
        }

        for (auto const& a : m_set3)
        {
            BEAST_EXPECT(!table->isSupported (amendmentId (a)));
            BEAST_EXPECT(!table->isEnabled (amendmentId (a)));
        }
    }

    void testGet ()
    {
        testcase ("Name to ID mapping");

        auto table = makeTable (1);

        for (auto const& a : m_set1)
            BEAST_EXPECT(table->find (a) == amendmentId (a));
        for (auto const& a : m_set2)
            BEAST_EXPECT(table->find (a) == amendmentId (a));

        for (auto const& a : m_set3)
            BEAST_EXPECT(!table->find (a));
        for (auto const& a : m_set4)
            BEAST_EXPECT(!table->find (a));
    }

    void testBadConfig ()
    {
        auto const section = makeSection (m_set1);
        auto const id = to_string (amendmentId (m_set2[0]));

        testcase ("Bad Config");

        { // Two arguments are required - we pass one
            Section test = section;
            test.append (id);

            try
            {
                if (makeTable (2, test, emptySection, emptySection))
                    fail ("Accepted only amendment ID");
            }
            catch (...)
            {
                pass();
            }
        }

        { // Two arguments are required - we pass three
            Section test = section;
            test.append (id + " Test Name");

            try
            {
                if (makeTable (2, test, emptySection, emptySection))
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
                if (makeTable (2, test, emptySection, emptySection))
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
                if (makeTable (2, test, emptySection, emptySection))
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
                if (makeTable (2, test, emptySection, emptySection))
                    fail ("Accepted non-hex amendment ID");
            }
            catch (...)
            {
                pass();
            }
        }
    }

    std::map<uint256, bool>
    getState (
        AmendmentTable *table,
        std::set<uint256> const& exclude)
    {
        std::map<uint256, bool> state;

        auto track = [&state,table](std::vector<std::string> const& v)
        {
            for (auto const& a : v)
            {
                auto const id = amendmentId(a);
                state[id] = table->isEnabled (id);
            }
        };

        track (m_set1);
        track (m_set2);
        track (m_set3);
        track (m_set4);

        for (auto const& a : exclude)
            state.erase(a);

        return state;
    }

    void testEnableDisable ()
    {
        testcase ("enable & disable");

        auto const testAmendment = amendmentId("TestAmendment");
        auto table = makeTable (2);

        // Subset of amendments to enable
        std::set<uint256> enabled;
        enabled.insert (testAmendment);
        enabled.insert (amendmentId(m_set1[0]));
        enabled.insert (amendmentId(m_set2[0]));
        enabled.insert (amendmentId(m_set3[0]));
        enabled.insert (amendmentId(m_set4[0]));

        // Get the state before, excluding the items we'll change:
        auto const pre_state = getState (table.get(), enabled);

        // Enable the subset and verify
        for (auto const& a : enabled)
            table->enable (a);

        for (auto const& a : enabled)
            BEAST_EXPECT(table->isEnabled (a));

        // Disable the subset and verify
        for (auto const& a : enabled)
            table->disable (a);

        for (auto const& a : enabled)
            BEAST_EXPECT(!table->isEnabled (a));

        // Get the state after, excluding the items we changed:
        auto const post_state = getState (table.get(), enabled);

        // Ensure the states are identical
        auto ret = std::mismatch(
            pre_state.begin(), pre_state.end(),
            post_state.begin(), post_state.end());

        BEAST_EXPECT(ret.first == pre_state.end());
        BEAST_EXPECT(ret.second == post_state.end());
    }

    std::vector <PublicKey> makeValidators (int num)
    {
        std::vector <PublicKey> ret;
        ret.reserve (num);
        for (int i = 0; i < num; ++i)
        {
            ret.push_back (
                randomKeyPair(KeyType::secp256k1).first);
        }
        return ret;
    }

    static NetClock::time_point weekTime (weeks w)
    {
        return NetClock::time_point{w};
    }

    // Execute a pretend consensus round for a flag ledger
    void doRound(
        AmendmentTable& table,
        weeks week,
        std::vector <PublicKey> const& validators,
        std::vector <std::pair <uint256, int>> const& votes,
        std::vector <uint256>& ourVotes,
        std::set <uint256>& enabled,
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

        auto const roundTime = weekTime (week);

        // Build validations
        std::vector<STValidation::pointer> validations;
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
            validations.emplace_back(v);
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
                        Throw<std::runtime_error> ("enabling already enabled");
                    if (majority.find (hash) == majority.end ())
                        Throw<std::runtime_error> ("enabling without majority");
                    enabled.insert (hash);
                    majority.erase (hash);
                    break;

                case tfGotMajority:
                    if (majority.find (hash) != majority.end ())
                        Throw<std::runtime_error> ("got majority while having majority");
                    majority[hash] = roundTime;
                    break;

                case tfLostMajority:
                    if (majority.find (hash) == majority.end ())
                        Throw<std::runtime_error> ("lost majority without majority");
                    majority.erase (hash);
                    break;

                default:
                    Throw<std::runtime_error> ("unknown action");
            }
        }
    }

    // No vote on unknown amendment
    void testNoOnUnknown ()
    {
        testcase ("Vote NO on unknown");

        auto const testAmendment = amendmentId("TestAmendment");
        auto const validators = makeValidators (10);

        auto table = makeTable (2,
            emptySection,
            emptySection,
            emptySection);

        std::vector <std::pair <uint256, int>> votes;
        std::vector <uint256> ourVotes;
        std::set <uint256> enabled;
        majorityAmendments_t majority;

        doRound (*table, weeks{1},
            validators,
            votes,
            ourVotes,
            enabled,
            majority);
        BEAST_EXPECT(ourVotes.empty());
        BEAST_EXPECT(enabled.empty());
        BEAST_EXPECT(majority.empty());

        votes.emplace_back (testAmendment, 256);

        doRound (*table, weeks{2},
                validators,
                votes,
                ourVotes,
                enabled,
                majority);
        BEAST_EXPECT(ourVotes.empty());
        BEAST_EXPECT(enabled.empty());

        majority[testAmendment] = weekTime(weeks{1});

        // Note that the simulation code assumes others behave as we do,
        // so the amendment won't get enabled
        doRound (*table, weeks{5},
                validators,
                votes,
                ourVotes,
                enabled,
                majority);
        BEAST_EXPECT(ourVotes.empty());
        BEAST_EXPECT(enabled.empty());
    }

    // No vote on vetoed amendment
    void testNoOnVetoed ()
    {
        testcase ("Vote NO on vetoed");

        auto const testAmendment = amendmentId ("vetoedAmendment");

        auto table = makeTable (2,
            emptySection,
            emptySection,
            makeSection (testAmendment));

        auto const validators = makeValidators (10);

        std::vector <std::pair <uint256, int>> votes;
        std::vector <uint256> ourVotes;
        std::set <uint256> enabled;
        majorityAmendments_t majority;

        doRound (*table, weeks{1},
            validators,
            votes,
            ourVotes,
            enabled,
            majority);
        BEAST_EXPECT(ourVotes.empty());
        BEAST_EXPECT(enabled.empty());
        BEAST_EXPECT(majority.empty());

        votes.emplace_back (testAmendment, 256);

        doRound (*table, weeks{2},
                validators,
                votes,
                ourVotes,
                enabled,
                majority);
        BEAST_EXPECT(ourVotes.empty());
        BEAST_EXPECT(enabled.empty());

        majority[testAmendment] = weekTime(weeks{1});

        doRound (*table, weeks{5},
                validators,
                votes,
                ourVotes,
                enabled,
                majority);
        BEAST_EXPECT(ourVotes.empty());
        BEAST_EXPECT(enabled.empty());
    }

    // Vote on and enable known, not-enabled amendment
    void testVoteEnable ()
    {
        testcase ("voteEnable");

        auto table = makeTable (
            2,
            makeSection (m_set1),
            emptySection,
            emptySection);

        auto const validators = makeValidators (10);
        std::vector <std::pair <uint256, int>> votes;
        std::vector <uint256> ourVotes;
        std::set <uint256> enabled;
        majorityAmendments_t majority;

        // Week 1: We should vote for all known amendments not enabled
        doRound (*table, weeks{1},
            validators,
            votes,
            ourVotes,
            enabled,
            majority);
        BEAST_EXPECT(ourVotes.size() == m_set1.size());
        BEAST_EXPECT(enabled.empty());
        for (auto const& i : m_set1)
            BEAST_EXPECT(majority.find(amendmentId (i)) == majority.end());

        // Now, everyone votes for this feature
        for (auto const& i : m_set1)
            votes.emplace_back (amendmentId(i), 256);

        // Week 2: We should recognize a majority
        doRound (*table, weeks{2},
                validators,
                votes,
                ourVotes,
                enabled,
                majority);
        BEAST_EXPECT(ourVotes.size() == m_set1.size());
        BEAST_EXPECT(enabled.empty());

        for (auto const& i : m_set1)
            BEAST_EXPECT(majority[amendmentId (i)] == weekTime(weeks{2}));

        // Week 5: We should enable the amendment
        doRound (*table, weeks{5},
                validators,
                votes,
                ourVotes,
                enabled,
                majority);
        BEAST_EXPECT(enabled.size() == m_set1.size());

        // Week 6: We should remove it from our votes and from having a majority
        doRound (*table, weeks{6},
                validators,
                votes,
                ourVotes,
                enabled,
                majority);
        BEAST_EXPECT(enabled.size() == m_set1.size());
        BEAST_EXPECT(ourVotes.empty());
        for (auto const& i : m_set1)
            BEAST_EXPECT(majority.find(amendmentId (i)) == majority.end());
    }

    // Detect majority at 80%, enable later
    void testDetectMajority ()
    {
        testcase ("detectMajority");

        auto const testAmendment = amendmentId ("detectMajority");
        auto table = makeTable (
            2,
            makeSection (testAmendment),
            emptySection,
            emptySection);

        auto const validators = makeValidators (16);

        std::set <uint256> enabled;
        majorityAmendments_t majority;

        for (int i = 0; i <= 17; ++i)
        {
            std::vector <std::pair <uint256, int>> votes;
            std::vector <uint256> ourVotes;

            if ((i > 0) && (i < 17))
                votes.emplace_back (testAmendment, i * 16);

            doRound (*table, weeks{i},
                validators, votes, ourVotes, enabled, majority);

            if (i < 13)
            {
                // We are voting yes, not enabled, no majority
                BEAST_EXPECT(!ourVotes.empty());
                BEAST_EXPECT(enabled.empty());
                BEAST_EXPECT(majority.empty());
            }
            else if (i < 15)
            {
                // We have a majority, not enabled, keep voting
                BEAST_EXPECT(!ourVotes.empty());
                BEAST_EXPECT(!majority.empty());
                BEAST_EXPECT(enabled.empty());
            }
            else if (i == 15)
            {
                // enable, keep voting, remove from majority
                BEAST_EXPECT(!ourVotes.empty());
                BEAST_EXPECT(majority.empty());
                BEAST_EXPECT(!enabled.empty());
            }
            else
            {
                // Done, we should be enabled and not voting
                BEAST_EXPECT(ourVotes.empty());
                BEAST_EXPECT(majority.empty());
                BEAST_EXPECT(!enabled.empty());
            }
        }
    }

    // Detect loss of majority
    void testLostMajority ()
    {
        testcase ("lostMajority");

        auto const testAmendment = amendmentId ("lostMajority");
        auto const validators = makeValidators (16);

        auto table = makeTable (
            8,
            makeSection (testAmendment),
            emptySection,
            emptySection);

        std::set <uint256> enabled;
        majorityAmendments_t majority;

        {
            // establish majority
            std::vector <std::pair <uint256, int>> votes;
            std::vector <uint256> ourVotes;

            votes.emplace_back (testAmendment, 250);

            doRound (*table, weeks{1},
                validators, votes, ourVotes, enabled, majority);

            BEAST_EXPECT(enabled.empty());
            BEAST_EXPECT(!majority.empty());
        }

        for (int i = 1; i < 16; ++i)
        {
            std::vector <std::pair <uint256, int>> votes;
            std::vector <uint256> ourVotes;

            // Gradually reduce support
            votes.emplace_back (testAmendment, 256 - i * 8);

            doRound (*table, weeks{i + 1},
                validators, votes, ourVotes, enabled, majority);

            if (i < 8)
            {
                // We are voting yes, not enabled, majority
                BEAST_EXPECT(!ourVotes.empty());
                BEAST_EXPECT(enabled.empty());
                BEAST_EXPECT(!majority.empty());
            }
            else
            {
                // No majority, not enabled, keep voting
                BEAST_EXPECT(!ourVotes.empty());
                BEAST_EXPECT(majority.empty());
                BEAST_EXPECT(enabled.empty());
            }
        }
    }

    void
    testSupportedAmendments ()
    {
        for (auto const& amend : detail::supportedAmendments ())
        {
            auto const f = getRegisteredFeature(amend.substr (65));
            BEAST_EXPECT(f && amend.substr (0, 64) == to_string (*f));
        }
    }

    void testHasUnsupported ()
    {
        testcase ("hasUnsupportedEnabled");

        auto table = makeTable(1);
        BEAST_EXPECT(! table->hasUnsupportedEnabled());

        std::set <uint256> enabled;
        std::for_each(m_set4.begin(), m_set4.end(),
            [&enabled](auto const &s){ enabled.insert(amendmentId(s)); });
        table->doValidatedLedger(1, enabled);
        BEAST_EXPECT(table->hasUnsupportedEnabled());
    }

    void run ()
    {
        testConstruct();
        testGet ();
        testBadConfig ();
        testEnableDisable ();
        testNoOnUnknown ();
        testNoOnVetoed ();
        testVoteEnable ();
        testDetectMajority ();
        testLostMajority ();
        testSupportedAmendments ();
        testHasUnsupported ();
    }
};

BEAST_DEFINE_TESTSUITE (AmendmentTable, app, ripple);

}  // ripple
