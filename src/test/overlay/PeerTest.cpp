#include <test/overlay/PeerTest.h>

#include <test/jtx/Env.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/Message.h>
#include <xrpld/overlay/Peer.h>
#include <xrpld/overlay/detail/OverlayImpl.h>
#include <xrpld/overlay/detail/PeerImp.h>
#include <xrpld/overlay/detail/ProtocolVersion.h>
#include <xrpld/overlay/detail/Tuning.h>

#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/peerfinder/Slot.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/resource/Charge.h>
#include <xrpl/resource/Consumer.h>
#include <xrpl/server/Handoff.h>
#include <xrpl/shamap/SHAMapNodeID.h>

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>

#include <xrpl.pb.h>

#include <memory>
#include <optional>
#include <utility>
#include <vector>

namespace xrpl::test {

PeerTest::PeerTest(
    Application& app,
    std::shared_ptr<peer_finder::Slot> const& slot,
    HttpRequestType&& request,
    PublicKey const& publicKey,
    ProtocolVersion protocol,
    resource::Consumer consumer,
    std::unique_ptr<StreamType>&& streamPtr,
    OverlayImpl& overlay)
    : PeerImp{
          app,
          id++,
          slot,
          std::move(request),
          publicKey,
          protocol,
          consumer,
          std::move(streamPtr),
          overlay}
{
}

void
PeerTest::run()
{
}

void
PeerTest::send(std::shared_ptr<Message> const& message)
{
    lastSentMessage_ = message;
}

std::shared_ptr<Message>
PeerTest::getLastSentMessage() const
{
    return lastSentMessage_;
}

void
PeerTest::runProcessGetObjectByHash(std::shared_ptr<protocol::TMGetObjectByHash> const& message)
{
    PeerImp::processGetObjectByHash(message);
}

void
PeerTest::runProcessLedgerRequest(
    std::shared_ptr<protocol::TMGetLedger> const& message,
    std::vector<SHAMapNodeID> nodeIDs)
{
    PeerImp::processLedgerRequest(message, std::move(nodeIDs));
}

resource::Charge
PeerTest::getCurrentFeeCharge() const
{
    return PeerImp::currentFeeCharge();
}

void
PeerTest::resetId()
{
    id = 0;
}

bool
PeerTest::compressionEnabled() const
{
    if (compressionEnabled_.has_value())
    {
        return *compressionEnabled_;
    }
    return PeerImp::compressionEnabled();
}

void
PeerTest::compressionEnabled(std::optional<bool> enabled)
{
    compressionEnabled_ = enabled;
}

bool
PeerTest::txReduceRelayEnabled() const
{
    if (reduceRelayEnabled_.has_value())
    {
        return *reduceRelayEnabled_;
    }
    return PeerImp::txReduceRelayEnabled();
}

void
PeerTest::txReduceRelayEnabled(std::optional<bool> enabled)
{
    reduceRelayEnabled_ = enabled;
}

std::shared_ptr<PeerTest>
makePeerTest(jtx::Env& env, PeerTest::SharedContext const& context, ProtocolVersion protocolVersion)
{
    using SocketType = boost::asio::ip::tcp::socket;

    auto& overlay = dynamic_cast<OverlayImpl&>(env.app().getOverlay());
    boost::beast::http::request<boost::beast::http::dynamic_body> request;
    auto streamPtr =
        std::make_unique<PeerTest::StreamType>(SocketType(env.app().getIOContext()), *context);

    beast::ip::Endpoint const local(boost::asio::ip::make_address("172.1.1.1"), 51235);
    beast::ip::Endpoint const remote(boost::asio::ip::make_address("172.1.1.2"), 51235);

    PublicKey const key{std::get<0>(randomKeyPair(KeyType::Ed25519))};
    auto consumer = overlay.resourceManager().newInboundEndpoint(remote);
    auto [slot, _] = overlay.peerFinder().newInboundSlot(local, remote);

    auto peer = std::make_shared<PeerTest>(
        env.app(),
        slot,
        std::move(request),
        key,
        protocolVersion,
        consumer,
        std::move(streamPtr),
        overlay);

    overlay.addActive(peer);
    return peer;
}

}  // namespace xrpl::test
