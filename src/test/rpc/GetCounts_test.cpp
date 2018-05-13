//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2017 Ripple Labs Inc.

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

#include <test/jtx.h>
#include <ripple/beast/unit_test.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/SField.h>
#include <ripple/basics/CountedObject.h>

namespace ripple {

class GetCounts_test : public beast::unit_test::suite
{
    void testGetCounts()
    {
        using namespace test::jtx;
        Env env(*this);

        Json::Value result;
        {
            // check counts with no transactions posted
            result = env.rpc("get_counts")[jss::result];
            BEAST_EXPECT(result[jss::status] == "success");
            BEAST_EXPECT(! result.isMember("Transaction"));
            BEAST_EXPECT(! result.isMember("STObject"));
            BEAST_EXPECT(! result.isMember("HashRouterEntry"));
            BEAST_EXPECT(
                result.isMember(jss::uptime) &&
                ! result[jss::uptime].asString().empty());
            BEAST_EXPECT(
                result.isMember(jss::dbKBTotal) &&
                result[jss::dbKBTotal].asInt() > 0);
        }

        // create some transactions
        env.close();
        Account alice {"alice"};
        Account bob {"bob"};
        env.fund (XRP(10000), alice, bob);
        env.trust (alice["USD"](1000), bob);
        for(auto i=0; i<20; ++i)
        {
            env (pay (alice, bob, alice["USD"](5)));
            env.close();
        }

        {
            // check counts, default params
            result = env.rpc("get_counts")[jss::result];
            BEAST_EXPECT(result[jss::status] == "success");
            // compare with values reported by CountedObjects
            auto const& objectCounts = CountedObjects::getInstance ().getCounts (10);
            for (auto const& it : objectCounts)
            {
                BEAST_EXPECTS(result.isMember(it.first), it.first);
                BEAST_EXPECTS(result[it.first].asInt() == it.second, it.first);
            }
            BEAST_EXPECT(! result.isMember(jss::local_txs));
        }

        {
            // make request with min threshold 100 and verify
            // that only STObject and NodeObject are reported
            result = env.rpc("get_counts", "100")[jss::result];
            BEAST_EXPECT(result[jss::status] == "success");

            // compare with values reported by CountedObjects
            auto const& objectCounts = CountedObjects::getInstance ().getCounts (100);
            for (auto const& it : objectCounts)
            {
                BEAST_EXPECTS(result.isMember(it.first), it.first);
                BEAST_EXPECTS(result[it.first].asInt() == it.second, it.first);
            }
            BEAST_EXPECT(! result.isMember("Transaction"));
            BEAST_EXPECT(! result.isMember("STTx"));
            BEAST_EXPECT(! result.isMember("STArray"));
            BEAST_EXPECT(! result.isMember("HashRouterEntry"));
            BEAST_EXPECT(! result.isMember("STLedgerEntry"));
        }

        {
            // local_txs field will exist when there are open Txs
            env (pay (alice, bob, alice["USD"](5)));
            result = env.rpc("get_counts")[jss::result];
            // deliberately don't call close so we have open Tx
            BEAST_EXPECT(
                result.isMember(jss::local_txs) &&
                result[jss::local_txs].asInt() > 0);
        }
    }

public:
    void run () override
    {
        testGetCounts();
    }
};

BEAST_DEFINE_TESTSUITE(GetCounts,rpc,ripple);

} // ripple

