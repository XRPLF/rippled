#pragma once

#include <test/jtx/Env.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/Message.h>
#include <xrpld/overlay/Peer.h>
#include <xrpld/overlay/detail/OverlayImpl.h>
#include <xrpld/overlay/detail/PeerImp.h>
#include <xrpld/overlay/detail/ProtocolVersion.h>

#include <xrpl/basics/contract.h>
#include <xrpl/basics/make_SSLContext.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/peerfinder/Slot.h>
#include <xrpl/protocol/KeyType.h>
#include <xrpl/protocol/PublicKey.h>
#include <xrpl/protocol/SecretKey.h>
#include <xrpl/resource/Charge.h>
#include <xrpl/resource/Consumer.h>
#include <xrpl/server/Handoff.h>

#include <boost/asio/ip/address.hpp>
#include <boost/asio/ip/address_v4.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/beast/core/tcp_stream.hpp>
#include <boost/beast/ssl/ssl_stream.hpp>

#include <cstdint>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::test {

/**
 * A real `PeerImp` that captures the messages it would have sent.
 *
 * Only `send` and `run` are overridden, so `onMessage` runs production code.
 * Derive from this to reach a `protected` `PeerImp` member.
 */
class CapturePeer : public PeerImp
{
public:
    using MiddleType = boost::beast::tcp_stream;
    using StreamType = boost::beast::ssl_stream<MiddleType>;
    using SocketType = boost::asio::ip::tcp::socket;

    /**
     * Takes `PeerImp`'s two rvalue-reference parameters by value instead, so a
     * derived double can inherit this constructor without a never-moved-from
     * warning.
     *
     * @param app      The application owning the peer.
     * @param id       The connection id, unique among the overlay's peers.
     * @param slot     The peer finder slot; must be seated.
     * @param request  The handshake request.
     * @param publicKey  The peer's node public key.
     * @param protocol   The negotiated protocol version.
     * @param consumer   The resource manager endpoint for the peer.
     * @param streamPtr  The connection's ssl stream.
     * @param overlay    The overlay to register with.
     */
    CapturePeer(
        Application& app,
        Peer::id_t id,
        std::shared_ptr<peer_finder::Slot> const& slot,
        http_request_type request,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        resource::Consumer consumer,
        std::unique_ptr<StreamType> streamPtr,
        OverlayImpl& overlay)
        : PeerImp(
              app,
              id,
              slot,
              std::move(request),
              publicKey,
              protocol,
              consumer,  // copy-only, so `std::move` would be a copy anyway
              std::move(streamPtr),
              overlay)
    {
    }

    ~CapturePeer() override = default;

    /**
     * Does nothing, so the peer stays registered. The real `run()` reaches
     * `PeerImp::doAccept`, which fails on an unconnected socket and detaches.
     */
    void
    run() override
    {
    }

    /**
     * Captures the message instead of writing it, so replies are observable.
     */
    void
    send(std::shared_ptr<Message> const& m) override
    {
        sent_.push_back(m);
    }

    /**
     * @return Every message sent to this peer, in order.
     */
    std::vector<std::shared_ptr<Message>> const&
    sent() const
    {
        return sent_;
    }

    /**
     * @return The most recent message sent, or null if there was none.
     */
    std::shared_ptr<Message>
    lastSent() const
    {
        return sent_.empty() ? nullptr : sent_.back();
    }

    /**
     * Reads the accumulated charge without draining it through `charge()`.
     *
     * @return The charge accumulated on the peer so far.
     */
    resource::Charge
    feeCharge() const
    {
        return currentFeeCharge();
    }

private:
    std::vector<std::shared_ptr<Message>> sent_;
};

namespace detail {

// `inline` so the functions below name one entity across translation units.
inline constexpr std::uint16_t kCapturePeerPort = 51235;

/**
 * Non-template, so all `makeCapturePeer` instantiations share one counter. A
 * per-instantiation counter would give two peer types the same id, and
 * `addActive` would silently drop the second from `ids_`.
 *
 * @return The next unused connection id.
 */
inline Peer::id_t
nextCapturePeerId()
{
    static Peer::id_t id{0};
    return ++id;
}

/**
 * Non-template for the same reason as `nextCapturePeerId`. Each peer needs its
 * own address, not just its own port: the peer finder caps inbound connections
 * per address at `ipLimit`, which is at most 2 unless configured.
 *
 * @return The next unused remote endpoint.
 */
inline beast::ip::Endpoint
nextCapturePeerRemote()
{
    // From 172.2.0.1 upward, so ~900k fit before reaching 172.16/12, where the
    // peer finder would treat them as private rather than as real inbound.
    static std::uint32_t next{0xAC020001};
    return beast::ip::Endpoint(boost::asio::ip::address_v4(next++), kCapturePeerPort);
}

/**
 * Outlives every peer, whose stream keeps a reference to it. `PeerImp::charge`
 * posts a handler holding the peer, so a peer can outlive its caller's scope.
 *
 * @return The ssl context every test peer's stream is built on.
 */
inline boost::asio::ssl::context&
capturePeerSslContext()
{
    static std::shared_ptr<boost::asio::ssl::context> const kContext{makeSslContext("")};
    return *kContext;
}

}  // namespace detail

/**
 * Build an active `CapturePeer` and register it with the overlay.
 *
 * @tparam PeerType  The peer class to build; must derive from `CapturePeer` and
 *                   inherit its constructor.
 * @param env      The environment owning the overlay.
 * @param key      The peer's node public key, or unseated for a fresh random
 *                 one.
 * @param request  The handshake request. `PeerImp` reads its `X-Protocol-Ctl`
 *                 header in the constructor to negotiate features.
 * @return The peer, already registered with the overlay. Throws if the peer
 *         finder refused a slot.
 */
template <class PeerType = CapturePeer>
std::shared_ptr<PeerType>
makeCapturePeer(
    jtx::Env& env,
    std::optional<PublicKey> key = std::nullopt,
    http_request_type request = {})
{
    auto& overlay = dynamic_cast<OverlayImpl&>(env.app().getOverlay());
    auto streamPtr = std::make_unique<CapturePeer::StreamType>(
        CapturePeer::SocketType(env.app().getIOContext()), detail::capturePeerSslContext());

    beast::ip::Endpoint const local(
        boost::asio::ip::make_address("172.1.1.1"), detail::kCapturePeerPort);
    auto const remote = detail::nextCapturePeerRemote();

    auto consumer = overlay.resourceManager().newInboundEndpoint(remote);
    auto [slot, _] = overlay.peerFinder().newInboundSlot(local, remote);

    // Unseated when the endpoint is already connected or at the per-address
    // limit. `PeerImp` dereferences the slot, so fail here, not there.
    if (!slot)
    {
        Throw<std::runtime_error>("makeCapturePeer: no slot for " + to_string(remote));
    }

    if (!key)
        key = PublicKey(std::get<0>(randomKeyPair(KeyType::Ed25519)));

    auto peer = std::make_shared<PeerType>(
        env.app(),
        detail::nextCapturePeerId(),
        slot,
        std::move(request),
        *key,
        // An unsupported version fails every `supportsFeature` test, so a
        // version-gated reply would only ever take its legacy branch.
        newestSupportedProtocolVersion(),
        consumer,
        std::move(streamPtr),
        overlay);

    overlay.addActive(peer);
    return peer;
}

}  // namespace xrpl::test
