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

#ifndef RIPPLE_SERVER_HANDOFF_H_INCLUDED
#define RIPPLE_SERVER_HANDOFF_H_INCLUDED

#include <ripple/server/Writer.h>
#include <boost/beast/http/dynamic_body.hpp>
#include <boost/beast/http/message.hpp>
#include <memory>

namespace ripple {

using http_request_type =
    boost::beast::http::request<boost::beast::http::dynamic_body>;

using http_response_type =
    boost::beast::http::response<boost::beast::http::dynamic_body>;

/** Used to indicate the result of a server connection handoff. */
struct Handoff
{
    // When `true`, the Session will close the socket. The
    // Handler may optionally take socket ownership using std::move
    bool moved = false;

    // If response is set, this determines the keep alive
    bool keep_alive = false;

    // When set, this will be sent back
    std::shared_ptr<Writer> response;

    bool
    handled() const
    {
        return moved || response;
    }
};

}  // namespace ripple

#endif
