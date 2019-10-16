//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2020 Ripple Labs Inc.

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

#include <ripple/protocol/Feature.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>
#include <test/jtx/WSClient.h>

#include <ripple/resource/Charge.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/GRPCHandlers.h>
#include <test/rpc/GRPCTestClientBase.h>

namespace ripple {
namespace test {

class Submit_test : public beast::unit_test::suite
{
public:
    class SubmitClient : public GRPCTestClientBase
    {
    public:
        rpc::v1::SubmitTransactionRequest request;
        rpc::v1::SubmitTransactionResponse reply;

        explicit SubmitClient(std::string const& port)
            : GRPCTestClientBase(port)
        {
        }

        void
        SubmitTransaction()
        {
            status = stub_->SubmitTransaction(&context, request, &reply);
        }
    };

    struct TestData
    {
        std::string xrpTxBlob;
        std::string xrpTxHash;
        std::string usdTxBlob;
        std::string usdTxHash;
        const static int fund = 10000;
    } testData;

    void
    fillTestData()
    {
        testcase("fill test data");

        using namespace jtx;
        Env env(*this, envconfig(addGrpcConfig));
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        env.fund(XRP(TestData::fund), "alice", "bob");
        env.trust(bob["USD"](TestData::fund), alice);
        env.close();

        auto toBinary = [](std::string const& text) {
            std::string binary;
            for (size_t i = 0; i < text.size(); ++i)
            {
                unsigned int c = charUnHex(text[i]);
                c = c << 4;
                ++i;
                c = c | charUnHex(text[i]);
                binary.push_back(c);
            }

            return binary;
        };

        // use a websocket client to fill transaction blobs
        auto wsc = makeWSClient(env.app().config());
        {
            Json::Value jrequestXrp;
            jrequestXrp[jss::secret] = toBase58(generateSeed("alice"));
            jrequestXrp[jss::tx_json] =
                pay("alice", "bob", XRP(TestData::fund / 2));
            Json::Value jreply_xrp = wsc->invoke("sign", jrequestXrp);

            if (!BEAST_EXPECT(jreply_xrp.isMember(jss::result)))
                return;
            if (!BEAST_EXPECT(jreply_xrp[jss::result].isMember(jss::tx_blob)))
                return;
            testData.xrpTxBlob =
                toBinary(jreply_xrp[jss::result][jss::tx_blob].asString());
            if (!BEAST_EXPECT(jreply_xrp[jss::result].isMember(jss::tx_json)))
                return;
            if (!BEAST_EXPECT(
                    jreply_xrp[jss::result][jss::tx_json].isMember(jss::hash)))
                return;
            testData.xrpTxHash = toBinary(
                jreply_xrp[jss::result][jss::tx_json][jss::hash].asString());
        }
        {
            Json::Value jrequestUsd;
            jrequestUsd[jss::secret] = toBase58(generateSeed("bob"));
            jrequestUsd[jss::tx_json] =
                pay("bob", "alice", bob["USD"](TestData::fund / 2));
            Json::Value jreply_usd = wsc->invoke("sign", jrequestUsd);

            if (!BEAST_EXPECT(jreply_usd.isMember(jss::result)))
                return;
            if (!BEAST_EXPECT(jreply_usd[jss::result].isMember(jss::tx_blob)))
                return;
            testData.usdTxBlob =
                toBinary(jreply_usd[jss::result][jss::tx_blob].asString());
            if (!BEAST_EXPECT(jreply_usd[jss::result].isMember(jss::tx_json)))
                return;
            if (!BEAST_EXPECT(
                    jreply_usd[jss::result][jss::tx_json].isMember(jss::hash)))
                return;
            testData.usdTxHash = toBinary(
                jreply_usd[jss::result][jss::tx_json][jss::hash].asString());
        }
    }

    void
    testSubmitGoodBlobGrpc()
    {
        testcase("Submit good blobs, XRP, USD, and same transaction twice");

        using namespace jtx;
        std::unique_ptr<Config> config = envconfig(addGrpcConfig);
        std::string grpcPort = *(*config)["port_grpc"].get<std::string>("port");
        Env env(*this, std::move(config));
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        env.fund(XRP(TestData::fund), "alice", "bob");
        env.trust(bob["USD"](TestData::fund), alice);
        env.close();

        auto getClient = [&grpcPort]() { return SubmitClient(grpcPort); };

        // XRP
        {
            auto client = getClient();
            client.request.set_signed_transaction(testData.xrpTxBlob);
            client.SubmitTransaction();
            if (!BEAST_EXPECT(client.status.ok()))
            {
                return;
            }
            BEAST_EXPECT(client.reply.engine_result().result() == "tesSUCCESS");
            BEAST_EXPECT(client.reply.engine_result_code() == 0);
            BEAST_EXPECT(client.reply.hash() == testData.xrpTxHash);
        }
        // USD
        {
            auto client = getClient();
            client.request.set_signed_transaction(testData.usdTxBlob);
            client.SubmitTransaction();
            if (!BEAST_EXPECT(client.status.ok()))
            {
                return;
            }
            BEAST_EXPECT(client.reply.engine_result().result() == "tesSUCCESS");
            BEAST_EXPECT(client.reply.engine_result_code() == 0);
            BEAST_EXPECT(client.reply.hash() == testData.usdTxHash);
        }
        // USD, error, same transaction again
        {
            auto client = getClient();
            client.request.set_signed_transaction(testData.usdTxBlob);
            client.SubmitTransaction();
            if (!BEAST_EXPECT(client.status.ok()))
            {
                return;
            }
            BEAST_EXPECT(client.reply.engine_result().result() == "tefALREADY");
            BEAST_EXPECT(client.reply.engine_result_code() == -198);
        }
    }

    void
    testSubmitErrorBlobGrpc()
    {
        testcase("Submit error, bad blob, no account");

        using namespace jtx;
        std::unique_ptr<Config> config = envconfig(addGrpcConfig);
        std::string grpcPort = *(*config)["port_grpc"].get<std::string>("port");
        Env env(*this, std::move(config));

        auto getClient = [&grpcPort]() { return SubmitClient(grpcPort); };

        // short transaction blob, cannot parse
        {
            auto client = getClient();
            client.request.set_signed_transaction("deadbeef");
            client.SubmitTransaction();
            BEAST_EXPECT(!client.status.ok());
        }
        // bad blob with correct length, cannot parse
        {
            auto client = getClient();
            auto xrpTxBlobCopy(testData.xrpTxBlob);
            std::reverse(xrpTxBlobCopy.begin(), xrpTxBlobCopy.end());
            client.request.set_signed_transaction(xrpTxBlobCopy);
            client.SubmitTransaction();
            BEAST_EXPECT(!client.status.ok());
        }
        // good blob, can parse but no account
        {
            auto client = getClient();
            client.request.set_signed_transaction(testData.xrpTxBlob);
            client.SubmitTransaction();
            if (!BEAST_EXPECT(client.status.ok()))
            {
                return;
            }
            BEAST_EXPECT(
                client.reply.engine_result().result() == "terNO_ACCOUNT");
            BEAST_EXPECT(client.reply.engine_result_code() == -96);
        }
    }

    void
    testSubmitInsufficientFundsGrpc()
    {
        testcase("Submit good blobs but insufficient funds");

        using namespace jtx;
        std::unique_ptr<Config> config = envconfig(addGrpcConfig);
        std::string grpcPort = *(*config)["port_grpc"].get<std::string>("port");
        Env env(*this, std::move(config));

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        // fund 1000 (TestData::fund/10) XRP, the transaction sends 5000
        // (TestData::fund/2) XRP, so insufficient funds
        env.fund(XRP(TestData::fund / 10), "alice", "bob");
        env.trust(bob["USD"](TestData::fund), alice);
        env.close();

        {
            SubmitClient client(grpcPort);
            client.request.set_signed_transaction(testData.xrpTxBlob);
            client.SubmitTransaction();
            if (!BEAST_EXPECT(client.status.ok()))
            {
                return;
            }
            BEAST_EXPECT(
                client.reply.engine_result().result() == "tecUNFUNDED_PAYMENT");
            BEAST_EXPECT(client.reply.engine_result_code() == 104);
        }
    }

    void
    run() override
    {
        fillTestData();
        testSubmitGoodBlobGrpc();
        testSubmitErrorBlobGrpc();
        testSubmitInsufficientFundsGrpc();
    }
};

BEAST_DEFINE_TESTSUITE(Submit, app, ripple);

}  // namespace test
}  // namespace ripple
