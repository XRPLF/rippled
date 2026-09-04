#include <test/jtx/Env.h>
#include <test/overlay/PeerTest.h>

#include <xrpld/overlay/Compression.h>
#include <xrpld/overlay/Peer.h>
#include <xrpld/overlay/detail/OverlayImpl.h>
#include <xrpld/overlay/detail/PeerImp.h>
#include <xrpld/overlay/detail/ProtocolVersion.h>
#include <xrpld/overlay/detail/Tuning.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/make_SSLContext.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/resource/Charge.h>
#include <xrpl/resource/Fees.h>
#include <xrpl/shamap/SHAMapNodeID.h>

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>

#include <xrpl.pb.h>

#include <cstddef>
#include <memory>
#include <vector>

namespace xrpl::test {

using namespace jtx;

class TMGetLedger_test : public beast::unit_test::Suite
{
    PeerTest::SharedContext context_{makeSslContext("")};
    ProtocolVersion protocolVersion_{1, 7};

    // Build a well-formed TMGetLedger node request carrying `numNodeIds` node
    // IDs.
    static std::shared_ptr<protocol::TMGetLedger>
    createRequest(std::size_t const numNodeIds)
    {
        auto request = std::make_shared<protocol::TMGetLedger>();
        request->set_itype(protocol::liTX_NODE);

        // A uint256-sized ledger hash so the request passes the earlier
        // structural validation and reaches the node-ID checks.
        uint256 const ledgerHash{1};
        request->set_ledgerhash(ledgerHash.data(), ledgerHash.size());

        // Valid, deserializable SHAMap node IDs (the root node ID, repeated).
        // The count is what the hard bound cares about.
        auto const rootNodeId = SHAMapNodeID{}.getRawString();
        for (std::size_t i = 0; i < numNodeIds; ++i)
        {
            request->add_nodeids(rootNodeId);
        }

        return request;
    }

    void
    testNodeIdHardLimit(std::size_t const numNodeIds, bool const expectRejected)
    {
        testcase("Node ID Hard Limit");

        Env env{*this};
        PeerTest::resetId();

        auto peer = makePeerTest(env, context_, protocolVersion_);
        peer->onMessage(createRequest(numNodeIds));

        // An over-limit request is charged kFeeInvalidData; an in-limit request should not be.
        // The JobQueue handler may run concurrently and update the fee for in-limit requests.
        BEAST_EXPECT(
            expectRejected ? (peer->getCurrentFeeCharge() == resource::kFeeInvalidData)
                           : !(peer->getCurrentFeeCharge() == resource::kFeeInvalidData));
    }

    void
    testProcessLedgerRequestReplyCapped(std::size_t const numNodeIds)
    {
        testcase("Process Ledger Request Reply Capped");

        Env env{*this};
        env.close();
        PeerTest::resetId();

        auto peer = makePeerTest(env, context_, protocolVersion_);

        // Request the account-state root node of the closed ledger, asking
        // for far more node IDs than the hard bound allows.
        auto request = createRequest(numNodeIds);
        request->clear_ledgerhash();
        request->set_itype(protocol::liAS_NODE);
        request->set_ltype(protocol::ltCLOSED);

        peer->runProcessLedgerRequest(request, std::vector<SHAMapNodeID>(numNodeIds));

        auto sentMessage = peer->getLastSentMessage();
        BEAST_EXPECT(sentMessage != nullptr);
        if (!sentMessage)
        {
            return;
        }

        auto const& buffer = sentMessage->getBuffer(compression::Compressed::Off);
        BEAST_EXPECT(buffer.size() > 6);

        // Skip the message header (6 bytes: 4 for size, 2 for type).
        protocol::TMLedgerData reply;
        BEAST_EXPECT(reply.ParseFromArray(buffer.data() + 6, buffer.size() - 6) == true);

        BEAST_EXPECT(reply.type() == protocol::liAS_NODE);
        BEAST_EXPECT(reply.nodes_size() > 0);
        BEAST_EXPECT(reply.nodes_size() <= static_cast<std::size_t>(tuning::kHardMaxReplyNodes));
    }

    void
    run() override
    {
        auto const limit = static_cast<std::size_t>(tuning::kHardMaxReplyNodes);
        testNodeIdHardLimit(limit + 1, true);
        testNodeIdHardLimit(limit, false);
        testNodeIdHardLimit(limit - 1, false);
        testProcessLedgerRequestReplyCapped(limit + 1);
        testProcessLedgerRequestReplyCapped(limit);
        testProcessLedgerRequestReplyCapped(limit - 1);
    }
};

BEAST_DEFINE_TESTSUITE(TMGetLedger, overlay, xrpl);

}  // namespace xrpl::test
