//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2017 Ripple Labs Inc.

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

#include <ripple/beast/unit_test.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>

namespace ripple {

namespace test {

class AccountTX_test : public beast::unit_test::suite
{
    void
    testParameters()
    {
        using namespace test::jtx;

        Env env(*this);
        Account A1{"A1"};
        env.fund(XRP(10000), A1);
        env.close();

        // Ledger 3 has the two txs associated with funding the account
        // All other ledgers have no txs

        auto hasTxs = [](Json::Value const& j) {
            return j.isMember(jss::result) &&
                (j[jss::result][jss::status] == "success") &&
                (j[jss::result][jss::transactions].size() == 2) &&
                (j[jss::result][jss::transactions][0u][jss::tx]
                  [jss::TransactionType] == "AccountSet") &&
                (j[jss::result][jss::transactions][1u][jss::tx]
                  [jss::TransactionType] == "Payment");
        };

        auto noTxs = [](Json::Value const& j) {
            return j.isMember(jss::result) &&
                (j[jss::result][jss::status] == "success") &&
                (j[jss::result][jss::transactions].size() == 0);
        };

        auto isErr = [](Json::Value const& j, error_code_i code) {
            return j.isMember(jss::result) &&
                j[jss::result].isMember(jss::error) &&
                j[jss::result][jss::error] == RPC::get_error_info(code).token;
        };

        Json::Value jParms;

        BEAST_EXPECT(isErr(
            env.rpc("json", "account_tx", to_string(jParms)),
            rpcINVALID_PARAMS));

        jParms[jss::account] = "0xDEADBEEF";

        BEAST_EXPECT(isErr(
            env.rpc("json", "account_tx", to_string(jParms)),
            rpcACT_MALFORMED));

        jParms[jss::account] = A1.human();
        BEAST_EXPECT(hasTxs(env.rpc("json", "account_tx", to_string(jParms))));

        // Ledger min/max index
        {
            Json::Value p{jParms};
            p[jss::ledger_index_min] = -1;
            p[jss::ledger_index_max] = -1;
            BEAST_EXPECT(hasTxs(env.rpc("json", "account_tx", to_string(p))));

            p[jss::ledger_index_min] = 0;
            p[jss::ledger_index_max] = 100;
            BEAST_EXPECT(hasTxs(env.rpc("json", "account_tx", to_string(p))));

            p[jss::ledger_index_min] = 1;
            p[jss::ledger_index_max] = 2;
            BEAST_EXPECT(noTxs(env.rpc("json", "account_tx", to_string(p))));

            p[jss::ledger_index_min] = 2;
            p[jss::ledger_index_max] = 1;
            BEAST_EXPECT(isErr(
                env.rpc("json", "account_tx", to_string(p)),
                rpcLGR_IDXS_INVALID));
        }

        // Ledger index min only
        {
            Json::Value p{jParms};
            p[jss::ledger_index_min] = -1;
            BEAST_EXPECT(hasTxs(env.rpc("json", "account_tx", to_string(p))));

            p[jss::ledger_index_min] = 1;
            BEAST_EXPECT(hasTxs(env.rpc("json", "account_tx", to_string(p))));

            p[jss::ledger_index_min] = env.current()->info().seq;
            BEAST_EXPECT(isErr(
                env.rpc("json", "account_tx", to_string(p)),
                rpcLGR_IDXS_INVALID));
        }

        // Ledger index max only
        {
            Json::Value p{jParms};
            p[jss::ledger_index_max] = -1;
            BEAST_EXPECT(hasTxs(env.rpc("json", "account_tx", to_string(p))));

            p[jss::ledger_index_max] = env.current()->info().seq;
            BEAST_EXPECT(hasTxs(env.rpc("json", "account_tx", to_string(p))));

            p[jss::ledger_index_max] = env.closed()->info().seq;
            BEAST_EXPECT(hasTxs(env.rpc("json", "account_tx", to_string(p))));

            p[jss::ledger_index_max] = env.closed()->info().seq - 1 ;
            BEAST_EXPECT(noTxs(env.rpc("json", "account_tx", to_string(p))));
        }

        // Ledger Sequence
        {
            Json::Value p{jParms};

            p[jss::ledger_index] = env.closed()->info().seq;
            BEAST_EXPECT(hasTxs(env.rpc("json", "account_tx", to_string(p))));

            p[jss::ledger_index] = env.closed()->info().seq - 1;
            BEAST_EXPECT(noTxs(env.rpc("json", "account_tx", to_string(p))));

            p[jss::ledger_index] = env.current()->info().seq;
            BEAST_EXPECT(isErr(
                env.rpc("json", "account_tx", to_string(p)),
                rpcLGR_NOT_VALIDATED));

            p[jss::ledger_index] = env.current()->info().seq + 1;
            BEAST_EXPECT(isErr(
                env.rpc("json", "account_tx", to_string(p)),
                rpcLGR_NOT_FOUND));
        }

        // Ledger Hash
        {
            Json::Value p{jParms};

            p[jss::ledger_hash] = to_string(env.closed()->info().hash);
            BEAST_EXPECT(hasTxs(env.rpc("json", "account_tx", to_string(p))));

            p[jss::ledger_hash] = to_string(env.closed()->info().parentHash);
            BEAST_EXPECT(noTxs(env.rpc("json", "account_tx", to_string(p))));
        }
    }

public:
    void
    run() override
    {
        testParameters();
    }
};
BEAST_DEFINE_TESTSUITE(AccountTX, app, ripple);

}  // namespace test

}  // namespace ripple
