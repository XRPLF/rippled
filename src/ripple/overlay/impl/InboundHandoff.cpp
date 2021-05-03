//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2021 Ripple Labs Inc.

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

#include <ripple/overlay/impl/InboundHandoff.h>

#include <boost/beast/core/ostream.hpp>

namespace ripple {

InboundHandoff::InboundHandoff(
    Application& app,
    id_t id,
    std::shared_ptr<PeerFinder::Slot> const& slot,
    http_request_type&& request,
    PublicKey const& publicKey,
    ProtocolVersion protocol,
    Resource::Consumer consumer,
    std::unique_ptr<stream_type>&& stream_ptr,
    OverlayImpl& overlay)
    : OverlayImpl::Child(overlay)
    , app_(app)
    , id_(id)
    , sink_(
          app_.journal("Peer"),
          [id]() {
              std::stringstream ss;
              ss << "[" << std::setfill('0') << std::setw(3) << id << "] ";
              return ss.str();
          }())
    , journal_(sink_)
    , stream_ptr_(std::move(stream_ptr))
    , strand_(stream_ptr_->next_layer().socket().get_executor())
    , remote_address_(slot->remote_endpoint())
    , protocol_(protocol)
    , publicKey_(publicKey)
    , usage_(consumer)
    , slot_(slot)
    , request_(std::move(request))
{
}

void
InboundHandoff::run()
{
    if (!strand_.running_in_this_thread())
        return post(
            strand_, std::bind(&InboundHandoff::run, shared_from_this()));
    sendResponse();
}

void
InboundHandoff::stop()
{
    if (!strand_.running_in_this_thread())
        return post(
            strand_, std::bind(&InboundHandoff::stop, shared_from_this()));
    if (stream_ptr_->next_layer().socket().is_open())
    {
        JLOG(journal_.debug()) << "Stop";
    }
    close();
}

void
InboundHandoff::sendResponse()
{
    auto const sharedValue = makeSharedValue(*stream_ptr_, journal_);
    // This shouldn't fail since we already computed
    // the shared value successfully in OverlayImpl
    if (!sharedValue)
        return fail("makeSharedValue: Unexpected failure");

    JLOG(journal_.info()) << "Protocol: " << to_string(protocol_);
    JLOG(journal_.info()) << "Public Key: "
                          << toBase58(TokenType::NodePublic, publicKey_);

    auto write_buffer = std::make_shared<boost::beast::multi_buffer>();

    boost::beast::ostream(*write_buffer) << makeResponse(
        !overlay_.peerFinder().config().peerPrivate,
        request_,
        overlay_.setup().public_ip,
        remote_address_.address(),
        *sharedValue,
        overlay_.setup().networkID,
        protocol_,
        app_);

    // Write the whole buffer and only start protocol when that's done.
    boost::asio::async_write(
        *stream_ptr_,
        write_buffer->data(),
        boost::asio::transfer_all(),
        bind_executor(
            strand_,
            [this, write_buffer, self = shared_from_this()](
                error_code ec, std::size_t bytes_transferred) {
                if (!stream_ptr_->next_layer().socket().is_open())
                    return;
                if (ec == boost::asio::error::operation_aborted)
                    return;
                if (ec)
                    return fail("onWriteResponse", ec);
                if (write_buffer->size() == bytes_transferred)
                    return createPeer();
                return fail("Failed to write header");
            }));
}

void
InboundHandoff::fail(std::string const& name, error_code const& ec)
{
    if (socket().is_open())
    {
        JLOG(journal_.warn())
            << name << " from " << toBase58(TokenType::NodePublic, publicKey_)
            << " at " << remote_address_.to_string() << ": " << ec.message();
    }
    close();
}

void
InboundHandoff::fail(std::string const& reason)
{
    if (journal_.active(beast::severities::kWarning) && socket().is_open())
    {
        auto const n = app_.cluster().member(publicKey_);
        JLOG(journal_.warn())
            << (n ? remote_address_.to_string() : *n) << " failed: " << reason;
    }
    close();
}

void
InboundHandoff::close()
{
    if (socket().is_open())
    {
        socket().close();
        JLOG(journal_.debug()) << "Closed";
    }
}

void
InboundHandoff::createPeer()
{
    auto peer = std::make_shared<PeerImp>(
        app_,
        id_,
        slot_,
        std::move(request_),
        publicKey_,
        protocol_,
        usage_,
        std::move(stream_ptr_),
        overlay_);

    overlay_.add_active(peer);
}

InboundHandoff::socket_type&
InboundHandoff::socket() const
{
    return stream_ptr_->next_layer().socket();
}

}  // namespace ripple