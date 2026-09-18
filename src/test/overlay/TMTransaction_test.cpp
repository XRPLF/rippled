#include <test/jtx/Env.h>
#include <test/jtx/envconfig.h>
#include <test/overlay/CapturePeer.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/resource/Fees.h>

#include <xrpl.pb.h>

#include <memory>

namespace xrpl::test {

using namespace jtx;

class TMTransaction_test : public beast::unit_test::Suite
{
    void
    testFailureDeserializingTransactionIsCharged()
    {
        testcase("Undeserializable Transaction Is Charged");

        Env env{*this, envconfig()};
        CapturePeerBuilder builder;

        auto peer = builder.build(env);
        auto tx = std::make_shared<protocol::TMTransaction>();
        tx->set_status(protocol::tsNEW);

        // Bytes that are not a serialized transaction, so deserialization fails.
        tx->set_rawtransaction("\x01\x02\x03", 3);

        peer->onMessage(tx);
        BEAST_EXPECT(peer->feeCharge() == resource::kFeeInvalidData);
    }

    void
    run() override
    {
        testFailureDeserializingTransactionIsCharged();
    }
};

BEAST_DEFINE_TESTSUITE(TMTransaction, overlay, xrpl);

}  // namespace xrpl::test
