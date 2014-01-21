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

namespace ripple {
namespace Resource {

class Tests : public UnitTest
{
public:
    class TestLogic
        : private boost::base_from_member <manual_clock <std::chrono::seconds>>
        , public Logic

    {
    private:
        typedef boost::base_from_member <
            manual_clock <std::chrono::seconds>> clock_type;

    public:
        explicit TestLogic (Journal journal)
            : Logic (insight::NullCollector::New(), member, journal)
        {
        }

        void advance ()
        {
            ++member;
        }

        manual_clock <std::chrono::seconds>& clock ()
        {
            return member;
        }
    };

    //--------------------------------------------------------------------------

    void createGossip (Gossip& gossip)
    {
        int const v (10 + random().nextInt (10));
        int const n (10 + random().nextInt (10));
        gossip.items.reserve (n);
        for (int i = 0; i < n; ++i)
        {
            Gossip::Item item;
            item.balance = 100 + random().nextInt (500);
            item.address = IPAddress (IPAddress::V4 (
                207, 127, 82, v + i));
            gossip.items.push_back (item);
        }
    }

    //--------------------------------------------------------------------------

    enum
    {
        maxLoopCount = 10000
    };

    void testDrop (Journal j)
    {
        beginTestCase ("Warn/drop");

        Tests::TestLogic logic (j);

        Charge const fee (dropThreshold + 1);
        IPAddress const addr (
            IPAddress::from_string ("207.127.82.2"));
        
        {
            Consumer c (logic.newInboundEndpoint (addr));

            // Create load until we get a warning
            for (std::size_t n (maxLoopCount); n>=0; --n)
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
            for (std::size_t n (maxLoopCount); n>=0; --n)
            {
                if (n == 0)
                {
                    fail ("Loop count exceeded without dropping");
                    return;
                }

                if (c.charge (fee) == drop)
                {
                    pass ();
                    break;
                }
                ++logic.clock ();
            }

        }

        {
            Consumer c (logic.newInboundEndpoint (addr));
            expect (c.disconnect ());
        }

        for (std::size_t n (maxLoopCount); n>=0; --n)
        {
            Consumer c (logic.newInboundEndpoint (addr));
            if (n == 0)
            {
                fail ("Loop count exceeded without expiring black list");
                return;
            }

            if (c.disposition() != drop)
            {
                pass ();
                break;
            }
        }
    }

    void testImports (Journal j)
    {
        beginTestCase ("Imports");

        TestLogic logic (j);

        Gossip g[5];

        for (int i = 0; i < 5; ++i)
            createGossip (g[i]);

        for (int i = 0; i < 5; ++i)
            logic.importConsumers (String::fromNumber (i).toStdString(), g[i]);

        pass();
    }

    void testImport (Journal j)
    {
        beginTestCase ("Import");

        TestLogic logic (j);

        Gossip g;
        Gossip::Item item;
        item.balance = 100;
        item.address = IPAddress (IPAddress::V4 (207, 127, 82, 1));
        g.items.push_back (item);

        logic.importConsumers ("g", g);

        pass();
    }

    void testCharges (Journal j)
    {
        beginTestCase ("Charge");

        TestLogic logic (j);

        {
            IPAddress address (IPAddress::from_string ("207.127.82.1"));
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
            IPAddress address (IPAddress::from_string ("207.127.82.2"));
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

    void runTest ()
    {
        //Journal j (journal());
        Journal j;

        testDrop (j);
        testCharges (j);
        testImports (j);
        testImport (j);
    }

    Tests () : UnitTest ("ResourceManager", "ripple")
    {
    }
};

static Tests tests;

}
}
