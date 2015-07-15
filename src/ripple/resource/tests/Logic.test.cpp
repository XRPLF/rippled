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
#include <ripple/basics/chrono.h>
#include <beast/unit_test/suite.h>
#include <beast/chrono/chrono_io.h>
#include <beast/module/core/maths/Random.h>
#include <boost/utility/base_from_member.hpp>

namespace ripple {
namespace Resource {

class Manager_test : public beast::unit_test::suite
{
public:
    class TestLogic
        : private boost::base_from_member<TestStopwatch>
        , public Logic

    {
    private:
        using clock_type =
            boost::base_from_member<TestStopwatch>;

    public:
        explicit TestLogic (beast::Journal journal)
            : Logic (beast::insight::NullCollector::New(), member, journal)
        {
        }

        void advance ()
        {
            ++member;
        }

        TestStopwatch& clock ()
        {
            return member;
        }
    };

    //--------------------------------------------------------------------------

    void createGossip (Gossip& gossip)
    {
        beast::Random r;
        int const v (10 + r.nextInt (10));
        int const n (10 + r.nextInt (10));
        gossip.items.reserve (n);
        for (int i = 0; i < n; ++i)
        {
            Gossip::Item item;
            item.balance = 100 + r.nextInt (500);
            item.address = beast::IP::Endpoint (
                beast::IP::AddressV4 (207, 127, 82, v + i));
            gossip.items.push_back (item);
        }
    }

    //--------------------------------------------------------------------------

    void testDrop (beast::Journal j)
    {
        testcase ("Warn/drop");

        TestLogic logic (j);

        Charge const fee (dropThreshold + 1);
        beast::IP::Endpoint const addr (
            beast::IP::Endpoint::from_string ("207.127.82.2"));

        {
            Consumer c (logic.newInboundEndpoint (addr));

            // Create load until we get a warning
            int n = 10000;

            while (--n >= 0)
            {
                if (n == 0)
                {
                    fail ("Loop count exceeded without warning");
                    return;
                }

                if (c.charge (fee) == warn)
                {
                    pass ();
                    break;
                }
                ++logic.clock ();
            }

            // Create load until we get dropped
            while (--n >= 0)
            {
                if (n == 0)
                {
                    fail ("Loop count exceeded without dropping");
                    return;
                }

                if (c.charge (fee) == drop)
                {
                    // Disconnect abusive Consumer
                    expect (c.disconnect ());
                    break;
                }
                ++logic.clock ();
            }
        }

        // Make sure the consumer is on the blacklist for a while.
        {
            Consumer c (logic.newInboundEndpoint (addr));
            logic.periodicActivity();
            if (c.disposition () != drop)
            {
                fail ("Dropped consumer not put on blacklist");
                return;
            }
        }

        // Makes sure the Consumer is eventually removed from blacklist
        bool readmitted = false;
        {
            // Give Consumer time to become readmitted.  Should never
            // exceed expiration time.
            std::size_t n (secondsUntilExpiration + 1);
            while (--n > 0)
            {
                ++logic.clock ();
                logic.periodicActivity();
                Consumer c (logic.newInboundEndpoint (addr));
                if (c.disposition () != drop)
                {
                    readmitted = true;
                    break;
                }
            }
        }
        if (readmitted == false)
        {
            fail ("Dropped Consumer left on blacklist too long");
            return;
        }
        pass();
    }

    void testImports (beast::Journal j)
    {
        testcase ("Imports");

        TestLogic logic (j);

        Gossip g[5];

        for (int i = 0; i < 5; ++i)
            createGossip (g[i]);

        for (int i = 0; i < 5; ++i)
            logic.importConsumers (std::to_string (i), g[i]);

        pass();
    }

    void testImport (beast::Journal j)
    {
        testcase ("Import");

        TestLogic logic (j);

        Gossip g;
        Gossip::Item item;
        item.balance = 100;
        item.address = beast::IP::Endpoint (
            beast::IP::AddressV4 (207, 127, 82, 1));
        g.items.push_back (item);

        logic.importConsumers ("g", g);

        pass();
    }

    void testCharges (beast::Journal j)
    {
        testcase ("Charge");

        TestLogic logic (j);

        {
            beast::IP::Endpoint address (beast::IP::Endpoint::from_string ("207.127.82.1"));
            Consumer c (logic.newInboundEndpoint (address));
            Charge fee (1000);
            j.info <<
                "Charging " << c.to_string() << " " << fee << " per second";
            c.charge (fee);
            for (int i = 0; i < 128; ++i)
            {
                j.info <<
                    "Time= " << logic.clock().now().time_since_epoch() <<
                    ", Balance = " << c.balance();
                logic.advance();
            }
        }

        {
            beast::IP::Endpoint address (beast::IP::Endpoint::from_string ("207.127.82.2"));
            Consumer c (logic.newInboundEndpoint (address));
            Charge fee (1000);
            j.info <<
                "Charging " << c.to_string() << " " << fee << " per second";
            for (int i = 0; i < 128; ++i)
            {
                c.charge (fee);
                j.info <<
                    "Time= " << logic.clock().now().time_since_epoch() <<
                    ", Balance = " << c.balance();
                logic.advance();
            }
        }

        pass();
    }

    void run()
    {
        beast::Journal j;

        testDrop (j);
        testCharges (j);
        testImports (j);
        testImport (j);
    }
};

BEAST_DEFINE_TESTSUITE(Manager,resource,ripple);

}
}
