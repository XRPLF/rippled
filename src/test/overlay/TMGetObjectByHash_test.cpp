#include <test/jtx/Env.h>
#include <test/overlay/PeerTest.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/Compression.h>
#include <xrpld/overlay/detail/OverlayImpl.h>
#include <xrpld/overlay/detail/PeerImp.h>
#include <xrpld/overlay/detail/ProtocolVersion.h>
#include <xrpld/overlay/detail/Tuning.h>

#include <xrpl/basics/Blob.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/make_SSLContext.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/protocol/digest.h>

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>

#include <xrpl.pb.h>

#include <cstddef>
#include <memory>
#include <utility>
#include <vector>

namespace xrpl::test {

using namespace jtx;

/**
 * Coverage for the TMGetObjectByHash object-count bound.
 *
 * A generic query names some number of objects; the number of entries the
 * reply carries is bounded by tuning::kHardMaxReplyNodes. These cases pin that
 * bound at and either side of its boundary.
 */
class TMGetObjectByHash_test : public beast::unit_test::Suite
{
    PeerTest::SharedContext context_{makeSslContext("")};
    ProtocolVersion protocolVersion_{1, 7};

    static std::shared_ptr<protocol::TMGetObjectByHash>
    createRequest(size_t const numObjects, Env& env)
    {
        // Store objects in the NodeStore that will be found during the query
        auto& nodeStore = env.app().getNodeStore();

        // Create and store objects
        std::vector<UInt256> hashes;
        hashes.reserve(numObjects);
        for (int i = 0; i < numObjects; ++i)
        {
            UInt256 const hash(xrpl::sha512Half(i));
            hashes.push_back(hash);

            Blob data(100, static_cast<unsigned char>(i % 256));
            nodeStore.store(
                NodeObjectType::Ledger, std::move(data), hash, nodeStore.earliestLedgerSeq());
        }

        // Name every stored object in a single generic query.
        auto request = std::make_shared<protocol::TMGetObjectByHash>();
        request->set_type(protocol::TMGetObjectByHash_ObjectType_otLEDGER);
        request->set_query(true);

        for (int i = 0; i < numObjects; ++i)
        {
            auto object = request->add_objects();
            object->set_hash(hashes[i].data(), hashes[i].size());
            object->set_ledgerseq(i);
        }
        return request;
    }

    /**
     * Check the object count a generic-query reply carries.
     *
     * `onMessage(TMGetObjectByHash)` dispatches the generic-query path
     * to the JobQueue, so tests invoke the synchronous processor
     * directly via `runProcessGetObjectByHash`.
     */
    void
    testReplyObjectCount(size_t const numObjects, int const expectedReplySize)
    {
        testcase("Reply Object Count");

        Env env(*this);
        PeerTest::resetId();

        auto peer = makePeerTest(env, context_, protocolVersion_);

        auto request = createRequest(numObjects, env);
        peer->runProcessGetObjectByHash(request);

        // Verify that a reply was sent
        auto sentMessage = peer->getLastSentMessage();
        BEAST_EXPECT(sentMessage != nullptr);

        // Parse the reply message
        auto const& buffer = sentMessage->getBuffer(compression::Compressed::Off);

        BEAST_EXPECT(buffer.size() > 6);
        // Skip the message header (6 bytes: 4 for size, 2 for type)
        protocol::TMGetObjectByHash reply;
        BEAST_EXPECT(reply.ParseFromArray(buffer.data() + 6, buffer.size() - 6) == true);

        // The reply carries the expected number of objects.
        BEAST_EXPECT(reply.objects_size() == expectedReplySize);
    }

    void
    run() override
    {
        int const limit = static_cast<int>(tuning::kHardMaxReplyNodes);
        testReplyObjectCount(limit + 1, limit);
        testReplyObjectCount(limit, limit);
        testReplyObjectCount(limit - 1, limit - 1);
    }
};

BEAST_DEFINE_TESTSUITE(TMGetObjectByHash, overlay, xrpl);

}  // namespace xrpl::test
