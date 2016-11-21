//-----------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2015 Ripple Labs Inc.

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
#include <ripple/app/ledger/Ledger.h>
#include <ripple/test/jtx.h>
#include <ripple/beast/unit_test.h>

namespace ripple {
namespace test {

// Test that converting a ledger to SHAMapV2
// works as expected

class SHAMapV2_test : public beast::unit_test::suite
{
    void
    testSHAMapV2()
    {
        jtx::Env env(*this);
        Config config;

        std::set<uint256> amendments;
        std::vector<uint256> amendments_v;
        amendments_v.push_back(from_hex_text<uint256>("12345"));
        amendments.insert(from_hex_text<uint256>("12345"));

        auto ledger =
            std::make_shared<Ledger>(create_genesis, config,
                amendments_v, env.app().family());
        BEAST_EXPECT(! getSHAMapV2 (ledger->info()));
        BEAST_EXPECT(ledger->stateMap().get_version() == SHAMap::version{1});
        BEAST_EXPECT(ledger->txMap().get_version() == SHAMap::version{1});
        BEAST_EXPECT(getEnabledAmendments(*ledger) == amendments);

        ledger =
            std::make_shared<Ledger>(*ledger, NetClock::time_point{});
        BEAST_EXPECT(! getSHAMapV2 (ledger->info()));
        BEAST_EXPECT(ledger->stateMap().get_version() == SHAMap::version{1});
        BEAST_EXPECT(ledger->txMap().get_version() == SHAMap::version{1});
        BEAST_EXPECT(getEnabledAmendments(*ledger) == amendments);

        ledger->make_v2();
        BEAST_EXPECT(getSHAMapV2 (ledger->info()));
        BEAST_EXPECT(ledger->stateMap().get_version() == SHAMap::version{2});
        BEAST_EXPECT(ledger->txMap().get_version() == SHAMap::version{2});
        BEAST_EXPECT(getEnabledAmendments(*ledger) == amendments);

        ledger = std::make_shared<Ledger>(*ledger, NetClock::time_point{});
        BEAST_EXPECT(getSHAMapV2 (ledger->info()));
        BEAST_EXPECT(ledger->stateMap().get_version() == SHAMap::version{2});
        BEAST_EXPECT(ledger->txMap().get_version() == SHAMap::version{2});
        BEAST_EXPECT(getEnabledAmendments(*ledger) == amendments);
    }

    void run()
    {
        testSHAMapV2();
    }
};

BEAST_DEFINE_TESTSUITE(SHAMapV2,ledger,ripple);

}  // test
}  // ripple
