#include <test/jtx.h>
#include <test/jtx/Env.h>

#include <xrpld/overlay/Message.h>
#include <xrpld/overlay/detail/OverlayImpl.h>
#include <xrpld/overlay/detail/PeerImp.h>
#include <xrpld/overlay/detail/Tuning.h>
#include <xrpld/peerfinder/detail/SlotImp.h>

#include <xrpl/basics/make_SSLContext.h>
#include <xrpl/beast/unit_test.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/protocol/digest.h>
#include <xrpl/protocol/messages.h>

namespace xrpl {
namespace test {

using namespace jtx;

/**
 * Test for TMGetObjectByHash reply size limiting.
 *
 * This verifies the fix that limits TMGetObjectByHash replies to
 * Tuning::hardMaxReplyNodes to prevent excessive memory usage and
 * potential DoS attacks from peers requesting large numbers of objects.
 */
class TMGetObjectByHash_test : public beast::unit_test::suite
{
    using middle_type = boost::beast::tcp_stream;
    using stream_type = boost::beast::ssl_stream<middle_type>;
    using socket_type = boost::asio::ip::tcp::socket;
    using shared_context = std::shared_ptr<boost::asio::ssl::context>;
    /**
     * Test peer that captures sent messages for verification.
     */
    class PeerTest : public PeerImp
    {
    public:
        PeerTest(
            Application& app,
            std::shared_ptr<PeerFinder::Slot> const& slot,
            http_request_type&& request,
            PublicKey const& publicKey,
            ProtocolVersion protocol,
            Resource::Consumer consumer,
            std::unique_ptr<TMGetObjectByHash_test::stream_type>&& stream_ptr,
            OverlayImpl& overlay)
            : PeerImp(
                  app,
                  id_++,
                  slot,
                  std::move(request),
                  publicKey,
                  protocol,
                  consumer,
                  std::move(stream_ptr),
                  overlay)
        {
        }

        ~PeerTest() = default;

        void
        run() override
        {
        }

        void
        send(std::shared_ptr<Message> const& m) override
        {
            lastSentMessage_ = m;
        }

        std::shared_ptr<Message>
        getLastSentMessage() const
        {
            return lastSentMessage_;
        }

        static void
        resetId()
        {
            id_ = 0;
        }

    private:
        inline static Peer::id_t id_ = 0;
        std::shared_ptr<Message> lastSentMessage_;
    };

    shared_context context_{make_SSLContext("")};
    ProtocolVersion protocolVersion_{1, 7};

    std::shared_ptr<PeerTest>
    createPeer(jtx::Env& env)
    {
        auto& overlay = dynamic_cast<OverlayImpl&>(env.app().overlay());
        boost::beast::http::request<boost::beast::http::dynamic_body> request;
        auto stream_ptr = std::make_unique<stream_type>(
            socket_type(env.app().getIOContext()), *context_);

        beast::IP::Endpoint local(
            boost::asio::ip::make_address("172.1.1.1"), 51235);
        beast::IP::Endpoint remote(
            boost::asio::ip::make_address("172.1.1.2"), 51235);

        PublicKey key(std::get<0>(randomKeyPair(KeyType::ed25519)));
        auto consumer = overlay.resourceManager().newInboundEndpoint(remote);
        auto [slot, _] = overlay.peerFinder().new_inbound_slot(local, remote);

        auto peer = std::make_shared<PeerTest>(
            env.app(),
            slot,
            std::move(request),
            key,
            protocolVersion_,
            consumer,
            std::move(stream_ptr),
            overlay);

        overlay.add_active(peer);
        return peer;
    }

    std::shared_ptr<protocol::TMGetObjectByHash>
    createRequest(size_t const numObjects, Env& env)
    {
        // Store objects in the NodeStore that will be found during the query
        auto& nodeStore = env.app().getNodeStore();

        // Create and store objects
        std::vector<uint256> hashes;
        hashes.reserve(numObjects);
        for (int i = 0; i < numObjects; ++i)
        {
            uint256 hash(xrpl::sha512Half(i));
            hashes.push_back(hash);

            Blob data(100, static_cast<unsigned char>(i % 256));
            nodeStore.store(
                hotLEDGER,
                std::move(data),
                hash,
                nodeStore.earliestLedgerSeq());
        }

        // Create a request with more objects than hardMaxReplyNodes
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
     * Test that reply is limited to hardMaxReplyNodes when more objects
     * are requested than the limit allows.
     */
    void
    testReplyLimit(size_t const numObjects, int const expectedReplySize)
    {
        testcase("Reply Limit");

        Env env(*this);
        PeerTest::resetId();

        auto peer = createPeer(env);

        auto request = createRequest(numObjects, env);
        // Call the onMessage handler
        peer->onMessage(request);

        // Verify that a reply was sent
        auto sentMessage = peer->getLastSentMessage();
        BEAST_EXPECT(sentMessage != nullptr);

        // Parse the reply message
        auto const& buffer =
            sentMessage->getBuffer(compression::Compressed::Off);

        BEAST_EXPECT(buffer.size() > 6);
        // Skip the message header (6 bytes: 4 for size, 2 for type)
        protocol::TMGetObjectByHash reply;
        BEAST_EXPECT(
            reply.ParseFromArray(buffer.data() + 6, buffer.size() - 6) == true);

        // Verify the reply is limited to expectedReplySize
        BEAST_EXPECT(reply.objects_size() == expectedReplySize);
    }

    void
    run() override
    {
        int const limit = static_cast<int>(Tuning::hardMaxReplyNodes);
        testReplyLimit(limit + 1, limit);
        testReplyLimit(limit, limit);
        testReplyLimit(limit - 1, limit - 1);
    }
};

BEAST_DEFINE_TESTSUITE(TMGetObjectByHash, overlay, xrpl);

}  // namespace test
}  // namespace xrpl
