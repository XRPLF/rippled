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

    void testImports ()
    {
        beginTestCase ("Imports");

        LogicType <ManualClock> logic (journal());

        Gossip g[5];

        for (int i = 0; i < 5; ++i)
            createGossip (g[i]);

        for (int i = 0; i < 5; ++i)
            logic.importConsumers (String::fromNumber (i).toStdString(), g[i]);

        pass();
    }

    void testImport ()
    {
        beginTestCase ("Import");

        LogicType <ManualClock> logic (journal());

        Gossip g;
        Gossip::Item item;
        item.balance = 100;
        item.address = IPAddress (IPAddress::V4 (207, 127, 82, 1));
        g.items.push_back (item);

        logic.importConsumers ("g", g);

        pass();
    }

    void testCharges ()
    {
        beginTestCase ("Charge");

        LogicType <ManualClock> logic (journal());

        {
            IPAddress address (IPAddress::from_string ("207.127.82.1"));
            Consumer c (logic.newInboundEndpoint (address));
            logMessage ("Charging " + c.label() + " 10,000 units");
            c.charge (10000);
            for (int i = 0; i < 128; ++i)
            {
                logMessage (
                    "Time = " + String::fromNumber (logic.clock().now()) +
                    ", Balance = " + String::fromNumber (c.balance()));
                ++logic.clock().now();
            }
        }

        {
            IPAddress address (IPAddress::from_string ("207.127.82.2"));
            Consumer c (logic.newInboundEndpoint (address));
            logMessage ("Charging " + c.label() + " 1000 units per second");
            for (int i = 0; i < 128; ++i)
            {
                c.charge (1000);
                logMessage (
                    "Time = " + String::fromNumber (logic.clock().now()) +
                    ", Balance = " + String::fromNumber (c.balance()));
                ++logic.clock().now();
            }
        }

        pass();
    }

    void runTest ()
    {
        testCharges();
        testImports();
        testImport();
    }

    Tests () : UnitTest ("ResourceManager", "ripple", runManual)
    {
    }
};

static Tests tests;

}
}
