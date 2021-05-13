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

#ifndef RIPPLE_OVERLAY_INBOUNDHANDOFF_H_INCLUDED
#define RIPPLE_OVERLAY_INBOUNDHANDOFF_H_INCLUDED

#include <ripple/overlay/impl/OverlayImpl.h>

namespace ripple {

/** Sends HTTP response. Instantiates the inbound peer
 * once the response is sent. Maintains all data members
 * required for the inbound peer instantiation.
 */
class InboundHandoff : public OverlayImpl::Child,
                       public std::enable_shared_from_this<InboundHandoff>
{
private:
    using error_code = boost::system::error_code;
    using socket_type = boost::asio::ip::tcp::socket;
    using middle_type = boost::beast::tcp_stream;
    using stream_type = boost::beast::ssl_stream<middle_type>;
    Application& app_;
    std::uint32_t const id_;
    beast::WrappedSink sink_;
    beast::Journal const journal_;
    std::unique_ptr<stream_type> stream_ptr_;
    boost::asio::strand<boost::asio::executor> strand_;
    beast::IP::Endpoint const remote_address_;
    ProtocolVersion protocol_;
    PublicKey const publicKey_;
    Resource::Consumer usage_;
    std::shared_ptr<PeerFinder::Slot> const slot_;
    http_request_type request_;

public:
    virtual ~InboundHandoff() override = default;

    InboundHandoff(
        Application& app,
        id_t id,
        std::shared_ptr<PeerFinder::Slot> const& slot,
        http_request_type&& request,
        PublicKey const& publicKey,
        ProtocolVersion protocol,
        Resource::Consumer consumer,
        std::unique_ptr<stream_type>&& stream_ptr,
        OverlayImpl& overlay);

    // This class isn't meant to be copied
    InboundHandoff(InboundHandoff const&) = delete;
    InboundHandoff&
    operator=(InboundHandoff const&) = delete;

    /** Start the handshake */
    void
    run();
    /** Stop the child */
    void
    stop() override;

private:
    /** Send upgrade response to the client */
    void
    sendResponse();
    /** Instantiate and run the overlay peer */
    void
    createPeer();
    /** Log and close */
    void
    fail(std::string const& name, error_code const& ec);
    /** Log and close */
    void
    fail(std::string const& reason);
    /** Close connection */
    void
    close();
    /** Get underlying socket */
    socket_type&
    socket() const;
};

}  // namespace ripple

#endif  // RIPPLE_OVERLAY_INBOUNDHANDOFF_H_INCLUDED
