#include <test/jtx/CheckMessageLogs.h>
#include <test/jtx/Env.h>
#include <test/jtx/envconfig.h>
#include <test/overlay/PeerTest.h>

#include <xrpld/overlay/ReduceRelayCommon.h>
#include <xrpld/overlay/detail/OverlayImpl.h>
#include <xrpld/overlay/detail/PeerImp.h>
#include <xrpld/overlay/detail/ProtocolVersion.h>
#include <xrpld/overlay/detail/Tuning.h>

#include <xrpl/basics/make_SSLContext.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/resource/Fees.h>

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>

#include <xrpl.pb.h>

#include <cstddef>
#include <memory>

namespace xrpl::test {

using namespace jtx;

class TMTransactions_test : public beast::unit_test::Suite
{
    PeerTest::SharedContext context_{makeSslContext("")};
    ProtocolVersion protocolVersion_{1, 7};

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
        PeerTest::resetId();

        auto peer = makePeerTest(env, context_, protocolVersion_);
        peer->txReduceRelayEnabled(true);
        peer->onMessage(createRequest(numTransactions));

        auto fee = peer->getCurrentFeeCharge();
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
