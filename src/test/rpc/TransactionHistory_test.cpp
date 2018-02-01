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
#include <test/jtx/Env.h>
#include <test/jtx/envconfig.h>
#include <ripple/protocol/JsonFields.h>
#include <boost/container/static_vector.hpp>
#include <algorithm>

namespace ripple {

class TransactionHistory_test : public beast::unit_test::suite
{
    void
    testBadInput()
    {
        testcase("Invalid request params");
        using namespace test::jtx;
        Env env {*this, envconfig(no_admin)};

        {
            //no params
            auto const result = env.client()
                .invoke("tx_history", {})[jss::result];
            BEAST_EXPECT(result[jss::error] == "invalidParams");
            BEAST_EXPECT(result[jss::status] == "error");
        }

        {
            // test at 1 greater than the allowed non-admin limit
            Json::Value params {Json::objectValue};
            params[jss::start] = 10001; //limited to <= 10000 for non admin
            auto const result = env.client()
                .invoke("tx_history", params)[jss::result];
            BEAST_EXPECT(result[jss::error] == "noPermission");
            BEAST_EXPECT(result[jss::status] == "error");
        }
    }

    void testRequest()
    {
        testcase("Basic request");
        using namespace test::jtx;
        Env env {*this};

        // create enough transactions to provide some
        // history...
        size_t const numAccounts = 20;
        boost::container::static_vector<Account, numAccounts> accounts;
        for(size_t i = 0; i<numAccounts; ++i)
        {
            accounts.emplace_back("A" + std::to_string(i));
            auto const& acct=accounts.back();
            env.fund(XRP(10000), acct);
            env.close();
            if(i > 0)
            {
                auto const& prev=accounts[i-1];
                env.trust(acct["USD"](1000), prev);
                env(pay(acct, prev, acct["USD"](5)));
            }
            env(offer(acct, XRP(100), acct["USD"](1)));
            env.close();

            // verify the latest transaction in env (offer)
            // is available in tx_history.
            Json::Value params {Json::objectValue};
            params[jss::start] = 0;
            auto result =
                env.client().invoke("tx_history", params)[jss::result];
            if(! BEAST_EXPECT(result[jss::txs].isArrayorNull() &&
                    result[jss::txs].size() > 0))
                return;

            // search for a tx in history matching the last offer
            bool const txFound = [&] {
                auto const toFind = env.tx()->getJson(0);
                for (auto tx : result[jss::txs])
                {
                    tx.removeMember(jss::inLedger);
                    tx.removeMember(jss::ledger_index);
                    if (toFind == tx)
                        return true;
                }
                return false;
            }();
            BEAST_EXPECT(txFound);
        }

        unsigned int start = 0;
        unsigned int total = 0;
        // also summarize the transaction types in this map
        std::unordered_map<std::string, unsigned> typeCounts;
        while(start < 120)
        {
            Json::Value params {Json::objectValue};
            params[jss::start] = start;
            auto result =
                env.client().invoke("tx_history", params)[jss::result];
            if(! BEAST_EXPECT(result[jss::txs].isArrayorNull() &&
                    result[jss::txs].size() > 0))
                break;
            total += result[jss::txs].size();
            start += 20;
            for (auto const& t : result[jss::txs])
            {
                typeCounts[t[sfTransactionType.fieldName].asString()]++;
            }
        }
        BEAST_EXPECT(total == 117);
        BEAST_EXPECT(typeCounts["AccountSet"] == 20);
        BEAST_EXPECT(typeCounts["TrustSet"] == 19);
        BEAST_EXPECT(typeCounts["Payment"] == 58);
        BEAST_EXPECT(typeCounts["OfferCreate"] == 20);

        // also, try a request with max non-admin start value
        {
            Json::Value params {Json::objectValue};
            params[jss::start] = 10000; //limited to <= 10000 for non admin
            auto const result = env.client()
                .invoke("tx_history", params)[jss::result];
            BEAST_EXPECT(result[jss::status] == "success");
            BEAST_EXPECT(result[jss::index] == 10000);
        }
    }

public:
    void run ()
    {
        testBadInput();
        testRequest();
    }
};

BEAST_DEFINE_TESTSUITE (TransactionHistory, rpc, ripple);

}  // ripple
