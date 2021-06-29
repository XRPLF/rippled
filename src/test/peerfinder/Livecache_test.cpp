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

#include <ripple/basics/chrono.h>
#include <ripple/basics/safe_cast.h>
#include <ripple/beast/clock/manual_clock.h>
#include <ripple/beast/unit_test.h>
#include <ripple/peerfinder/impl/Livecache.h>
#include <boost/algorithm/string.hpp>
#include <test/beast/IPEndpointCommon.h>
#include <test/unit_test/SuiteJournal.h>

namespace ripple {
namespace PeerFinder {

class Livecache_test : public beast::unit_test::suite
{
    TestStopwatch clock_;
    test::SuiteJournal journal_;

public:
    Livecache_test() : journal_("Livecache_test", *this)
    {
    }

    void
    testBasicInsert()
    {
        testcase("Basic Insert");
        Livecache<> c(clock_, journal_);
        BEAST_EXPECT(c.empty());

        for (auto i = 0; i < 10; ++i)
            c.insert(beast::IP::randomEP(true), 0);

        BEAST_EXPECT(!c.empty());
        BEAST_EXPECT(c.size() == 10);

        for (auto i = 0; i < 10; ++i)
            c.insert(beast::IP::randomEP(false), 0);

        BEAST_EXPECT(!c.empty());
        BEAST_EXPECT(c.size() == 20);
    }

    void
    testInsertUpdate()
    {
        testcase("Insert/Update");
        Livecache<> c(clock_, journal_);

        auto ep1 = beast::IP::randomEP();
        c.insert(ep1, 0);
        BEAST_EXPECT(c.size() == 1);
        // should not add the same endpoint
        c.insert(ep1, 0);
        BEAST_EXPECT(c.size() == 1);
        auto ep2 = beast::IP::randomEP();
        c.insert(ep2, 0);
        BEAST_EXPECT(c.size() == 2);
    }

    void
    testExpire()
    {
        testcase("Expire");
        using namespace std::chrono_literals;
        Livecache<> c(clock_, journal_);

        c.insert(beast::IP::randomEP(), 0);
        BEAST_EXPECT(c.size() == 1);
        c.expire();
        BEAST_EXPECT(c.size() == 1);
        // verify that advancing to 1 sec before expiration
        // leaves our entry intact
        clock_.advance(Tuning::liveCacheSecondsToLive - 1s);
        c.expire();
        BEAST_EXPECT(c.size() == 1);
        // now advance to the point of expiration
        clock_.advance(1s);
        c.expire();
        BEAST_EXPECT(c.empty());

        c.insert(beast::IP::randomEP(), 0);
        auto adv = Tuning::liveCacheSecondsToLive / 3;
        clock_.advance(adv);
        c.insert(beast::IP::randomEP(), 0);
        clock_.advance(Tuning::liveCacheSecondsToLive - adv + 1s);
        c.expire();
        BEAST_EXPECT(c.size() == 1);
    }

    void
    testShuffle()
    {
        testcase("Shuffle");
        Livecache<> c(clock_, journal_);
        for (auto i = 0; i < 100; ++i)
            c.insert(beast::IP::randomEP(true), 0);

        std::vector<std::pair<
            std::reference_wrapper<const beast::IP::Endpoint>,
            std::uint32_t>>
            endpoints;
        std::copy(c.cbegin(), c.cend(), std::back_inserter(endpoints));
        std::shuffle(endpoints.begin(), endpoints.end(), default_prng());
        BEAST_EXPECT(c.size() == endpoints.size());

        for (auto it : endpoints)
            BEAST_EXPECT(c.find(it.first) != c.cend());
    }

    void
    run() override
    {
        testBasicInsert();
        testInsertUpdate();
        testExpire();
        testShuffle();
    }
};

BEAST_DEFINE_TESTSUITE(Livecache, peerfinder, ripple);

}  // namespace PeerFinder
}  // namespace ripple
