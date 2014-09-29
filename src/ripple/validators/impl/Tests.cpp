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

#include <beast/unit_test/suite.h>

namespace ripple {
namespace Validators {

class Logic_test : public beast::unit_test::suite
{
public:
    enum
    {
        numberOfTestValidators = 1000,
        numberofTestSources = 50
    };

    //--------------------------------------------------------------------------

    struct TestSource : Source
    {
        TestSource (std::string const& name, std::uint32_t start, std::uint32_t end)
            : m_name (name)
            , m_start (start)
            , m_end (end)
        {
        }

        std::string to_string () const
        {
            return uniqueID();
        }

        std::string uniqueID () const
        {
            return "Test," + m_name + "," +
                std::to_string (m_start) + "," +
                std::to_string (m_end);
        }

        std::string createParam ()
        {
            return std::string{};
        }

        void fetch (Results& results, beast::Journal)
        {
            results.success = true;
            results.message = std::string{};
            results.list.reserve (numberOfTestValidators);

            for (std::uint32_t i = m_start ; i < m_end; ++i)
            {
                Item item;
                item.publicKey = RipplePublicKey::createFromInteger (i);
                item.label = std::to_string (i);
                results.list.push_back (item);
            }
        }

        std::string m_name;
        std::size_t m_start;
        std::size_t m_end;
    };

    //--------------------------------------------------------------------------

    class TestStore : public Store
    {
    public:
        TestStore ()
        {
        }

        ~TestStore ()
        {
        }

        void insertSourceDesc (SourceDesc& desc)
        {
        }

        void updateSourceDesc (SourceDesc& desc)
        {
        }

        void updateSourceDescInfo (SourceDesc& desc)
        {
        }
    };

    //--------------------------------------------------------------------------

    void addSources (Logic& logic)
    {
        beast::Random r;
        for (int i = 1; i <= numberofTestSources; ++i)
        {
            std::string const name (std::to_string (i));
            std::uint32_t const start = r.nextInt (numberOfTestValidators);
            std::uint32_t const end   = start + r.nextInt (numberOfTestValidators);
            logic.add (new TestSource (name, start, end));
        }
    }

    void testLRUCache ()
    {
        detail::LRUCache<std::string> testCache {3};
        expect (testCache.size () == 0, "Wrong initial size");

        struct TestValues
        {
            char const* const value;
            bool const insertResult;
        };
        {
            std::array <TestValues, 3> const v1 {
                {{"A", true}, {"B", true}, {"C", true}}};
            for (auto const& v : v1)
            {
                expect (testCache.insert (v.value) == v.insertResult,
                    "Failed first insert tests");
            }
            expect (testCache.size() == 3, "Unexpected intermediate size");
            expect (*testCache.oldest() == "A", "Unexpected oldest member");
        }
        {
            std::array <TestValues, 3> const v2 {
                {{"A", false}, {"D", true}, {"C", false}}};
            for (auto const& v : v2)
            {
                expect (testCache.insert (v.value) == v.insertResult,
                    "Failed second insert tests");
            }
            expect (testCache.size() == 3, "Unexpected final size");
            expect (*testCache.oldest() == "A",
                "Unexpected oldest member");
        }
    }

    void testValidator ()
    {
        int receivedCount = 0;
        int expectedCount = 0;
        int closedCount = 0;

        // Lambda as local function
        auto updateCounts = [&](bool received, bool validated)
        {
            bool const sent = received || validated;

            receivedCount += sent && !validated ? 1 : 0;
            expectedCount += sent && !received  ? 1 : 0;
            closedCount   += validated && received ? 1 : 0;
        };

        auto checkCounts = [&] (Count const& count)
        {
//          std::cout << "Received actual: " << count.received << " expected: " << receivedCount << std::endl;
//          std::cout << "Expected actual: " << count.expected << " expected: " << expectedCount << std::endl;
//          std::cout << "Closed actual:   " << count.closed   << " expected: " << closedCount   << std::endl;
            expect (count.received == receivedCount, "Bad received count");
            expect (count.expected == expectedCount, "Bad expected count");
            expect (count.closed == closedCount, "Bad closed count");
        };

        Validator validator;
        std::uint64_t i = 1;

        // Received before closed
        for (; i <= ledgersPerValidator; ++i)
        {
            RippleLedgerHash const hash {i};

            bool const received = (i % 13 != 0);
            bool const validated = (i % 7 != 0);
            updateCounts (received, validated);

            if (received)
                validator.on_validation (hash);

            if (validated)
                validator.on_ledger (hash);
        }
        checkCounts (validator.count ());

        // Closed before received
        for (; i <= ledgersPerValidator * 2; ++i)
        {
            RippleLedgerHash const hash {i};

            bool const received = (i % 11 != 0);
            bool const validated = (i % 17 != 0);
            updateCounts (received, validated);

            if (validated)
                validator.on_ledger (hash);

            if (received)
                validator.on_validation (hash);
        }
        checkCounts (validator.count ());

        {
            // Repeated receives
            RippleLedgerHash const hash {++i};
            receivedCount += 1;
            for (auto j = 0; j < 100; ++j)
            {
                validator.on_validation (hash);
            }
        }
        checkCounts (validator.count ());

        {
            // Repeated closes
            RippleLedgerHash const hash {++i};
            expectedCount += 1;
            for (auto j = 0; j < 100; ++j)
            {
                validator.on_ledger (hash);
            }
        }
       checkCounts (validator.count ());
    }

    void testLogic ()
    {
        //TestStore store;
        StoreSqdb storage;

        beast::File const file (
            beast::File::getSpecialLocation (
                beast::File::userDocumentsDirectory).getChildFile (
                    "validators-test.sqlite"));

        // Can't call this 'error' because of ADL and Journal::error
        beast::Error err (storage.open (file));

        expect (! err, err.what());

        Logic logic (storage, beast::Journal ());
        logic.load ();

        addSources (logic);

        logic.fetch_one ();

//      auto chosenSize (logic.getChosenSize ());

        pass ();
    }

    void
    run ()
    {
        testLRUCache ();
        testValidator ();
        testLogic ();
    }
};

BEAST_DEFINE_TESTSUITE(Logic,validators,ripple);

}
}
