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
#include <xrpld/app/rdb/backend/SQLiteDatabase.h>
#include <xrpld/rpc/CTID.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/STBase.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/protocol/serialize.h>

#include <optional>
#include <tuple>

namespace ripple {

namespace test {

class Simulate_test : public beast::unit_test::suite
{
    void
    checkBasicReturnValidity(Json::Value result)
    {
        BEAST_EXPECT(result[jss::applied] == false);
        BEAST_EXPECT(result.isMember(jss::engine_result));
        BEAST_EXPECT(result.isMember(jss::engine_result_code));
        BEAST_EXPECT(result.isMember(jss::engine_result_message));
    }

    void
    testParamErrors()
    {
        testcase("Test errors in the parameters");

        using namespace jtx;
        Env env(*this);

        {
            auto resp = env.rpc("json", "simulate");
            BEAST_EXPECT(resp[jss::error_message] == "Syntax error.");
        }

        {
            Json::Value params;
            params[jss::tx_json] = Json::objectValue;
            params[jss::tx_blob] = "1200";

            auto resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] == "Invalid parameters.");
        }

        {
            Json::Value params;
            params[jss::tx_blob] = "1200";
            params[jss::binary] = "100";
            auto resp = env.rpc("json", "simulate", to_string(params));
            BEAST_EXPECT(
                resp[jss::result][jss::error_message] == "Invalid parameters.");
        }
    }
    void
    testBasic()
    {
        testcase("Basic test");

        using namespace jtx;
        Env env(*this);

        {
            Json::Value tx;

            auto newDomain = "123ABC";

            tx[jss::Account] = Account::master.human();
            tx[jss::TransactionType] = jss::AccountSet;
            tx[sfDomain.jsonName] = newDomain;
            tx[sfSigningPubKey.jsonName] = "";
            tx[sfTxnSignature.jsonName] = "";
            tx[sfSequence.jsonName] = 1;
            tx[sfFee.jsonName] = "12";

            Json::Value params;
            params[jss::tx_json] = tx;

            auto resp = env.rpc("json", "simulate", to_string(params));
            auto result = resp[jss::result];
            checkBasicReturnValidity(result);

            BEAST_EXPECT(result[jss::engine_result] == "tesSUCCESS");
            BEAST_EXPECT(result[jss::engine_result_code] == 0);
            BEAST_EXPECT(
                result[jss::engine_result_message] ==
                "The simulated transaction would have been applied.");

            if (BEAST_EXPECT(result.isMember(jss::metadata)))
            {
                auto metadata = result[jss::metadata];
                if (BEAST_EXPECT(metadata.isMember(sfAffectedNodes.jsonName)))
                {
                    auto node = metadata[sfAffectedNodes.jsonName][0u];
                    if (BEAST_EXPECT(node.isMember(sfModifiedNode.jsonName)))
                    {
                        auto modifiedNode = node[sfModifiedNode.jsonName];
                        BEAST_EXPECT(
                            modifiedNode[sfLedgerEntryType.jsonName] ==
                            "AccountRoot");
                        auto finalFields = modifiedNode[sfFinalFields.jsonName];
                        BEAST_EXPECT(
                            finalFields[sfDomain.jsonName] == newDomain);
                    }
                }
                BEAST_EXPECT(metadata[sfTransactionIndex.jsonName] == 0);
                BEAST_EXPECT(
                    metadata[sfTransactionResult.jsonName] == "tesSUCCESS");
            }

            // TODO: check that the ledger wasn't affected
        }
    }

public:
    void
    run() override
    {
        testParamErrors();
        testBasic();
    }
};

BEAST_DEFINE_TESTSUITE(Simulate, rpc, ripple);

}  // namespace test

}  // namespace ripple
