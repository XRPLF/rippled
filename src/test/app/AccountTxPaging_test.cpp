//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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
#include <ripple/protocol/SField.h>
#include <ripple/protocol/JsonFields.h>
#include <cstdlib>

namespace ripple {

class AccountTxPaging_test : public beast::unit_test::suite
{
    bool
    checkTransaction (Json::Value const& tx, int sequence, int ledger)
    {
        return (
            tx[jss::tx][jss::Sequence].asInt() == sequence &&
            tx[jss::tx][jss::ledger_index].asInt() == ledger);
    }

    auto next(
        test::jtx::Env & env,
        test::jtx::Account const& account,
        int ledger_min,
        int ledger_max,
        int limit,
        bool forward,
        Json::Value const& marker = Json::Value{})
    {
        Json::Value jvc;
        jvc[jss::account] = account.human();
        jvc[jss::ledger_index_min] = ledger_min;
        jvc[jss::ledger_index_max] = ledger_max;
        jvc[jss::forward] = forward;
        jvc[jss::limit] = limit;
        if (marker)
            jvc[jss::marker] = marker;

        return env.rpc("json", "account_tx", to_string(jvc))[jss::result];
    }

    void
    testAccountTxPaging ()
    {
        testcase("Paging for Single Account");
        using namespace test::jtx;

        Env env(*this);
        Account A1 {"A1"};
        Account A2 {"A2"};
        Account A3 {"A3"};

        env.fund(XRP(10000), A1, A2, A3);
        env.close();

        env.trust(A3["USD"](1000), A1);
        env.trust(A2["USD"](1000), A1);
        env.trust(A3["USD"](1000), A2);
        env.close();

        for (auto i = 0; i < 5; ++i)
        {
            env(pay(A2, A1, A2["USD"](2)));
            env(pay(A3, A1, A3["USD"](2)));
            env(offer(A1, XRP(11), A1["USD"](1)));
            env(offer(A2, XRP(10), A2["USD"](1)));
            env(offer(A3, XRP(9),  A3["USD"](1)));
            env.close();
        }

        /* The sequence/ledger for A3 are as follows:
         * seq     ledger_index
         * 3  ----> 3
         * 1  ----> 3
         * 2  ----> 4
         * 2  ----> 4
         * 2  ----> 5
         * 3  ----> 5
         * 4  ----> 6
         * 5  ----> 6
         * 6  ----> 7
         * 7  ----> 7
         * 8  ----> 8
         * 9  ----> 8
         * 10 ----> 9
         * 11 ----> 9
        */

        // page through the results in several ways.
        {
            // limit = 2, 3 batches giving the first 6 txs
            auto jrr = next(env, A3, 2, 5, 2, true);
            auto txs = jrr[jss::transactions];
            if (! BEAST_EXPECT(txs.isArray() && txs.size() == 2))
                return;
            BEAST_EXPECT(checkTransaction (txs[0u], 3, 3));
            BEAST_EXPECT(checkTransaction (txs[1u], 1, 3));
            if(! BEAST_EXPECT(jrr[jss::marker]))
                return;

            jrr = next(env, A3, 2, 5, 2, true, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (! BEAST_EXPECT(txs.isArray() && txs.size() == 2))
                return;
            BEAST_EXPECT(checkTransaction (txs[0u], 2, 4));
            BEAST_EXPECT(checkTransaction (txs[1u], 2, 4));
            if(! BEAST_EXPECT(jrr[jss::marker]))
                return;

            jrr = next(env, A3, 2, 5, 2, true, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (! BEAST_EXPECT(txs.isArray() && txs.size() == 2))
                return;
            BEAST_EXPECT(checkTransaction (txs[0u], 2, 5));
            BEAST_EXPECT(checkTransaction (txs[1u], 3, 5));
            BEAST_EXPECT(! jrr[jss::marker]);
        }

        {
            // limit 1, 3 requests giving the first 3 txs
            auto jrr = next(env, A3, 3, 9, 1, true);
            auto txs = jrr[jss::transactions];
            if (! BEAST_EXPECT(txs.isArray() && txs.size() == 1))
                return;
            BEAST_EXPECT(checkTransaction (txs[0u], 3, 3));
            if(! BEAST_EXPECT(jrr[jss::marker]))
                return;

            jrr = next(env, A3, 3, 9, 1, true, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (! BEAST_EXPECT(txs.isArray() && txs.size() == 1))
                return;
            BEAST_EXPECT(checkTransaction (txs[0u], 1, 3));
            if(! BEAST_EXPECT(jrr[jss::marker]))
                return;

            jrr = next(env, A3, 3, 9, 1, true, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (! BEAST_EXPECT(txs.isArray() && txs.size() == 1))
                return;
            BEAST_EXPECT(checkTransaction (txs[0u], 2, 4));
            if(! BEAST_EXPECT(jrr[jss::marker]))
                return;

            // continue with limit 3, to end of all txs
            jrr = next(env, A3, 3, 9, 3, true, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (! BEAST_EXPECT(txs.isArray() && txs.size() == 3))
                return;
            BEAST_EXPECT(checkTransaction (txs[0u], 2, 4));
            BEAST_EXPECT(checkTransaction (txs[1u], 2, 5));
            BEAST_EXPECT(checkTransaction (txs[2u], 3, 5));
            if(! BEAST_EXPECT(jrr[jss::marker]))
                return;

            jrr = next(env, A3, 3, 9, 3, true, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (! BEAST_EXPECT(txs.isArray() && txs.size() == 3))
                return;
            BEAST_EXPECT(checkTransaction (txs[0u], 4, 6));
            BEAST_EXPECT(checkTransaction (txs[1u], 5, 6));
            BEAST_EXPECT(checkTransaction (txs[2u], 6, 7));
            if(! BEAST_EXPECT(jrr[jss::marker]))
                return;

            jrr = next(env, A3, 3, 9, 3, true, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (! BEAST_EXPECT(txs.isArray() && txs.size() == 3))
                return;
            BEAST_EXPECT(checkTransaction (txs[0u], 7, 7));
            BEAST_EXPECT(checkTransaction (txs[1u], 8, 8));
            BEAST_EXPECT(checkTransaction (txs[2u], 9, 8));
            if(! BEAST_EXPECT(jrr[jss::marker]))
                return;

            jrr = next(env, A3, 3, 9, 3, true, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (! BEAST_EXPECT(txs.isArray() && txs.size() == 2))
                return;
            BEAST_EXPECT(checkTransaction (txs[0u], 10, 9));
            BEAST_EXPECT(checkTransaction (txs[1u], 11, 9));
            BEAST_EXPECT(! jrr[jss::marker]);
        }

        {
            // limit 2, descending, 2 batches giving last 4 txs
            auto jrr = next(env, A3, 3, 9, 2, false);
            auto txs = jrr[jss::transactions];
            if (! BEAST_EXPECT(txs.isArray() && txs.size() == 2))
                return;
            BEAST_EXPECT(checkTransaction (txs[0u], 11, 9));
            BEAST_EXPECT(checkTransaction (txs[1u], 10, 9));
            if(! BEAST_EXPECT(jrr[jss::marker]))
                return;

            jrr = next(env, A3, 3, 9, 2, false, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (! BEAST_EXPECT(txs.isArray() && txs.size() == 2))
                return;
            BEAST_EXPECT(checkTransaction (txs[0u], 9, 8));
            BEAST_EXPECT(checkTransaction (txs[1u], 8, 8));
            if(! BEAST_EXPECT(jrr[jss::marker]))
                return;

            // continue with limit 3 until all txs have been seen
            jrr = next(env, A3, 3, 9, 3, false, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (! BEAST_EXPECT(txs.isArray() && txs.size() == 3))
                return;
            BEAST_EXPECT(checkTransaction (txs[0u], 7, 7));
            BEAST_EXPECT(checkTransaction (txs[1u], 6, 7));
            BEAST_EXPECT(checkTransaction (txs[2u], 5, 6));
            if(! BEAST_EXPECT(jrr[jss::marker]))
                return;

            jrr = next(env, A3, 3, 9, 3, false, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (! BEAST_EXPECT(txs.isArray() && txs.size() == 3))
                return;
            BEAST_EXPECT(checkTransaction (txs[0u], 4, 6));
            BEAST_EXPECT(checkTransaction (txs[1u], 3, 5));
            BEAST_EXPECT(checkTransaction (txs[2u], 2, 5));
            if(! BEAST_EXPECT(jrr[jss::marker]))
                return;

            jrr = next(env, A3, 3, 9, 3, false, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (! BEAST_EXPECT(txs.isArray() && txs.size() == 3))
                return;
            BEAST_EXPECT(checkTransaction (txs[0u], 2, 4));
            BEAST_EXPECT(checkTransaction (txs[1u], 2, 4));
            BEAST_EXPECT(checkTransaction (txs[2u], 1, 3));
            if(! BEAST_EXPECT(jrr[jss::marker]))
                return;

            jrr = next(env, A3, 3, 9, 3, false, jrr[jss::marker]);
            txs = jrr[jss::transactions];
            if (! BEAST_EXPECT(txs.isArray() && txs.size() == 1))
                return;
            BEAST_EXPECT(checkTransaction (txs[0u], 3, 3));
            BEAST_EXPECT(! jrr[jss::marker]);
        }
    }

public:
    void
    run() override
    {
        testAccountTxPaging();
    }
};

BEAST_DEFINE_TESTSUITE(AccountTxPaging,app,ripple);

}

