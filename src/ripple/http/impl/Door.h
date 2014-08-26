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

#ifndef RIPPLE_HTTP_DOOR_H_INCLUDED
#define RIPPLE_HTTP_DOOR_H_INCLUDED

#include <ripple/http/impl/ServerImpl.h>
#include <ripple/http/impl/Types.h>
#include <beast/asio/placeholders.h>

namespace ripple {
namespace HTTP {

/** A listening socket. */
class Door
    : public beast::SharedObject
    , public beast::List <Door>::Node
    , public beast::LeakChecked <Door>
{
private:
    // VFALCO TODO Use shared_ptr
    typedef beast::SharedPtr <Door> Ptr;

    ServerImpl& server_;
    acceptor acceptor_;
    Port port_;

public:
    Door (ServerImpl& impl, Port const& port);

    ~Door ();

    Port const&
    port () const;

    void
    cancel ();

    void
    failed (error_code ec);

    void
    async_accept ();

    void
    handle_accept (error_code ec,
        std::shared_ptr <Peer> const& peer);
};

}
}

#endif
