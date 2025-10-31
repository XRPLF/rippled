//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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

#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

namespace test {

class ServerDefinitions_test : public beast::unit_test::suite
{
public:
    void
    testServerDefinitions()
    {
        testcase("server_definitions");

        using namespace test::jtx;

        {
            Env env(*this);
            auto const result = env.rpc("server_definitions");
            BEAST_EXPECT(!result[jss::result].isMember(jss::error));
            BEAST_EXPECT(result[jss::result][jss::status] == "success");
            BEAST_EXPECT(result[jss::result].isMember(jss::FIELDS));
            BEAST_EXPECT(result[jss::result].isMember(jss::LEDGER_ENTRY_TYPES));
            BEAST_EXPECT(
                result[jss::result].isMember(jss::TRANSACTION_RESULTS));
            BEAST_EXPECT(result[jss::result].isMember(jss::TRANSACTION_TYPES));
            BEAST_EXPECT(result[jss::result].isMember(jss::TYPES));
            BEAST_EXPECT(result[jss::result].isMember(jss::hash));

            // test a random element of each result
            // (testing the whole output would be difficult to maintain)

            {
                auto const firstField = result[jss::result][jss::FIELDS][0u];
                BEAST_EXPECT(firstField[0u].asString() == "Generic");
                BEAST_EXPECT(
                    firstField[1][jss::isSerialized].asBool() == false);
                BEAST_EXPECT(
                    firstField[1][jss::isSigningField].asBool() == false);
                BEAST_EXPECT(firstField[1][jss::isVLEncoded].asBool() == false);
                BEAST_EXPECT(firstField[1][jss::nth].asUInt() == 0);
                BEAST_EXPECT(firstField[1][jss::type].asString() == "Unknown");
            }

            BEAST_EXPECT(
                result[jss::result][jss::LEDGER_ENTRY_TYPES]["AccountRoot"]
                    .asUInt() == 97);
            BEAST_EXPECT(
                result[jss::result][jss::TRANSACTION_RESULTS]["tecDIR_FULL"]
                    .asUInt() == 121);
            BEAST_EXPECT(
                result[jss::result][jss::TRANSACTION_TYPES]["Payment"]
                    .asUInt() == 0);
            BEAST_EXPECT(
                result[jss::result][jss::TYPES]["AccountID"].asUInt() == 8);

            // check exception SFields
            {
                auto const fieldExists = [&](std::string name) {
                    for (auto& field : result[jss::result][jss::FIELDS])
                    {
                        if (field[0u].asString() == name)
                        {
                            return true;
                        }
                    }
                    return false;
                };
                BEAST_EXPECT(fieldExists("Generic"));
                BEAST_EXPECT(fieldExists("Invalid"));
                BEAST_EXPECT(fieldExists("ObjectEndMarker"));
                BEAST_EXPECT(fieldExists("ArrayEndMarker"));
                BEAST_EXPECT(fieldExists("taker_gets_funded"));
                BEAST_EXPECT(fieldExists("taker_pays_funded"));
                BEAST_EXPECT(fieldExists("hash"));
                BEAST_EXPECT(fieldExists("index"));
            }

            // test that base_uint types are replaced with "Hash" prefix
            {
                auto const types = result[jss::result][jss::TYPES];
                BEAST_EXPECT(types["Hash128"].asUInt() == 4);
                BEAST_EXPECT(types["Hash160"].asUInt() == 17);
                BEAST_EXPECT(types["Hash192"].asUInt() == 21);
                BEAST_EXPECT(types["Hash256"].asUInt() == 5);
                BEAST_EXPECT(types["Hash384"].asUInt() == 22);
                BEAST_EXPECT(types["Hash512"].asUInt() == 23);
            }
        }

        // test providing the same hash
        {
            Env env(*this);
            auto const firstResult = env.rpc("server_definitions");
            auto const hash = firstResult[jss::result][jss::hash].asString();
            auto const hashParam =
                std::string("{ ") + "\"hash\": \"" + hash + "\"}";

            auto const result =
                env.rpc("json", "server_definitions", hashParam);
            BEAST_EXPECT(!result[jss::result].isMember(jss::error));
            BEAST_EXPECT(result[jss::result][jss::status] == "success");
            BEAST_EXPECT(!result[jss::result].isMember(jss::FIELDS));
            BEAST_EXPECT(
                !result[jss::result].isMember(jss::LEDGER_ENTRY_TYPES));
            BEAST_EXPECT(
                !result[jss::result].isMember(jss::TRANSACTION_RESULTS));
            BEAST_EXPECT(!result[jss::result].isMember(jss::TRANSACTION_TYPES));
            BEAST_EXPECT(!result[jss::result].isMember(jss::TYPES));
            BEAST_EXPECT(result[jss::result].isMember(jss::hash));
        }

        // test providing a different hash
        {
            Env env(*this);
            std::string const hash =
                "54296160385A27154BFA70A239DD8E8FD4CC2DB7BA32D970BA3A5B132CF749"
                "D1";
            auto const hashParam =
                std::string("{ ") + "\"hash\": \"" + hash + "\"}";

            auto const result =
                env.rpc("json", "server_definitions", hashParam);
            BEAST_EXPECT(!result[jss::result].isMember(jss::error));
            BEAST_EXPECT(result[jss::result][jss::status] == "success");
            BEAST_EXPECT(result[jss::result].isMember(jss::FIELDS));
            BEAST_EXPECT(result[jss::result].isMember(jss::LEDGER_ENTRY_TYPES));
            BEAST_EXPECT(
                result[jss::result].isMember(jss::TRANSACTION_RESULTS));
            BEAST_EXPECT(result[jss::result].isMember(jss::TRANSACTION_TYPES));
            BEAST_EXPECT(result[jss::result].isMember(jss::TYPES));
            BEAST_EXPECT(result[jss::result].isMember(jss::hash));
        }
    }

    void
    run() override
    {
        testServerDefinitions();
    }
};

BEAST_DEFINE_TESTSUITE(ServerDefinitions, rpc, ripple);

}  // namespace test
}  // namespace ripple
