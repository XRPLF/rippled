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

#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/misc/TxQ.h>
#include <ripple/basics/mulDiv.h>
#include <ripple/protocol/ErrorCodes.h>
#include <ripple/protocol/jss.h>
#include <ripple/rpc/GRPCHandlers.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <test/jtx.h>
#include <test/jtx/Env.h>
#include <test/jtx/envconfig.h>
#include <test/rpc/GRPCTestClientBase.h>

namespace ripple {
namespace test {

class Fee_test : public beast::unit_test::suite
{
    class GrpcFeeClient : public GRPCTestClientBase
    {
    public:
        org::xrpl::rpc::v1::GetFeeRequest request;
        org::xrpl::rpc::v1::GetFeeResponse reply;

        explicit GrpcFeeClient(std::string const& grpcPort)
            : GRPCTestClientBase(grpcPort)
        {
        }

        void
        GetFee()
        {
            status = stub_->GetFee(&context, request, &reply);
        }
    };

    std::pair<bool, org::xrpl::rpc::v1::GetFeeResponse>
    grpcGetFee(std::string const& grpcPort)
    {
        GrpcFeeClient client(grpcPort);
        client.GetFee();
        return std::pair<bool, org::xrpl::rpc::v1::GetFeeResponse>(
            client.status.ok(), client.reply);
    }

    void
    testFeeGrpc()
    {
        testcase("Test Fee Grpc");

        using namespace test::jtx;
        std::unique_ptr<Config> config = envconfig(addGrpcConfig);
        std::string grpcPort = *(*config)["port_grpc"].get<std::string>("port");
        Env env(*this, std::move(config));
        Account A1{"A1"};
        Account A2{"A2"};
        env.fund(XRP(10000), A1);
        env.fund(XRP(10000), A2);
        env.close();
        env.trust(A2["USD"](1000), A1);
        env.close();
        for (int i = 0; i < 7; ++i)
        {
            env(pay(A2, A1, A2["USD"](100)));
            if (i == 4)
                env.close();
        }

        auto view = env.current();

        auto const metrics = env.app().getTxQ().getMetrics(*env.current());

        auto const result = grpcGetFee(grpcPort);

        BEAST_EXPECT(result.first == true);

        auto reply = result.second;

        // current ledger data
        BEAST_EXPECT(reply.current_ledger_size() == metrics.txInLedger);
        BEAST_EXPECT(reply.current_queue_size() == metrics.txCount);
        BEAST_EXPECT(reply.expected_ledger_size() == metrics.txPerLedger);
        BEAST_EXPECT(reply.ledger_current_index() == view->info().seq);
        BEAST_EXPECT(reply.max_queue_size() == *metrics.txQMaxSize);

        // fee levels data
        org::xrpl::rpc::v1::FeeLevels& levels = *reply.mutable_levels();
        BEAST_EXPECT(levels.median_level() == metrics.medFeeLevel);
        BEAST_EXPECT(levels.minimum_level() == metrics.minProcessingFeeLevel);
        BEAST_EXPECT(levels.open_ledger_level() == metrics.openLedgerFeeLevel);
        BEAST_EXPECT(levels.reference_level() == metrics.referenceFeeLevel);

        // fee data
        org::xrpl::rpc::v1::Fee& fee = *reply.mutable_fee();
        auto const baseFee = view->fees().base;
        BEAST_EXPECT(
            fee.base_fee().drops() ==
            toDrops(metrics.referenceFeeLevel, baseFee));
        BEAST_EXPECT(
            fee.minimum_fee().drops() ==
            toDrops(metrics.minProcessingFeeLevel, baseFee));
        BEAST_EXPECT(
            fee.median_fee().drops() == toDrops(metrics.medFeeLevel, baseFee));
        auto openLedgerFee =
            toDrops(metrics.openLedgerFeeLevel - FeeLevel64{1}, baseFee) + 1;
        BEAST_EXPECT(fee.open_ledger_fee().drops() == openLedgerFee.drops());
    }

public:
    void
    run() override
    {
        testFeeGrpc();
    }
};

BEAST_DEFINE_TESTSUITE(Fee, app, ripple);

}  // namespace test
}  // namespace ripple
