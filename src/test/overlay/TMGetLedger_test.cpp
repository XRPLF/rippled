#include <test/jtx/Env.h>
#include <test/overlay/CapturePeer.h>

#include <xrpld/overlay/Compression.h>
#include <xrpld/overlay/detail/Tuning.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/resource/Fees.h>
#include <xrpl/shamap/SHAMapNodeID.h>

#include <xrpl.pb.h>

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace xrpl::test {

using namespace jtx;

class TMGetLedger_test : public beast::unit_test::Suite
{
    /**
     * Calls the JtLedgerReq-dispatched processor synchronously, so the reply is
     * visible through `lastSent()`.
     */
    class GetLedgerPeer : public CapturePeer
    {
    public:
        using CapturePeer::CapturePeer;

        void
        runProcessLedgerRequest(
            std::shared_ptr<protocol::TMGetLedger> const& m,
            std::vector<SHAMapNodeID> nodeIDs)
        {
            processLedgerRequest(m, std::move(nodeIDs));
        }
    };

    // Build a well-formed TMGetLedger node request carrying `numNodeIds` node
    // IDs.
    static std::shared_ptr<protocol::TMGetLedger>
    createRequest(std::size_t const numNodeIds)
    {
        auto request = std::make_shared<protocol::TMGetLedger>();
        request->set_itype(protocol::liTX_NODE);

        // A uint256-sized ledger hash, as a well-formed request carries.
        uint256 const ledgerHash{1};
        request->set_ledgerhash(ledgerHash.data(), ledgerHash.size());

        // Valid, deserializable SHAMap node IDs.
        auto const rootNodeId = SHAMapNodeID{}.getRawString();
        for (std::size_t i = 0; i < numNodeIds; ++i)
        {
            request->add_nodeids(rootNodeId);
        }

        return request;
    }

    void
    testNodeIdCountAccepted(std::size_t const numNodeIds, bool const expectRejected)
    {
        testcase("Node ID Count Accepted");

        Env env{*this};

        auto peer = makeCapturePeer(env);
        peer->onMessage(createRequest(numNodeIds));

        // A request outside the accepted node-ID count is charged kFeeInvalidData; one inside
        // it is not. The JobQueue handler may run concurrently and update the fee in the
        // accepted case.
        BEAST_EXPECT(
            expectRejected ? (peer->feeCharge() == resource::kFeeInvalidData)
                           : !(peer->feeCharge() == resource::kFeeInvalidData));
    }

    void
    testProcessLedgerRequestNodeCount(std::size_t const numNodeIds)
    {
        testcase("Process Ledger Request Node Count");

        Env env{*this};
        env.close();

        auto peer = makeCapturePeer<GetLedgerPeer>(env);

        // Ask for the account-state root node of the closed ledger.
        auto request = createRequest(numNodeIds);
        request->clear_ledgerhash();
        request->set_itype(protocol::liAS_NODE);
        request->set_ltype(protocol::ltCLOSED);

        peer->runProcessLedgerRequest(request, std::vector<SHAMapNodeID>(numNodeIds));

        auto sentMessage = peer->lastSent();
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
        testNodeIdCountAccepted(limit + 1, true);
        testNodeIdCountAccepted(limit, false);
        testNodeIdCountAccepted(limit - 1, false);
        testProcessLedgerRequestNodeCount(limit + 1);
        testProcessLedgerRequestNodeCount(limit);
        testProcessLedgerRequestNodeCount(limit - 1);
    }
};

BEAST_DEFINE_TESTSUITE(TMGetLedger, overlay, xrpl);

}  // namespace xrpl::test
