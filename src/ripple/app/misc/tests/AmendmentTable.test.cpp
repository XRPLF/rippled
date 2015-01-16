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
class AmendmentTable_test : public beast::unit_test::suite
{
public:
    using StringPairVec = std::vector<std::pair<std::string, std::string>>;

private:
    // 204/256 about 80%
    static int const majorityFraction{204};

    void populateTable (AmendmentTable& table,
                        std::vector<std::string> const& configLines)
    {
        Section section (SECTION_AMENDMENTS);
        section.append (configLines);
        table.addInitial (section);
    }

    std::vector<AmendmentName> getAmendmentNames (
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
        StringPairVec const& amendmentPairs)
    {
        std::vector<AmendmentName> const amendmentNames (
            getAmendmentNames (amendmentPairs));
        std::vector<std::string> configLines;
        configLines.reserve (amendmentPairs.size ());
        for (auto const& i : amendmentPairs)
        {
            configLines.emplace_back (i.first + " " + i.second);
        }
        populateTable (table, configLines);
        return amendmentNames;
    }

public:
    void testAddInitial ()
    {
        testcase ("addInitial");

        auto makeTable = []()
        {
            beast::Journal journal;
            return make_AmendmentTable (weeks (2),
                                        majorityFraction,
                                        journal,
                                        make_MOCAmendmentTableInjections ());
        };

        {
            // test that the amendmens we add are enabled and amendments we
            // didn't add are not enabled

            // Create the amendments by string pairs instead of AmendmentNames
            // as this helps test the AmendmentNames class
            StringPairVec const amendmentPairs (
                {{"a49f90e7cddbcadfed8fc89ec4d02011", "Added1"},
                 {"ca956ccabf25151a16d773171c485423", "Added2"},
                 {"60dcd528f057711c5d26b57be28e23df", "Added3"}});

            StringPairVec const notAddedAmendmentPairs (
                {{"a9f90e7cddbcadfed8fc89ec4d02011c", "NotAdded1"},
                 {"c956ccabf25151a16d773171c485423b", "NotAdded2"},
                 {"6dcd528f057711c5d26b57be28e23dfa", "NotAdded3"}});

            auto table (makeTable ());
            std::vector<AmendmentName> const amendmentNames (
                populateTable (*table, amendmentPairs));
            std::vector<AmendmentName> const notAddedAmendmentNames (
                getAmendmentNames (notAddedAmendmentPairs));

            for (auto const& i : amendmentNames)
            {
                expect (table->isEnabled (i.id ()));
            }

            for (auto const& i : notAddedAmendmentNames)
            {
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
                    populateTable (*table, v);
                    // line above should throw
                    expect (false);
                }
                catch (...)
                {
                }
                try
                {
                    populateTable (*table, badHexPairs);
                    // line above should throw
                    expect (false);
                }
                catch (...)
                {
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
                    expect (false);
                }
                catch (...)
                {
                }
                try
                {
                    populateTable (*table, badNumTokensConfigLines);
                    // line above should throw
                    expect (false);
                }
                catch (...)
                {
                }
            }
        }
    }

    void run ()
    {
        testAddInitial ();
    }
};

BEAST_DEFINE_TESTSUITE (AmendmentTable, ripple_app, ripple);

}  // ripple
