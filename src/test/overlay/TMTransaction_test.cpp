#include <test/jtx/Env.h>
#include <test/jtx/envconfig.h>
#include <test/overlay/PeerTest.h>

#include <xrpld/overlay/detail/OverlayImpl.h>
#include <xrpld/overlay/detail/PeerImp.h>
#include <xrpld/overlay/detail/ProtocolVersion.h>

#include <xrpl/basics/make_SSLContext.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/resource/Fees.h>

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>

#include <xrpl.pb.h>

#include <memory>

namespace xrpl::test {

using namespace jtx;

class TMTransaction_test : public beast::unit_test::Suite
{
    PeerTest::SharedContext context_{makeSslContext("")};
    ProtocolVersion protocolVersion_{1, 7};

    void
    testFailureDeserializingTransactionIsCharged()
    {
        testcase("Undeserializable Transaction Is Charged");

        Env env{*this, envconfig()};
        PeerTest::resetId();

        auto peer = makePeerTest(env, context_, protocolVersion_);
        auto tx = std::make_shared<protocol::TMTransaction>();
        tx->set_status(protocol::tsNEW);

        // Bytes that are not a serialized transaction, so deserialization fails.
        tx->set_rawtransaction("\x01\x02\x03", 3);

        peer->onMessage(tx);
        BEAST_EXPECT(peer->getCurrentFeeCharge() == resource::kFeeInvalidData);
    }

    void
    run() override
    {
        testFailureDeserializingTransactionIsCharged();
    }
};

BEAST_DEFINE_TESTSUITE(TMTransaction, overlay, xrpl);

}  // namespace xrpl::test
