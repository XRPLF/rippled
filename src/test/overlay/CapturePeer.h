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
 * Only `send` and `run` are overridden, so everything a test drives through
 * `onMessage` runs production code. Derive from this to reach a `protected`
 * `PeerImp` member; `CapturePeerBuilder::build` takes the derived type.
 */
class CapturePeer : public PeerImp
{
public:
    using MiddleType = boost::beast::tcp_stream;
    using StreamType = boost::beast::ssl_stream<MiddleType>;
    using SocketType = boost::asio::ip::tcp::socket;

    /**
     * Forwards to `PeerImp`, restating its two sinks by value.
     *
     * `PeerImp` declares `request` and `streamPtr` as rvalue references. Taking
     * them by value instead is what lets a derived double write a plain
     * `using CapturePeer::CapturePeer;`. An inherited constructor has no body,
     * so an inherited rvalue-reference parameter is always reported as never
     * moved from.
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
              // `resource::Consumer` is copy-only, so `std::move` here would
              // be a copy anyway.
              consumer,
              std::move(streamPtr),
              overlay)
    {
    }

    ~CapturePeer() override = default;

    /**
     * Deliberately does nothing, which is what keeps the peer alive.
     *
     * `OverlayImpl::addActive` calls `run()`, and for an inbound peer that
     * reaches `PeerImp::doAccept`, which reads the ssl handshake off a socket
     * a test never connected. That fails, and the peer closes and detaches
     * itself again. Doing nothing leaves it registered and inert.
     */
    void
    run() override
    {
    }

    /**
     * Captures rather than writes, which is what makes replies observable.
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
     * Exposes the charge `PeerImp` has accumulated but not yet applied.
     *
     * `PeerImp::currentFeeCharge` is `protected` for exactly this reason: a
     * test can check which fee a message earned without draining it through
     * `charge()`. Reading it needs no production accessor, only a derived
     * class, and every suite that reads it wants the same one.
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

/**
 * Builds active `CapturePeer` peers against an environment's overlay.
 *
 * Holds the SSL context and hands out connection ids and remote addresses, so
 * no two peers built by one builder collide. Ids start at 1 and only ever
 * increase, as in production.
 */
class CapturePeerBuilder
{
public:
    /**
     * Build an active peer and register it with the overlay.
     *
     * @tparam PeerType  The peer class to build; must derive from `CapturePeer`
     *                   and inherit its constructor.
     * @param env        The environment owning the overlay.
     * @param key        The peer's node public key, or unseated for a fresh
     *                   random one.
     * @param request    The handshake request. Pass one carrying an
     *                   `X-Protocol-Ctl` header to negotiate features;
     *                   `PeerImp` reads it in its constructor.
     * @return The peer, already registered with the overlay. Throws rather
     *         than returning if the peer finder refused a slot.
     */
    template <class PeerType = CapturePeer>
    std::shared_ptr<PeerType>
    build(
        jtx::Env& env,
        std::optional<PublicKey> key = std::nullopt,
        http_request_type request = {})
    {
        auto& overlay = dynamic_cast<OverlayImpl&>(env.app().getOverlay());
        auto streamPtr = std::make_unique<CapturePeer::StreamType>(
            CapturePeer::SocketType(env.app().getIOContext()), *context_);

        // Every peer needs its own remote address, not merely its own port. The
        // peer finder caps inbound connections per address at `ipLimit`, which
        // is at most 2 unless configured, and it refuses the slot once that is
        // reached.
        beast::ip::Endpoint const local(boost::asio::ip::make_address("172.1.1.1"), kPort);
        beast::ip::Endpoint const remote(boost::asio::ip::address_v4(nextRemote_++), kPort);

        auto consumer = overlay.resourceManager().newInboundEndpoint(remote);
        auto [slot, _] = overlay.peerFinder().newInboundSlot(local, remote);

        // The slot is unseated when the endpoint is already connected or its
        // address is at the peer finder's per-address limit, and `PeerImp`
        // dereferences the slot in its constructor. Fail here, where the cause
        // is visible, rather than there with a segfault.
        if (!slot)
        {
            Throw<std::runtime_error>(
                "CapturePeerBuilder::build: no slot for " + to_string(remote));
        }

        if (!key)
            key = PublicKey(std::get<0>(randomKeyPair(KeyType::Ed25519)));

        auto peer = std::make_shared<PeerType>(
            env.app(),
            nextId_++,
            slot,
            std::move(request),
            *key,
            // A peer claiming an unsupported version would silently fail every
            // test `PeerImp::supportsFeature` makes against the version, and so
            // would only ever reach the legacy branch of a version-gated reply.
            newestSupportedProtocolVersion(),
            consumer,
            std::move(streamPtr),
            overlay);

        overlay.addActive(peer);
        return peer;
    }

private:
    static constexpr std::uint16_t kPort = 51235;
    // Remote addresses are handed out from 172.2.0.1 upward, so ~900k fit
    // before the counter reaches 172.16/12 and the peer finder starts treating
    // them as private. Keeping them public is what makes a test peer look like
    // a real inbound connection.
    static constexpr std::uint32_t kFirstRemote = 0xAC020001;

    std::shared_ptr<boost::asio::ssl::context> context_{makeSslContext("")};
    Peer::id_t nextId_{1};
    std::uint32_t nextRemote_{kFirstRemote};
};

}  // namespace xrpl::test
