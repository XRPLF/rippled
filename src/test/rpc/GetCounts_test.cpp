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

namespace ripple {

class GetCounts_test : public beast::unit_test::suite
{
    void testGetCounts()
    {
        using namespace test::jtx;

        Env env(*this);
        // check counts with no transactions posted
        auto jrr = env.rpc("get_counts")[jss::result];
        BEAST_EXPECTS(jrr[jss::status] == "success", "status success");
        BEAST_EXPECTS(! jrr.isMember("Transaction"), "Transaction");
        BEAST_EXPECTS(! jrr.isMember("STObject"), "STObject");
        BEAST_EXPECTS(! jrr.isMember("HashRouterEntry"),
            "HashRouterEntry field");
        BEAST_EXPECTS(jrr.isMember(jss::uptime) &&
            ! jrr[jss::uptime].asString().empty(),
            "uptime");
        BEAST_EXPECTS(jrr.isMember(jss::dbKBTotal) &&
            jrr[jss::dbKBTotal].asInt() > 0,
            "dbKBTotal");

        // create some transactions
        env.close();
        Account alice {"alice"};
        Account bob {"bob"};
        env.fund (XRP(10000), alice, bob);
        env.trust (alice["USD"](1000), bob);
        for(auto i=0; i<10; ++i)
        {
            env (pay (alice, bob, alice["USD"](5)));
            env.close();
        }

        jrr = env.rpc("get_counts")[jss::result];
        BEAST_EXPECTS(jrr[jss::status] == "success", "status success");
        BEAST_EXPECTS(jrr.isMember("Transaction") &&
            jrr["Transaction"].asInt() > 0,
            "Transaction field");
        BEAST_EXPECTS(jrr.isMember("STTx") && jrr["STTx"].asInt() > 0,
            "STTx field");
        BEAST_EXPECTS(jrr.isMember("STObject") && jrr["STObject"].asInt() > 0,
            "STObject field");
        BEAST_EXPECTS(jrr.isMember("HashRouterEntry") &&
            jrr["HashRouterEntry"].asInt() > 0,
            "HashRouterEntry field");
        BEAST_EXPECTS(! jrr.isMember(jss::local_txs), "local_txs");

        //local_txs field will exist when there are open TXs
        env (pay (alice, bob, alice["USD"](5)));
        jrr = env.rpc("get_counts")[jss::result];
        BEAST_EXPECTS(jrr.isMember(jss::local_txs) &&
            jrr[jss::local_txs].asInt() > 0,
            "local_txs field");
    }

public:
    void run ()
    {
        testGetCounts();
    }
};

BEAST_DEFINE_TESTSUITE(GetCounts,rpc,ripple);

} // ripple

