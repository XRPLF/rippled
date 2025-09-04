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

#include <xrpld/overlay/detail/Handshake.h>
#include <xrpld/overlay/detail/InboundHandshake.h>
#include <xrpld/overlay/detail/OverlayImpl.h>
#include <xrpld/overlay/detail/PeerImp.h>

#include <boost/beast/core/ostream.hpp>

namespace ripple {

InboundHandshake::InboundHandshake(
    Application& app,
    std::uint32_t id,
    std::shared_ptr<PeerFinder::Slot> const& slot,
    http_request_type&& request,
    PublicKey const& publicKey,
    ProtocolVersion protocolVersion,
    Resource::Consumer consumer,
    std::unique_ptr<StreamInterface>&& stream_ptr,
    PeerAttributes const& attributes,
    endpoint_type const& remoteEndpoint,
    OverlayImpl& overlay)
    : Child(overlay)
    , app_(app)
    , id_(id)
    , sink_(app_.logs()["Peer"], OverlayImpl::makePrefix(id))
    , journal_(sink_)
    , stream_ptr_(std::move(stream_ptr))
    , request_(std::move(request))
    , publicKey_(publicKey)
    , protocolVersion_(protocolVersion)
    , consumer_(consumer)
    , attributes_(attributes)
    , slot_(slot)
    , remoteEndpoint_(remoteEndpoint)
    , strand_(boost::asio::make_strand(stream_ptr_->get_executor()))
{
}

InboundHandshake::~InboundHandshake()
{
    if (slot_ != nullptr)
        overlay_.peerFinder().on_closed(slot_);
}

void
InboundHandshake::stop()
{
    if (!strand_.running_in_this_thread())
        return boost::asio::post(
            strand_, std::bind(&InboundHandshake::stop, shared_from_this()));

    shutdown();
}

void
InboundHandshake::shutdown()
{
    XRPL_ASSERT(
        strand_.running_in_this_thread(),
        "ripple::InboundHandshake::shutdown : strand in this thread");

    if (!stream_ptr_->is_open() || shutdown_)
        return;

    shutdown_ = true;

    stream_ptr_->cancel();

    tryAsyncShutdown();
}

void
InboundHandshake::tryAsyncShutdown()
{
    XRPL_ASSERT(
        strand_.running_in_this_thread(),
        "ripple::InboundHandshake::tryAsyncShutdown : strand in this thread");

    if (!stream_ptr_->is_open())
        return;

    if (shutdown_ || shutdownStarted_)
        return;

    if (ioPending_)
        return;

    shutdownStarted_ = true;

    return stream_ptr_->async_shutdown(boost::asio::bind_executor(
        strand_,
        std::bind(
            &InboundHandshake::onShutdown,
            shared_from_this(),
            std::placeholders::_1)));
}

void
InboundHandshake::onShutdown(error_code ec)
{
    XRPL_ASSERT(
        strand_.running_in_this_thread(),
        "ripple::InboundHandshake::onShutdown : strand in this thread");

    if (!stream_ptr_->is_open())
        return;

    if (ec == boost::asio::error::operation_aborted)
        return;

    if (ec)
    {
        JLOG(journal_.warn()) << "onShutdown: " << ec.message();
    }

    stream_ptr_->close();
}

void
InboundHandshake::run()
{
    if (!strand_.running_in_this_thread())
        return boost::asio::post(
            strand_, std::bind(&InboundHandshake::run, shared_from_this()));

    // TODO: implement fail overload to handle strings
    auto const sharedValue = stream_ptr_->makeSharedValue(journal_);
    if (!sharedValue)
        return fail("makeSharedValue", boost::system::error_code{});

    // Create the handshake response
    auto const response = makeResponse(
        !overlay_.peerFinder().config().peerPrivate,
        request_,
        overlay_.setup().public_ip,
        remoteEndpoint_.address(),
        *sharedValue,
        overlay_.setup().networkID,
        protocolVersion_,
        app_);

    // Convert response to buffer for async_write
    auto write_buffer = std::make_shared<boost::beast::multi_buffer>();
    boost::beast::ostream(*write_buffer) << response;

    ioPending_ = true;
    // Write the response asynchronously
    stream_ptr_->async_write(
        write_buffer->data(),
        boost::asio::bind_executor(
            strand_,
            [this, write_buffer, self = shared_from_this()](
                error_code ec, std::size_t bytes_transferred) {
                onHandshake(ec, bytes_transferred);
            }));
}

void
InboundHandshake::onHandshake(error_code ec, std::size_t bytes_transferred)
{
    ioPending_ = false;
    if (!stream_ptr_->is_open())
        return;

    if (ec == boost::asio::error::operation_aborted || shutdown_)
        return tryAsyncShutdown();

    if (ec)
        return fail("onHandshake", ec);

    JLOG(journal_.debug()) << "InboundHandshake completed for "
                           << remoteEndpoint_
                           << ", bytes transferred: " << bytes_transferred;

    // Handshake successful, create the peer
    createPeer();
}

void
InboundHandshake::createPeer()
{
    auto const peer = std::make_shared<PeerImp>(
        app_,
        overlay_,
        std::move(slot_),
        std::move(stream_ptr_),
        consumer_,
        protocolVersion_,
        attributes_,
        publicKey_,
        id_);

    // Add the peer to the overlay
    overlay_.add_active(peer);
    JLOG(journal_.debug()) << "Created peer for " << remoteEndpoint_;
}

void
InboundHandshake::fail(std::string const& name, error_code ec)
{
    XRPL_ASSERT(
        strand_.running_in_this_thread(),
        "ripple::InboundHandshake::fail : strand in this thread");

    JLOG(journal_.warn()) << name << " from "
                          << toBase58(TokenType::NodePublic, publicKey_)
                          << " at " << remoteEndpoint_.address().to_string()
                          << ": " << ec.message();

    shutdown();
}

}  // namespace ripple