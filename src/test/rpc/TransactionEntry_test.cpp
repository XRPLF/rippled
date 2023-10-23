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

#include <ripple/json/json_reader.h>
#include <ripple/json/json_value.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>
#include <test/jtx/Env.h>

namespace ripple {

class TransactionEntry_test : public beast::unit_test::suite
{
    void
    testBadInput()
    {
        testcase("Invalid request params");
        using namespace test::jtx;
        Env env{*this};

        {
            // no params
            auto const result =
                env.client().invoke("transaction_entry", {})[jss::result];
            BEAST_EXPECT(result[jss::error] == "fieldNotFoundTransaction");
            BEAST_EXPECT(result[jss::status] == "error");
        }

        {
            Json::Value params{Json::objectValue};
            params[jss::ledger] = 20;
            auto const result =
                env.client().invoke("transaction_entry", params)[jss::result];
            BEAST_EXPECT(result[jss::error] == "lgrNotFound");
            BEAST_EXPECT(result[jss::status] == "error");
        }

        {
            Json::Value params{Json::objectValue};
            params[jss::ledger] = "current";
            params[jss::tx_hash] = "DEADBEEF";
            auto const result =
                env.client().invoke("transaction_entry", params)[jss::result];
            BEAST_EXPECT(result[jss::error] == "notYetImplemented");
            BEAST_EXPECT(result[jss::status] == "error");
        }

        {
            Json::Value params{Json::objectValue};
            params[jss::ledger] = "closed";
            params[jss::tx_hash] = "DEADBEEF";
            auto const result =
                env.client().invoke("transaction_entry", params)[jss::result];
            BEAST_EXPECT(!result[jss::ledger_hash].asString().empty());
            BEAST_EXPECT(result[jss::error] == "malformedRequest");
            BEAST_EXPECT(result[jss::status] == "error");
        }

        std::string const txHash{
            "E2FE8D4AF3FCC3944DDF6CD8CDDC5E3F0AD50863EF8919AFEF10CB6408CD4D05"};

        // Command line format
        {
            // No arguments
            Json::Value const result{env.rpc("transaction_entry")};
            BEAST_EXPECT(result[jss::ledger_hash].asString().empty());
            BEAST_EXPECT(result[jss::error] == "badSyntax");
            BEAST_EXPECT(result[jss::status] == "error");
        }

        {
            // One argument
            Json::Value const result{env.rpc("transaction_entry", txHash)};
            BEAST_EXPECT(result[jss::error] == "badSyntax");
            BEAST_EXPECT(result[jss::status] == "error");
        }

        {
            // First argument with too few characters
            Json::Value const result{
                env.rpc("transaction_entry", txHash.substr(1), "closed")};
            BEAST_EXPECT(result[jss::error] == "invalidParams");
            BEAST_EXPECT(result[jss::status] == "error");
        }

        {
            // First argument with too many characters
            Json::Value const result{
                env.rpc("transaction_entry", txHash + "A", "closed")};
            BEAST_EXPECT(result[jss::error] == "invalidParams");
            BEAST_EXPECT(result[jss::status] == "error");
        }

        {
            // Second argument not valid
            Json::Value const result{
                env.rpc("transaction_entry", txHash, "closer")};
            BEAST_EXPECT(result[jss::error] == "invalidParams");
            BEAST_EXPECT(result[jss::status] == "error");
        }

        {
            // Ledger index of 0 is not valid
            Json::Value const result{env.rpc("transaction_entry", txHash, "0")};
            BEAST_EXPECT(result[jss::error] == "invalidParams");
            BEAST_EXPECT(result[jss::status] == "error");
        }

        {
            // Three arguments
            Json::Value const result{
                env.rpc("transaction_entry", txHash, "closed", "extra")};
            BEAST_EXPECT(result[jss::error] == "badSyntax");
            BEAST_EXPECT(result[jss::status] == "error");
        }

        {
            // Valid structure, but transaction not found.
            Json::Value const result{
                env.rpc("transaction_entry", txHash, "closed")};
            BEAST_EXPECT(
                !result[jss::result][jss::ledger_hash].asString().empty());
            BEAST_EXPECT(
                result[jss::result][jss::error] == "transactionNotFound");
            BEAST_EXPECT(result[jss::result][jss::status] == "error");
        }
    }

    void
    testRequest()
    {
        testcase("Basic request");
        using namespace test::jtx;
        Env env{*this};

        auto check_tx = [this, &env](
                            int index,
                            std::string const txhash,
                            std::string const expected_json = "") {
            // first request using ledger_index to lookup
            Json::Value const resIndex{[&env, index, &txhash]() {
                Json::Value params{Json::objectValue};
                params[jss::ledger_index] = index;
                params[jss::tx_hash] = txhash;
                return env.client().invoke(
                    "transaction_entry", params)[jss::result];
            }()};

            if (!BEAST_EXPECTS(resIndex.isMember(jss::tx_json), txhash))
                return;

            BEAST_EXPECT(resIndex[jss::tx_json][jss::hash] == txhash);
            if (!expected_json.empty())
            {
                Json::Value expected;
                Json::Reader().parse(expected_json, expected);
                if (RPC::contains_error(expected))
                    Throw<std::runtime_error>(
                        "Internal JSONRPC_test error.  Bad test JSON.");

                for (auto memberIt = expected.begin();
                     memberIt != expected.end();
                     memberIt++)
                {
                    auto const name = memberIt.memberName();
                    if (BEAST_EXPECT(resIndex[jss::tx_json].isMember(name)))
                    {
                        auto const received = resIndex[jss::tx_json][name];
                        BEAST_EXPECTS(
                            received == *memberIt,
                            txhash + " contains \n\"" + name + "\": "  //
                                + to_string(received)                  //
                                + " but expected "                     //
                                + to_string(expected));
                    }
                }
            }

            // second request using ledger_hash to lookup and verify
            // both responses match
            {
                Json::Value params{Json::objectValue};
                params[jss::ledger_hash] = resIndex[jss::ledger_hash];
                params[jss::tx_hash] = txhash;
                Json::Value const resHash = env.client().invoke(
                    "transaction_entry", params)[jss::result];
                BEAST_EXPECT(resHash == resIndex);
            }

            // Use the command line form with the index.
            {
                Json::Value const clIndex{env.rpc(
                    "transaction_entry", txhash, std::to_string(index))};
                BEAST_EXPECT(clIndex["result"] == resIndex);
            }

            // Use the command line form with the ledger_hash.
            {
                Json::Value const clHash{env.rpc(
                    "transaction_entry",
                    txhash,
                    resIndex[jss::ledger_hash].asString())};
                BEAST_EXPECT(clHash["result"] == resIndex);
            }
        };

        Account A1{"A1"};
        Account A2{"A2"};

        env.fund(XRP(10000), A1);
        auto fund_1_tx =
            boost::lexical_cast<std::string>(env.tx()->getTransactionID());

        env.fund(XRP(10000), A2);
        auto fund_2_tx =
            boost::lexical_cast<std::string>(env.tx()->getTransactionID());

        env.close();

        // these are actually AccountSet txs because fund does two txs and
        // env.tx only reports the last one
        check_tx(env.closed()->seq(), fund_1_tx, R"(
{
    "Account" : "r4nmQNH4Fhjfh6cHDbvVSsBv7KySbj4cBf",
    "Fee" : "10",
    "Sequence" : 3,
    "SetFlag" : 8,
    "SigningPubKey" : "0324CAAFA2212D2AEAB9D42D481535614AED486293E1FB1380FF070C3DD7FB4264",
    "TransactionType" : "AccountSet",
    "TxnSignature" : "3044022007B35E3B99460534FF6BC3A66FBBA03591C355CC38E38588968E87CCD01BE229022071A443026DE45041B55ABB1CC76812A87EA701E475BBB7E165513B4B242D3474",
    "hash" : "F4E9DF90D829A9E8B423FF68C34413E240D8D8BB0EFD080DF08114ED398E2506"
}
)");
        check_tx(env.closed()->seq(), fund_2_tx, R"(
{
    "Account" : "rGpeQzUWFu4fMhJHZ1Via5aqFC3A5twZUD",
    "Fee" : "10",
    "Sequence" : 3,
    "SetFlag" : 8,
    "SigningPubKey" : "03CFF28E067A2CCE6CC5A598C0B845CBD3F30A7863BE9C0DD55F4960EFABCCF4D0",
    "TransactionType" : "AccountSet",
    "TxnSignature" : "3045022100C8857FC0759A2AC0D2F320684691A66EAD252EAED9EF88C79791BC58BFCC9D860220421722286487DD0ED6BBA626CE6FCBDD14289F7F4726870C3465A4054C2702D7",
    "hash" : "6853CD8226A05068C951CB1F54889FF4E40C5B440DC1C5BA38F114C4E0B1E705"
}
)");

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

        check_tx(env.closed()->seq(), trust_tx, R"(
{
    "Account" : "rHb9CJAWyB4rj91VRWn96DkukG4bwdtyTh",
    "DeliverMax" : "10",
    "Destination" : "r4nmQNH4Fhjfh6cHDbvVSsBv7KySbj4cBf",
    "Fee" : "10",
    "Flags" : 2147483648,
    "Sequence" : 3,
    "SigningPubKey" : "0330E7FC9D56BB25D6893BA3F317AE5BCF33B3291BD63DB32654A313222F7FD020",
    "TransactionType" : "Payment",
    "TxnSignature" : "3044022033D9EBF7F02950AF2F6B13C07AEE641C8FEBDD540A338FCB9027A965A4AED35B02206E4E227DCC226A3456C0FEF953449D21645A24EB63CA0BB7C5B62470147FD1D1",
    "hash" : "C992D97D88FF444A1AB0C06B27557EC54B7F7DA28254778E60238BEA88E0C101"
}
)");

        check_tx(
            env.closed()->seq(),
            pay_tx,
            R"(
{
    "Account" : "rGpeQzUWFu4fMhJHZ1Via5aqFC3A5twZUD",
    "DeliverMax" :
    {
        "currency" : "USD",
        "issuer" : "rGpeQzUWFu4fMhJHZ1Via5aqFC3A5twZUD",
        "value" : "5"
    },
    "Destination" : "r4nmQNH4Fhjfh6cHDbvVSsBv7KySbj4cBf",
    "Fee" : "10",
    "Flags" : 2147483648,
    "Sequence" : 4,
    "SigningPubKey" : "03CFF28E067A2CCE6CC5A598C0B845CBD3F30A7863BE9C0DD55F4960EFABCCF4D0",
    "TransactionType" : "Payment",
    "TxnSignature" : "30450221008A722B7F16EDB2348886E88ED4EC682AE9973CC1EE0FF37C93BB2CEC821D3EDF022059E464472031BA5E0D88A93E944B6A8B8DB3E1D5E5D1399A805F615789DB0BED",
    "hash" : "988046D484ACE9F5F6A8C792D89C6EA2DB307B5DDA9864AEBA88E6782ABD0865"
}
)");

        env(offer(A2, XRP(100), A2["USD"](1)));
        auto offer_tx =
            boost::lexical_cast<std::string>(env.tx()->getTransactionID());

        env.close();
        check_tx(
            env.closed()->seq(),
            offer_tx,
            R"(
{
    "Account" : "rGpeQzUWFu4fMhJHZ1Via5aqFC3A5twZUD",
    "Fee" : "10",
    "Sequence" : 5,
    "SigningPubKey" : "03CFF28E067A2CCE6CC5A598C0B845CBD3F30A7863BE9C0DD55F4960EFABCCF4D0",
    "TakerGets" :
    {
        "currency" : "USD",
        "issuer" : "rGpeQzUWFu4fMhJHZ1Via5aqFC3A5twZUD",
        "value" : "1"
    },
    "TakerPays" : "100000000",
    "TransactionType" : "OfferCreate",
    "TxnSignature" : "304502210093FC93ACB77B4E3DE3315441BD010096734859080C1797AB735EB47EBD541BD102205020BB1A7C3B4141279EE4C287C13671E2450EA78914EFD0C6DB2A18344CD4F2",
    "hash" : "5FCC1A27A7664F82A0CC4BE5766FBBB7C560D52B93AA7B550CD33B27AEC7EFFB"
}
)");
    }

public:
    void
    run() override
    {
        testBadInput();
        testRequest();
    }
};

BEAST_DEFINE_TESTSUITE(TransactionEntry, rpc, ripple);

}  // namespace ripple
