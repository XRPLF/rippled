#include <test/jtx/CheckMessageLogs.h>
#include <test/jtx/Env.h>
#include <test/jtx/envconfig.h>
#include <test/overlay/CapturePeer.h>

#include <xrpld/overlay/ReduceRelayCommon.h>
#include <xrpld/overlay/detail/Handshake.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/resource/Fees.h>
#include <xrpl/server/Handoff.h>

#include <xrpl.pb.h>

#include <cstddef>
#include <memory>
#include <optional>
#include <utility>

namespace xrpl::test {

using namespace jtx;

class TMTransactions_test : public beast::unit_test::Suite
{
    static std::shared_ptr<protocol::TMTransactions>
    createRequest(std::size_t const numTransactions)
    {
        auto request = std::make_shared<protocol::TMTransactions>();
        for (std::size_t i = 0; i < numTransactions; ++i)
        {
            request->mutable_transactions()->Add(protocol::TMTransaction{});
        }
        return request;
    }

    void
    testTransactionCountAccepted(std::size_t const numTransactions, bool const expectRejected)
    {
        testcase("Transaction Count Accepted");

        static constexpr auto kLimitExceededMessage = "TMTransactions: transaction list too large";
        auto foundExpectedLog = false;
        Env env{
            *this,
            envconfig(),
            std::make_unique<CheckMessageLogs>(kLimitExceededMessage, &foundExpectedLog)};

        // `PeerImp` decides `txReduceRelayEnabled()` in its constructor, from
        // the config and the handshake header, so set this first.
        env.app().config().txReduceRelayEnable = true;
        http_request_type request;
        request.insert("X-Protocol-Ctl", makeFeaturesRequestHeader(false, false, true, false));

        auto peer = makeCapturePeer(env, std::nullopt, std::move(request));
        peer->onMessage(createRequest(numTransactions));

        auto fee = peer->feeCharge();
        if (expectRejected)
        {
            BEAST_EXPECT(fee == resource::kFeeMalformedRequest);
            BEAST_EXPECT(foundExpectedLog);
        }
        else
        {
            BEAST_EXPECT(!foundExpectedLog);
        }
    }

    void
    run() override
    {
        auto const limit = reduce_relay::kMaxTxQueueSize;
        testTransactionCountAccepted(limit + 1, true);
        testTransactionCountAccepted(limit, false);
        testTransactionCountAccepted(limit - 1, false);
    }
};

BEAST_DEFINE_TESTSUITE(TMTransactions, overlay, xrpl);

}  // namespace xrpl::test
