//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_OVERLAY_INBOUNDHANDSHAKE_H_INCLUDED
#define RIPPLE_OVERLAY_INBOUNDHANDSHAKE_H_INCLUDED

#include <xrpld/overlay/detail/Handshake.h>
#include <xrpld/overlay/detail/OverlayImpl.h>
#include <xrpld/overlay/detail/PeerImp.h>

#include <xrpl/server/Handoff.h>

namespace ripple {

/** Manages an inbound peer handshake. */
class InboundHandshake : public OverlayImpl::Child,
                         public std::enable_shared_from_this<InboundHandshake>
{
    using error_code = boost::system::error_code;
    using endpoint_type = boost::asio::ip::tcp::endpoint;

private:
    Application& app_;
    std::uint32_t const id_;
    beast::WrappedSink sink_;
    beast::Journal const journal_;
    std::unique_ptr<StreamInterface> stream_ptr_;
    http_request_type request_;
    PublicKey publicKey_;
    ProtocolVersion protocolVersion_;
    Resource::Consumer consumer_;
    PeerAttributes attributes_;
    std::shared_ptr<PeerFinder::Slot> slot_;
    endpoint_type remoteEndpoint_;
    boost::asio::strand<boost::asio::executor> strand_;
    bool shutdown_ = false;
    bool ioPending_ = false;
    bool shutdownStarted_ = false;

public:
    InboundHandshake(
        Application& app,
        std::uint32_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& public_key,
        ProtocolVersion protocol_version,
        Resource::Consumer consumer,
        std::unique_ptr<StreamInterface>&& stream_ptr,
        PeerAttributes const& attributes,
        endpoint_type const& remote_endpoint,
        OverlayImpl& overlay);

    ~InboundHandshake();

    void
    stop() override;

    void
    run();

private:
    void
    setTimer();

    void
    onTimer(error_code ec);

    void
    cancelTimer();

    void
    shutdown();

    void
    tryAsyncShutdown();

    void
    onShutdown(error_code ec);

    void
    onHandshake(error_code ec, std::size_t bytes_transferred);

    void
    createPeer();

    void
    fail(std::string const& name, error_code ec);
};

}  // namespace ripple

#endif
