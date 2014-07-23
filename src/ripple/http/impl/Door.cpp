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

#include <ripple/http/impl/Door.h>
#include <ripple/http/impl/Peer.h>
#include <beast/asio/placeholders.h>

namespace ripple {
namespace HTTP {

Door::Door (ServerImpl& impl, Port const& port)
    : impl_ (impl)
    , acceptor_ (impl_.get_io_service(), to_asio (port))
    , port_ (port)
{
    impl_.add (*this);

    error_code ec;

    acceptor_.set_option (acceptor::reuse_address (true), ec);
    if (ec)
    {
        impl_.journal().error <<
            "Error setting acceptor socket option: " << ec.message();
    }

    if (! ec)
    {
        impl_.journal().info << "Bound to endpoint " <<
            to_string (acceptor_.local_endpoint());

        async_accept();
    }
    else
    {
        impl_.journal().error << "Error binding to endpoint " <<
            to_string (acceptor_.local_endpoint()) <<
            ", '" << ec.message() << "'";
    }
}

Door::~Door ()
{
    impl_.remove (*this);
}

Port const&
Door::port () const
{
    return port_;
}

void
Door::cancel ()
{
    acceptor_.cancel();
}

void
Door::failed (error_code ec)
{
}

void
Door::async_accept ()
{
    auto const peer (std::make_shared <Peer> (impl_, port_));
    acceptor_.async_accept (peer->get_socket(), std::bind (
        &Door::handle_accept, Ptr(this),
            beast::asio::placeholders::error, peer));
}

void
Door::handle_accept (error_code ec,
    std::shared_ptr <Peer> const& peer)
{
    if (ec == boost::asio::error::operation_aborted)
        return;

    if (ec)
    {
        impl_.journal().error << "Accept failed: " << ec.message();
        return;
    }

    async_accept();

    peer->accept();
}

}
}
