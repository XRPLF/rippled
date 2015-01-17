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
#include <ripple/core/ConfigSections.h>
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

    // 204/256 about 80%
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

    static std::unique_ptr<AmendmentTable> makeTable ()
    {
        beast::Journal journal;
        return make_AmendmentTable (weeks (2),
                                    majorityFraction,
                                    journal,
                                    make_MOCAmendmentTableInjections ());
    };

    // Create the amendments by string pairs instead of AmendmentNames
    // as this helps test the AmendmentNames class
    StringPairVec const m_validAmendmentPairs;
    StringPairVec const m_notAddedAmendmentPairs;

public:
    AmendmentTable_test ()
        : m_validAmendmentPairs (
              {{"a49f90e7cddbcadfed8fc89ec4d02011", "Added1"},
               {"ca956ccabf25151a16d773171c485423", "Added2"},
               {"60dcd528f057711c5d26b57be28e23df", "Added3"},
               {"da956ccabf25151a16d773171c485423", "Added4"},
               {"70dcd528f057711c5d26b57be28e23df", "Added5"},
               {"70dcd528f057711c5d26b57be28e23d0", "Added6"}})
        , m_notAddedAmendmentPairs (
              {{"a9f90e7cddbcadfed8fc89ec4d02011c", "NotAdded1"},
               {"c956ccabf25151a16d773171c485423b", "NotAdded2"},
               {"6dcd528f057711c5d26b57be28e23dfa", "NotAdded3"}})
    {
    }

    void testGet ()
    {
        testcase ("get");
        auto table (makeTable ());
        std::vector<AmendmentName> const amendmentNames (
            populateTable (*table, m_validAmendmentPairs));
        std::vector<AmendmentName> const notAddedAmendmentNames (
            getAmendmentNames (m_notAddedAmendmentPairs));
        for (auto const& i : amendmentNames)
        {
            expect (table->get (i.friendlyName ()) == i.id ());
        }

        for (auto const& i : notAddedAmendmentNames)
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
                // test that the amendmens we add are enabled and amendments we
                // didn't add are not enabled

                auto table (makeTable ());
                std::vector<AmendmentName> const amendmentNames (populateTable (
                    *table, m_validAmendmentPairs, tablePopulationAlgo));
                std::vector<AmendmentName> const notAddedAmendmentNames (
                    getAmendmentNames (m_notAddedAmendmentPairs));

                for (auto const& i : amendmentNames)
                {
                    expect (table->isSupported (i.id ()));
                    if (tablePopulationAlgo == TablePopulationAlgo::addInitial)
                        expect (table->isEnabled (i.id ()));
                }

                for (auto const& i : notAddedAmendmentNames)
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
                    auto table (makeTable ());
                    try
                    {
                        populateTable (*table, v, tablePopulationAlgo);
                        // line above should throw
                        fail ("didn't throw");
                    }
                    catch (...)
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
                    catch (...)
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
                auto table (makeTable ());
                try
                {
                    populateTable (*table, v);
                    // line above should throw
                    fail ("didn't throw");
                }
                catch (...)
                {
                    pass ();
                }
                try
                {
                    populateTable (*table, badNumTokensConfigLines);
                    // line above should throw
                    fail ("didn't throw");
                }
                catch (...)
                {
                    pass ();
                }
            }
        }
    }

    void testEnable ()
    {
        testcase ("enable");
        auto table (makeTable ());
        std::vector<AmendmentName> const amendmentNames (
            populateTable (*table, m_validAmendmentPairs));
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
        auto table (makeTable ());
        // make pointer to ref syntax a little nicer
        auto& tableRef (*table);
        std::vector<AmendmentName> const amendmentNames (
            populateTable (tableRef, m_validAmendmentPairs));

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
        auto table (makeTable ());

        std::vector<AmendmentName> const amendmentNames (
            populateTable (*table, m_validAmendmentPairs));

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

    // TBD: veto/reportValidations/getJson/doValidation/doVoting
    // Threading test?

    void run ()
    {
        testGet ();
        testAddInitialAddKnown ();
        testEnable ();
        testSupported ();
        testSupportedEnabled ();
    }
};

BEAST_DEFINE_TESTSUITE (AmendmentTable, app, ripple);

}  // ripple
