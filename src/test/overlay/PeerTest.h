#pragma once

#include <test/jtx/Env.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/overlay/Message.h>
#include <xrpld/overlay/Peer.h>
#include <xrpld/overlay/detail/OverlayImpl.h>
#include <xrpld/overlay/detail/PeerImp.h>
#include <xrpld/overlay/detail/ProtocolVersion.h>
#include <xrpld/overlay/detail/Tuning.h>

#include <xrpl/peerfinder/Slot.h>
#include <xrpl/protocol/PublicKey.h>
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
#include <vector>

namespace xrpl::test {

/**
 * Test peer that captures sent messages for verification.
 */
class PeerTest : public PeerImp
{
private:
    inline static Peer::id_t id{};
    std::shared_ptr<Message> lastSentMessage_;

public:
    using MiddleType = boost::beast::tcp_stream;
    using SharedContext = std::shared_ptr<boost::asio::ssl::context>;
    using StreamType = boost::beast::ssl_stream<MiddleType>;

    PeerTest(
        Application& app,
        std::shared_ptr<peer_finder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        resource::Consumer consumer,
        std::unique_ptr<StreamType>&& streamPtr,
        OverlayImpl& overlay);

    ~PeerTest() override = default;

    void
    run() override;

    void
    send(std::shared_ptr<Message> const& m) override;

    std::shared_ptr<Message>
    getLastSentMessage() const;

    // Synchronous test access to the JobQueue-dispatched processor.
    // The production path runs this on JtLedgerReq; tests need a
    // synchronous entry point to inspect the reply via send().
    // PeerImp::processGetObjectByHash is `protected` so the derived
    // test subclass can call it directly.
    void
    runProcessGetObjectByHash(std::shared_ptr<protocol::TMGetObjectByHash> const& m);

    void
    runProcessLedgerRequest(
        std::shared_ptr<protocol::TMGetLedger> const& m,
        std::vector<SHAMapNodeID> nodeIDs);

    resource::Charge
    getCurrentFeeCharge() const;

    static void
    resetId();
};

std::shared_ptr<PeerTest>
makePeerTest(
    jtx::Env& env,
    PeerTest::SharedContext const& context,
    ProtocolVersion protocolVersion);

}  // namespace xrpl::test
