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
#include <ripple/basics/contract.h>
#include <ripple/basics/Log.h>
#include <ripple/core/ConfigSections.h>
#include <ripple/protocol/TxFlags.h>
#include <beast/unit_test/suite.h>

namespace ripple
{

class AmendmentTable_test final : public beast::unit_test::suite
{
public:
    using StringPairVec = std::vector<std::pair<std::string, std::string>>;

private:
    enum class TablePopulationAlgo
    {
        addInitial,
        addKnown
    };

    // 204/256 about 80% (we round down because the implementation rounds up)
    static int const majorityFraction{204};

    static void populateTable (AmendmentTable& table,
                               std::vector<std::string> const& configLines)
    {
        Section section (SECTION_AMENDMENTS);
        section.append (configLines);
        table.addInitial (section);
    }

    static std::vector<AmendmentName> getAmendmentNames (
        StringPairVec const& amendmentPairs)
    {
        std::vector<AmendmentName> amendmentNames;
        amendmentNames.reserve (amendmentPairs.size ());
        for (auto const& i : amendmentPairs)
        {
            amendmentNames.emplace_back (i.first, i.second);
        }
        return amendmentNames;
    }

    std::vector<AmendmentName> populateTable (
        AmendmentTable& table,
        StringPairVec const& amendmentPairs,
        TablePopulationAlgo populationAlgo = TablePopulationAlgo::addKnown)
    {
        std::vector<AmendmentName> const amendmentNames (
            getAmendmentNames (amendmentPairs));
        switch (populationAlgo)
        {
            case TablePopulationAlgo::addKnown:
                for (auto const& i : amendmentNames)
                {
                    table.addKnown (i);
                }
                break;
            case TablePopulationAlgo::addInitial:
            {
                std::vector<std::string> configLines;
                configLines.reserve (amendmentPairs.size ());
                for (auto const& i : amendmentPairs)
                {
                    configLines.emplace_back (i.first + " " + i.second);
                }
                populateTable (table, configLines);
            }
            break;
            default:
                fail ("Error in test case logic");
        }

        return amendmentNames;
    }

    static std::unique_ptr< AmendmentTable >
    makeTable (int w)
    {
        return make_AmendmentTable (
            weeks (w),
            majorityFraction,
            beast::Journal{});
    };

    // Create the amendments by string pairs instead of AmendmentNames
    // as this helps test the AmendmentNames class
    StringPairVec const m_knownAmendmentPairs;
    StringPairVec const m_unknownAmendmentPairs;

public:
    AmendmentTable_test ()
        : m_knownAmendmentPairs (
              {{"a49f90e7cddbcadfed8fc89ec4d02011", "Known1"},
               {"ca956ccabf25151a16d773171c485423", "Known2"},
               {"60dcd528f057711c5d26b57be28e23df", "Known3"},
               {"da956ccabf25151a16d773171c485423", "Known4"},
               {"70dcd528f057711c5d26b57be28e23df", "Known5"},
               {"70dcd528f057711c5d26b57be28e23d0", "Known6"}})
        , m_unknownAmendmentPairs (
              {{"a9f90e7cddbcadfed8fc89ec4d02011c", "Unknown1"},
               {"c956ccabf25151a16d773171c485423b", "Unknown2"},
               {"6dcd528f057711c5d26b57be28e23dfa", "Unknown3"}})
    {
    }

    void testGet ()
    {
        testcase ("get");
        auto table (makeTable (2));
        std::vector<AmendmentName> const amendmentNames (
            populateTable (*table, m_knownAmendmentPairs));
        std::vector<AmendmentName> const unknownAmendmentNames (
            getAmendmentNames (m_unknownAmendmentPairs));
        for (auto const& i : amendmentNames)
        {
            expect (table->get (i.friendlyName ()) == i.id ());
        }

        for (auto const& i : unknownAmendmentNames)
        {
            expect (table->get (i.friendlyName ()) == uint256 ());
        }
    }

    void testAddInitialAddKnown ()
    {
        testcase ("addInitialAddKnown");

        for (auto tablePopulationAlgo :
             {TablePopulationAlgo::addInitial, TablePopulationAlgo::addKnown})
        {
            {
                // test that the amendments we add are enabled and amendments we
                // didn't add are not enabled

                auto table (makeTable (2));
                std::vector<AmendmentName> const amendmentNames (populateTable (
                    *table, m_knownAmendmentPairs, tablePopulationAlgo));
                std::vector<AmendmentName> const unknownAmendmentNames (
                    getAmendmentNames (m_unknownAmendmentPairs));

                for (auto const& i : amendmentNames)
                {
                    expect (table->isSupported (i.id ()));
                    if (tablePopulationAlgo == TablePopulationAlgo::addInitial)
                        expect (table->isEnabled (i.id ()));
                }

                for (auto const& i : unknownAmendmentNames)
                {
                    expect (!table->isSupported (i.id ()));
                    expect (!table->isEnabled (i.id ()));
                }
            }

            {
                // check that we throw an exception on bad hex pairs
                StringPairVec const badHexPairs (
                    {{"a9f90e7cddbcadfedm8fc89ec4d02011c", "BadHex1"},
                     {"c956ccabf25151a16d77T3171c485423b", "BadHex2"},
                     {"6dcd528f057711c5d2Z6b57be28e23dfa", "BadHex3"}});

                // make sure each element throws
                for (auto const& i : badHexPairs)
                {
                    StringPairVec v ({i});
                    auto table (makeTable (2));
                    try
                    {
                        populateTable (*table, v, tablePopulationAlgo);
                        // line above should throw
                        fail ("didn't throw");
                    }
                    catch (std::exception const&)
                    {
                        pass ();
                    }
                    try
                    {
                        populateTable (
                            *table, badHexPairs, tablePopulationAlgo);
                        // line above should throw
                        fail ("didn't throw");
                    }
                    catch (std::exception const&)
                    {
                        pass ();
                    }
                }
            }
        }

        {
            // check that we thow on bad num tokens
            std::vector<std::string> const badNumTokensConfigLines (
                {"19f6d",
                 "19fd6 bad friendly name"
                 "9876 one two"});

            // make sure each element throws
            for (auto const& i : badNumTokensConfigLines)
            {
                std::vector<std::string> v ({i});
                auto table (makeTable (2));
                try
                {
                    populateTable (*table, v);
                    // line above should throw
                    fail ("didn't throw");
                }
                catch (std::exception const&)
                {
                    pass ();
                }
                try
                {
                    populateTable (*table, badNumTokensConfigLines);
                    // line above should throw
                    fail ("didn't throw");
                }
                catch (std::exception const&)
                {
                    pass ();
                }
            }
        }
    }

    void testEnable ()
    {
        testcase ("enable");
        auto table (makeTable (2));
        std::vector<AmendmentName> const amendmentNames (
            populateTable (*table, m_knownAmendmentPairs));
        {
            // enable/disable tests
            for (auto const& i : amendmentNames)
            {
                auto id (i.id ());
                table->enable (id);
                expect (table->isEnabled (id));
                table->disable (id);
                expect (!table->isEnabled (id));
                table->enable (id);
                expect (table->isEnabled (id));
            }

            std::vector<uint256> toEnable;
            for (auto const& i : amendmentNames)
            {
                auto id (i.id ());
                toEnable.emplace_back (id);
                table->disable (id);
                expect (!table->isEnabled (id));
            }
            table->setEnabled (toEnable);
            for (auto const& i : toEnable)
            {
                expect (table->isEnabled (i));
            }
        }
    }

    using ATSetter =
        void (AmendmentTable::*)(const std::vector<uint256>& amendments);
    using ATGetter = bool (AmendmentTable::*)(uint256 const& amendment);
    void testVectorSetUnset (ATSetter setter, ATGetter getter)
    {
        auto table (makeTable (2));
        // make pointer to ref syntax a little nicer
        auto& tableRef (*table);
        std::vector<AmendmentName> const amendmentNames (
            populateTable (tableRef, m_knownAmendmentPairs));

        // they should all be set
        for (auto const& i : amendmentNames)
        {
            expect ((tableRef.*getter)(i.id ()));  // i.e. "isSupported"
        }

        {
            // only set every other amendment
            std::vector<uint256> toSet;
            toSet.reserve (amendmentNames.size ());
            for (int i = 0; i < amendmentNames.size (); ++i)
            {
                if (i % 2)
                {
                    toSet.emplace_back (amendmentNames[i].id ());
                }
            }
            (tableRef.*setter)(toSet);
            for (int i = 0; i < amendmentNames.size (); ++i)
            {
                bool const shouldBeSet = i % 2;
                expect (shouldBeSet ==
                        (tableRef.*getter)(
                            amendmentNames[i].id ()));  // i.e. "isSupported"
            }
        }
    }
    void testSupported ()
    {
        testcase ("supported");
        testVectorSetUnset (&AmendmentTable::setSupported,
                            &AmendmentTable::isSupported);
    }
    void testEnabled ()
    {
        testcase ("enabled");
        testVectorSetUnset (&AmendmentTable::setEnabled,
                            &AmendmentTable::isEnabled);
    }
    void testSupportedEnabled ()
    {
        // Check that supported/enabled aren't the same thing
        testcase ("supportedEnabled");
        auto table (makeTable (2));

        std::vector<AmendmentName> const amendmentNames (
            populateTable (*table, m_knownAmendmentPairs));

        {
            // support every even amendment
            // enable every odd amendment
            std::vector<uint256> toSupport;
            toSupport.reserve (amendmentNames.size ());
            std::vector<uint256> toEnable;
            toEnable.reserve (amendmentNames.size ());
            for (int i = 0; i < amendmentNames.size (); ++i)
            {
                if (i % 2)
                {
                    toSupport.emplace_back (amendmentNames[i].id ());
                }
                else
                {
                    toEnable.emplace_back (amendmentNames[i].id ());
                }
            }
            table->setEnabled (toEnable);
            table->setSupported (toSupport);
            for (int i = 0; i < amendmentNames.size (); ++i)
            {
                bool const shouldBeSupported = i % 2;
                bool const shouldBeEnabled = !(i % 2);
                expect (shouldBeEnabled ==
                        (table->isEnabled (amendmentNames[i].id ())));
                expect (shouldBeSupported ==
                        (table->isSupported (amendmentNames[i].id ())));
            }
        }
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
    void doRound
        ( AmendmentTable& table
        , int week
        , std::vector <RippleAddress> const& validators
        , std::vector <std::pair <uint256, int> > const& votes
        , std::vector <uint256>& ourVotes
        , enabledAmendments_t& enabled
        , majorityAmendments_t& majority)
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
            STValidation::pointer v =
                std::make_shared <STValidation>
                    (uint256(), roundTime, val, true);

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

        auto actions = table.doVoting (roundTime, enabled, majority, validations);
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
                    assert (false);
                    Throw<std::runtime_error> ("unknown action");
            }
        }
    }

    // No vote on unknown amendment
    void testNoUnknown ()
    {
        testcase ("voteNoUnknown");

        auto table (makeTable (2));

        auto const validators = makeValidators (10);

        uint256 testAmendment;
        testAmendment.SetHex("6dcd528f057711c5d26b57be28e23dfa");

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
    void testNoVetoed ()
    {
        testcase ("voteNoVetoed");

        auto table (makeTable (2));

        auto const validators = makeValidators (10);

        uint256 testAmendment;
        testAmendment.SetHex("6dcd528f057711c5d26b57be28e23dfa");
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
        auto const amendmentNames (
            populateTable (*table, m_knownAmendmentPairs));

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
        expect (ourVotes.size() == amendmentNames.size(), "Did not vote");
        expect (enabled.empty(), "Enabled amendment for no reason");
        for (auto const& i : amendmentNames)
            expect(majority.find(i.id()) == majority.end(), "majority detected for no reaosn");

        // Now, everyone votes for this feature
        for (auto const& i : amendmentNames)
            votes.emplace_back (i.id(), 256);

        // Week 2: We should recognize a majority
        doRound (*table, 2,
                validators,
                votes,
                ourVotes,
                enabled,
                majority);
        expect (ourVotes.size() == amendmentNames.size(), "Did not vote");
        expect (enabled.empty(), "Enabled amendment for no reason");
        for (auto const& i : amendmentNames)
            expect (majority[i.id()] == weekTime(2), "majority not detected");

        // Week 5: We should enable the amendment
        doRound (*table, 5,
                validators,
                votes,
                ourVotes,
                enabled,
                majority);
        expect (enabled.size() == amendmentNames.size(), "Did not enable");

        // Week 6: We should remove it from our votes and from having a majority
        doRound (*table, 6,
                validators,
                votes,
                ourVotes,
                enabled,
                majority);
        expect (enabled.size() == amendmentNames.size(), "Disabled");
        expect (ourVotes.empty(), "Voted after enabling");
        for (auto const& i : amendmentNames)
            expect(majority.find(i.id()) == majority.end(), "majority not removed");
    }

    // Detect majority at 80%, enable later
    void testDetectMajority ()
    {
        testcase ("detectMajority");
        auto table (makeTable (2));

        uint256 testAmendment;
        testAmendment.SetHex("6dcd528f057711c5d26b57be28e23dfa");
        table->addKnown({testAmendment, "testAmendment"});

        auto const validators = makeValidators (16);

        enabledAmendments_t enabled;
        majorityAmendments_t majority;

        for (int i = 0; i <= 17; ++i)
        {
            std::vector <std::pair <uint256, int>> votes;
            std::vector <uint256> ourVotes;

            if ((i > 0) && (i < 17))
                votes.emplace_back (testAmendment, i * 16);

            doRound (*table, i,
                validators, votes, ourVotes, enabled, majority);

            if (i < 14)
            {
                // rounds 0-13
                // We are voting yes, not enabled, no majority
                expect (!ourVotes.empty(), "We aren't voting");
                expect (enabled.empty(), "Enabled too early");
                expect (majority.empty(), "Majority too early");
            }
            else if (i < 16)
            {
                // rounds 14 and 15
                // We have a majority, not enabled, keep voting
                expect (!ourVotes.empty(), "We stopped voting");
                expect (!majority.empty(), "Failed to detect majority");
                expect (enabled.empty(), "Enabled too early");
            }
            else if (i == 16) // round 16
            {
                // round 16
                // enable, keep voting, remove from majority
                expect (!ourVotes.empty(), "We stopped voting");
                expect (majority.empty(), "Failed to remove from majority");
                expect (!enabled.empty(), "Did not enable");
            }
            else
            {
                // round 17
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

        uint256 testAmendment;
        testAmendment.SetHex("6dcd528f057711c5d26b57be28e23dfa");
        table->addKnown({testAmendment, "testAmendment"});

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

            if (i < 6)
            {
                // rounds 1 to 5
                // We are voting yes, not enabled, majority
                expect (!ourVotes.empty(), "We aren't voting");
                expect (enabled.empty(), "Enabled for no reason");
                expect (!majority.empty(), "Lost majority too early");
            }
            else
            {
                // rounds 6 to 15
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
        testAddInitialAddKnown ();
        testEnable ();
        testSupported ();
        testSupportedEnabled ();
        testNoUnknown ();
        testNoVetoed ();
        testVoteEnable ();
        testDetectMajority ();
        testLostMajority ();
    }
};

BEAST_DEFINE_TESTSUITE (AmendmentTable, app, ripple);

}  // ripple
