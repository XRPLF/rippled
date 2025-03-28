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

#include <test/unit_test/SuiteJournal.h>

#include <xrpl/basics/chrono.h>
#include <xrpl/basics/random.h>
#include <xrpl/beast/unit_test.h>
#include <xrpl/resource/Consumer.h>
#include <xrpl/resource/detail/Entry.h>
#include <xrpl/resource/detail/Logic.h>

#include <boost/utility/base_from_member.hpp>

#include <functional>

namespace ripple {
namespace Resource {

class ResourceManager_test : public beast::unit_test::suite
{
public:
    class TestLogic : private boost::base_from_member<TestStopwatch>,
                      public Logic

    {
    private:
        using clock_type = boost::base_from_member<TestStopwatch>;

    public:
        explicit TestLogic(beast::Journal journal)
            : Logic(beast::insight::NullCollector::New(), member, journal)
        {
        }

        void
        advance()
        {
            ++member;
        }

        TestStopwatch&
        clock()
        {
            return member;
        }
    };

    //--------------------------------------------------------------------------

    void
    createGossip(Gossip& gossip)
    {
        std::uint8_t const v(10 + rand_int(9));
        std::uint8_t const n(10 + rand_int(9));
        gossip.items.reserve(n);
        for (std::uint8_t i = 0; i < n; ++i)
        {
            Gossip::Item item;
            item.balance = 100 + rand_int(499);
            beast::IP::AddressV4::bytes_type d = {
                {192, 0, 2, static_cast<std::uint8_t>(v + i)}};
            item.address = beast::IP::Endpoint{beast::IP::AddressV4{d}};
            gossip.items.push_back(item);
        }
    }

    //--------------------------------------------------------------------------

    void
    testDrop(beast::Journal j, bool limited)
    {
        if (limited)
            testcase("Limited warn/drop");
        else
            testcase("Unlimited warn/drop");

        TestLogic logic(j);

        Charge const fee(dropThreshold + 1);
        beast::IP::Endpoint const addr(
            beast::IP::Endpoint::from_string("192.0.2.2"));

        std::function<Consumer(beast::IP::Endpoint)> ep = limited
            ? std::bind(
                  &TestLogic::newInboundEndpoint, &logic, std::placeholders::_1)
            : std::bind(
                  &TestLogic::newUnlimitedEndpoint,
                  &logic,
                  std::placeholders::_1);

        {
            Consumer c(ep(addr));

            // Create load until we get a warning
            int n = 10000;

            while (--n >= 0)
            {
                if (n == 0)
                {
                    if (limited)
                        fail("Loop count exceeded without warning");
                    else
                        pass();
                    return;
                }

                if (c.charge(fee) == warn)
                {
                    if (limited)
                        pass();
                    else
                        fail("Should loop forever with no warning");
                    break;
                }
                ++logic.clock();
            }

            // Create load until we get dropped
            while (--n >= 0)
            {
                if (n == 0)
                {
                    if (limited)
                        fail("Loop count exceeded without dropping");
                    else
                        pass();
                    return;
                }

                if (c.charge(fee) == drop)
                {
                    // Disconnect abusive Consumer
                    BEAST_EXPECT(c.disconnect(j) == limited);
                    break;
                }
                ++logic.clock();
            }
        }

        // Make sure the consumer is on the blacklist for a while.
        {
            Consumer c(logic.newInboundEndpoint(addr));
            logic.periodicActivity();
            if (c.disposition() != drop)
            {
                if (limited)
                    fail("Dropped consumer not put on blacklist");
                else
                    pass();
                return;
            }
        }

        // Makes sure the Consumer is eventually removed from blacklist
        bool readmitted = false;
        {
            using namespace std::chrono_literals;
            // Give Consumer time to become readmitted.  Should never
            // exceed expiration time.
            auto n = secondsUntilExpiration + 1s;
            while (--n > 0s)
            {
                ++logic.clock();
                logic.periodicActivity();
                Consumer c(logic.newInboundEndpoint(addr));
                if (c.disposition() != drop)
                {
                    readmitted = true;
                    break;
                }
            }
        }
        if (readmitted == false)
        {
            fail("Dropped Consumer left on blacklist too long");
            return;
        }
        pass();
    }

    void
    testImports(beast::Journal j)
    {
        testcase("Imports");

        TestLogic logic(j);

        Gossip g[5];

        for (int i = 0; i < 5; ++i)
            createGossip(g[i]);

        for (int i = 0; i < 5; ++i)
            logic.importConsumers(std::to_string(i), g[i]);

        pass();
    }

    void
    testImport(beast::Journal j)
    {
        testcase("Import");

        TestLogic logic(j);

        Gossip g;
        Gossip::Item item;
        item.balance = 100;
        beast::IP::AddressV4::bytes_type d = {{192, 0, 2, 1}};
        item.address = beast::IP::Endpoint{beast::IP::AddressV4{d}};
        g.items.push_back(item);

        logic.importConsumers("g", g);

        pass();
    }

    void
    testCharges(beast::Journal j)
    {
        testcase("Charge");

        TestLogic logic(j);

        {
            beast::IP::Endpoint address(
                beast::IP::Endpoint::from_string("192.0.2.1"));
            Consumer c(logic.newInboundEndpoint(address));
            Charge fee(1000);
            JLOG(j.info()) << "Charging " << c.to_string() << " " << fee
                           << " per second";
            c.charge(fee);
            for (int i = 0; i < 128; ++i)
            {
                JLOG(j.info()) << "Time= "
                               << logic.clock().now().time_since_epoch().count()
                               << ", Balance = " << c.balance();
                logic.advance();
            }
        }

        {
            beast::IP::Endpoint address(
                beast::IP::Endpoint::from_string("192.0.2.2"));
            Consumer c(logic.newInboundEndpoint(address));
            Charge fee(1000);
            JLOG(j.info()) << "Charging " << c.to_string() << " " << fee
                           << " per second";
            for (int i = 0; i < 128; ++i)
            {
                c.charge(fee);
                JLOG(j.info()) << "Time= "
                               << logic.clock().now().time_since_epoch().count()
                               << ", Balance = " << c.balance();
                logic.advance();
            }
        }

        pass();
    }

    void
    run() override
    {
        using namespace beast::severities;
        test::SuiteJournal journal("ResourceManager_test", *this);

        testDrop(journal, true);
        testDrop(journal, false);
        testCharges(journal);
        testImports(journal);
        testImport(journal);
    }
};

BEAST_DEFINE_TESTSUITE(ResourceManager, resource, ripple);

}  // namespace Resource
}  // namespace ripple
