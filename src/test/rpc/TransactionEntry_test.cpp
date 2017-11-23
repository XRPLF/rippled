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
#include <ripple/protocol/JsonFields.h>

namespace ripple {

class TransactionEntry_test : public beast::unit_test::suite
{
    void
    testBadInput()
    {
        testcase("Invalid request params");
        using namespace test::jtx;
        Env env {*this};

        {
            //no params
            auto const result = env.client()
                .invoke("transaction_entry", {})[jss::result];
            BEAST_EXPECT(result[jss::error] == "fieldNotFoundTransaction");
            BEAST_EXPECT(result[jss::status] == "error");
        }

        {
            Json::Value params {Json::objectValue};
            params[jss::ledger] = 20;
            auto const result = env.client()
                .invoke("transaction_entry", params)[jss::result];
            BEAST_EXPECT(result[jss::error] == "lgrNotFound");
            BEAST_EXPECT(result[jss::status] == "error");
        }

        {
            Json::Value params {Json::objectValue};
            params[jss::ledger] = "current";
            params[jss::tx_hash] = "DEADBEEF";
            auto const result = env.client()
                .invoke("transaction_entry", params)[jss::result];
            BEAST_EXPECT(result[jss::error] == "notYetImplemented");
            BEAST_EXPECT(result[jss::status] == "error");
        }

        {
            Json::Value params {Json::objectValue};
            params[jss::ledger] = "closed";
            params[jss::tx_hash] = "DEADBEEF";
            auto const result = env.client()
                .invoke("transaction_entry", params)[jss::result];
            BEAST_EXPECT(! result[jss::ledger_hash].asString().empty());
            BEAST_EXPECT(result[jss::error] == "transactionNotFound");
            BEAST_EXPECT(result[jss::status] == "error");
        }

        std::string const txHash {
            "E2FE8D4AF3FCC3944DDF6CD8CDDC5E3F0AD50863EF8919AFEF10CB6408CD4D05"};

        // Command line format
        {
            // No arguments
            Json::Value const result {env.rpc ("transaction_entry")};
            BEAST_EXPECT(result[jss::ledger_hash].asString().empty());
            BEAST_EXPECT(result[jss::error] == "badSyntax");
            BEAST_EXPECT(result[jss::status] == "error");
        }

        {
            // One argument
            Json::Value const result {env.rpc ("transaction_entry", txHash)};
            BEAST_EXPECT(result[jss::error] == "badSyntax");
            BEAST_EXPECT(result[jss::status] == "error");
        }

        {
            // First argument with too few characters
            Json::Value const result {env.rpc (
                "transaction_entry", txHash.substr (1), "closed")};
            BEAST_EXPECT(result[jss::error] == "invalidParams");
            BEAST_EXPECT(result[jss::status] == "error");
        }

        {
            // First argument with too many characters
            Json::Value const result {env.rpc (
                "transaction_entry", txHash + "A", "closed")};
            BEAST_EXPECT(result[jss::error] == "invalidParams");
            BEAST_EXPECT(result[jss::status] == "error");
        }

        {
            // Second argument not valid
            Json::Value const result {env.rpc (
                "transaction_entry", txHash, "closer")};
            BEAST_EXPECT(result[jss::error] == "invalidParams");
            BEAST_EXPECT(result[jss::status] == "error");
        }

        {
            // Ledger index of 0 is not valid
            Json::Value const result {env.rpc (
                "transaction_entry", txHash, "0")};
            BEAST_EXPECT(result[jss::error] == "invalidParams");
            BEAST_EXPECT(result[jss::status] == "error");
        }

        {
            // Three arguments
            Json::Value const result {env.rpc (
                "transaction_entry", txHash, "closed", "extra")};
            BEAST_EXPECT(result[jss::error] == "badSyntax");
            BEAST_EXPECT(result[jss::status] == "error");
        }

        {
            // Valid structure, but transaction not found.
            Json::Value const result {env.rpc (
                "transaction_entry", txHash, "closed")};
            BEAST_EXPECT(
                ! result[jss::result][jss::ledger_hash].asString().empty());
            BEAST_EXPECT(
                result[jss::result][jss::error] == "transactionNotFound");
            BEAST_EXPECT(result[jss::result][jss::status] == "error");
        }
    }

    void testRequest()
    {
        testcase("Basic request");
        using namespace test::jtx;
        Env env {*this};

        auto check_tx = [this, &env]
            (int index, std::string const txhash, std::string const type = "")
            {
                // first request using ledger_index to lookup
                Json::Value const resIndex {[&env, index, &txhash] ()
                {
                    Json::Value params {Json::objectValue};
                    params[jss::ledger_index] = index;
                    params[jss::tx_hash] = txhash;
                    return env.client()
                        .invoke("transaction_entry", params)[jss::result];
                }()};

                if(! BEAST_EXPECTS(resIndex.isMember(jss::tx_json), txhash))
                    return;

                BEAST_EXPECT(resIndex[jss::tx_json][jss::hash] == txhash);
                if(! type.empty())
                {
                    BEAST_EXPECTS(
                        resIndex[jss::tx_json][jss::TransactionType] == type,
                        txhash + " is " +
                            resIndex[jss::tx_json][jss::TransactionType].asString());
                }

                // second request using ledger_hash to lookup and verify
                // both responses match
                {
                    Json::Value params {Json::objectValue};
                    params[jss::ledger_hash] = resIndex[jss::ledger_hash];
                    params[jss::tx_hash] = txhash;
                    Json::Value const resHash = env.client()
                        .invoke("transaction_entry", params)[jss::result];
                    BEAST_EXPECT(resHash == resIndex);
                }

                // Use the command line form with the index.
                {
                    Json::Value const clIndex {env.rpc (
                        "transaction_entry", txhash, std::to_string (index))};
                    BEAST_EXPECT (clIndex["result"] == resIndex);
                }

                // Use the command line form with the ledger_hash.
                {
                    Json::Value const clHash {env.rpc (
                        "transaction_entry", txhash,
                        resIndex[jss::ledger_hash].asString())};
                    BEAST_EXPECT (clHash["result"] == resIndex);
                }
            };

        Account A1 {"A1"};
        Account A2 {"A2"};

        env.fund(XRP(10000), A1);
        auto fund_1_tx =
            boost::lexical_cast<std::string>(env.tx()->getTransactionID());

        env.fund(XRP(10000), A2);
        auto fund_2_tx =
            boost::lexical_cast<std::string>(env.tx()->getTransactionID());

        env.close();

        // these are actually AccountSet txs because fund does two txs and
        // env.tx only reports the last one
        check_tx(env.closed()->seq(), fund_1_tx);
        check_tx(env.closed()->seq(), fund_2_tx);

        env.trust(A2["USD"](1000), A1);
        // the trust tx is actually a payment since the trust method
        // refunds fees with a payment after TrustSet..so just ignore the type
        // in the check below
        auto trust_tx =
            boost::lexical_cast<std::string>(env.tx()->getTransactionID());

        env(pay(A2, A1, A2["USD"](5)));
        auto pay_tx =
            boost::lexical_cast<std::string>(env.tx()->getTransactionID());
        env.close();

        check_tx(env.closed()->seq(), trust_tx);
        check_tx(env.closed()->seq(), pay_tx, "Payment");

        env(offer(A2, XRP(100), A2["USD"](1)));
        auto offer_tx =
            boost::lexical_cast<std::string>(env.tx()->getTransactionID());

        env.close();

        check_tx(env.closed()->seq(), offer_tx, "OfferCreate");
    }

public:
    void run ()
    {
        testBadInput();
        testRequest();
    }
};

BEAST_DEFINE_TESTSUITE (TransactionEntry, rpc, ripple);

}  // ripple
